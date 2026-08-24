#include "haze_helper.hpp"

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
namespace {

#if ENABLE_NETWORK_INSTALL
struct InstallSharedData {
    Mutex mutex;
    std::string current_file;

    void* user;
    OnInstallStart on_start;
    OnInstallWrite on_write;
    OnInstallClose on_close;

    bool in_progress;
    bool enabled;
};
#endif

constexpr int THREAD_PRIO = 0x20;
constexpr int THREAD_CORE = 2;
std::atomic_bool g_should_exit = false;
bool g_is_running{false};
Mutex g_mutex{};

Mutex g_mtp_ui_mutex;
ui::ProgressBox* g_mtp_pbox{nullptr};
UEvent g_mtp_done_event;
bool g_mtp_ui_alive{false};
std::string g_mtp_current_filename;
std::atomic<bool> g_mtp_new_transfer{false};

// an optional extra "pinned" storage (a folder or a virtual mount) exposed on
// top of the configured MTP storages. Set via MountFs(), cleared by
// UnmountPinned(); Init() includes it when the factory is set.
std::vector<PinnedMount> g_pinned{};

#if ENABLE_NETWORK_INSTALL
InstallSharedData g_shared_data{};

const char* SUPPORTED_EXT[] = {
    ".nsp", ".xci", ".nsz", ".xcz",
};

// ive given up with good names.
void on_thing() {
    log_write("[MTP] doing on_thing\n");
    SCOPED_MUTEX(&g_shared_data.mutex);
    log_write("[MTP] locked on_thing\n");

    if (!g_shared_data.in_progress) {
        if (!g_shared_data.current_file.empty()) {
            log_write("[MTP] pushing new file data\n");
            if (!g_shared_data.on_start || !g_shared_data.on_start(g_shared_data.current_file.c_str())) {
                g_shared_data.current_file.clear();
            } else {
                log_write("[MTP] success on new file push\n");
                g_shared_data.in_progress = true;
            }
        }
    }
}
#endif

const char* GetFileName(const char* s) {
    const auto file_name = std::strrchr(s, '/');
    if (!file_name || file_name[1] == '\0') {
        return nullptr;
    }
    return file_name + 1;
}

// parent directory of an MTP path ("/16.0.3/file.nca" -> "/16.0.3"). used as the
// progress "address" line so a folder copy shows the folder, not each filename.
std::string MtpParentDir(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

// routing rules for files dropped into the root of the microSD card over MTP.
// to change how a file type is handled, add / edit a rule in ROOT_DROP_RULES.
enum class RootDropAction {
    Install,     // stream the file to the background installer, nothing is written to SD.
    RedirectDir, // write the file into target_dir instead of the root.
};

struct RootDropRule {
    std::span<const char* const> extensions;
    RootDropAction action;
    const char* target_dir;   // only used by RedirectDir.
    bool per_name_subdir;     // RedirectDir: write into target_dir/<file name without ext>/.
};

const char* NRO_EXT[] = {
    ".nro",
};

const RootDropRule ROOT_DROP_RULES[] = {
#if ENABLE_NETWORK_INSTALL
    { SUPPORTED_EXT, RootDropAction::Install, nullptr, false },
#endif
    { NRO_EXT, RootDropAction::RedirectDir, "/switch", true },
};

// returns the last path component, works with or without slashes.
const char* GetLastComponent(const char* s) {
    const auto p = std::strrchr(s, '/');
    return p ? p + 1 : s;
}

// libhaze builds paths as parent + "/" + name, where the root of an unnamed
// fs (the microSD card) is "/". a file in the root thus arrives as "//file.nsz".
// count non-empty components rather than slashes to handle all forms:
// "file.nsz", "/file.nsz" and "//file.nsz" are root, "//switch/file.nro" is not.
bool IsRootPath(const char* path) {
    int components = 0;
    for (const char* p = path; *p;) {
        while (*p == '/') p++;
        if (*p) {
            components++;
            while (*p && *p != '/') p++;
        }
    }
    return components == 1;
}

const RootDropRule* FindRootDropRule(const char* fixed_path) {
    if (!IsRootPath(fixed_path)) {
        return nullptr;
    }

    const char* ext = std::strrchr(fixed_path, '.');
    if (!ext) {
        return nullptr;
    }

    for (const auto& rule : ROOT_DROP_RULES) {
        for (const auto rule_ext : rule.extensions) {
            if (!strcasecmp(ext, rule_ext)) {
                return &rule;
            }
        }
    }

    return nullptr;
}



struct FsProxyBase : ::haze::FileSystemProxyImpl {
    FsProxyBase(const char* name, const char* display_name, const char* base_path = "")
    : m_name{name}, m_display_name{display_name}, m_base_path{base_path} {

    }

    auto FixPath(const char* path) const {
        fs::FsPath stripped;
        const auto len = std::strlen(GetName());

        if (len && !strncasecmp(path + 1, GetName(), len)) {
            std::snprintf(stripped, sizeof(stripped), "/%s", path + 1 + len);
        } else {
            std::strcpy(stripped, path);
        }

        // root the storage at m_base_path when set (e.g. a specific folder):
        // "/x" becomes "<base>/x".
        fs::FsPath buf;
        if (!m_base_path.empty()) {
            std::snprintf(buf, sizeof(buf), "%s%s", m_base_path.c_str(), stripped.s);
        } else {
            std::strcpy(buf, stripped);
        }

        // log_write("[FixPath] %s -> %s\n", path, buf.s);
        return buf;
    }

    const char* GetName() const override {
        return m_name.c_str();
    }
    const char* GetDisplayName() const override {
        return m_display_name.c_str();
    }

protected:
    const std::string m_name;
    const std::string m_display_name;
    const std::string m_base_path;
};

struct VirtualFile {
    std::string name;
    s64 size;
    u32 mode;
};

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
                    std::strcpy(entry.name, file_name);
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
                R_THROW(FsError_NotImplemented);
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

// fake fs that allows for files to create r/w on the root.
// folders are not yet supported.
struct FsProxyVfs : FsProxyBase {
    using FsProxyBase::FsProxyBase;
    virtual ~FsProxyVfs() = default;

    virtual Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) {
        if (FixPath(path) == "/") {
            *out_entry_type = FsDirEntryType_Dir;
            R_SUCCEED();
        } else {
            const auto file_name = GetFileName(path);
            R_UNLESS(file_name, FsError_PathNotFound);

            const auto it = std::ranges::find_if(m_entries, [file_name](auto& e){
                return !strcasecmp(file_name, e.name);
            });
            R_UNLESS(it != m_entries.end(), FsError_PathNotFound);

            *out_entry_type = FsDirEntryType_File;
            R_SUCCEED();
        }
    }
    virtual Result CreateFile(const char* path, s64 size, u32 option) {
        const auto file_name = GetFileName(path);
        R_UNLESS(file_name, FsError_PathNotFound);

        const auto it = std::ranges::find_if(m_entries, [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        R_UNLESS(it == m_entries.end(), FsError_PathAlreadyExists);

        FsDirectoryEntry entry{};
        std::strcpy(entry.name, file_name);
        entry.type = FsDirEntryType_File;
        entry.file_size = size;

        m_entries.emplace_back(entry);
        R_SUCCEED();
    }
    virtual Result DeleteFile(const char* path) {
        const auto file_name = GetFileName(path);
        R_UNLESS(file_name, FsError_PathNotFound);

        const auto it = std::ranges::find_if(m_entries, [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        R_UNLESS(it != m_entries.end(), FsError_PathNotFound);

        m_entries.erase(it);
        R_SUCCEED();
    }
    virtual Result RenameFile(const char *old_path, const char *new_path) {
        const auto file_name = GetFileName(old_path);
        R_UNLESS(file_name, FsError_PathNotFound);

        const auto it = std::ranges::find_if(m_entries, [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        R_UNLESS(it != m_entries.end(), FsError_PathNotFound);

        const auto file_name_new = GetFileName(new_path);
        R_UNLESS(file_name_new, FsError_PathNotFound);

        const auto new_it = std::ranges::find_if(m_entries, [file_name_new](auto& e){
            return !strcasecmp(file_name_new, e.name);
        });
        R_UNLESS(new_it == m_entries.end(), FsError_PathAlreadyExists);

        std::strcpy(it->name, file_name_new);
        R_SUCCEED();
    }
    virtual Result OpenFile(const char *path, u32 mode, FsFile *out_file) {
        const auto file_name = GetFileName(path);
        R_UNLESS(file_name, FsError_PathNotFound);

        const auto it = std::ranges::find_if(m_entries, [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        R_UNLESS(it != m_entries.end(), FsError_PathNotFound);

        out_file->s.object_id = std::distance(m_entries.begin(), it);
        out_file->s.own_handle = mode;
        R_SUCCEED();
    }
    virtual Result GetFileSize(FsFile *file, s64 *out_size) {
        auto& e = m_entries[file->s.object_id];
        *out_size = e.file_size;
        R_SUCCEED();
    }
    virtual Result SetFileSize(FsFile *file, s64 size) {
        auto& e = m_entries[file->s.object_id];
        e.file_size = size;
        R_SUCCEED();
    }
    virtual Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) {
        // stub for now as it may confuse users who think that the returned file is valid.
        // the code below can be used to benchmark mtp reads.
        R_THROW(FsError_NotImplemented);
        // auto& e = m_entries[file->s.object_id];
        // read_size = std::min<s64>(e.file_size - off, read_size);
        // std::memset(buf, 0, read_size);
        // *out_bytes_read = read_size;
        // R_SUCCEED();
    }
    virtual Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) {
        auto& e = m_entries[file->s.object_id];
        e.file_size = std::max<s64>(e.file_size, off + write_size);
        R_SUCCEED();
    }
    virtual void CloseFile(FsFile *file) {
        std::memset(file, 0, sizeof(*file));
    }

    Result CreateDirectory(const char* path) override {
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        R_THROW(FsError_NotImplemented);
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        R_THROW(FsError_NotImplemented);
    }
    Result OpenDirectory(const char *path, u32 mode, FsDir *out_dir) override {
        std::memset(out_dir, 0, sizeof(*out_dir));
        R_SUCCEED();
    }
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override {
        max_entries = std::min<s64>(m_entries.size()- d->s.object_id, max_entries);
        std::memcpy(buf, m_entries.data() + d->s.object_id, max_entries * sizeof(*buf));
        d->s.object_id += max_entries;
        *out_total_entries = max_entries;
        R_SUCCEED();
    }
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override {
        *out_count = m_entries.size();
        R_SUCCEED();
    }
    void CloseDirectory(FsDir *d) override {
        std::memset(d, 0, sizeof(*d));
    }

protected:
    std::vector<FsDirectoryEntry> m_entries;
};



#if ENABLE_NETWORK_INSTALL
struct FsInstallProxy final : FsProxyVfs {
    using FsProxyVfs::FsProxyVfs;

    Result FailedIfNotEnabled() {
        SCOPED_MUTEX(&g_shared_data.mutex);
        if (!g_shared_data.enabled) {
            App::Notify("Please launch MTP install menu before trying to install"_i18n);
            R_THROW(FsError_NotImplemented);
        }
        R_SUCCEED();
    }

    Result IsValidFileType(const char* name) {
        const char* ext = std::strrchr(name, '.');
        if (!ext) {
            R_THROW(FsError_NotImplemented);
        }

        bool found = false;
        for (size_t i = 0; i < std::size(SUPPORTED_EXT); i++) {
            if (!strcasecmp(ext, SUPPORTED_EXT[i])) {
                found = true;
                break;
            }
        }

        if (!found) {
            R_THROW(FsError_NotImplemented);
        }

        R_SUCCEED();
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        if (App::GetInstallSdEnable()) {
            return fs::FsNativeContentStorage(FsContentStorageId_SdCard).GetTotalSpace("/", out);
        } else {
            return fs::FsNativeContentStorage(FsContentStorageId_User).GetTotalSpace("/", out);
        }
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        if (App::GetInstallSdEnable()) {
            return fs::FsNativeContentStorage(FsContentStorageId_SdCard).GetFreeSpace("/", out);
        } else {
            return fs::FsNativeContentStorage(FsContentStorageId_User).GetFreeSpace("/", out);
        }
    }

    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override {
        R_TRY(FsProxyVfs::GetEntryType(path, out_entry_type));
        if (*out_entry_type == FsDirEntryType_File) {
            R_TRY(FailedIfNotEnabled());
        }
        R_SUCCEED();
    }
    Result CreateFile(const char* path, s64 size, u32 option) override {
        R_TRY(FailedIfNotEnabled());
        R_TRY(IsValidFileType(path));
        R_TRY(FsProxyVfs::CreateFile(path, size, option));
        R_SUCCEED();
    }
    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        R_TRY(FailedIfNotEnabled());
        R_TRY(IsValidFileType(path));
        R_TRY(FsProxyVfs::OpenFile(path, mode, out_file));
        log_write("[MTP] done file open: %s mode: 0x%X\n", path, mode);

        if (mode & FsOpenMode_Write) {
            const auto& e = m_entries[out_file->s.object_id];

            // check if we already have this file queued.
            log_write("[MTP] checking if empty\n");
            R_UNLESS(g_shared_data.current_file.empty(), FsError_NotImplemented);
            log_write("[MTP] is empty\n");
            g_shared_data.current_file = e.name;
            on_thing();
        }

        log_write("[MTP] got file: %s\n", path);
        R_SUCCEED();
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        SCOPED_MUTEX(&g_shared_data.mutex);
        if (!g_shared_data.enabled) {
            log_write("[MTP] failing as not enabled\n");
            R_THROW(FsError_NotImplemented);
        }

        if (!g_shared_data.on_write || !g_shared_data.on_write(buf, write_size)) {
            log_write("[MTP] failing as not written\n");
            R_THROW(FsError_NotImplemented);
        }

        R_TRY(FsProxyVfs::WriteFile(file, off, buf, write_size, option));
        R_SUCCEED();
    }
    void CloseFile(FsFile *file) override {
        bool update{};
        {
            SCOPED_MUTEX(&g_shared_data.mutex);
            if (file->s.own_handle & FsOpenMode_Write) {
                log_write("[MTP] closing current file\n");
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

        FsProxyVfs::CloseFile(file);
    }

    // installs are already multi-threaded via yati.
    bool MultiThreadTransfer(s64 size, bool read) override {
        App::IsFileBaseEmummc();
        return false;
    }
};
#endif

// keys of a virtual tree are matched case-insensitively, as windows treats
// paths case-insensitively and may send back a differently-cased path.
struct CaseInsensitiveLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    }
};

// strips leading spaces and trailing spaces / dots (invalid in windows
// folder names).
auto TrimName(std::string s) -> std::string {
    while (!s.empty() && s.front() == ' ') {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) {
        s.pop_back();
    }
    return s;
}

auto MakeVirtualDirEntry(const std::string& name) -> FsDirectoryEntry {
    FsDirectoryEntry e{};
    std::snprintf(e.name, sizeof(e.name), "%s", name.c_str());
    e.type = FsDirEntryType_Dir;
    return e;
}

auto MakeVirtualFileEntry(const std::string& name, s64 size) -> FsDirectoryEntry {
    FsDirectoryEntry e{};
    std::snprintf(e.name, sizeof(e.name), "%s", name.c_str());
    e.type = FsDirEntryType_File;
    e.file_size = size;
    return e;
}

// "<Game Name> [TitleID]", or "[TitleID]" when no usable name exists.
// note: title::Init() must be held by the caller, so title::Get() can load the
// control data.
auto BuildGameDirName(u64 application_id) -> std::string {
    const char* localized_name = nullptr;
    if (const auto data = title::Get(application_id); data && data->status == title::NacpLoadStatus::Loaded) {
        localized_name = data->lang.name;
    }
    return title::FormatMtpGameDirName(localized_name, nullptr, nullptr, application_id, sizeof(FsDirectoryEntry::name) - 1);
}


// read-only virtual drive exposing decrypted game saves as
// /<GameName [TitleID]>/<Account <user> | BCAT | Device | Cache>/<save files>.
// the save list is scanned once at registration; each save is mounted lazily
// (read-only) on first access and kept in a small LRU cache.
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

// read-only virtual drive of installed titles as NSP dumps. layout is a
// setting: compatible (merged NSPs at the root), separate (a folder per game),
// or both (Merged/, Separate/, Forwarders/ — names follow the UI language).
// HOME-menu forwarders are never mixed into the game folders. copying a file
// to the pc streams it from ncm; nothing is written to the sd.
struct FsGameProxy final : FsProxyBase {
    FsGameProxy(const char* name, const char* display_name) : FsProxyBase{name, display_name} {
        // holds the ncm storages (and the control loader) open for our
        // lifetime - every listing and every read goes through them.
        m_has_title = R_SUCCEEDED(title::Init());
        m_is_file_based_emummc = App::IsFileBaseEmummc();
        m_layout = static_cast<sphaira::mtp::GamesLayout>(App::GetMtpGamesLayout());
        m_names.merged = "Merged"_i18n;
        m_names.separate = "Separate"_i18n;
        m_names.forwarders = "Forwarders"_i18n;
        m_names.readme = "Readme.txt"_i18n;
        ScanGames();
        log_write("[MTP-GAMES] layout=%d scanned %zu games, %zu forwarders\n",
            static_cast<int>(m_layout), m_games.size(), m_forwarders.size());
    }

    ~FsGameProxy() {
        if (m_has_title) {
            title::Exit();
        }
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        R_SUCCEED();
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        // read-only drive: nothing can be written into it.
        *out = 0;
        R_SUCCEED();
    }

    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override {
        const auto pp = Parse(path);

        switch (pp.kind) {
            case sphaira::mtp::PathKind::Root:
            case sphaira::mtp::PathKind::MergedDir:
            case sphaira::mtp::PathKind::SeparateDir:
            case sphaira::mtp::PathKind::ForwardersDir:
                *out_entry_type = FsDirEntryType_Dir;
                R_SUCCEED();

            case sphaira::mtp::PathKind::SeparateGameDir: {
                R_UNLESS(m_games.count(pp.game), FsError_PathNotFound);
                *out_entry_type = FsDirEntryType_Dir;
                R_SUCCEED();
            }

            case sphaira::mtp::PathKind::InfoFile:
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();

            case sphaira::mtp::PathKind::MergedFile: {
                std::shared_ptr<GameNsp> nsp;
                size_t index{};
                R_TRY(FindMergedFile(pp.filename, nsp, index));
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();
            }

            case sphaira::mtp::PathKind::ForwardersFile: {
                std::shared_ptr<GameNsp> nsp;
                size_t index{};
                R_TRY(FindForwarderFile(pp.filename, nsp, index));
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();
            }

            case sphaira::mtp::PathKind::SeparateFile: {
                std::shared_ptr<GameNsp> nsp;
                size_t index{};
                R_TRY(FindFile(pp.game, pp.filename, nsp, index));
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();
            }

            default:
                R_THROW(FsError_PathNotFound);
        }
    }

    // the drive is a view of installed content: everything that would modify
    // it is rejected, same as the saves drive.
    Result CreateFile(const char* path, s64 size, u32 option) override {
        log_write("[MTP-GAMES] rejecting CreateFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteFile(const char* path) override {
        log_write("[MTP-GAMES] rejecting DeleteFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameFile(const char *old_path, const char *new_path) override {
        log_write("[MTP-GAMES] rejecting RenameFile(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result CreateDirectory(const char* path) override {
        log_write("[MTP-GAMES] rejecting CreateDirectory(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        log_write("[MTP-GAMES] rejecting DeleteDirectoryRecursively(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        log_write("[MTP-GAMES] rejecting RenameDirectory(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result SetFileSize(FsFile *file, s64 size) override {
        log_write("[MTP-GAMES] rejecting SetFileSize()\n");
        R_THROW(FsError_NotImplemented);
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        log_write("[MTP-GAMES] rejecting WriteFile()\n");
        R_THROW(FsError_NotImplemented);
    }

    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        log_write("[MTP-GAMES] OpenFile(%s)\n", path);
        R_UNLESS(!(mode & (FsOpenMode_Write | FsOpenMode_Append)), FsError_NotImplemented);

        const auto pp = Parse(path);
        auto handle = std::make_unique<FileHandle>();

        if (pp.kind == sphaira::mtp::PathKind::InfoFile) {
            handle->info = InfoText(pp.game);
        } else if (pp.kind == sphaira::mtp::PathKind::MergedFile) {
            R_TRY(FindMergedFile(pp.filename, handle->nsp, handle->index));
        } else if (pp.kind == sphaira::mtp::PathKind::ForwardersFile) {
            R_TRY(FindForwarderFile(pp.filename, handle->nsp, handle->index));
        } else if (pp.kind == sphaira::mtp::PathKind::SeparateFile) {
            R_TRY(FindFile(pp.game, pp.filename, handle->nsp, handle->index));
        } else {
            R_THROW(FsError_PathNotFound);
        }

        auto raw = handle.release();
        std::memcpy(&out_file->s, &raw, sizeof(raw));
        R_SUCCEED();
    }
    Result GetFileSize(FsFile *file, s64 *out_size) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        if (!h->info.empty()) {
            *out_size = static_cast<s64>(h->info.size());
            R_SUCCEED();
        }
        *out_size = h->nsp->entries[h->index].nsp_size;
        R_SUCCEED();
    }
    Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));

        if (!h->info.empty()) {
            *out_bytes_read = 0;
            if (off < 0 || off >= static_cast<s64>(h->info.size())) {
                R_SUCCEED();
            }
            const auto n = std::min<u64>(read_size, static_cast<u64>(h->info.size() - off));
            std::memcpy(buf, h->info.data() + off, n);
            *out_bytes_read = n;
            R_SUCCEED();
        }

        const auto rc = h->nsp->entries[h->index].Read(buf, off, read_size, out_bytes_read);
        if (m_is_file_based_emummc) {
            svcSleepThread(2e+6); // 2ms, same throttle the sd card dump uses.
        }
        return rc;
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

        switch (pp.kind) {
            case sphaira::mtp::PathKind::Root: {
                if (m_layout == sphaira::mtp::GamesLayout::Compatible) {
                    if (mode & FsDirOpenMode_ReadFiles) {
                        R_TRY(BuildMergedCacheIfNeeded());
                        SCOPED_MUTEX(&m_cache_mutex);
                        for (const auto& [name, nsp] : m_merged_cache) {
                            if (!nsp->listing.empty()) {
                                handle->entries.emplace_back(nsp->listing[0]);
                            }
                        }
                        handle->entries.emplace_back(InfoDirEntry(""));
                    }
                    if (mode & FsDirOpenMode_ReadDirs) {
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                    }
                } else if (m_layout == sphaira::mtp::GamesLayout::Separate) {
                    if (mode & FsDirOpenMode_ReadDirs) {
                        for (const auto& [name, game] : m_games) {
                            handle->entries.emplace_back(MakeVirtualDirEntry(name));
                        }
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                    }
                    if (mode & FsDirOpenMode_ReadFiles) {
                        handle->entries.emplace_back(InfoDirEntry(""));
                    }
                } else {
                    if (mode & FsDirOpenMode_ReadDirs) {
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.merged));
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.separate));
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                    }
                    if (mode & FsDirOpenMode_ReadFiles) {
                        handle->entries.emplace_back(InfoDirEntry(""));
                    }
                }
                break;
            }

            case sphaira::mtp::PathKind::MergedDir: {
                if (mode & FsDirOpenMode_ReadFiles) {
                    R_TRY(BuildMergedCacheIfNeeded());
                    SCOPED_MUTEX(&m_cache_mutex);
                    for (const auto& [name, nsp] : m_merged_cache) {
                        if (!nsp->listing.empty()) {
                            handle->entries.emplace_back(nsp->listing[0]);
                        }
                    }
                    handle->entries.emplace_back(InfoDirEntry("merged"));
                }
                break;
            }

            case sphaira::mtp::PathKind::ForwardersDir: {
                if (mode & FsDirOpenMode_ReadFiles) {
                    R_TRY(BuildForwarderCacheIfNeeded());
                    SCOPED_MUTEX(&m_cache_mutex);
                    for (const auto& [name, nsp] : m_forwarder_cache) {
                        if (!nsp->listing.empty()) {
                            handle->entries.emplace_back(nsp->listing[0]);
                        }
                    }
                    handle->entries.emplace_back(InfoDirEntry("forwarders"));
                }
                break;
            }

            case sphaira::mtp::PathKind::SeparateDir: {
                if (mode & FsDirOpenMode_ReadDirs) {
                    for (const auto& [name, game] : m_games) {
                        handle->entries.emplace_back(MakeVirtualDirEntry(name));
                    }
                }
                if (mode & FsDirOpenMode_ReadFiles) {
                    handle->entries.emplace_back(InfoDirEntry("separate"));
                }
                break;
            }

            case sphaira::mtp::PathKind::SeparateGameDir: {
                std::shared_ptr<GameNsp> nsp;
                R_TRY(GetNsp(pp.game, nsp));
                if (mode & FsDirOpenMode_ReadFiles) {
                    handle->entries = nsp->listing;
                }
                break;
            }

            default:
                R_THROW(FsError_PathNotFound);
        }

        auto raw = handle.release();
        std::memcpy(&out_dir->s, &raw, sizeof(raw));
        log_write("[MTP-GAMES] OpenDirectory(%s) kind=%d\n", path, static_cast<int>(pp.kind));
        R_SUCCEED();
    }
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));

        max_entries = std::min<s64>(h->entries.size() - h->index, max_entries);
        std::memcpy(buf, h->entries.data() + h->index, max_entries * sizeof(*buf));
        h->index += max_entries;
        *out_total_entries = max_entries;
        R_SUCCEED();
    }
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));
        *out_count = h->entries.size();
        R_SUCCEED();
    }
    void CloseDirectory(FsDir *d) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));
        delete h;
        std::memset(d, 0, sizeof(*d));
    }

    // ncm reads are not safe to issue concurrently, one reader thread only.
    bool MultiThreadTransfer(s64 size, bool read) override {
        return false;
    }

