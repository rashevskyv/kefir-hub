#include "ui/menus/save/save_locations.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "fs.hpp"
#include "location.hpp"
#include <cstring>
#include <algorithm>
#include <memory>
#include <utility>

namespace sphaira::ui::menu::save {

auto WebdavLocationKey(std::string url) -> std::string {
    constexpr const char* whitespace = " \t\r\n";
    const auto first = url.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return {};
    }
    const auto last = url.find_last_not_of(whitespace);
    url = url.substr(first, last - first + 1);
    if (url.starts_with("webdav://")) {
        url.replace(0, std::strlen("webdav"), "https");
    }
    while (url.ends_with('/')) {
        url.pop_back();
    }
    return url;
}

auto GetWebdavLocations() -> std::vector<location::Entry> {
    std::vector<location::Entry> locations;

    for (const auto& loc : location::Load()) {
        const auto is_webdav = loc.url.starts_with("webdav://") ||
            loc.url.starts_with("http://") || loc.url.starts_with("https://");
        if (is_webdav) {
            locations.push_back(loc);
        }
    }

    return locations;
}

auto MakeSdCardDumpLocation() -> dump::DumpLocation {
    dump::DumpLocation location{};
    location.entry = {dump::DumpLocationType_SdCard, 0};
    return location;
}

auto MakeDumpLocationFromFsEntry(const filebrowser::FsEntry& fs_entry) -> dump::DumpLocation {
    dump::DumpLocation location{};
    if (fs_entry.type == filebrowser::FsType::Stdio) {
        location.entry = {dump::DumpLocationType_Stdio, 0};

        location::StdioEntry stdio{};
        stdio.mount = fs_entry.root.s;
        stdio.name = fs_entry.name.s;
        location.stdio.emplace_back(std::move(stdio));
    } else {
        location.entry = {dump::DumpLocationType_SdCard, 0};
    }

    return location;
}

auto MakeLocationLabel(const std::string& name, const fs::FsPath& backup_root) -> std::string {
    return name + ": " + backup_root.toString();
}

auto MakeSdLocationLabel(const fs::FsPath& backup_root) -> std::string {
    auto path = backup_root.toString();
    if (!path.empty() && path.front() == '/') {
        path.erase(0, 1);
    }
    return "sd://" + path;
}

auto SerializeRecentBackupDir(const RecentBackupDir& d) -> std::string {
    return std::string{d.stdio ? "stdio" : "sd"} + "|" + d.mount + "|" + d.name + "|" + d.path.toString();
}

auto MakeLocationKey(const RecentBackupDir& d) -> std::string {
    return std::string{d.stdio ? "stdio|" : "sd|"} + d.mount + "|" + d.path.toString();
}

auto ParseRecentBackupDir(const std::string& s, RecentBackupDir& out) -> bool {
    const auto type_end = s.find('|');
    const auto path_start = s.rfind('|');
    if (type_end == std::string::npos || path_start <= type_end) {
        return false;
    }

    const auto type = s.substr(0, type_end);
    if (type != "sd" && type != "stdio") {
        return false;
    }

    const auto mount_end = s.find('|', type_end + 1);
    if (mount_end == std::string::npos || mount_end > path_start) {
        return false;
    }

    out.stdio = type == "stdio";
    out.mount = s.substr(type_end + 1, mount_end - type_end - 1);
    out.name = s.substr(mount_end + 1, path_start - mount_end - 1);
    out.path = s.substr(path_start + 1);
    if (out.path.empty() || (out.stdio && out.mount.empty())) {
        return false;
    }

    return true;
}

auto RecentBackupDirExists(const RecentBackupDir& d) -> bool {
    if (d.stdio) {
        return fs::DirExists(fs::FsPath{d.mount + d.path.toString()});
    }
    return fs::DirExists(d.path);
}

auto MakeDumpLocationFromRecent(const RecentBackupDir& d) -> dump::DumpLocation {
    dump::DumpLocation location{};
    if (d.stdio) {
        location.entry = {dump::DumpLocationType_Stdio, 0};

        location::StdioEntry stdio{};
        stdio.mount = d.mount;
        stdio.name = d.name;
        location.stdio.emplace_back(std::move(stdio));
    } else {
        location.entry = {dump::DumpLocationType_SdCard, 0};
    }

    return location;
}

auto MakeFsForLocation(const dump::DumpLocation& location) -> std::unique_ptr<fs::Fs> {
    if (location.entry.type == dump::DumpLocationType_Stdio) {
        return std::make_unique<fs::FsStdio>(true, location.stdio[location.entry.index].mount);
    } else if (location.entry.type == dump::DumpLocationType_SdCard) {
        return std::make_unique<fs::FsNativeSd>();
    }
    std::unreachable();
}

auto MakeAggregateProgressCb(ProgressBox* pbox, bool is_upload, s64 files_done, s64 total_units) -> curl::OnProgress {
    auto max_units = std::make_shared<s64>(files_done * SYNC_PROGRESS_SCALE);

    return curl::OnProgress{[pbox, is_upload, files_done, total_units, max_units](s64 dltotal, s64 dlnow, s64 ultotal, s64 ulnow) -> bool {
        if (pbox->ShouldExit()) {
            return false;
        }

        const auto total = is_upload ? ultotal : dltotal;
        const auto now = is_upload ? ulnow : dlnow;
        s64 file_units = 0;
        if (total > 0) {
            file_units = static_cast<s64>((static_cast<double>(now) / static_cast<double>(total)) * SYNC_PROGRESS_SCALE);
            file_units = std::min(file_units, SYNC_PROGRESS_SCALE);
        }

        *max_units = std::max(*max_units, files_done * SYNC_PROGRESS_SCALE + file_units);
        pbox->UpdateTransfer(*max_units, total_units);
        return true;
    }};
}

} // namespace sphaira::ui::menu::save
