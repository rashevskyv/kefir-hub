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
#include <span>
#include <vector>

// Every step is fail-closed: when a list it depends on (installed titles, game records, users) could
// not be read, the step does nothing and is counted as skipped. An unreadable or empty list must
// never make every file, ticket, folder or save look like a leftover.
namespace sphaira::cleanup {
namespace {

using Id16 = std::array<u8, 16>;

auto ToId(const void* p) -> Id16 {
    Id16 id;
    std::memcpy(id.data(), p, id.size());
    return id;
}

auto SameKey(const NcmContentMetaKey& a, const NcmContentMetaKey& b) -> bool {
    return a.id == b.id && a.version == b.version && a.type == b.type;
}

struct Storage {
    NcmStorageId id{};
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};
    bool open{};
    // false when the list of installed titles could not be read.
    bool keys_ok{};
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
        keys_ok = R_SUCCEEDED(ncm::ListAllKeys(&db, keys));
        if (!keys_ok) {
            keys.clear();
        }
    }

    auto Ready() const -> bool {
        return open && keys_ok;
    }

    // every content id the registered titles still use, their own meta nca included; the titles in
    // `except` are left out (they are about to be removed). False when one title could not be read.
    auto Referenced(std::set<Id16>& out, std::span<const NcmContentMetaKey> except = {}) -> bool {
        for (const auto& key : keys) {
            if (std::ranges::any_of(except, [&key](const auto& e){ return SameKey(e, key); })) {
                continue;
            }
            std::vector<NcmContentInfo> infos;
            if (R_FAILED(ncm::GetContentInfos(&db, &key, infos))) {
                log_write("[CLEAN] cannot read the contents of %016lX\n", key.id);
                return false;
            }
            for (const auto& info : infos) {
                out.emplace(ToId(&info.content_id));
            }
            NcmContentId meta_id;
            if (R_SUCCEEDED(ncmContentMetaDatabaseGetContentIdByType(&db, &meta_id, &key, NcmContentType_Meta))) {
                out.emplace(ToId(&meta_id));
            }
        }
        return true;
    }

    auto FreeSpace() -> s64 {
        s64 free{};
        ncmContentStorageGetFreeSpaceSize(&cs, &free);
        return free;
    }
};

Result DeleteOrphans(ui::ProgressBox* pbox, Storage& st, Report& out) {
    std::set<Id16> used;
    if (!st.Ready() || !st.Referenced(used)) {
        log_write("[CLEAN] lost content: skipped, the installed titles are not readable\n");
        out.skipped++;
        R_SUCCEED();
    }
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
            out.removed++;
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
    std::erase_if(records, [&key](const auto& r){ return SameKey(r.key, key); });
    if (records.size() == before || records.empty()) {
        R_SUCCEED();
    }
    R_TRY(ns::DeleteApplicationRecord(am.get(), app_id));
    return ns::PushApplicationRecord(am.get(), app_id, records.data(), records.size());
}

