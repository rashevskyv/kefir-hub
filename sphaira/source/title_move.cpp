#include "title_info.hpp"
#include "defines.hpp"
#include "ui/types.hpp"
#include "i18n.hpp"
#include "app.hpp"
#include "log.hpp"

#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "ui/progress_box.hpp"
#include "utils/utils.hpp"

#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace sphaira::title {
namespace {

constexpr bool IsMovableStorage(u8 storage_id) {
    return storage_id == NcmStorageId_SdCard || storage_id == NcmStorageId_BuiltInUser;
}

// a move runs on a worker thread while the menu underneath keeps issuing ncm
// requests from its Draw() (icons, per-title sizes). the kernel serialises
// requests per session, so sharing the global handles would park every frame
// behind a multi-MiB transfer and freeze the ui - the move gets its own.
struct NcmSession {
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};

    Result Open(u8 storage_id) {
        R_TRY(ncmOpenContentStorage(std::addressof(cs), (NcmStorageId)storage_id));
        R_TRY(ncmOpenContentMetaDatabase(std::addressof(db), (NcmStorageId)storage_id));
        R_SUCCEED();
    }

    ~NcmSession() {
        ncmContentStorageClose(std::addressof(cs));
        ncmContentMetaDatabaseClose(std::addressof(db));
    }
};

// resolves the ncm meta key of a component on its own storage, plus the ncas it
// is made of.
Result GetComponentContents(NcmContentMetaDatabase& db, const NsApplicationContentMetaStatus& status, NcmContentMetaKey& key, std::vector<NcmContentInfo>& infos) {
    const auto app_id = ncm::GetAppId(status.meta_type, status.application_id);
    auto id_min = status.application_id;
    auto id_max = status.application_id;
    if (status.storageID == NcmStorageId_None || status.storageID == NcmStorageId_GameCard) {
        id_min -= 1;
        id_max += 1;
    }

    s32 meta_total{};
    s32 meta_written{};
    key = {};
    R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(meta_total), std::addressof(meta_written), std::addressof(key), 1, (NcmContentMetaType)status.meta_type, app_id, id_min, id_max, NcmContentInstallType_Full));
    R_UNLESS(meta_written == 1, Result_GameEmptyMetaEntries);

    return ncm::GetContentInfos(std::addressof(db), std::addressof(key), infos);
}

auto SumContentSize(const std::vector<NcmContentInfo>& infos) -> u64 {
    u64 total{};
    for (const auto& info : infos) {
        u64 size{};
        ncmContentInfoSizeToU64(std::addressof(info), std::addressof(size));
        total += size;
    }
    return total;
}

// undoes everything a component copy placed on the target storage. armed until
// Commit(), which is only reached once the application record points at the new
// storage - up to that moment the source copy is still whole and still the one
// the system launches from.
struct MoveRollback {
    NcmContentStorage* cs{};
    NcmContentMetaDatabase* db{};
    std::vector<NcmPlaceHolderId> placeholders{};
    std::vector<NcmContentId> registered{};
    NcmContentMetaKey meta_key{};
    bool has_meta{};
    bool committed{};

    void Commit() {
        committed = true;
    }

    ~MoveRollback() {
        if (committed) {
            return;
        }

        log_write("[MOVE] rolling back %zu placeholders, %zu ncas, meta: %u\n", placeholders.size(), registered.size(), has_meta);

        if (has_meta) {
            ncmContentMetaDatabaseRemove(db, std::addressof(meta_key));
            ncmContentMetaDatabaseCommit(db);
        }
        for (const auto& id : registered) {
            ncmContentStorageDelete(cs, std::addressof(id));
        }
        for (const auto& id : placeholders) {
            ncmContentStorageDeletePlaceHolder(cs, std::addressof(id));
        }
    }
};

// rewrites the application record so the moved key resolves to its new storage.
// ns keeps its own copy of "which storage holds which meta key" and it is that
// copy - not the ncm databases - which decides whether the title launches, so
// this is the single commit point of a move.
Result RepointApplicationRecord(u64 app_id, const NcmContentMetaKey& key, NcmStorageId target_storage) {
    ns::AppManager am;
    R_UNLESS(am, Result_GameMoveNoAppManager);

    // the whole list is re-pushed below, so a truncated read would silently drop
    // records (and with them, installed dlc): page until ns runs out.
    std::vector<ncm::ContentStorageRecord> records;
    constexpr s32 PAGE = 32;
    for (;;) {
        const auto offset = records.size();
        records.resize(offset + PAGE);

        s32 count{};
        R_TRY(ns::ListApplicationRecordContentMeta(am.get(), offset, app_id, records.data() + offset, PAGE, std::addressof(count)));
        records.resize(offset + count);

        if (count < PAGE) {
            break;
        }
    }

    bool found{};
    for (auto& record : records) {
        if (record.key.id == key.id && record.key.type == key.type) {
            record.storage_id = target_storage;
            found = true;
        }
    }

    if (!found) {
        auto& record = records.emplace_back();
        record.key = key;
        record.storage_id = target_storage;
    }

    // ns has no "update one record" cmd - the list is replaced wholesale.
    R_TRY(ns::DeleteApplicationRecord(am.get(), app_id));
    R_TRY(ns::PushApplicationRecord(am.get(), app_id, records.data(), records.size()));
    R_SUCCEED();
}

