#include "system_cleanup.hpp"
#include "system_cleanup_plan.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "title_info.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <vector>

namespace sphaira::cleanup {
namespace {

using Id16 = std::array<u8, 16>;

auto ToId(const void* p) -> Id16 {
    Id16 id;
    std::memcpy(id.data(), p, id.size());
    return id;
}

struct Storage {
    NcmStorageId id{};
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};
    bool open{};
    std::vector<NcmContentMetaKey> keys{};

    explicit Storage(NcmStorageId storage_id) : id{storage_id} {
        open = R_SUCCEEDED(ncmOpenContentStorage(&cs, id)) && R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, id));
        if (open) {
            LoadKeys();
        }
    }
    ~Storage() {
        ncmContentMetaDatabaseClose(&db);
        ncmContentStorageClose(&cs);
    }

    void LoadKeys() {
        if (R_FAILED(ncm::ListAllKeys(&db, keys))) {
            keys.clear();
        }
    }

    // every content id a registered title still uses, its own meta nca included.
    auto ReferencedContent() -> std::set<Id16> {
        std::set<Id16> out;
        for (const auto& key : keys) {
            std::vector<NcmContentInfo> infos;
            if (R_SUCCEEDED(ncm::GetContentInfos(&db, &key, infos))) {
                for (const auto& info : infos) {
                    out.emplace(ToId(&info.content_id));
                }
            }
            NcmContentId meta_id;
            if (R_SUCCEEDED(ncmContentMetaDatabaseGetContentIdByType(&db, &meta_id, &key, NcmContentType_Meta))) {
                out.emplace(ToId(&meta_id));
            }
        }
        return out;
    }

    auto FreeSpace() -> s64 {
        s64 free{};
        ncmContentStorageGetFreeSpaceSize(&cs, &free);
        return free;
    }
};

Result DeleteOrphans(ui::ProgressBox* pbox, Storage& st, u32& removed) {
    const auto used = st.ReferencedContent();
    s32 count{};
    R_TRY(ncmContentStorageGetContentCount(&st.cs, &count));
    std::vector<NcmContentId> ids(count);
    s32 written{};
    if (count > 0) {
        R_TRY(ncmContentStorageListContentId(&st.cs, ids.data(), ids.size(), &written, 0));
    }
    ids.resize(written);
    for (const auto& id : ids) {
        R_TRY(pbox->ShouldExitResult());
        if (!used.contains(ToId(&id)) && R_SUCCEEDED(ncmContentStorageDelete(&st.cs, &id))) {
            removed++;
        }
    }
    R_SUCCEED();
}

// ns keeps its own list of a game's metas; it has no "remove one" command, so the list is re-pushed.
Result RemoveFromRecord(u64 app_id, const NcmContentMetaKey& key) {
    ns::AppManager am;
    R_UNLESS(am, Result_GameMoveNoAppManager);
    std::vector<ncm::ContentStorageRecord> records;
    constexpr s32 PAGE = 32;
    for (;;) {
        const auto offset = records.size();
        records.resize(offset + PAGE);
        s32 got{};
        R_TRY(ns::ListApplicationRecordContentMeta(am.get(), offset, app_id, records.data() + offset, PAGE, &got));
        records.resize(offset + got);
        if (got < PAGE) {
            break;
        }
    }
    const auto before = records.size();
    std::erase_if(records, [&key](const auto& r){ return r.key.id == key.id && r.key.version == key.version && r.key.type == key.type; });
    if (records.size() == before || records.empty()) {
        R_SUCCEED();
    }
    R_TRY(ns::DeleteApplicationRecord(am.get(), app_id));
    return ns::PushApplicationRecord(am.get(), app_id, records.data(), records.size());
}

