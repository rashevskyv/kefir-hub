#pragma once

#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "dumper.hpp"
#include "download.hpp"
#include "ui/progress_box.hpp"
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {

constexpr s64 SYNC_PROGRESS_SCALE = 1'000'000;

auto WebdavLocationKey(std::string url) -> std::string;
auto GetWebdavLocations() -> std::vector<location::Entry>;
// the "Save sync location" of the settings, else the first WebDAV location; none when there is no WebDAV location.
auto PickSyncLocation() -> std::optional<location::Entry>;
// uploads a backup file on the microSD card to "sphaira-saves/<save folder>/<file name>" on the WebDAV location.
auto UploadSdBackup(const location::Entry& loc, const Entry& e, const fs::FsPath& path, curl::OnProgress progress) -> curl::ApiResult;
// deletes "sphaira-saves/<save folder>/<name>" on the WebDAV location; false when the server refuses or has no such file.
auto DeleteRemoteBackup(const location::Entry& loc, const Entry& e, const std::string& name) -> bool;
auto MakeSdCardDumpLocation() -> dump::DumpLocation;
auto MakeDumpLocationFromFsEntry(const filebrowser::FsEntry& fs_entry) -> dump::DumpLocation;
auto MakeLocationLabel(const std::string& name, const fs::FsPath& backup_root) -> std::string;
auto MakeSdLocationLabel(const fs::FsPath& backup_root) -> std::string;
auto SerializeRecentBackupDir(const RecentBackupDir& d) -> std::string;
auto MakeLocationKey(const RecentBackupDir& d) -> std::string;
auto ParseRecentBackupDir(const std::string& s, RecentBackupDir& out) -> bool;
auto RecentBackupDirExists(const RecentBackupDir& d) -> bool;
auto LoadRecentBackupDirs() -> std::vector<RecentBackupDir>;
void PushRecentBackupDir(const RecentBackupDir& dir);

struct BackupLocationChoice {
    std::string key{};
    std::string label{};
};
auto ListBackupLocationChoices() -> std::vector<BackupLocationChoice>;
auto MakeDumpLocationFromRecent(const RecentBackupDir& d) -> dump::DumpLocation;
auto MakeFsForLocation(const dump::DumpLocation& location) -> std::unique_ptr<fs::Fs>;
auto MakeAggregateProgressCb(ProgressBox* pbox, bool is_upload, s64 files_done, s64 total_units) -> curl::OnProgress;

} // namespace sphaira::ui::menu::save
