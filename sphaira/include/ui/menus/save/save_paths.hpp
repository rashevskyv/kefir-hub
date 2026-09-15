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

constexpr u32 NX_SAVE_META_MAGIC = 0x4A4B5356; // JKSV
constexpr u32 NX_SAVE_META_VERSION = 1;
constexpr const char* NX_SAVE_META_NAME = ".nx_save_meta.bin";

// https://github.com/J-D-K/JKSV/issues/264#issuecomment-2618962807
struct NXSaveMeta {
    u32 magic{}; // NX_SAVE_META_MAGIC
    u32 version{}; // NX_SAVE_META_VERSION
    FsSaveDataAttribute attr{}; // FsSaveDataExtraData::attr
    u64 owner_id{}; // FsSaveDataExtraData::owner_id
    u64 timestamp{}; // FsSaveDataExtraData::timestamp
    u32 flags{}; // FsSaveDataExtraData::flags
    u32 unk_x54{}; // FsSaveDataExtraData::unk_x54
    s64 data_size{}; // FsSaveDataExtraData::data_size
    s64 journal_size{}; // FsSaveDataExtraData::journal_size
    u64 commit_id{}; // FsSaveDataExtraData::commit_id
    u64 raw_size{}; // FsSaveDataInfo::size
};
static_assert(sizeof(NXSaveMeta) == 128);

struct BackupArchiveInfo {
    u64 application_id{};
    u64 system_save_data_id{};
    u8 save_data_type{0xFF};
    AccountUid uid{};
    u16 save_data_index{};
    u8 save_data_rank{};
    u64 timestamp{};
    std::string dbi_game_dir{};
    fs::FsPath path{};
    int source{};
    u64 commit_id{};
    u64 source_timestamp{};
};

auto ParseDbiTypeLetter(char c) -> u8;
auto ParseDbiBackupIndex(std::string_view name) -> u16;
auto ParseHex16(std::string_view str) -> u64;
auto IsHex16(std::string_view str) -> bool;
auto PosixToTimestamp(u64 posix_sec) -> u64;
auto InferBackupIdFromPath(std::string_view full_path) -> u64;

auto InspectBackupArchive(fs::Fs* fs, const fs::FsPath& path, std::string_view filename, std::string_view dbi_game_dir_name, BackupArchiveInfo& out) -> bool;
auto VerifyZipIntegrity(const fs::FsPath& path) -> bool;
auto VerifyDisaIntegrity(fs::Fs* fs, const fs::FsPath& path) -> bool;

struct BackupSecondaryColumns {
    std::string title_id;
    std::string account;
    std::string timestamp;
    std::string archive_count;
};

auto FormatBackupAccount(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string;
auto FormatBackupTimestamp(u64 ts, bool compact = false) -> std::string;
auto GetBackupSecondaryColumns(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> BackupSecondaryColumns;
auto FormatBackupSecondaryText(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string;
auto BackupGroupKey(const BackupArchiveInfo& info) -> std::string;
auto BackupGroupKey(const Entry& e) -> std::string;

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
auto BuildDbiSavePath(const Entry& e, const struct tm& tm, const fs::FsPath& base = DBI_SAVES_PATH) -> fs::FsPath;
auto IsDbiBackupName(const Entry& e, const char* name) -> bool;
auto DbiBackupMatchesEntry(const fs::FsPath& zip_path, const Entry& e) -> bool;
auto CollectDbiBackups(fs::Fs* fs, const Entry& e) -> std::vector<fs::FsPath>;
auto IsDisaSaveFile(fs::Fs* fs, const fs::FsPath& path) -> bool;
auto IsRawSaveCandidate(fs::Fs* fs, const fs::FsPath& path, std::string_view name) -> bool;
auto GetBackupSearchPaths() -> std::vector<std::string>;
auto GetShareableSaveBackupRoots() -> std::vector<std::string>;
auto AddBackupSearchPath(const fs::FsPath& path) -> bool;
auto RemoveBackupSearchPath(const fs::FsPath& path) -> bool;
auto NormalizeBackupRoot(const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> fs::FsPath;

} // namespace sphaira::ui::menu::save
