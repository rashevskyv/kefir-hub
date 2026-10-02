#include "haze/haze_internal.hpp"
#include "haze/haze_save_proxy_internal.hpp"

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
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <functional>
#include <haze.h>

namespace sphaira::haze {

struct FsSaveProxy;

namespace {

// the live MTP "saves" storage (at most one), so a save restore can drop its cached mounts.
Mutex g_save_proxy_mutex{};
FsSaveProxy* g_save_proxy{}; // guarded by g_save_proxy_mutex

} // namespace

struct FsSaveProxy final : FsProxyBase {
    FsSaveProxy(const char* name, const char* display_name) : FsProxyBase{name, display_name} {
        ScanMtpSaves(m_tree);
        SCOPED_MUTEX(&g_save_proxy_mutex);
        g_save_proxy = this;
    }

    ~FsSaveProxy() {
        SCOPED_MUTEX(&g_save_proxy_mutex);
        if (g_save_proxy == this) {
            g_save_proxy = nullptr;
        }
    }

    // closes the cached save filesystems. false if the PC has a file or folder of a save open.
    bool ReleaseMounts() {
        SCOPED_MUTEX(&m_mount_mutex);
        for (const auto& [key, mount] : m_mounts) {
            if (mount.fs.use_count() > 1) {
                return false;
            }
        }
        m_mounts.clear();
        return true;
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        const auto pp = Parse(path);
        if (pp.depth >= 2) {
            std::shared_ptr<fs::FsNative> fs;
            R_TRY(MountSave(pp, fs));
            return fs->GetTotalSpace("/", out);
        }
        *out = 1024ULL * 1024ULL * 1024ULL * 32ULL;
        R_SUCCEED();
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        const auto pp = Parse(path);
        if (pp.depth >= 2) {
            std::shared_ptr<fs::FsNative> fs;
            R_TRY(MountSave(pp, fs));
            return fs->GetFreeSpace("/", out);
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

    // the drive is a view of decrypted saves: everything that would modify
    // it is rejected fail-closed, matching the settings contract.
    Result CreateFile(const char* path, s64 size, u32 option) override {
        log_write("[MTP-SAVES] rejecting CreateFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteFile(const char* path) override {
        log_write("[MTP-SAVES] rejecting DeleteFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameFile(const char *old_path, const char *new_path) override {
        log_write("[MTP-SAVES] rejecting RenameFile(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result CreateDirectory(const char* path) override {
        log_write("[MTP-SAVES] rejecting CreateDirectory(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        log_write("[MTP-SAVES] rejecting DeleteDirectoryRecursively(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        log_write("[MTP-SAVES] rejecting RenameDirectory(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result SetFileSize(FsFile *file, s64 size) override {
        log_write("[MTP-SAVES] rejecting SetFileSize()\n");
        R_THROW(FsError_NotImplemented);
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        log_write("[MTP-SAVES] rejecting WriteFile()\n");
        R_THROW(FsError_NotImplemented);
    }

    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        log_write("[MTP-SAVES] OpenFile(%s)\n", path);
        R_UNLESS(!(mode & (FsOpenMode_Write | FsOpenMode_Append)), FsError_NotImplemented);

        const auto pp = Parse(path);
        R_UNLESS(pp.depth >= 3, FsError_PathNotFound);

        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));

        auto handle = std::make_unique<FileHandle>();
        handle->fs = fs;
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
        delete h;
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

    using TypeMap = SaveTypeMap;

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

        auto fs = std::make_shared<fs::FsNativeSave>((FsSaveDataType)info.save_data_type, (FsSaveDataSpaceId)info.save_data_space_id, &attr, true);
        if (const auto rc = fs->GetFsOpenResult(); R_FAILED(rc)) {
            log_write("[MTP-SAVES] failed to mount save %s 0x%X\n", key.c_str(), rc);
            return rc;
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
    SaveTreeMap m_tree{};

    // lazily mounted saves. cleared by the (default) destructor, which closes
    // every cached save fs when haze::Exit() drops g_fs_entries.
    std::map<std::string, CachedMount> m_mounts{};
    u64 m_mount_tick{};
    Mutex m_mount_mutex{};
};

std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsSaveProxy(const char* name, const char* display_name) {
    return std::make_shared<FsSaveProxy>(name, display_name);
}

bool ReleaseSaveMounts() {
    SCOPED_MUTEX(&g_save_proxy_mutex);
    return !g_save_proxy || g_save_proxy->ReleaseMounts();
}

} // namespace sphaira::haze