// copy -> register -> repoint the record -> only then delete the source.
// done/total drive one continuous progress bar across every component of a title.
Result MoveComponentImpl(const NsApplicationContentMetaStatus& status, NcmStorageId target_storage, ui::ProgressBox* pbox, u64& done, u64 total) {
    const auto src_storage = static_cast<NcmStorageId>(status.storageID);
    if (src_storage == target_storage) {
        R_SUCCEED();
    }
    R_UNLESS(IsMovableStorage(src_storage), Result_GameEmptyMetaEntries);
    R_UNLESS(IsMovableStorage(target_storage), Result_GameEmptyMetaEntries);

    NcmSession src{}, dst{};
    R_TRY(src.Open(src_storage));
    R_TRY(dst.Open(target_storage));

    auto& src_db = src.db;
    auto& src_cs = src.cs;
    auto& dst_db = dst.db;
    auto& dst_cs = dst.cs;

    NcmContentMetaKey key{};
    std::vector<NcmContentInfo> infos;
    R_TRY(GetComponentContents(src_db, status, key, infos));

    u64 meta_size{};
    R_TRY(ncmContentMetaDatabaseGetSize(std::addressof(src_db), std::addressof(meta_size), std::addressof(key)));
    std::vector<u8> meta_buf(meta_size);
    u64 out_meta_size{};
    R_TRY(ncmContentMetaDatabaseGet(std::addressof(src_db), std::addressof(key), std::addressof(out_meta_size), meta_buf.data(), meta_buf.size()));

    const auto meta_label = i18n::get(ncm::GetReadableMetaTypeStr(status.meta_type));

    auto file_label = [&](const NcmContentInfo& info, size_t i, u64 nca_size) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s · %s  (%zu/%zu)  %s",
            meta_label.c_str(), ncm::GetContentTypeStr(info.content_type),
            i + 1, infos.size(), utils::formatSizeStorage(nca_size).c_str());
        return std::string{buf};
    };

    auto show_step = [&](const std::string& step, const std::string& file, s64 offset, s64 size) {
        if (!pbox) {
            return;
        }
        pbox->SetTransfer(file.empty() ? step : (step + " · " + file));
        if (size > 0) {
            pbox->UpdateTransfer(offset, size);
        }
        // ncm CreatePlaceHolder can sit on a multi-GB allocate with no byte
        // callbacks; sleep one frame so the label actually paints first.
        pbox->Yield();
        svcSleepThread(16'000'000);
    };

    MoveRollback rollback{std::addressof(dst_cs), std::addressof(dst_db)};

    // every WritePlaceHolder is its own transaction on a journalled bis
    // partition, so small chunks cost throughput rather than saving latency -
    // 512 KiB made a move roughly four times slower, which is what a bar that
    // "never moves" actually was. same sizing as the installer.
    const u64 CHUNK_SIZE = App::IsFileBaseEmummc() ? 512ULL * 1024ULL : 4ULL * 1024ULL * 1024ULL;
    std::vector<u8> chunk_buf(CHUNK_SIZE);

    for (size_t i = 0; i < infos.size(); i++) {
        const auto& info = infos[i];
        u64 nca_size{};
        ncmContentInfoSizeToU64(std::addressof(info), std::addressof(nca_size));
        const auto file = file_label(info, i, nca_size);

        bool has{};
        ncmContentStorageHas(std::addressof(dst_cs), std::addressof(has), std::addressof(info.content_id));
        if (has) {
            // already present on the target (shared nca), nothing to copy.
            done += nca_size;
            if (pbox) {
                pbox->UpdateTransfer(nca_size, nca_size ? nca_size : 1);
            }
            continue;
        }

        show_step("Allocating"_i18n, file, 0, nca_size ? static_cast<s64>(nca_size) : 1);

        NcmPlaceHolderId placeholder_id{};
        R_TRY(ncmContentStorageGeneratePlaceHolderId(std::addressof(dst_cs), std::addressof(placeholder_id)));
        R_TRY(ncmContentStorageCreatePlaceHolder(std::addressof(dst_cs), std::addressof(info.content_id), std::addressof(placeholder_id), nca_size));
        rollback.placeholders.emplace_back(placeholder_id);

        show_step("Copying"_i18n, file, 0, nca_size ? static_cast<s64>(nca_size) : 1);

        u64 nca_offset{};
        TimeStamp log_ts;
        u64 log_done = done;
        while (nca_offset < nca_size) {
            R_TRY(pbox ? pbox->ShouldExitResult() : 0);

            const u64 to_read = std::min<u64>(CHUNK_SIZE, nca_size - nca_offset);
            R_TRY(ncmContentStorageReadContentIdFile(std::addressof(src_cs), chunk_buf.data(), to_read, std::addressof(info.content_id), nca_offset));
            R_TRY(ncmContentStorageWritePlaceHolder(std::addressof(dst_cs), std::addressof(placeholder_id), nca_offset, chunk_buf.data(), to_read));

            nca_offset += to_read;
            done += to_read;
            if (pbox) {
                pbox->UpdateTransfer(static_cast<s64>(nca_offset), static_cast<s64>(nca_size));
            }

            // one line a second, so a "the bar never moves" report can be
            // answered with the actual throughput instead of a guess.
            if (log_ts.GetSeconds() >= 1) {
                const auto secs = log_ts.GetSecondsD();
                log_write("[MOVE] %llu/%llu MiB  %.2f MiB/s\n",
                    (unsigned long long)(done >> 20), (unsigned long long)(total >> 20),
                    secs > 0 ? ((double)(done - log_done) / (1024.0 * 1024.0)) / secs : 0.0);
                log_ts.Update();
                log_done = done;
            }
        }

        R_TRY(ncmContentStorageRegister(std::addressof(dst_cs), std::addressof(info.content_id), std::addressof(placeholder_id)));
        rollback.placeholders.pop_back();
        rollback.registered.emplace_back(info.content_id);
    }

    // every nca is on the target now; publish the meta entry there.
    show_step("Updating ncm database"_i18n, {}, 1, 1);
    R_TRY(ncmContentMetaDatabaseSet(std::addressof(dst_db), std::addressof(key), meta_buf.data(), meta_buf.size()));
    R_TRY(ncmContentMetaDatabaseCommit(std::addressof(dst_db)));
    rollback.meta_key = key;
    rollback.has_meta = true;

    // point of no return: from here the title launches from the target copy.
    // cancelling is refused rather than half-applied.
    show_step("Pushing application record"_i18n, {}, 1, 1);
    R_TRY(RepointApplicationRecord(ncm::GetAppId(key), key, target_storage));
    rollback.Commit();

    show_step("Removing old copy"_i18n, {}, 1, 1);
    R_TRY(ncm::DeleteKey(std::addressof(src_cs), std::addressof(src_db), std::addressof(key)));

    if (ns::AppManager am; am) {
        ns::InvalidateApplicationControlCache(am.get(), status.application_id);
    }

    R_SUCCEED();
}

} // namespace