private:
    // how many games keep their built nsp headers around.
    static constexpr size_t CACHE_MAX = 8;

    struct GameRec {
        u64 app_id{};
        // display name as it comes from the control data, may be empty.
        std::string name{};
    };

    // every installed component of one game, plus the directory listing that
    // describes them.
    struct GameNsp {
        std::vector<title::NspEntry> entries{};
        std::vector<FsDirectoryEntry> listing{};
    };

    // open handles hold their own reference, so a cache flush never pulls the
    // nsp out from under a transfer in progress. `info` is a virtual readme.
    struct FileHandle {
        std::shared_ptr<GameNsp> nsp{};
        size_t index{};
        std::string info{};
    };

    struct DirHandle {
        std::vector<FsDirectoryEntry> entries{};
        s64 index{};
    };

    // one folder per installed title. archived titles (a record with no
    // content) are skipped - there would be nothing to dump.
    void ScanGames() {
        constexpr auto RECORD_CHUNK = 1000;
        std::vector<NsApplicationRecord> records(RECORD_CHUNK);

        s32 offset{};
        while (true) {
            s32 count{};
            if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count)) || !count) {
                break;
            }

            for (s32 i = 0; i < count; i++) {
                const auto app_id = records[i].application_id;

                title::MetaEntries installed;
                if (R_FAILED(title::GetMetaEntries(app_id, installed)) || installed.empty()) {
                    continue;
                }

                GameRec rec{app_id};
                if (const auto data = title::Get(app_id); data && data->status == title::NacpLoadStatus::Loaded) {
                    rec.name = data->lang.name;
                }

                if (sphaira::mtp::IsForwarderTitleId(app_id)) {
                    m_forwarders.emplace(BuildGameDirName(app_id), std::move(rec));
                } else {
                    m_games.emplace(BuildGameDirName(app_id), std::move(rec));
                }
            }

            offset += count;
        }

        log_write("[MTP-GAMES] scanned %zu games, %zu forwarders\n",
            m_games.size(), m_forwarders.size());
    }

    Result GetNsp(const std::string& game, std::shared_ptr<GameNsp>& out) {
        const auto it = m_games.find(game);
        R_UNLESS(it != m_games.end(), FsError_PathNotFound);

        // canonical key, so differently-cased requests share one build.
        const auto& key = it->first;

        // ops run on the haze thread(s) and may interleave, guard the cache.
        SCOPED_MUTEX(&m_cache_mutex);

        if (const auto cached = m_cache.find(key); cached != m_cache.end()) {
            out = cached->second;
            R_SUCCEED();
        }

        auto nsp = std::make_shared<GameNsp>();
        const auto rc = title::BuildNspEntries(it->second.app_id, it->second.name.c_str(), title::ContentFlag_All, false, nsp->entries);
        if (R_FAILED(rc)) {
            // e.g. missing keys or a corrupt install - only this game fails.
            log_write("[MTP-GAMES] failed to build nsp for %s 0x%X\n", key.c_str(), rc);
            return rc;
        }

        for (const auto& e : nsp->entries) {
            FsDirectoryEntry fe{};
            std::snprintf(fe.name, sizeof(fe.name), "%s", e.path.s);
            fe.type = FsDirEntryType_File;
            fe.file_size = e.nsp_size;
            nsp->listing.emplace_back(fe);
        }

        // a cached game is only its nsp header, file table and tickets (tens of
        // kb), so the whole cache is dropped at once rather than tracking an
        // lru - the cost of a miss is one rebuild.
        if (m_cache.size() >= CACHE_MAX) {
            m_cache.clear();
        }

        m_cache.emplace(key, nsp);
        out = nsp;
        R_SUCCEED();
    }

    Result BuildMergedCacheIfNeeded() {
        SCOPED_MUTEX(&m_cache_mutex);
        if (m_merged_built) {
            R_SUCCEED();
        }

        // ponytail: Merged listing needs exact sizes, so headers/tickets stay cached
        // for this MTP session; add lightweight sizing only if hardware measurements
        // show memory or listing latency is a problem.
        for (const auto& [dir_name, rec] : m_games) {
            title::NspEntry entry;
            const auto rc = title::BuildMergedNspEntry(rec.app_id, rec.name.c_str(), entry);
            if (R_FAILED(rc)) {
                log_write("[MTP-GAMES] failed to build merged nsp for %s 0x%X\n", dir_name.c_str(), rc);
                continue;
            }

            auto nsp = std::make_shared<GameNsp>();
            FsDirectoryEntry fe{};
            std::snprintf(fe.name, sizeof(fe.name), "%s", entry.path.s);
            fe.type = FsDirEntryType_File;
            fe.file_size = entry.nsp_size;

            nsp->entries.emplace_back(std::move(entry));
            nsp->listing.emplace_back(fe);

            m_merged_cache.emplace(fe.name, std::move(nsp));
        }

        m_merged_built = true;
        R_SUCCEED();
    }

    Result FindMergedFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index) {
        R_TRY(BuildMergedCacheIfNeeded());

        SCOPED_MUTEX(&m_cache_mutex);
        const auto it = m_merged_cache.find(filename);
        R_UNLESS(it != m_merged_cache.end(), FsError_PathNotFound);

        nsp = it->second;
        index = 0;
        R_SUCCEED();
    }

    Result BuildForwarderCacheIfNeeded() {
        SCOPED_MUTEX(&m_cache_mutex);
        if (m_forwarder_built) {
            R_SUCCEED();
        }

        for (const auto& [dir_name, rec] : m_forwarders) {
            title::NspEntry entry;
            const auto rc = title::BuildMergedNspEntry(rec.app_id, rec.name.c_str(), entry);
            if (R_FAILED(rc)) {
                log_write("[MTP-GAMES] failed to build forwarder nsp for %s 0x%X\n", dir_name.c_str(), rc);
                continue;
            }

            auto nsp = std::make_shared<GameNsp>();
            FsDirectoryEntry fe{};
            std::snprintf(fe.name, sizeof(fe.name), "%s", entry.path.s);
            fe.type = FsDirEntryType_File;
            fe.file_size = entry.nsp_size;

            nsp->entries.emplace_back(std::move(entry));
            nsp->listing.emplace_back(fe);

            m_forwarder_cache.emplace(fe.name, std::move(nsp));
        }

        m_forwarder_built = true;
        R_SUCCEED();
    }

    Result FindForwarderFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index) {
        R_TRY(BuildForwarderCacheIfNeeded());

        SCOPED_MUTEX(&m_cache_mutex);
        const auto it = m_forwarder_cache.find(filename);
        R_UNLESS(it != m_forwarder_cache.end(), FsError_PathNotFound);

        nsp = it->second;
        index = 0;
        R_SUCCEED();
    }

    Result FindFile(const std::string& game, const std::string& file, std::shared_ptr<GameNsp>& nsp, size_t& index) {
        R_TRY(GetNsp(game, nsp));

        for (size_t i = 0; i < nsp->entries.size(); i++) {
            if (!strcasecmp(nsp->entries[i].path.s, file.c_str())) {
                index = i;
                R_SUCCEED();
            }
        }

        R_THROW(FsError_PathNotFound);
    }

    auto Parse(const char* path) const -> sphaira::mtp::ParsedPath {
        // libhaze names the storage root "/games"; strip that so listings and
        // reads see "/", "/Merged", "/Separate/...", "/Forwarders" like the
        // host tests (and the locale's names, which m_names holds).
        return sphaira::mtp::ParseGamesPath(FixPath(path).s, m_layout, m_names);
    }

    auto InfoText(std::string_view which) const -> std::string {
        if (which == "merged") {
            return "Each file is one game. Base, update and DLC are packed into a single NSP. Use this when the installer expects one file per title."_i18n + "\n";
        }
        if (which == "separate") {
            return "Each folder is one game. Inside are separate NSP files for the base, the update and each DLC."_i18n + "\n";
        }
        if (which == "forwarders") {
            return "HOME-menu forwarders (homebrew icons). These are not Nintendo games. Copy one to dump that icon as an NSP."_i18n + "\n";
        }
        return "This drive dumps installed titles as NSP files you can copy to a PC."_i18n + "\n\n"
            + m_names.merged + " — " + "One NSP per game with base, update and DLC together."_i18n + "\n"
            + m_names.separate + " — " + "A folder per game with each component as its own NSP."_i18n + "\n"
            + m_names.forwarders + " — " + "HOME-menu homebrew icons, not Nintendo games."_i18n + "\n";
    }

    auto InfoDirEntry(std::string_view which) const -> FsDirectoryEntry {
        return MakeVirtualFileEntry(m_names.readme, static_cast<s64>(InfoText(which).size()));
    }

    // game folder name -> title. built once at registration, immutable
    // afterwards (safe for concurrent reads). forwarders live in m_forwarders.
    std::map<std::string, GameRec, CaseInsensitiveLess> m_games{};
    std::map<std::string, GameRec, CaseInsensitiveLess> m_forwarders{};

    // built on demand, see GetNsp().
    std::map<std::string, std::shared_ptr<GameNsp>, CaseInsensitiveLess> m_cache{};
    std::map<std::string, std::shared_ptr<GameNsp>, CaseInsensitiveLess> m_merged_cache{};
    std::map<std::string, std::shared_ptr<GameNsp>, CaseInsensitiveLess> m_forwarder_cache{};
    bool m_merged_built{false};
    bool m_forwarder_built{false};
    Mutex m_cache_mutex{};

    bool m_has_title{};
    bool m_is_file_based_emummc{};
    sphaira::mtp::GamesLayout m_layout{sphaira::mtp::GamesLayout::Both};
    sphaira::mtp::GamesFolderNames m_names{};
};