Result DeleteOldUpdates(ui::ProgressBox* pbox, std::span<Storage*> storages, u32& removed) {
    struct Where { Storage* st; NcmContentMetaKey key; };
    std::vector<Where> all;
    std::vector<PatchRef> patches;
    for (auto* st : storages) {
        for (const auto& key : st->keys) {
            if (key.type == NcmContentMetaType_Patch) {
                patches.push_back({ncm::GetAppId(key), key.version, all.size()});
                all.push_back({st, key});
            }
        }
    }
    for (const auto i : OldUpdates(patches)) {
        R_TRY(pbox->ShouldExitResult());
        const auto& w = all[i];
        log_write("[CLEAN] old update %016lX v%u\n", w.key.id, w.key.version);
        if (R_SUCCEEDED(ncm::DeleteKey(&w.st->cs, &w.st->db, &w.key))) {
            RemoveFromRecord(ncm::GetAppId(w.key), w.key);
            removed++;
        }
    }
    for (auto* st : storages) {
        st->LoadKeys();
    }
    R_SUCCEED();
}

Result DeleteUnusedTickets(std::span<Storage*> storages, u32& removed) {
    std::set<Id16> used;
    for (auto* st : storages) {
        for (const auto& key : st->keys) {
            std::vector<NcmContentInfo> infos;
            if (R_FAILED(ncm::GetContentInfos(&st->db, &key, infos))) {
                continue;
            }
            for (const auto& info : infos) {
                NcmRightsId rid{};
                if (R_SUCCEEDED(ncmContentStorageGetRightsIdFromContentId(&st->cs, &rid, &info.content_id, FsContentAttributes_All))) {
                    used.emplace(ToId(&rid.rights_id));
                }
            }
        }
    }

    R_TRY(es::Initialize());
    ON_SCOPE_EXIT(es::Exit());
    std::vector<FsRightsId> unused;
    const auto collect = [&](auto count_fn, auto list_fn) {
        s32 count{};
        if (R_FAILED(count_fn(&count)) || count <= 0) {
            return;
        }
        std::vector<FsRightsId> ids(count);
        s32 written{};
        if (R_SUCCEEDED(list_fn(&written, ids.data(), count))) {
            for (s32 i = 0; i < written; i++) {
                if (!used.contains(ToId(&ids[i]))) {
                    unused.push_back(ids[i]);
                }
            }
        }
    };
    collect(es::CountCommonTicket, es::ListCommonTicket);
    collect(es::CountPersonalizedTicket, es::ListPersonalizedTicket);
    log_write("[CLEAN] %zu unused tickets (%zu rights ids in use)\n", unused.size(), used.size());
    if (!unused.empty()) {
        R_TRY(es::DeleteTicket(unused.data(), unused.size()));
        removed += unused.size();
    }
    R_SUCCEED();
}

void DeleteFolderContents(fs::Fs& fs, const fs::FsPath& dir, u32& removed) {
    fs::Dir d;
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(fs.OpenDirectory(dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d)) || R_FAILED(d.ReadAll(entries))) {
        return;
    }
    for (const auto& e : entries) {
        const auto path = fs::AppendPath(dir, e.name);
        if (R_SUCCEEDED(e.type == FsDirEntryType_Dir ? fs.DeleteDirectoryRecursively(path) : fs.DeleteFile(path))) {
            removed++;
        }
    }
}

// folders for games that are no longer on the console; sysmodules never match IsGameContentsFolder.
void DeleteStaleContentsFolders(fs::Fs& fs, u32& removed) {
    std::set<u64> known;
    std::vector<NsApplicationRecord> records(1000);
    for (s32 offset = 0;;) {
        s32 count{};
        if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count)) || !count) {
            break;
        }
        for (s32 i = 0; i < count; i++) {
            known.emplace(records[i].application_id);
        }
        offset += count;
    }
    fs::Dir d;
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(fs.OpenDirectory("/atmosphere/contents", FsDirOpenMode_ReadDirs, &d)) || R_FAILED(d.ReadAll(entries))) {
        return;
    }
    for (const auto& e : entries) {
        const auto tid = forced_language::ParseTitleId(e.name);
        if (!IsGameContentsFolder(e.name) || known.contains(tid)) {
            continue;
        }
        log_write("[CLEAN] stale contents folder %s\n", e.name);
        if (R_SUCCEEDED(fs.DeleteDirectoryRecursively(fs::AppendPath("/atmosphere/contents", e.name)))) {
            removed++;
        }
    }
}

