#include "haze/haze_internal.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "title_export_name.hpp"
#include "title_nsp.hpp"
#include "mtp_games_path.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/progress_box.hpp"
#include <usbhsfs.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <functional>
#include <haze.h>

namespace sphaira::haze {

struct FsSaveProxy final : FsProxyBase {
    FsSaveProxy(const char* name, const char* display_name) : FsProxyBase{name, display_name} {
        ScanSaves();
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        const auto pp = Parse(path);
        if (pp.depth >= 2) {
            std::shared_ptr<fs::FsNative> fs;
            if (R_SUCCEEDED(MountSave(pp, fs))) {
                return fs->GetTotalSpace("/", out);
            }
        }
        *out = 1024ULL * 1024ULL * 1024ULL * 32ULL;
        R_SUCCEED();
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        const auto pp = Parse(path);
        if (pp.depth >= 2) {
            std::shared_ptr<fs::FsNative> fs;
            if (R_SUCCEEDED(MountSave(pp, fs))) {
                return fs->GetFreeSpace("/", out);
            }
        }
        *out = 1024ULL * 1024ULL * 1024ULL * 32ULL;
        R_SUCCEED();
    }

    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override {
        const auto pp = Parse(path);

        // levels 0-2 are fully virtual directories.
        if (pp.depth < 3) {
            if (pp.depth >= 1) {
                const auto game = FindGame(pp.game);
                R_UNLESS(game, FsError_PathNotFound);
                R_UNLESS(pp.depth == 1 || game->contains(pp.type), FsError_PathNotFound);
            }
            *out_entry_type = FsDirEntryType_Dir;
            R_SUCCEED();
        }

        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));
        return fs->GetEntryType(pp.rest, out_entry_type);
    }

    // read-write save filesystem: mutating operations commit immediately to ensure
    // data persistence on the console.
    Result CreateFile(const char* path, s64 size, u32 option) override {
        log_write("[MTP-SAVES] CreateFile(%s)\n", path);
        const auto pp = Parse(path);
        R_UNLESS(pp.depth >= 3, FsError_PathNotFound);
        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));
        R_TRY(fs->CreateFile(pp.rest, size, option));
        R_TRY(fs->Commit());
        R_SUCCEED();
    }
    Result DeleteFile(const char* path) override {
        log_write("[MTP-SAVES] DeleteFile(%s)\n", path);
        const auto pp = Parse(path);
        R_UNLESS(pp.depth >= 3, FsError_PathNotFound);
        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));
        R_TRY(fs->DeleteFile(pp.rest));
        R_TRY(fs->Commit());
        R_SUCCEED();
    }
    Result RenameFile(const char *old_path, const char *new_path) override {
        log_write("[MTP-SAVES] RenameFile(%s -> %s)\n", old_path, new_path);
        const auto pp_old = Parse(old_path);
        const auto pp_new = Parse(new_path);
        R_UNLESS(pp_old.depth >= 3 && pp_new.depth >= 3, FsError_PathNotFound);
        R_UNLESS(pp_old.game == pp_new.game && pp_old.type == pp_new.type, FsError_PathNotFound);
        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp_old, fs));
        R_TRY(fs->RenameFile(pp_old.rest, pp_new.rest));
        R_TRY(fs->Commit());
        R_SUCCEED();
    }
    Result CreateDirectory(const char* path) override {
        log_write("[MTP-SAVES] CreateDirectory(%s)\n", path);
        const auto pp = Parse(path);
        R_UNLESS(pp.depth >= 3, FsError_PathNotFound);
        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));
        R_TRY(fs->CreateDirectory(pp.rest));
        R_TRY(fs->Commit());
        R_SUCCEED();
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        log_write("[MTP-SAVES] DeleteDirectoryRecursively(%s)\n", path);
        const auto pp = Parse(path);
        R_UNLESS(pp.depth >= 3, FsError_PathNotFound);
        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));
        R_TRY(fs->DeleteDirectoryRecursively(pp.rest));
        R_TRY(fs->Commit());
        R_SUCCEED();
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        log_write("[MTP-SAVES] RenameDirectory(%s -> %s)\n", old_path, new_path);
        const auto pp_old = Parse(old_path);
        const auto pp_new = Parse(new_path);
        R_UNLESS(pp_old.depth >= 3 && pp_new.depth >= 3, FsError_PathNotFound);
        R_UNLESS(pp_old.game == pp_new.game && pp_old.type == pp_new.type, FsError_PathNotFound);
        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp_old, fs));
        R_TRY(fs->RenameDirectory(pp_old.rest, pp_new.rest));
        R_TRY(fs->Commit());
        R_SUCCEED();
    }
    Result SetFileSize(FsFile *file, s64 size) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        R_UNLESS(h != nullptr, FsError_PathNotFound);
        return h->file.SetSize(size);
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        R_UNLESS(h != nullptr, FsError_PathNotFound);
        return h->file.Write(off, buf, write_size, option);
    }

    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        log_write("[MTP-SAVES] OpenFile(%s)\n", path);

        const auto pp = Parse(path);
        R_UNLESS(pp.depth >= 3, FsError_PathNotFound);

        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));

        auto handle = std::make_unique<FileHandle>();
        handle->fs = fs;
        handle->writable = (mode & (FsOpenMode_Write | FsOpenMode_Append)) != 0;
        R_TRY(fs->OpenFile(pp.rest, mode, &handle->file));

        auto raw = handle.release();
        std::memcpy(&out_file->s, &raw, sizeof(raw));
        R_SUCCEED();
    }
    Result GetFileSize(FsFile *file, s64 *out_size) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        return h->file.GetSize(out_size);
    }
    Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        return h->file.Read(off, buf, read_size, option, out_bytes_read);
    }
    void CloseFile(FsFile *file) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        if (h) {
            if (h->writable && h->fs) {
                h->fs->Commit();
            }
            delete h;
        }
        std::memset(file, 0, sizeof(*file));
    }

    Result OpenDirectory(const char *path, u32 mode, FsDir *out_dir) override {
        const auto pp = Parse(path);
        auto handle = std::make_unique<DirHandle>();

        if (pp.depth < 2) {
            const TypeMap* game{};
            if (pp.depth == 1) {
                game = FindGame(pp.game);
                R_UNLESS(game, FsError_PathNotFound);
            }

            handle->is_virtual = true;
            // levels 1-2 only contain directories.
            if (mode & FsDirOpenMode_ReadDirs) {
                if (game) {
                    for (const auto& [name, info] : *game) {
                        handle->virt.emplace_back(MakeVirtualDirEntry(name));
                    }
                } else {
                    for (const auto& [name, types] : m_tree) {
                        handle->virt.emplace_back(MakeVirtualDirEntry(name));
                    }
                }
            }
        } else {
            std::shared_ptr<fs::FsNative> fs;
            R_TRY(MountSave(pp, fs));
            handle->fs = fs;
            R_TRY(fs->OpenDirectory(pp.rest, mode, &handle->dir));
        }

        auto raw = handle.release();
        std::memcpy(&out_dir->s, &raw, sizeof(raw));
        log_write("[MTP-SAVES] OpenDirectory(%s) depth=%d\n", path, pp.depth);
        R_SUCCEED();
    }
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));

        if (h->is_virtual) {
            // same generation pattern as FsProxyVfs::ReadDirectory.
            max_entries = std::min<s64>(h->virt.size() - h->index, max_entries);
            std::memcpy(buf, h->virt.data() + h->index, max_entries * sizeof(*buf));
            h->index += max_entries;
            *out_total_entries = max_entries;
            R_SUCCEED();
        }

        return h->dir.Read(out_total_entries, max_entries, buf);
    }
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));

        if (h->is_virtual) {
            *out_count = h->virt.size();
            R_SUCCEED();
        }

        return h->dir.GetEntryCount(out_count);
    }
    void CloseDirectory(FsDir *d) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));
        delete h;
        std::memset(d, 0, sizeof(*d));
    }

    // saves are small, keep transfers single-threaded.
    bool MultiThreadTransfer(s64 size, bool read) override {
        return false;
    }