Result GetMovePlan(u64 app_id, NcmStorageId target_storage, MovePlan& out) {
    out = {};

    MetaEntries entries;
    R_TRY(GetMetaEntries(app_id, entries));

    for (const auto& status : entries) {
        NcmContentMetaKey key{};
        std::vector<NcmContentInfo> infos;
        // plans are built on the ui thread before any transfer starts, so the
        // shared sessions are fine here.
        if (R_FAILED(GetComponentContents(GetNcmDb(status.storageID), status, key, infos))) {
            continue;
        }

        const MoveEntry entry{status, SumContentSize(infos)};
        if (status.storageID == NcmStorageId_SdCard) {
            out.sd_size += entry.size;
        } else if (status.storageID == NcmStorageId_BuiltInUser) {
            out.nand_size += entry.size;
        }

        if (IsMovableStorage(status.storageID) && status.storageID != target_storage) {
            out.move_size += entry.size;
            out.move.emplace_back(entry);
        } else {
            out.stay.emplace_back(entry);
        }
    }

    R_SUCCEED();
}

Result MoveComponent(const NsApplicationContentMetaStatus& status, NcmStorageId target_storage, ui::ProgressBox* pbox) {
    NcmContentMetaKey key{};
    std::vector<NcmContentInfo> infos;
    R_TRY(GetComponentContents(GetNcmDb(status.storageID), status, key, infos));

    u64 done{};
    const auto total = SumContentSize(infos);
    if (pbox) {
        pbox->NewTransfer(i18n::get(ncm::GetReadableMetaTypeStr(status.meta_type)));
        pbox->UpdateTransfer(0, total);
    }

    return MoveComponentImpl(status, target_storage, pbox, done, total);
}

Result MoveApplication(u64 app_id, NcmStorageId target_storage, ui::ProgressBox* pbox) {
    if (pbox) {
        pbox->NewTransfer("Preparing move"_i18n);
        pbox->UpdateTransfer(0, 1);
    }

    MovePlan plan;
    R_TRY(GetMovePlan(app_id, target_storage, plan));
    if (plan.move.empty()) {
        R_SUCCEED();
    }

    // refuse up front rather than filling the target and failing halfway: a
    // partial move leaves ncas stranded on both storages.
    s64 nand_free{}, sd_free{};
    fs::GetStorageSpaces(&nand_free, nullptr, &sd_free, nullptr);
    const auto free_space = target_storage == NcmStorageId_SdCard ? sd_free : nand_free;
    R_UNLESS(free_space <= 0 || (u64)free_space > plan.move_size, Result_GameMoveNotEnoughSpace);

    u64 done{};

    for (const auto& entry : plan.move) {
        R_TRY(pbox ? pbox->ShouldExitResult() : 0);
        R_TRY(MoveComponentImpl(entry.status, target_storage, pbox, done, plan.move_size));
    }

    R_SUCCEED();
}

} // namespace sphaira::title
