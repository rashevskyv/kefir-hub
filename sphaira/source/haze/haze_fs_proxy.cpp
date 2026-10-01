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

struct FsProxy final : FsProxyBase {
    FsProxy(std::unique_ptr<fs::Fs>&& fs, const char* name, const char* display_name, const char* base_path = "")
    : FsProxyBase{name, display_name, base_path}
    , m_fs{std::forward<decltype(fs)>(fs)} {
    }

    ~FsProxy() {
        if (m_fs->IsNative()) {
            auto fs = (fs::FsNative*)m_fs.get();
            fsFsCommit(&fs->m_fs);
        }
#if ENABLE_NETWORK_INSTALL
        for (auto h : m_virtual_handles) {
            delete h;
        }
#endif
    }

    bool IsInterceptedPath(const char* path) const {
#if ENABLE_NETWORK_INSTALL
        if (m_name.empty()) { // microSD card
            const auto rule = FindRootDropRule(FixPath(path));
            if (rule && rule->action == RootDropAction::Install) {
                log_write("[IsInterceptedPath] INTERCEPTED: %s\n", path);
                return true;
            }
        }
#endif
        return false;
    }

    // returns the rule if this file should be written into another folder (e.g. .nro -> /switch).
    const RootDropRule* GetRedirectRule(const char* path) const {
        if (!m_name.empty()) { // only the microSD card has routing rules.
            return nullptr;
        }

        const auto rule = FindRootDropRule(FixPath(path));
        if (rule && rule->action == RootDropAction::RedirectDir) {
            return rule;
        }
        return nullptr;
    }

    // directory a redirected file should be written into,
    // e.g. app.nro -> /switch/app (per_name_subdir) or /switch.
    fs::FsPath GetRedirectDir(const RootDropRule* rule, const char* file_name) const {
        fs::FsPath dir;
        if (rule->per_name_subdir) {
            const char* ext = std::strrchr(file_name, '.');
            const auto stem_len = ext ? size_t(ext - file_name) : std::strlen(file_name);
            std::snprintf(dir, sizeof(dir), "%s/%.*s", rule->target_dir, (int)stem_len, file_name);
        } else {
            std::snprintf(dir, sizeof(dir), "%s", rule->target_dir);
        }
        return dir;
    }

    // FixPath() + applies redirect rules for files dropped into the root.
    fs::FsPath RoutePath(const char* path) const {
        const auto fixed = FixPath(path);
        if (const auto rule = GetRedirectRule(path)) {
            const auto name = GetLastComponent(fixed.s);
            const auto dir = GetRedirectDir(rule, name);
            fs::FsPath buf;
            std::snprintf(buf, sizeof(buf), "%s/%s", dir.s, name);
            log_write("[MTP-SD] redirecting %s -> %s\n", fixed.s, buf.s);
            return buf;
        }
        return fixed;
    }