private:
    static constexpr size_t MOUNT_CACHE_MAX = 4;

    using TypeMap = std::map<std::string, FsSaveDataInfo, CaseInsensitiveLess>;

    struct CachedMount {
        std::shared_ptr<fs::FsNative> fs;
        u64 tick;
    };

    // handles keep a shared_ptr to their mount, so a mount evicted from the
    // LRU cache stays alive until every handle into it is closed.
    // note: member order matters - file/dir must be destroyed before fs.
    struct FileHandle {
        std::shared_ptr<fs::FsNative> fs{};
        fs::File file{};
        bool writable{false};
    };

    struct DirHandle {
        std::shared_ptr<fs::FsNative> fs{};
        fs::Dir dir{};
        std::vector<FsDirectoryEntry> virt{};
        s64 index{};
        bool is_virtual{};
    };

    struct ParsedPath {
        std::string game; // level 1 component.
        std::string type; // level 2 component.
        fs::FsPath rest{"/"}; // path inside the mounted save.
        int depth{}; // 0 = root, 1 = game, 2 = game/type, 3 = inside the save.
    };

    struct AccountName {
        AccountUid uid;
        std::string name;
    };

    static auto UidHex(const AccountUid& uid) -> std::string {
        char buf[0x30];
        std::snprintf(buf, sizeof(buf), "%016lX%016lX", uid.uid[0], uid.uid[1]);
        return buf;
    }

    static auto BuildAccountNames() -> std::vector<AccountName> {
        std::vector<AccountName> out;
        for (const auto& acc : App::GetAccountList()) {
            fs::FsPath buf{};
            std::snprintf(buf, sizeof(buf), "%s", acc.nickname);
            title::utilsReplaceIllegalCharacters(buf, true);

            auto name = TrimName(buf.s);
            if (name.empty()) {
                name = UidHex(acc.uid);
            }
            out.push_back({acc.uid, std::move(name)});
        }

        // two accounts with the same nickname get a short uid suffix to keep
        // their folders distinct.
        std::vector<bool> dup(out.size(), false);
        for (size_t i = 0; i < out.size(); i++) {
            for (size_t j = i + 1; j < out.size(); j++) {
                if (!strcasecmp(out[i].name.c_str(), out[j].name.c_str())) {
                    dup[i] = dup[j] = true;
                }
            }
        }
        for (size_t i = 0; i < out.size(); i++) {
            if (dup[i]) {
                out[i].name += " (" + UidHex(out[i].uid).substr(0, 8) + ")";
            }
        }

        return out;
    }

    static auto GetAccountDirName(const AccountUid& uid, const std::vector<AccountName>& accounts) -> std::string {
        for (const auto& acc : accounts) {
            if (!std::memcmp(&uid, &acc.uid, sizeof(uid))) {
                return acc.name;
            }
        }
        // account no longer exists - same fallback as save_menu's GetAccountName.
        return UidHex(uid);
    }

    static auto BuildTypeDirName(const FsSaveDataInfo& info, const std::vector<AccountName>& accounts) -> std::string {
        const auto subdir = ui::menu::save::GetSaveTypeSubdir(info.save_data_type).toString();

        if (info.save_data_type == FsSaveDataType_Account) {
            return subdir + " " + GetAccountDirName(info.uid, accounts);
        }

        // multiple cache saves per game are distinguished by their index.
        if (info.save_data_type == FsSaveDataType_Cache && info.save_data_index) {
            return subdir + " " + std::to_string(info.save_data_index);
        }

        return subdir;
    }

    // scans once at registration (never per ReadDirectory), building the
    // virtual tree: game dir -> type dir -> save info.
    void ScanSaves() {
        const auto accounts = BuildAccountNames();

        // ref-counted background loader used by title::Get() for game names.
        const bool has_title = R_SUCCEEDED(title::Init());
        ON_SCOPE_EXIT(if (has_title) { title::Exit(); });

        // user (non-system) save types only, first iteration.
        constexpr u8 SAVE_TYPES[] = {
            FsSaveDataType_Account,
            FsSaveDataType_Bcat,
            FsSaveDataType_Device,
            FsSaveDataType_Cache,
        };

        for (const auto data_type : SAVE_TYPES) {
            // mirrors GetFsSaveAttr() in save_menu.cpp: cache saves live in
            // the sd-user space, the rest in the user space.
            const auto space_id = data_type == FsSaveDataType_Cache ? FsSaveDataSpaceId_SdUser : FsSaveDataSpaceId_User;

            FsSaveDataFilter filter{};
            filter.attr.save_data_type = data_type;
            filter.filter_by_save_data_type = true;

            FsSaveDataInfoReader reader;
            if (R_FAILED(fsOpenSaveDataInfoReaderWithFilter(&reader, space_id, &filter))) {
                log_write("[MTP-SAVES] failed to open save info reader for type %u\n", data_type);
                continue;
            }
            ON_SCOPE_EXIT(fsSaveDataInfoReaderClose(&reader));

            std::vector<FsSaveDataInfo> info_list(256);
            while (true) {
                s64 record_count{};
                if (R_FAILED(fsSaveDataInfoReaderRead(&reader, info_list.data(), info_list.size(), &record_count)) || !record_count) {
                    break;
                }

                for (s64 i = 0; i < record_count; i++) {
                    const auto& info = info_list[i];
                    const auto game = BuildGameDirName(info.application_id);
                    const auto type = BuildTypeDirName(info, accounts);
                    // emplace: the first scanned save wins on (unlikely) key clashes.
                    m_tree[game].emplace(type, info);
                }
            }
        }

        log_write("[MTP-SAVES] scanned %zu games\n", m_tree.size());
    }

    auto Parse(const char* path) const -> ParsedPath {
        ParsedPath out{};
        const auto fixed = FixPath(path);

        // libhaze may produce double slashes ("//game"), skip empty components.
        const char* p = fixed.s;
        for (int level = 0; level < 2; level++) {
            while (*p == '/') {
                p++;
            }
            if (!*p) {
                return out;
            }

            const char* start = p;
            while (*p && *p != '/') {
                p++;
            }

            auto& dst = level == 0 ? out.game : out.type;
            dst.assign(start, p - start);
            out.depth = level + 1;
        }

        while (*p == '/') {
            p++;
        }
        if (*p) {
            out.depth = 3;
            std::snprintf(out.rest.s, sizeof(out.rest.s), "/%s", p);
        }
        return out;
    }

    auto FindGame(const std::string& game) const -> const TypeMap* {
        const auto it = m_tree.find(game);
        return it == m_tree.end() ? nullptr : &it->second;
    }

    Result MountSave(const ParsedPath& pp, std::shared_ptr<fs::FsNative>& out) {
        const auto game_it = m_tree.find(pp.game);
        R_UNLESS(game_it != m_tree.end(), FsError_PathNotFound);
        const auto type_it = game_it->second.find(pp.type);
        R_UNLESS(type_it != game_it->second.end(), FsError_PathNotFound);

        // canonical key, so that differently-cased requests share one mount.
        const auto key = game_it->first + "/" + type_it->first;

        // ops run on the haze thread(s) and may interleave, guard the cache.
        SCOPED_MUTEX(&m_mount_mutex);

        if (const auto it = m_mounts.find(key); it != m_mounts.end()) {
            it->second.tick = ++m_mount_tick;
            out = it->second.fs;
            R_SUCCEED();
        }

        const auto& info = type_it->second;
        FsSaveDataAttribute attr{};
        attr.application_id = info.application_id;
        attr.uid = info.uid;
        attr.system_save_data_id = info.system_save_data_id;
        attr.save_data_type = info.save_data_type;
        attr.save_data_rank = info.save_data_rank;
        attr.save_data_index = info.save_data_index;

        auto fs = std::make_shared<fs::FsNativeSave>((FsSaveDataType)info.save_data_type, (FsSaveDataSpaceId)info.save_data_space_id, &attr, false);
        if (const auto rc = fs->GetFsOpenResult(); R_FAILED(rc)) {
            // fallback to read-only if opening for write fails (e.g. system save permissions).
            fs = std::make_shared<fs::FsNativeSave>((FsSaveDataType)info.save_data_type, (FsSaveDataSpaceId)info.save_data_space_id, &attr, true);
            if (const auto rc2 = fs->GetFsOpenResult(); R_FAILED(rc2)) {
                log_write("[MTP-SAVES] failed to mount save %s 0x%X\n", key.c_str(), rc2);
                return rc2;
            }
        }

        if (m_mounts.size() >= MOUNT_CACHE_MAX) {
            // evict the least recently used mount. open handles keep their
            // own shared_ptr, so an evicted mount survives until they close.
            const auto lru = std::ranges::min_element(m_mounts, {}, [](const auto& e) { return e.second.tick; });
            m_mounts.erase(lru);
        }

        m_mounts.emplace(key, CachedMount{fs, ++m_mount_tick});
        out = fs;
        R_SUCCEED();
    }

    // level 1 (game) -> level 2 (type) -> save info. built once in the
    // constructor, immutable afterwards (safe for concurrent reads).
    std::map<std::string, TypeMap, CaseInsensitiveLess> m_tree{};

    // lazily mounted saves. cleared by the (default) destructor, which closes
    // every cached save fs when haze::Exit() drops g_fs_entries.
    std::map<std::string, CachedMount> m_mounts{};
    u64 m_mount_tick{};
    Mutex m_mount_mutex{};
};

std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsSaveProxy(const char* name, const char* display_name) {
    return std::make_shared<FsSaveProxy>(name, display_name);
}

} // namespace sphaira::haze