::haze::FsEntries g_fs_entries{};

void haze_callback(const ::haze::CallbackData *data) {
    if (g_should_exit) {
        return;
    }

    auto& e = *data;

    switch (e.type) {
        case ::haze::CallbackType_OpenSession:
            log_write("[LIBHAZE] Opening Session\n");
            App::Notify("MTP connected"_i18n);
            if (App::IsApplet()) {
                App::Notify("Applet Mode has limited memory. NSZ packages are unlikely to install. Use Title Mode for reliable installation."_i18n);
            }
            break;
        case ::haze::CallbackType_CloseSession:
            log_write("[LIBHAZE] Closing Session\n");
            App::Notify("MTP disconnected"_i18n);
            break;

        case ::haze::CallbackType_CreateFile: log_write("[LIBHAZE] Creating File: %s\n", e.file.filename); break;
        case ::haze::CallbackType_DeleteFile: log_write("[LIBHAZE] Deleting File: %s\n", e.file.filename); break;

        case ::haze::CallbackType_RenameFile: log_write("[LIBHAZE] Rename File: %s -> %s\n", e.rename.filename, e.rename.newname); break;
        case ::haze::CallbackType_RenameFolder: log_write("[LIBHAZE] Rename Folder: %s -> %s\n", e.rename.filename, e.rename.newname); break;

        case ::haze::CallbackType_CreateFolder: log_write("[LIBHAZE] Creating Folder: %s\n", e.file.filename); break;
        case ::haze::CallbackType_DeleteFolder: log_write("[LIBHAZE] Deleting Folder: %s\n", e.file.filename); break;

        case ::haze::CallbackType_ReadBegin:
        case ::haze::CallbackType_WriteBegin: {
            log_write("[LIBHAZE] Transfer Begin: %s \n", e.file.filename);
#if ENABLE_NETWORK_INSTALL
            {
                SCOPED_MUTEX(&g_shared_data.mutex);
                if (g_shared_data.in_progress && !g_shared_data.current_file.empty()) {
                    if (std::strstr(e.file.filename, g_shared_data.current_file.c_str()) != nullptr) {
                        break;
                    }
                }
            }
#endif
            bool trigger_ui = false;
            {
                SCOPED_MUTEX(&g_mtp_ui_mutex);
                g_mtp_current_filename = e.file.filename;
                g_mtp_new_transfer = true;
                if (!g_mtp_ui_alive) {
                    g_mtp_ui_alive = true;
                    trigger_ui = true;
                }
            }

            if (trigger_ui) {
                ueventClear(&g_mtp_done_event);
                evman::push(evman::FunctionalEventData {
                    []() {
                        log_write("[MTP-UI] UI event triggered, creating ProgressBox\n");
                        std::string init_filename;
                        {
                            SCOPED_MUTEX(&g_mtp_ui_mutex);
                            init_filename = g_mtp_current_filename;
                        }
                        App::PushTransfer(std::make_unique<ui::ProgressBox>(0, "Copying via MTP"_i18n, MtpParentDir(init_filename), [init_filename](auto pbox) -> Result {
                            std::string current_filename;
                            {
                                SCOPED_MUTEX(&g_mtp_ui_mutex);
                                g_mtp_pbox = pbox;
                                current_filename = g_mtp_current_filename;
                                if (current_filename == init_filename) {
                                    g_mtp_new_transfer = false;
                                }
                            }
                            pbox->SetTitle(MtpParentDir(current_filename));
                            pbox->NewTransferForce(GetLastComponent(current_filename.c_str()));
                            
                            while (!pbox->ShouldExit() && !g_should_exit) {
                                auto rc = waitSingle(waiterForUEvent(&g_mtp_done_event), 100000000ULL); // 100ms
                                if (R_SUCCEEDED(rc)) {
                                    bool has_new = false;
                                    for (int i = 0; i < 15; i++) {
                                        if (pbox->ShouldExit() || g_should_exit) break;
                                        if (g_mtp_new_transfer) {
                                            has_new = true;
                                            g_mtp_new_transfer = false;
                                            break;
                                        }
                                        svcSleepThread(100000000ULL); // 100ms
                                    }
                                    if (has_new) {
                                        ueventClear(&g_mtp_done_event);
                                        std::string next_filename;
                                        {
                                            SCOPED_MUTEX(&g_mtp_ui_mutex);
                                            next_filename = g_mtp_current_filename;
                                        }
                                        // address line = folder, current line = file name.
                                        pbox->SetTitle(MtpParentDir(next_filename));
                                        pbox->NewTransferForce(GetLastComponent(next_filename.c_str()));
                                        continue;
                                    }
                                    break;
                                }
                                
                                if (g_mtp_new_transfer) {
                                    std::string next_filename;
                                    {
                                        SCOPED_MUTEX(&g_mtp_ui_mutex);
                                        g_mtp_new_transfer = false;
                                        next_filename = g_mtp_current_filename;
                                    }
                                    pbox->SetTitle(MtpParentDir(next_filename));
                                    pbox->NewTransferForce(GetLastComponent(next_filename.c_str()));
                                }
                            }
                            
                            R_SUCCEED();
                        }, [](Result rc) {
                            SCOPED_MUTEX(&g_mtp_ui_mutex);
                            g_mtp_pbox = nullptr;
                            g_mtp_ui_alive = false;
                        }));
                    }
                }, false);
            }
            break;
        }

        case ::haze::CallbackType_ReadProgress:
        case ::haze::CallbackType_WriteProgress: {
            SCOPED_MUTEX(&g_mtp_ui_mutex);
            if (g_mtp_pbox) {
                const s64 offset = e.progress.offset;
                const s64 chunk = e.progress.size;
                const s64 transferred = (offset >= 0 && chunk >= 0 && offset <= INT64_MAX - chunk) ? (offset + chunk) : offset;
                const s64 total = e.progress.total > 0 ? e.progress.total : 0;
                g_mtp_pbox->UpdateTransferForce(transferred, total);
            }
            break;
        }

        case ::haze::CallbackType_ReadEnd:
        case ::haze::CallbackType_WriteEnd: {
            log_write("[LIBHAZE] Transfer Finished: %s\n", e.file.filename);
            ueventSignal(&g_mtp_done_event);
            break;
        }
    }

    App::NotifyFlashLed();
}

} // namespace