    // TODO: impl this for stdio
    Result GetTotalSpace(const char *path, s64 *out) override {
        if (m_fs->IsNative()) {
            auto fs = (fs::FsNative*)m_fs.get();
            return fsFsGetTotalSpace(&fs->m_fs, FixPath(path), out);
        }
        *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        R_SUCCEED();
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        if (m_fs->IsNative()) {
            auto fs = (fs::FsNative*)m_fs.get();
            return fsFsGetFreeSpace(&fs->m_fs, FixPath(path), out);
        }
        *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        R_SUCCEED();
    }
    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override {
#if ENABLE_NETWORK_INSTALL
        if (IsInterceptedPath(path)) {
            const auto file_name = GetFileName(path);
            if (file_name) {
                auto it = std::ranges::find_if(m_virtual_entries, [file_name](const auto& e) {
                    return !strcasecmp(file_name, e.name);
                });
                if (it != m_virtual_entries.end()) {
                    *out_entry_type = FsDirEntryType_File;
                    R_SUCCEED();
                }
            }
        }
#endif
        const auto rc = m_fs->GetEntryType(RoutePath(path), out_entry_type);
        log_write("[HAZE] GetEntryType(%s) 0x%X\n", path, rc);
        return rc;
    }
    Result CreateFile(const char* path, s64 size, u32 option) override {
        log_write("[HAZE] CreateFile(%s)\n", path);
#if ENABLE_NETWORK_INSTALL
        if (IsInterceptedPath(path)) {
            SCOPED_MUTEX(&g_shared_data.mutex);
            if (!g_shared_data.enabled) {
                log_write("[MTP-SD] failing CreateFile as not enabled\n");
                R_THROW(FsError_NotImplemented);
            }

            const auto file_name = GetFileName(path);
            if (file_name) {
                std::erase_if(m_virtual_entries, [file_name](const auto& e) {
                    return strcasecmp(file_name, e.name) != 0;
                });
                auto it = std::ranges::find_if(m_virtual_entries, [file_name](const auto& e) {
                    return !strcasecmp(file_name, e.name);
                });
                if (it == m_virtual_entries.end()) {
                    FsDirectoryEntry entry{};
                    std::snprintf(entry.name, sizeof(entry.name), "%s", file_name);
                    entry.type = FsDirEntryType_File;
                    entry.file_size = size;
                    m_virtual_entries.emplace_back(entry);
                } else {
                    it->file_size = size;
                }
                R_SUCCEED();
            }
        }
#endif
        if (const auto rule = GetRedirectRule(path)) {
            // make sure the target folder exists before writing into it.
            m_fs->CreateDirectoryRecursively(GetRedirectDir(rule, GetLastComponent(FixPath(path))));
        }
        return m_fs->CreateFile(RoutePath(path), size, option);
    }
    Result DeleteFile(const char* path) override {
        log_write("[HAZE] DeleteFile(%s)\n", path);
#if ENABLE_NETWORK_INSTALL
        if (IsInterceptedPath(path)) {
            const auto file_name = GetFileName(path);
            if (file_name) {
                auto it = std::ranges::find_if(m_virtual_entries, [file_name](const auto& e) {
                    return !strcasecmp(file_name, e.name);
                });
                if (it != m_virtual_entries.end()) {
                    m_virtual_entries.erase(it);
                }
            }

            // also remove any physical leftover (e.g. a file copied before interception
            // worked) and always succeed, so that explorer's replace flow can proceed.
            const auto routed_path = RoutePath(path);
            m_fs->DeleteFile(routed_path);
            m_fs->Commit();
            ui::menu::homebrew::NotifyFileDeleted(routed_path.s);
            R_SUCCEED();
        }
#endif
        const auto routed_path = RoutePath(path);
        const auto rc = m_fs->DeleteFile(routed_path);
        if (R_SUCCEEDED(rc)) {
            m_fs->Commit();
            ui::menu::homebrew::NotifyFileDeleted(routed_path.s);
        }
        return rc;
    }
    Result RenameFile(const char *old_path, const char *new_path) override {
        log_write("[HAZE] RenameFile(%s -> %s)\n", old_path, new_path);
        const auto routed_old = RoutePath(old_path);
        const auto routed_new = RoutePath(new_path);
        const auto rc = m_fs->RenameFile(routed_old, routed_new);
        if (R_SUCCEEDED(rc)) {
            m_fs->Commit();
            ui::menu::homebrew::NotifyRename(routed_old.s, routed_new.s, false);
        }
        return rc;
    }
    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        log_write("[HAZE] OpenFile(%s)\n", path);
#if ENABLE_NETWORK_INSTALL
        if (IsInterceptedPath(path)) {
            const auto file_name = GetFileName(path);
            if (file_name) {
                auto it = std::ranges::find_if(m_virtual_entries, [file_name](const auto& e) {
                    return !strcasecmp(file_name, e.name);
                });

                // read-only open without a pending transfer: fall through to the real fs,
                // otherwise explorer sees a phantom 0-byte file.
                if (it != m_virtual_entries.end() || (mode & FsOpenMode_Write)) {
                    if (mode & FsOpenMode_Write) {
                        {
                            SCOPED_MUTEX(&g_shared_data.mutex);
                            if (!g_shared_data.enabled) {
                                log_write("[MTP-SD] installer not enabled, failing write open\n");
                                R_THROW(FsError_NotImplemented);
                            }
                            // only one transfer can be queued at a time.
                            R_UNLESS(g_shared_data.current_file.empty(), FsError_NotImplemented);
                            g_shared_data.current_file = file_name;
                        }

                        // on_thing() locks the mutex itself, it must be called after releasing.
                        on_thing();
                    }

                    auto virtual_file = new VirtualFile();
                    virtual_file->name = file_name;
                    virtual_file->size = it != m_virtual_entries.end() ? it->file_size : 0;
                    virtual_file->mode = mode;

                    m_virtual_handles.insert(virtual_file);
                    std::memcpy(&out_file->s, &virtual_file, sizeof(virtual_file));
                    R_SUCCEED();
                }
            }
        }
#endif
        const auto rule = GetRedirectRule(path);
        if (rule && (mode & FsOpenMode_Write)) {
            // make sure the target folder exists before writing into it.
            m_fs->CreateDirectoryRecursively(GetRedirectDir(rule, GetLastComponent(FixPath(path))));
        }

