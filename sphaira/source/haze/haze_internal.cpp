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

#if ENABLE_NETWORK_INSTALL
InstallSharedData g_shared_data{};
#endif

std::atomic_bool g_should_exit = false;
bool g_is_running{false};
Mutex g_mutex{};

Mutex g_mtp_ui_mutex;
ui::ProgressBox* g_mtp_pbox{nullptr};
UEvent g_mtp_done_event;
bool g_mtp_ui_alive{false};
std::string g_mtp_current_filename;
std::atomic<bool> g_mtp_new_transfer{false};

std::vector<PinnedMount> g_pinned{};
::haze::FsEntries g_fs_entries{};

const RootDropRule ROOT_DROP_RULES[] = {
#if ENABLE_NETWORK_INSTALL
    { SUPPORTED_EXT, RootDropAction::Install, nullptr, false },
#endif
    { NRO_EXT, RootDropAction::RedirectDir, "/switch", true },
};

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


} // namespace sphaira::haze