bool Init() {
    SCOPED_MUTEX(&g_mutex);
    if (g_is_running) {
        log_write("[MTP] already enabled, cannot open\n");
        return false;
    }

    struct MtpStorageDef {
        bool enabled;
        std::string custom_name;
        const char* default_name;
        std::function<std::shared_ptr<::haze::FileSystemProxyImpl>(const char* display_name)> factory;
    };

    std::vector<MtpStorageDef> storage_defs;
    storage_defs.push_back({
        App::GetMtpShowSd(),
        App::GetMtpNameSd(),
        "microSD card",
        [](const char* display_name) {
            return std::make_shared<FsProxy>(std::make_unique<fs::FsNativeSd>(), "", display_name);
        }
    });

#if ENABLE_NETWORK_INSTALL
    storage_defs.push_back({
        App::GetMtpShowInstall(),
        App::GetMtpNameInstall(),
        "Install (NSP, XCI, NSZ, XCZ)",
        [](const char* display_name) {
            return std::make_shared<FsInstallProxy>("install", display_name);
        }
    });
#endif

    storage_defs.push_back({
        App::GetMtpShowSaves(),
        "", // no custom-name option for the Saves drive.
        "Saves",
        [](const char* display_name) {
            return std::make_shared<FsSaveProxy>("saves", display_name);
        }
    });

    storage_defs.push_back({
        App::GetMtpShowRawSaves(),
        "",
        "NAND Saves (USER:/save)",
        [](const char* display_name) {
            return std::make_shared<FsProxy>(std::make_unique<fs::FsNativeBis>(FsBisPartitionId_User), "user_save", display_name, "/save");
        }
    });

    storage_defs.push_back({
        App::GetMtpShowRawSystemSaves(),
        "",
        "NAND System Saves (SYSTEM:/save)",
        [](const char* display_name) {
            return std::make_shared<FsProxy>(std::make_unique<fs::FsNativeBis>(FsBisPartitionId_System), "system_save", display_name, "/save");
        }
    });

    storage_defs.push_back({
        App::GetMtpShowGames(),
        "", // no custom-name option for the Games drive.
        "Games (read-only)",
        [](const char* display_name) {
            return std::make_shared<FsGameProxy>("games", display_name);
        }
    });

    // user-configured extra folders (Settings -> Network -> MTP), each exposed
    // as its own storage rooted at that folder. names must be unique so libhaze
    // can address them; display defaults to the folder's leaf name.
    {
        int idx = 0;
        for (const auto& folder : App::GetMtpFolders()) {
            const char* leaf = std::strrchr(folder.c_str(), '/');
            std::string display = (leaf && leaf[1]) ? std::string(leaf + 1) : folder;
            std::string internal = "mntf" + std::to_string(idx++);
            storage_defs.push_back({
                true,
                std::move(display),
                "Folder",
                [folder, internal](const char* display_name) {
                    return std::make_shared<FsProxy>(std::make_unique<fs::FsNativeSd>(), internal.c_str(), display_name, folder.c_str());
                }
            });
        }
    }

    // pinned mounts (folders or virtual mounts) requested from the file manager.
    // each needs its own internal id, or two storages would fight over one
    // devoptab name.
    for (const auto& pin : g_pinned) {
        storage_defs.push_back({
            true,
            pin.display_name,
            "Mounted",
            [pin](const char* display_name) {
                return std::make_shared<FsProxy>(pin.fs_factory(), pin.internal.c_str(), display_name, pin.base_path.c_str());
            }
        });
    }

    for (const auto& def : storage_defs) {
        if (def.enabled) {
            const char* name = def.custom_name.empty() ? def.default_name : def.custom_name.c_str();
            g_fs_entries.emplace_back(def.factory(name));
        }
    }

    if (g_fs_entries.empty()) {
        log_write("[MTP] No MTP storages enabled\n");
        App::Notify("No MTP storages enabled"_i18n);
        return false;
    }

    ueventCreate(&g_mtp_done_event, true);
    {
        SCOPED_MUTEX(&g_mtp_ui_mutex);
        g_mtp_ui_alive = false;
        g_mtp_pbox = nullptr;
    }

    g_should_exit = false;
    // always drop the host stack, even if the "USB storage" setting is off:
    // detection keeps usbhsfs up whenever MTP is not actually serving a PC.
    if (usbHsFsGetStatusChangeUserEvent()) {
        usbHsFsExit();
    }

    if (!::haze::Initialize(haze_callback, THREAD_PRIO, THREAD_CORE, g_fs_entries)) {
        g_fs_entries.clear();
        if (App::GetWriteProtect()) {
            usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
        }
        usbHsFsInitialize(1);
        return false;
    }

    log_write("[MTP] started in %s mode\n", App::IsApplet() ? "applet" : "title");
    return g_is_running = true;
}

