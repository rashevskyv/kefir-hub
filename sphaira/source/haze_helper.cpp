#include "haze_helper.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "evman.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <set>
#include <span>
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
    FsProxyBase(const char* name, const char* display_name) : m_name{name}, m_display_name{display_name} {

    }

    auto FixPath(const char* path) const {
        fs::FsPath buf;
        const auto len = std::strlen(GetName());

        if (len && !strncasecmp(path + 1, GetName(), len)) {
            std::snprintf(buf, sizeof(buf), "/%s", path + 1 + len);
        } else {
            std::strcpy(buf, path);
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
};

struct VirtualFile {
    std::string name;
    s64 size;
    u32 mode;
};

struct FsProxy final : FsProxyBase {
    FsProxy(std::unique_ptr<fs::Fs>&& fs, const char* name, const char* display_name)
    : FsProxyBase{name, display_name}
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
            m_fs->DeleteFile(RoutePath(path));
            R_SUCCEED();
        }
#endif
        return m_fs->DeleteFile(RoutePath(path));
    }
    Result RenameFile(const char *old_path, const char *new_path) override {
        log_write("[HAZE] RenameFile(%s -> %s)\n", old_path, new_path);
        return m_fs->RenameFile(RoutePath(old_path), RoutePath(new_path));
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
        if (const auto rule = GetRedirectRule(path); rule && (mode & FsOpenMode_Write)) {
            // make sure the target folder exists before writing into it.
            m_fs->CreateDirectoryRecursively(GetRedirectDir(rule, GetLastComponent(FixPath(path))));
        }

        auto fptr = new fs::File();
        const auto rc = m_fs->OpenFile(RoutePath(path), mode, fptr);

        if (R_SUCCEEDED(rc)) {
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
    Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) override {
        log_write("[HAZE] ReadFile(%zd, %zu)\n", off, read_size);
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
        log_write("[HAZE] WriteFile(%zd, %zu)\n", off, write_size);
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

            auto it = std::ranges::find_if(m_virtual_entries, [&file_name](const auto& e) {
                return !strcasecmp(file_name.c_str(), e.name);
            });
            if (it != m_virtual_entries.end()) {
                m_virtual_entries.erase(it);
            }

            std::memset(file, 0, sizeof(*file));
            return;
        }
#endif
        fs::File* f;
        std::memcpy(&f, &file->s, sizeof(f));
        if (f) {
            delete f;
        }
        std::memset(file, 0, sizeof(*file));
    }

    Result CreateDirectory(const char* path) override {
        log_write("[HAZE] DeleteFile(%s)\n", path);
        return m_fs->CreateDirectory(FixPath(path));
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        log_write("[HAZE] DeleteDirectoryRecursively(%s)\n", path);
        return m_fs->DeleteDirectoryRecursively(FixPath(path));
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        log_write("[HAZE] RenameDirectory(%s -> %s)\n", old_path, new_path);
        return m_fs->RenameDirectory(FixPath(old_path), FixPath(new_path));
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
        return !App::IsFileBaseEmummc();
    }

private:
    std::unique_ptr<fs::Fs> m_fs{};
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

        case ::haze::CallbackType_ReadBegin: log_write("[LIBHAZE] Reading File Begin: %s \n", e.file.filename); break;
        case ::haze::CallbackType_ReadProgress: log_write("\t[LIBHAZE] Reading File: offset: %lld size: %lld\n", e.progress.offset, e.progress.size); break;
        case ::haze::CallbackType_ReadEnd: log_write("[LIBHAZE] Reading File Finished: %s\n", e.file.filename); break;

        case ::haze::CallbackType_WriteBegin: log_write("[LIBHAZE] Writing File Begin: %s \n", e.file.filename); break;
        case ::haze::CallbackType_WriteProgress: log_write("\t[LIBHAZE] Writing File: offset: %lld size: %lld\n", e.progress.offset, e.progress.size); break;
        case ::haze::CallbackType_WriteEnd: log_write("[LIBHAZE] Writing File Finished: %s\n", e.file.filename); break;
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

    g_fs_entries.emplace_back(std::make_shared<FsProxy>(std::make_unique<fs::FsNativeSd>(), "", "microSD card"));
#if ENABLE_NETWORK_INSTALL
    g_fs_entries.emplace_back(std::make_shared<FsInstallProxy>("install", "Install (NSP, XCI, NSZ, XCZ)"));
#endif

    g_should_exit = false;
    if (!::haze::Initialize(haze_callback, THREAD_PRIO, THREAD_CORE, g_fs_entries)) {
        return false;
    }

    log_write("[MTP] started\n");
    return g_is_running = true;
}

void Exit() {
    SCOPED_MUTEX(&g_mutex);
    if (!g_is_running) {
        return;
    }

    g_is_running = false;
    g_should_exit = true;
    ::haze::Exit();
    g_fs_entries.clear();

    log_write("[MTP] exitied\n");
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
