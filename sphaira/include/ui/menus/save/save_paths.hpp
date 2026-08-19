#pragma once

#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "fs.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <ctime>

namespace sphaira::ui::menu::save {

// default sphaira backup library root on the SD card. system-save backups and
// legacy-format scans live under it; non-system (game) backups are written in
// DBI format under DBI_SAVES_PATH instead. note: the default argument of
// Menu::BackupSaveInternal in save_menu.hpp repeats this literal, as that
// header cannot include this one (include cycle).
inline constexpr const char* DEFAULT_BACKUP_ROOT = "/dumps";
inline constexpr const char* DBI_SAVES_PATH = "/switch/DBI/saves";
inline constexpr const char* DBI_SAVE_INFO_NAME = ".dbi_save_info.ini";
inline constexpr const char* DBI_SAVE_EXTRA_NAME = ".dbi_save_extra";

auto GetSaveFolder(u8 data_type) -> fs::FsPath;
auto GetSaveFolder(const Entry& e) -> fs::FsPath;
auto GetSaveTypeSubdir(u8 data_type) -> fs::FsPath;
auto GetDbiTypeLetter(u8 data_type) -> char;
auto ParseDbiBackupNameTimestamp(std::string_view name) -> u64;
auto ParseBackupNameTimestamp(std::string_view name) -> u64;
// application id encoded at the start of a DBI backup file name
// (<016X>_<type>_<ts>_<idx>.zip); 0 if the name is not a DBI backup.
auto ParseDbiBackupAppId(std::string_view name) -> u64;
auto GetSaveTypeLabel(u8 data_type) -> const char*;
auto SaveTypeIndex(u8 data_type) -> size_t;
auto SaveEntryKey(const FsSaveDataInfo& e) -> std::string;
auto IsSystemLikeSave(u8 data_type) -> bool;
auto DisplayEntryKey(const Entry& e) -> std::string;
auto BuildSaveName(const Entry& e) -> fs::FsPath;
auto BuildSavePathName(const Entry& e, bool force_id_path) -> fs::FsPath;
auto BuildSaveBasePathLegacy(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath;
auto BuildSaveBasePath(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath;
auto BuildDbiGameFolderName(const Entry& e) -> fs::FsPath;
auto BuildDbiSavePath(const Entry& e, const struct tm& tm) -> fs::FsPath;
auto IsDbiBackupName(const Entry& e, const char* name) -> bool;
auto DbiBackupMatchesEntry(const fs::FsPath& zip_path, const Entry& e) -> bool;
auto CollectDbiBackups(fs::Fs* fs, const Entry& e) -> std::vector<fs::FsPath>;
auto IsDisaSaveFile(fs::Fs* fs, const fs::FsPath& path) -> bool;
auto IsRawSaveCandidate(fs::Fs* fs, const fs::FsPath& path, std::string_view name) -> bool;
auto GetBackupSearchPaths() -> std::vector<std::string>;
auto AddBackupSearchPath(const fs::FsPath& path) -> bool;
auto RemoveBackupSearchPath(const fs::FsPath& path) -> bool;
auto NormalizeBackupRoot(const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> fs::FsPath;

} // namespace sphaira::ui::menu::save