Result DeleteOldUpdates(ui::ProgressBox* pbox, std::span<Storage*> storages, Report& out) {
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

    // per storage: the files the kept titles use. An old patch may share a file with the newest one.
    std::vector<Where> doomed;
    for (const auto i : OldUpdates(patches)) {
        doomed.push_back(all[i]);
    }
    struct Keep { Storage* st; std::set<Id16> ids; bool ok; };
    std::vector<Keep> keeps;
    for (auto* st : storages) {
        std::vector<NcmContentMetaKey> except;
        for (const auto& w : doomed) {
            if (w.st == st) {
                except.push_back(w.key);
            }
        }
        Keep k{st, {}, st->Ready()};
        if (k.ok && !except.empty()) {
            k.ok = st->Referenced(k.ids, except);
        }
        keeps.push_back(std::move(k));
    }

    for (const auto& w : doomed) {
        R_TRY(pbox->ShouldExitResult());
        const auto keep = std::ranges::find_if(keeps, [&w](const auto& k){ return k.st == w.st; });
        if (keep == keeps.end() || !keep->ok) {
            log_write("[CLEAN] old update %016lX v%u: skipped, the installed titles are not readable\n", w.key.id, w.key.version);
            out.skipped++;
            continue;
        }
        log_write("[CLEAN] old update %016lX v%u\n", w.key.id, w.key.version);

        std::vector<NcmContentInfo> infos;
        if (R_FAILED(ncm::GetContentInfos(&w.st->db, &w.key, infos))) {
            out.skipped++;
            continue;
        }
        NcmContentId meta_id;
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetContentIdByType(&w.st->db, &meta_id, &w.key, NcmContentType_Meta))) {
            NcmContentInfo meta_info{};
            meta_info.content_id = meta_id;
            infos.push_back(meta_info);
        }

        bool failed = false;
        for (const auto& info : infos) {
            if (keep->ids.contains(ToId(&info.content_id))) {
                continue; // shared with a title that stays
            }
            if (R_FAILED(ncmContentStorageDelete(&w.st->cs, &info.content_id))) {
                // it may be gone already (the meta nca is listed twice on some titles)
                bool present{};
                if (R_SUCCEEDED(ncmContentStorageHas(&w.st->cs, &present, &info.content_id)) && present) {
                    failed = true;
                }
            }
        }
        if (failed) {
            log_write("[CLEAN] old update %016lX: a file could not be deleted, the title is kept\n", w.key.id);
            out.skipped++;
            continue;
        }

        if (R_FAILED(ncmContentMetaDatabaseRemove(&w.st->db, &w.key)) || R_FAILED(ncmContentMetaDatabaseCommit(&w.st->db))) {
            out.skipped++;
            continue;
        }
        const auto rc = RemoveFromRecord(ncm::GetAppId(w.key), w.key);
        if (R_FAILED(rc)) {
            log_write("[CLEAN] application record of %016lX not updated: 0x%X\n", ncm::GetAppId(w.key), rc);
        }
        out.removed++;
    }
    for (auto* st : storages) {
        st->LoadKeys();
    }
    R_SUCCEED();
}

Result DeleteUnusedTickets(ui::ProgressBox* pbox, std::span<Storage*> storages, Report& out) {
    // a ticket is unused only when every installed title, on both storages, was read and none needs it.
    std::set<Id16> used;
    for (auto* st : storages) {
        if (!st->Ready()) {
            log_write("[CLEAN] tickets: skipped, the installed titles are not readable\n");
            out.skipped++;
            R_SUCCEED();
        }
        for (const auto& key : st->keys) {
            R_TRY(pbox->ShouldExitResult());
            std::vector<NcmContentInfo> infos;
            if (R_FAILED(ncm::GetContentInfos(&st->db, &key, infos))) {
                log_write("[CLEAN] tickets: skipped, cannot read %016lX\n", key.id);
                out.skipped++;
                R_SUCCEED();
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
        R_TRY(pbox->ShouldExitResult());
        R_TRY(es::DeleteTicket(unused.data(), unused.size()));
        out.removed += unused.size();
    }
    R_SUCCEED();
}

Result DeleteFolderContents(ui::ProgressBox* pbox, fs::Fs& fs, const fs::FsPath& dir, u32& removed) {
    fs::Dir d;
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(fs.OpenDirectory(dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d)) || R_FAILED(d.ReadAll(entries))) {
        R_SUCCEED();
    }
    for (const auto& e : entries) {
        R_TRY(pbox->ShouldExitResult());
        const auto path = fs::AppendPath(dir, e.name);
        if (R_SUCCEEDED(e.type == FsDirEntryType_Dir ? fs.DeleteDirectoryRecursively(path) : fs.DeleteFile(path))) {
            removed++;
        }
    }
    R_SUCCEED();
}

// folders for games that are no longer on the console; sysmodules never match IsGameContentsFolder.
Result DeleteStaleContentsFolders(ui::ProgressBox* pbox, fs::Fs& fs, Report& out) {
    std::set<u64> known;
    std::vector<NsApplicationRecord> records(1000);
    bool listed = true;
    for (s32 offset = 0;;) {
        s32 count{};
        if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count))) {
            listed = false;
            break;
        }
        if (!count) {
            break;
        }
        for (s32 i = 0; i < count; i++) {
            known.emplace(records[i].application_id);
        }
        offset += count;
    }
    // no records at all can only be a failed read on a console that has games: do nothing.
    if (!listed || known.empty()) {
        log_write("[CLEAN] contents folders: skipped, the game records are not readable\n");
        out.skipped++;
        R_SUCCEED();
    }

    fs::Dir d;
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(fs.OpenDirectory("/atmosphere/contents", FsDirOpenMode_ReadDirs, &d)) || R_FAILED(d.ReadAll(entries))) {
        R_SUCCEED();
    }
    for (const auto& e : entries) {
        R_TRY(pbox->ShouldExitResult());
        const auto tid = forced_language::ParseTitleId(e.name);
        if (!IsGameContentsFolder(e.name) || known.contains(tid)) {
            continue;
        }
        log_write("[CLEAN] stale contents folder %s\n", e.name);
        if (R_SUCCEEDED(fs.DeleteDirectoryRecursively(fs::AppendPath("/atmosphere/contents", e.name)))) {
            out.removed++;
        }
    }
    R_SUCCEED();
}