void Exit() {
    SCOPED_MUTEX(&g_mutex);
    if (!g_is_running) {
        return;
    }

    g_is_running = false;
    g_should_exit = true;
    ueventSignal(&g_mtp_done_event);
    {
        SCOPED_MUTEX(&g_mtp_ui_mutex);
        if (g_mtp_pbox) {
            g_mtp_pbox->RequestExit();
        }
    }
    ::haze::Exit();
    g_fs_entries.clear();

    log_write("[MTP] exitied\n");

    // hand the port back to host so a flash drive is visible again.
    if (App::GetWriteProtect()) {
        usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
    }
    usbHsFsInitialize(1);
}

bool IsRunning() {
    SCOPED_MUTEX(&g_mutex);
    return g_is_running;
}

bool MountFs(std::vector<PinnedMount> mounts) {
    // stop any running session, install the pinned storages, then (re)start so
    // the PC re-enumerates with the new storages present.
    Exit();
    g_pinned = std::move(mounts);
    for (size_t i = 0; i < g_pinned.size(); i++) {
        g_pinned[i].internal = "mnt" + std::to_string(i);
    }
    return Init();
}

void UnmountPinned() {
    g_pinned.clear();
    Exit();
}

bool HasPinned() {
    return !g_pinned.empty();
}

std::string GetPinnedName() {
    std::string out;
    for (const auto& pin : g_pinned) {
        if (!out.empty()) out += ", ";
        out += pin.display_name;
    }
    return out;
}

#if ENABLE_NETWORK_INSTALL
void InitInstallMode(OnInstallStart on_start, OnInstallWrite on_write, OnInstallClose on_close) {
    SCOPED_MUTEX(&g_shared_data.mutex);
    g_shared_data.on_start = on_start;
    g_shared_data.on_write = on_write;
    g_shared_data.on_close = on_close;
    g_shared_data.enabled = true;
}

void DisableInstallMode() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    g_shared_data.enabled = false;
}
#endif

} // namespace sphaira::haze