void DeleteSavesOfRemovedUsers(u32& removed) {
    std::vector<AccountUid> users;
    for (const auto& acc : App::GetAccountList()) {
        users.push_back(acc.uid);
    }
    FsSaveDataInfoReader reader;
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        return;
    }
    std::vector<u64> to_delete;
    FsSaveDataInfo info;
    s64 total{};
    while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, &info, 1, &total)) && total) {
        if (info.save_data_type != FsSaveDataType_Account) {
            continue;
        }
        const bool known = std::ranges::any_of(users, [&info](const auto& u){
            return !std::memcmp(&u, &info.uid, sizeof(u));
        });
        if (!known) {
            to_delete.push_back(info.save_data_id);
        }
    }
    fsSaveDataInfoReaderClose(&reader);
    for (const auto id : to_delete) {
        if (R_SUCCEEDED(fsDeleteSaveDataFileSystemBySaveDataSpaceId(FsSaveDataSpaceId_User, id))) {
            removed++;
        }
    }
}

} // namespace

Result Run(ui::ProgressBox* pbox, const Options& o, Report& out) {
    out = {};
    Storage sd{NcmStorageId_SdCard};
    Storage nand{NcmStorageId_BuiltInUser};
    std::array<Storage*, 2> both{&sd, &nand};
    fs::FsNativeSd sd_fs;
    s64 sd_fs_before{};
    sd_fs.GetFreeSpace("/", &sd_fs_before);
    const auto nand_before = nand.FreeSpace();

    const auto step = [pbox](const char* name) {
        pbox->NewTransfer(i18n::get(name));
        return pbox->ShouldExitResult();
    };

    if (o.old_updates) {
        R_TRY(step("Deleting old game updates"));
        R_TRY(DeleteOldUpdates(pbox, both, out.removed));
    }
    if (o.orphans_sd && sd.open) {
        R_TRY(step("Deleting lost content on the SD card"));
        R_TRY(DeleteOrphans(pbox, sd, out.removed));
    }
    if (o.orphans_nand && nand.open) {
        R_TRY(step("Deleting lost content in system memory"));
        R_TRY(DeleteOrphans(pbox, nand, out.removed));
    }
    if (o.placeholders_sd && sd.open) {
        R_TRY(step("Deleting unfinished installs"));
        ncmContentStorageCleanupAllPlaceHolder(&sd.cs);
    }
    if (o.placeholders_nand && nand.open) {
        R_TRY(step("Deleting unfinished installs"));
        ncmContentStorageCleanupAllPlaceHolder(&nand.cs);
    }
    if (o.unused_tickets) {
        R_TRY(step("Deleting unused tickets"));
        R_TRY(DeleteUnusedTickets(both, out.removed));
    }
    if (o.erpt_reports) {
        R_TRY(step("Deleting error reports"));
        DeleteFolderContents(sd_fs, "/atmosphere/erpt_reports", out.removed);
    }
    if (o.contents_folders) {
        R_TRY(step("Deleting folders of removed games"));
        DeleteStaleContentsFolders(sd_fs, out.removed);
    }
    if (o.deleted_user_saves) {
        R_TRY(step("Deleting saves of removed users"));
        DeleteSavesOfRemovedUsers(out.removed);
    }

    s64 sd_fs_after{};
    sd_fs.GetFreeSpace("/", &sd_fs_after);
    out.freed_sd = std::max<s64>(0, sd_fs_after - sd_fs_before);
    out.freed_nand = std::max<s64>(0, nand.FreeSpace() - nand_before);
    log_write("[CLEAN] removed %u, freed sd %lld nand %lld\n", out.removed, (long long)out.freed_sd, (long long)out.freed_nand);
    R_SUCCEED();
}

} // namespace sphaira::cleanup
