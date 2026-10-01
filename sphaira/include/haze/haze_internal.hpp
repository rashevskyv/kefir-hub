#pragma once

#include "haze_helper.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "title_export_name.hpp"
#include "ui/progress_box.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <haze.h>
#include <usbhsfs.h>

namespace haze {

// Result cancelled for MTP transfers (module 420, description 19)
inline constexpr Result ResultCancelled() {
    return MAKERESULT(420, 19);
}

} // namespace haze

namespace sphaira::haze {

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

extern InstallSharedData g_shared_data;
inline constexpr const char* SUPPORTED_EXT[] = { ".nsp", ".xci", ".nsz", ".xcz" };
void on_thing();
#endif

constexpr int THREAD_PRIO = 0x20;
constexpr int THREAD_CORE = 2;
extern std::atomic_bool g_should_exit;
extern bool g_is_running; // guarded by g_mutex
extern Mutex g_mutex;

// g_mtp_* below: guarded by g_mtp_ui_mutex (libhaze callback, MTP ProgressBox worker, main thread).
extern Mutex g_mtp_ui_mutex;
extern ui::ProgressBox* g_mtp_pbox;
extern UEvent g_mtp_done_event;
extern bool g_mtp_ui_alive; // guarded by g_mtp_ui_mutex
extern std::string g_mtp_current_filename;
extern bool g_mtp_transfer_active; // guarded by g_mtp_ui_mutex
extern bool g_mtp_transfer_aborted; // guarded by g_mtp_ui_mutex
extern u64 g_mtp_transfer_seq; // guarded by g_mtp_ui_mutex
extern u64 g_mtp_handled_seq; // guarded by g_mtp_ui_mutex

extern std::vector<PinnedMount> g_pinned;
extern ::haze::FsEntries g_fs_entries;

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

inline constexpr const char* NRO_EXT[] = { ".nro" };
extern const RootDropRule ROOT_DROP_RULES[];

const char* GetFileName(const char* s);
std::string MtpParentDir(const std::string& path);
const char* GetLastComponent(const char* s);
bool IsRootPath(const char* path);
const RootDropRule* FindRootDropRule(const char* fixed_path);

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
            std::snprintf(stripped, sizeof(stripped), "%s", path);
        }

        // root the storage at m_base_path when set (e.g. a specific folder):
        // "/x" becomes "<base>/x".
        fs::FsPath buf;
        if (!m_base_path.empty()) {
            std::snprintf(buf, sizeof(buf), "%s%s", m_base_path.c_str(), stripped.s);
        } else {
            std::snprintf(buf, sizeof(buf), "%s", stripped.s);
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
        std::snprintf(entry.name, sizeof(entry.name), "%s", file_name);
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

        std::snprintf(it->name, sizeof(it->name), "%s", file_name_new);
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

struct CaseInsensitiveLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    }
};

auto TrimName(std::string s) -> std::string;
auto MakeVirtualDirEntry(const std::string& name) -> FsDirectoryEntry;
auto MakeVirtualFileEntry(const std::string& name, s64 size) -> FsDirectoryEntry;
auto BuildGameDirName(u64 application_id) -> std::string;

std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsProxy(std::unique_ptr<fs::Fs> fs, const char* name, const char* display_name, const char* base_path = "");
#if ENABLE_NETWORK_INSTALL
std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsInstallProxy(const char* name, const char* display_name);
#endif
std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsSaveProxy(const char* name, const char* display_name);
std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsGameProxy(const char* name, const char* display_name);

void haze_callback(const ::haze::CallbackData *data);

} // namespace sphaira::haze