Result DeleteSavesOfRemovedUsers(ui::ProgressBox* pbox, Report& out) {
    std::vector<AccountUid> users;
    for (const auto& acc : App::GetAccountList()) {
        users.push_back(acc.uid);
    }
    // the list is empty when the account service failed: every save would look like a removed user's.
    if (users.empty()) {
        log_write("[CLEAN] saves: skipped, the user list is not readable\n");
        out.skipped++;
        R_SUCCEED();
    }
    FsSaveDataInfoReader reader;
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        R_SUCCEED();
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
        R_TRY(pbox->ShouldExitResult());
        if (R_SUCCEEDED(fsDeleteSaveDataFileSystemBySaveDataSpaceId(FsSaveDataSpaceId_User, id))) {
            out.removed++;
        }
    }
    R_SUCCEED();
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

    // a step that is cancelled or fails ends the run, but the report below still counts what was
    // done, so a stopped cleanup says how much it freed.
    Result rc = 0;
    const auto step = [&](bool enabled, const char* name, auto fn) {
        if (!enabled || R_FAILED(rc)) {
            return;
        }
        pbox->NewTransfer(i18n::get(name));
        rc = pbox->ShouldExitResult();
        if (R_SUCCEEDED(rc)) {
            rc = fn();
        }
    };

    step(o.old_updates, "Deleting old game updates", [&]{ return DeleteOldUpdates(pbox, both, out); });
    step(o.orphans_sd && sd.open, "Deleting lost content on the SD card", [&]{ return DeleteOrphans(pbox, sd, out); });
    step(o.orphans_nand && nand.open, "Deleting lost content in system memory", [&]{ return DeleteOrphans(pbox, nand, out); });
    step(o.placeholders_sd && sd.open, "Deleting unfinished installs", [&]{ log_write("[CLEAN] placeholders on SD: 0x%X\n", ncmContentStorageCleanupAllPlaceHolder(&sd.cs)); return Result{0}; });
    step(o.placeholders_nand && nand.open, "Deleting unfinished installs", [&]{ log_write("[CLEAN] placeholders in NAND: 0x%X\n", ncmContentStorageCleanupAllPlaceHolder(&nand.cs)); return Result{0}; });
    step(o.unused_tickets, "Deleting unused tickets", [&]{ return DeleteUnusedTickets(pbox, both, out); });
    step(o.erpt_reports, "Deleting error reports", [&]{ return DeleteFolderContents(pbox, sd_fs, "/atmosphere/erpt_reports", out.removed); });
    step(o.contents_folders, "Deleting folders of removed games", [&]{ return DeleteStaleContentsFolders(pbox, sd_fs, out); });
    step(o.deleted_user_saves, "Deleting saves of removed users", [&]{ return DeleteSavesOfRemovedUsers(pbox, out); });

    s64 sd_fs_after{};
    sd_fs.GetFreeSpace("/", &sd_fs_after);
    out.freed_sd = std::max<s64>(0, sd_fs_after - sd_fs_before);
    out.freed_nand = std::max<s64>(0, nand.FreeSpace() - nand_before);
    log_write("[CLEAN] removed %u, skipped %u, freed sd %lld nand %lld, result 0x%X\n", out.removed, out.skipped, (long long)out.freed_sd, (long long)out.freed_nand, rc);
    return rc;
}

} // namespace sphaira::cleanup
