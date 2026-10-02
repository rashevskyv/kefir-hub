#pragma once

#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "fs.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <ctime>
#include <optional>
#include <cstring>

namespace sphaira::ui {
struct ProgressBox;
}

namespace sphaira::ui::menu::save {

// default sphaira backup library root on the SD card. Game backups use DBI ZIP
// format beneath the selected root; legacy DBI folders are restore sources.
// Note: the default argument of
// Menu::BackupSaveInternal in save_menu.hpp repeats this literal, as that
// header cannot include this one (include cycle).
inline constexpr const char* DEFAULT_BACKUP_ROOT = "/dumps";
inline constexpr const char* DBI_SAVES_PATH = "/switch/DBI/saves";
inline constexpr const char* DBI_SAVES_ROOT_PATH = "/DBISaves";
inline constexpr const char* JKSV_PATH = "/JKSV";
inline constexpr const char* JKSV_SWITCH_PATH = "/switch/JKSV";
inline constexpr const char* CHECKPOINT_SAVES_PATH = "/switch/Checkpoint/saves";
inline constexpr const char* CHECKPOINT_ROOT_SAVES_PATH = "/Checkpoint/saves";
inline constexpr const char* DBI_SAVE_INFO_NAME = ".dbi_save_info.ini";
inline constexpr const char* DBI_SAVE_EXTRA_NAME = ".dbi_save_extra";

constexpr u32 NX_SAVE_META_MAGIC = 0x4A4B5356; // JKSV
constexpr u32 NX_SAVE_META_VERSION = 1;
constexpr const char* NX_SAVE_META_NAME = ".nx_save_meta.bin";

enum class SaveReservedMetaKind {
    None,
    NxMeta,
    DbiExtra,
    DbiInfo,
};

inline auto ClassifySaveReservedMetadataRoot(std::string_view name) -> SaveReservedMetaKind {
    while (!name.empty() && name.front() == '/') {
        name.remove_prefix(1);
    }
    while (!name.empty() && name.back() == '/') {
        name.remove_suffix(1);
    }
    if (name.find('/') != std::string_view::npos) {
        return SaveReservedMetaKind::None;
    }
    const auto equals_ic = [](std::string_view a, const char* b) {
        const size_t b_len = std::strlen(b);
        return a.size() == b_len && !strncasecmp(a.data(), b, b_len);
    };
    if (equals_ic(name, NX_SAVE_META_NAME)) return SaveReservedMetaKind::NxMeta;
    if (equals_ic(name, DBI_SAVE_EXTRA_NAME)) return SaveReservedMetaKind::DbiExtra;
    if (equals_ic(name, DBI_SAVE_INFO_NAME)) return SaveReservedMetaKind::DbiInfo;
    return SaveReservedMetaKind::None;
}

inline auto IsSaveReservedMetadataRoot(std::string_view name) -> bool {
    return ClassifySaveReservedMetadataRoot(name) != SaveReservedMetaKind::None;
}

constexpr u32 JKSV_SAVE_META_MAGIC = 0x56534B4A;
constexpr u8 JKSV_SAVE_META_REVISION = 1;

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

enum class ArchiveMetaStatus {
    NoMetadata,
    Valid,
    Invalid
};

struct DecodedSaveMetadata {
    NXSaveMeta meta{};
    std::optional<u8> source_space{};
    bool has_nx_meta{false};
    bool has_dbi_extra{false};
    bool has_dbi_info{false};
    bool has_kefir_comment{false};
    s64 payload_count{0};
};

auto ReadArchiveSaveMetadata(void* zfile, ui::ProgressBox* pbox, DecodedSaveMetadata& out, Result* out_rc = nullptr) -> ArchiveMetaStatus;


struct BackupArchiveInfo {
    u64 application_id{};
    u64 system_save_data_id{};
    u8 save_data_type{0xFF};
    AccountUid uid{};
    u16 save_data_index{};
    u8 save_data_rank{};
    bool rank_known{false};
    u64 timestamp{};
    std::string dbi_game_dir{};
    fs::FsPath path{};
    int source{};
    u64 commit_id{};
    u64 source_timestamp{};
    s64 payload_count{0};
    BackupSource backup_source{BackupSource::Other};
    bool is_directory{false};
};

auto ParseDbiTypeLetter(char c) -> u8;
auto ParseDbiBackupIndex(std::string_view name) -> u16;
auto ParseHex16(std::string_view str) -> u64;
auto IsHex16(std::string_view str) -> bool;
auto PosixToTimestamp(u64 posix_sec) -> u64;
auto InferBackupIdFromPath(std::string_view full_path) -> u64;

auto InspectBackupArchive(fs::Fs* fs, const fs::FsPath& path, std::string_view filename, std::string_view dbi_game_dir_name, BackupArchiveInfo& out) -> bool;
auto VerifyZipIntegrity(const fs::FsPath& path) -> bool;

struct BackupSecondaryColumns {
    std::string title_id;
    std::string account;
    std::string timestamp;
    std::string archive_count;
};

auto FormatBackupAccount(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string;
// nicknames for a "pick a user" list; accounts that share a nickname get a uid suffix.
auto AccountPickerItems(const std::vector<AccountProfileBase>& accounts) -> std::vector<std::string>;
auto FormatBackupTimestamp(u64 ts, bool compact = false) -> std::string;
auto GetBackupSecondaryColumns(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> BackupSecondaryColumns;
auto FormatBackupSecondaryText(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string;
auto BackupGroupKey(const BackupArchiveInfo& info) -> std::string;
auto BackupGroupKey(const Entry& e) -> std::string;
inline auto MatchesRestoreDestination(const BackupArchiveInfo& info, const Entry& dst) -> bool {
    const bool rank_matches = (!info.rank_known || info.save_data_rank == dst.save_data_rank);
    if (info.application_id != dst.application_id ||
        info.system_save_data_id != dst.system_save_data_id ||
        info.save_data_type != dst.save_data_type ||
        !rank_matches ||
        info.save_data_index != dst.save_data_index) {
        return false;
    }
    if (info.save_data_type == FsSaveDataType_Account) {
        return (dst.uid.uid[0] != 0 || dst.uid.uid[1] != 0);
    }
    return (info.uid.uid[0] == dst.uid.uid[0] && info.uid.uid[1] == dst.uid.uid[1]);
}

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
auto DiscoverSaveDataInfo(const AccountUid* uid_filter = nullptr, const std::optional<u8>& type_filter = std::nullopt) -> std::vector<FsSaveDataInfo>;
inline auto DiscoverSaveDataInfo(u8 type_filter) -> std::vector<FsSaveDataInfo> {
    return DiscoverSaveDataInfo(nullptr, type_filter);
}
auto IsSystemLikeSave(u8 data_type) -> bool;
auto DisplayEntryKey(const Entry& e) -> std::string;
auto BuildSaveName(const Entry& e) -> fs::FsPath;
auto BuildSavePathName(const Entry& e, bool force_id_path) -> fs::FsPath;
auto BuildSaveBasePathLegacy(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath;
auto BuildSaveBasePath(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath;
auto BuildDbiGameFolderName(const Entry& e) -> fs::FsPath;
auto BuildDbiSavePath(const Entry& e, const struct tm& tm, const fs::FsPath& base) -> fs::FsPath;
auto IsDbiBackupName(const Entry& e, const char* name) -> bool;
auto DbiBackupMatchesEntry(const fs::FsPath& zip_path, const Entry& e) -> bool;
auto CollectDbiBackups(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root) -> std::vector<fs::FsPath>;
auto IsDisaSaveFile(fs::Fs* fs, const fs::FsPath& path) -> bool;
auto IsRawSaveCandidate(fs::Fs* fs, const fs::FsPath& path, std::string_view name) -> bool;
inline constexpr Result Result_RawSaveRestoreUnsupported = Result_FsInvalidType;
auto GetRawRestoreUnsupportedMessage() -> std::string;
auto GetBackupSearchPaths() -> std::vector<std::string>;
auto GetShareableSaveBackupRoots() -> std::vector<std::string>;
auto AddBackupSearchPath(const fs::FsPath& path) -> bool;
auto RemoveBackupSearchPath(const fs::FsPath& path) -> bool;
auto NormalizeBackupRoot(const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> fs::FsPath;

} // namespace sphaira::ui::menu::save