        const auto routed_path = RoutePath(path);
        auto fptr = new fs::File();
        const auto rc = m_fs->OpenFile(routed_path, mode, fptr);

        if (R_SUCCEEDED(rc)) {
            if (mode & FsOpenMode_Write) {
                m_open_write_files[fptr] = routed_path.s;
            }
            std::memcpy(&out_file->s, &fptr, sizeof(fptr));
        } else {
            delete fptr;
        }

        return rc;
    }
    Result GetFileSize(FsFile *file, s64 *out_size) override {
        log_write("[HAZE] GetFileSize()\n");
#if ENABLE_NETWORK_INSTALL
        void* ptr;
        std::memcpy(&ptr, &file->s, sizeof(ptr));
        if (m_virtual_handles.count(static_cast<VirtualFile*>(ptr))) {
            auto vf = static_cast<VirtualFile*>(ptr);
            *out_size = vf->size;
            R_SUCCEED();
        }
#endif
        fs::File* f;
        std::memcpy(&f, &file->s, sizeof(f));
        return f->GetSize(out_size);
    }
    Result SetFileSize(FsFile *file, s64 size) override {
        log_write("[HAZE] SetFileSize(%zd)\n", size);
#if ENABLE_NETWORK_INSTALL
        void* ptr;
        std::memcpy(&ptr, &file->s, sizeof(ptr));
        if (m_virtual_handles.count(static_cast<VirtualFile*>(ptr))) {
            auto vf = static_cast<VirtualFile*>(ptr);
            vf->size = size;
            R_SUCCEED();
        }
#endif
        fs::File* f;
        std::memcpy(&f, &file->s, sizeof(f));
        return f->SetSize(size);
    }
    // ReadFile/WriteFile run once per usb packet -- no logging in here, see the
    // note on Stream::ReadChunk in install_stream_menu_base.cpp.
    Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) override {
#if ENABLE_NETWORK_INSTALL
        void* ptr;
        std::memcpy(&ptr, &file->s, sizeof(ptr));
        if (m_virtual_handles.count(static_cast<VirtualFile*>(ptr))) {
            R_THROW(FsError_NotImplemented);
        }
#endif
        fs::File* f;
        std::memcpy(&f, &file->s, sizeof(f));
        return f->Read(off, buf, read_size, option, out_bytes_read);
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
#if ENABLE_NETWORK_INSTALL
        void* ptr;
        std::memcpy(&ptr, &file->s, sizeof(ptr));
        if (m_virtual_handles.count(static_cast<VirtualFile*>(ptr))) {
            auto vf = static_cast<VirtualFile*>(ptr);
            SCOPED_MUTEX(&g_shared_data.mutex);
            if (!g_shared_data.enabled) {
                log_write("[MTP-SD] failing WriteFile as not enabled\n");
                R_THROW(FsError_NotImplemented);
            }

            if (!g_shared_data.on_write || !g_shared_data.on_write(buf, write_size)) {
                log_write("[MTP-SD] failing WriteFile as not written\n");
                R_THROW(::haze::ResultCancelled());
            }

            vf->size = std::max<s64>(vf->size, off + write_size);
            R_SUCCEED();
        }
#endif
        fs::File* f;
        std::memcpy(&f, &file->s, sizeof(f));
        return f->Write(off, buf, write_size, option);
    }
    void CloseFile(FsFile *file) override {
        log_write("[HAZE] CloseFile()\n");
#if ENABLE_NETWORK_INSTALL
        void* ptr;
        std::memcpy(&ptr, &file->s, sizeof(ptr));
        if (m_virtual_handles.count(static_cast<VirtualFile*>(ptr))) {
            auto vf = static_cast<VirtualFile*>(ptr);
            bool update{};
            {
                SCOPED_MUTEX(&g_shared_data.mutex);
                if (vf->mode & FsOpenMode_Write) {
                    log_write("[MTP-SD] closing current file\n");
                    if (g_shared_data.on_close) {
                        g_shared_data.on_close();
                    }
                    g_shared_data.in_progress = false;
                    g_shared_data.current_file.clear();
                    update = true;
                }
            }

            if (update) {
                on_thing();
            }

            const auto file_name = vf->name;
            m_virtual_handles.erase(vf);
            delete vf;

            std::memset(file, 0, sizeof(*file));
            return;
        }
#endif
        fs::File* f;
        std::memcpy(&f, &file->s, sizeof(f));
        if (f) {
            auto it = m_open_write_files.find(f);
            if (it != m_open_write_files.end()) {
                const std::string written_path = std::move(it->second);
                m_open_write_files.erase(it);
                m_fs->Commit();
                ui::menu::homebrew::NotifyFileCreated(written_path);
            }
            delete f;
        }
        std::memset(file, 0, sizeof(*file));
    }

    Result CreateDirectory(const char* path) override {
        log_write("[HAZE] CreateDirectory(%s)\n", path);
        const auto fixed_path = FixPath(path);
        const auto rc = m_fs->CreateDirectory(fixed_path);
        if (R_SUCCEEDED(rc)) {
            m_fs->Commit();
            ui::menu::homebrew::NotifyDirectoryCreated(fixed_path);
        }
        return rc;
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        log_write("[HAZE] DeleteDirectoryRecursively(%s)\n", path);
        const auto fixed_path = FixPath(path);
        const auto rc = m_fs->DeleteDirectoryRecursively(fixed_path);
        if (R_SUCCEEDED(rc)) {
            m_fs->Commit();
            ui::menu::homebrew::NotifyDirectoryDeleted(fixed_path);
        }
        return rc;
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        log_write("[HAZE] RenameDirectory(%s -> %s)\n", old_path, new_path);
        const auto fixed_old = FixPath(old_path);
        const auto fixed_new = FixPath(new_path);
        const auto rc = m_fs->RenameDirectory(fixed_old, fixed_new);
        if (R_SUCCEEDED(rc)) {
            m_fs->Commit();
            ui::menu::homebrew::NotifyRename(fixed_old, fixed_new, true);
        }
        return rc;
    }
    Result OpenDirectory(const char *path, u32 mode, FsDir *out_dir) override {
        auto fptr = new fs::Dir();
        const auto rc = m_fs->OpenDirectory(FixPath(path), mode, fptr);

        if (R_SUCCEEDED(rc)) {
            std::memcpy(&out_dir->s, &fptr, sizeof(fptr));
        } else {
            delete fptr;
        }

        log_write("[HAZE] OpenDirectory(%s) 0x%X\n", path, rc);
        return rc;
    }
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override {
        fs::Dir* f;
        std::memcpy(&f, &d->s, sizeof(f));
        const auto rc = f->Read(out_total_entries, max_entries, buf);
        log_write("[HAZE] ReadDirectory(%zd) 0x%X\n", *out_total_entries, rc);
        return rc;
    }
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override {
        fs::Dir* f;
        std::memcpy(&f, &d->s, sizeof(f));
        const auto rc = f->GetEntryCount(out_count);
        log_write("[HAZE] GetDirectoryEntryCount(%zd) 0x%X\n", *out_count, rc);
        return rc;
    }
    void CloseDirectory(FsDir *d) override {
        log_write("[HAZE] CloseDirectory()\n");
        fs::Dir* f;
        std::memcpy(&f, &d->s, sizeof(f));
        if (f) {
            delete f;
        }
        std::memset(d, 0, sizeof(*d));
    }
    virtual bool MultiThreadTransfer(s64 size, bool read) override {
        // virtual backends (zip / ncm) are not safe for concurrent reads.
        if (m_fs->IsVirtual()) {
            return false;
        }
        return !App::IsFileBaseEmummc();
    }

private:
    std::unique_ptr<fs::Fs> m_fs{};
    // Tracks open files opened for write so CloseFile can notify the homebrew menu
    // via the shared mutation policy once the transfer successfully completes.
    std::map<fs::File*, std::string> m_open_write_files{};
#if ENABLE_NETWORK_INSTALL
    std::vector<FsDirectoryEntry> m_virtual_entries;
    std::set<VirtualFile*> m_virtual_handles;
#endif
};

std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsProxy(std::unique_ptr<fs::Fs> fs, const char* name, const char* display_name, const char* base_path) {
    return std::make_shared<FsProxy>(std::move(fs), name, display_name, base_path);
}

} // namespace sphaira::haze
