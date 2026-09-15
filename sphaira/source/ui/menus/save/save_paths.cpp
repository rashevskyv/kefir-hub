#include "ui/menus/save/save_paths.hpp"
#include "app.hpp"
#include "path_util.hpp"
#include "minizip_helper.hpp"
#include <minizip/unzip.h>
#include <minIni.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <utility>

namespace sphaira::ui::menu::save {

constexpr const char* BACKUP_PATHS_INI_SECTION = "save_backup_paths";


constexpr std::array<u8, 7> SAVE_TYPE_VALUES{
    FsSaveDataType_System,
    FsSaveDataType_Account,
    FsSaveDataType_Bcat,
    FsSaveDataType_Device,
    FsSaveDataType_Temporary,
    FsSaveDataType_Cache,
    FsSaveDataType_SystemBcat,
};

auto GetSaveFolder(u8 data_type) -> fs::FsPath {
    switch (data_type) {
        case FsSaveDataType_System:     return "Save System";
        case FsSaveDataType_SystemBcat: return "Save System BCAT";
        case FsSaveDataType_Account:    return "Save";
        case FsSaveDataType_Bcat:       return "Save BCAT";
        case FsSaveDataType_Device:     return "Save Device";
        case FsSaveDataType_Temporary:  return "Save Temporary";
        case FsSaveDataType_Cache:      return "Save Cache";
    }
    std::unreachable();
}

auto GetSaveFolder(const Entry& e) -> fs::FsPath {
    return GetSaveFolder(e.save_data_type);
}

auto GetSaveTypeSubdir(u8 data_type) -> fs::FsPath {
    switch (data_type) {
        case FsSaveDataType_Account:   return "Account";
        case FsSaveDataType_Bcat:      return "BCAT";
        case FsSaveDataType_Device:    return "Device";
        case FsSaveDataType_Temporary: return "Temporary";
        case FsSaveDataType_Cache:     return "Cache";
    }
    std::unreachable();
}

auto GetDbiTypeLetter(u8 data_type) -> char {
    switch (data_type) {
        case FsSaveDataType_Account:   return 'A';
        case FsSaveDataType_Bcat:      return 'B';
        case FsSaveDataType_Device:    return 'D';
        case FsSaveDataType_Temporary: return 'T';
        case FsSaveDataType_Cache:     return 'C';
    }
    return '?';
}

auto ParseDbiTypeLetter(char c) -> u8 {
    switch (c) {
        case 'A': case 'a': return FsSaveDataType_Account;
        case 'B': case 'b': return FsSaveDataType_Bcat;
        case 'D': case 'd': return FsSaveDataType_Device;
        case 'T': case 't': return FsSaveDataType_Temporary;
        case 'C': case 'c': return FsSaveDataType_Cache;
        default: return 0xFF;
    }
}

auto ParseDbiBackupIndex(std::string_view name) -> u16 {
    if (!name.ends_with(".zip")) {
        return 0;
    }
    name.remove_suffix(4);
    const auto last_under = name.rfind('_');
    if (last_under == name.npos || last_under + 1 >= name.size()) {
        return 0;
    }
    u32 idx = 0;
    for (size_t i = last_under + 1; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        idx = idx * 10 + (name[i] - '0');
    }
    return static_cast<u16>(idx);
}

auto ParseHex16(std::string_view str) -> u64 {
    if (str.size() != 16) {
        return 0;
    }
    u64 id = 0;
    for (size_t i = 0; i < 16; i++) {
        const char c = str[i];
        int nibble;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
        else return 0;
        id = (id << 4) | static_cast<u64>(nibble);
    }
    return id;
}

auto IsHex16(std::string_view str) -> bool {
    return str.size() == 16 && ParseHex16(str) != 0;
}

auto PosixToTimestamp(u64 posix_sec) -> u64 {
    if (!posix_sec) {
        return 0;
    }
    const time_t t = static_cast<time_t>(posix_sec);
    const auto tm = std::localtime(&t);
    if (!tm) {
        return 0;
    }
    return (u64)(tm->tm_year + 1900) * 10000000000ULL +
           (u64)(tm->tm_mon + 1) * 100000000ULL +
           (u64)(tm->tm_mday) * 1000000ULL +
           (u64)(tm->tm_hour) * 10000ULL +
           (u64)(tm->tm_min) * 100ULL +
           (u64)(tm->tm_sec);
}

auto ParseDbiBackupNameTimestamp(std::string_view name) -> u64 {
    if (name.size() < 34 || name[16] != '_' || name[18] != '_' || name[33] != '_') {
        return 0;
    }

    u64 ts{};
    for (size_t i = 19; i < 33; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        ts = ts * 10 + (name[i] - '0');
    }
    return ts;
}

auto ParseBackupNameTimestamp(std::string_view name) -> u64 {
    if (const auto ts = ParseDbiBackupNameTimestamp(name)) {
        return ts;
    }

    const auto at_pos = name.find(" @ ");
    if (at_pos != name.npos && at_pos >= 10 && at_pos + 11 <= name.size()) {
        u32 year, mon, day, hour, min, sec;
        if (6 == std::sscanf(name.data() + at_pos - 10, "%4u.%2u.%2u @ %2u.%2u.%2u", &year, &mon, &day, &hour, &min, &sec)) {
            return (u64)year * 10000000000ULL + (u64)mon * 100000000ULL + (u64)day * 1000000ULL
                + (u64)hour * 10000ULL + (u64)min * 100ULL + sec;
        }
    }

    for (size_t i = 0; i + 14 <= name.size(); i++) {
        u32 year, mon, day, hour, min, sec;
        if ((i + 15 <= name.size() && 6 == std::sscanf(name.data() + i, "%4u%2u%2u_%2u%2u%2u", &year, &mon, &day, &hour, &min, &sec)) ||
            (6 == std::sscanf(name.data() + i, "%4u%2u%2u%2u%2u%2u", &year, &mon, &day, &hour, &min, &sec))) {
            if (year >= 2000 && year <= 2099 && mon >= 1 && mon <= 12 && day >= 1 && day <= 31 && hour <= 23 && min <= 59 && sec <= 59) {
                return (u64)year * 10000000000ULL + (u64)mon * 100000000ULL + (u64)day * 1000000ULL
                    + (u64)hour * 10000ULL + (u64)min * 100ULL + sec;
            }
        }
    }

    return 0;
}

auto ParseDbiBackupAppId(std::string_view name) -> u64 {
    // must look like "<16 hex>_<letter>_..." (matches ParseDbiBackupNameTimestamp).
    if (name.size() < 19 || name[16] != '_' || name[18] != '_') {
        return 0;
    }

    u64 id{};
    for (size_t i = 0; i < 16; i++) {
        const char c = name[i];
        int nibble;
        if (c >= '0' && c <= '9') {
            nibble = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            nibble = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            nibble = c - 'A' + 10;
        } else {
            return 0;
        }
        id = (id << 4) | static_cast<u64>(nibble);
    }
    return id;
}

auto GetSaveTypeLabel(u8 data_type) -> const char* {
    switch (data_type) {
        case FsSaveDataType_System:     return "System";
        case FsSaveDataType_Account:    return "Account";
        case FsSaveDataType_Bcat:       return "BCAT";
        case FsSaveDataType_Device:     return "Device";
        case FsSaveDataType_Temporary:  return "Temporary";
        case FsSaveDataType_Cache:      return "Cache";
        case FsSaveDataType_SystemBcat: return "System BCAT";
    }
    return "Unknown";
}

auto SaveTypeIndex(u8 data_type) -> size_t {
    for (size_t i = 0; i < SAVE_TYPE_VALUES.size(); i++) {
        if (SAVE_TYPE_VALUES[i] == data_type) {
            return i;
        }
    }
    return 0;
}

auto SaveEntryKey(const FsSaveDataInfo& e) -> std::string {
    char key[0x80];
    std::snprintf(key, sizeof(key), "%u:%u:%016lX:%016lX:%016lX:%016lX:%u:%u",
        e.save_data_space_id, e.save_data_type, e.application_id,
        e.system_save_data_id, e.uid.uid[0], e.uid.uid[1],
        e.save_data_rank, e.save_data_index);
    return key;
}

auto IsSystemLikeSave(u8 data_type) -> bool {
    return data_type == FsSaveDataType_System || data_type == FsSaveDataType_SystemBcat;
}

auto DisplayEntryKey(const Entry& e) -> std::string {
    char key[0x80];
    if (e.is_backup) {
        if (IsSystemLikeSave(e.save_data_type)) {
            std::snprintf(key, sizeof(key), "backup:system:%u:%016lX:%u",
                e.save_data_type, e.system_save_data_id, e.save_data_index);
        } else {
            std::snprintf(key, sizeof(key), "backup:app:%016lX:%u:%016lX%016lX:%u",
                e.application_id, e.save_data_type, e.uid.uid[0], e.uid.uid[1], e.save_data_index);
        }
    } else if (IsSystemLikeSave(e.save_data_type)) {
        std::snprintf(key, sizeof(key), "system:%u:%016lX", e.save_data_type, e.system_save_data_id);
    } else {
        std::snprintf(key, sizeof(key), "app:%016lX", e.application_id);
    }
    return key;
}

auto BuildSaveName(const Entry& e) -> fs::FsPath {
    fs::FsPath name_buf = e.GetName();
    title::utilsReplaceIllegalCharacters(name_buf, true);
    return name_buf;
}

auto BuildSavePathName(const Entry& e, bool force_id_path) -> fs::FsPath {
    fs::FsPath name;
    if (e.save_data_type == FsSaveDataType_System || e.save_data_type == FsSaveDataType_SystemBcat) {
        std::snprintf(name, sizeof(name), "%016lX", e.system_save_data_id);
    } else if (force_id_path || !strcasecmp(e.GetName(), "corrupted")) {
        std::snprintf(name, sizeof(name), "%016lX", e.application_id);
    } else {
        name = BuildSaveName(e);
    }

    return name;
}

auto BuildSaveBasePathLegacy(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath {
    return fs::AppendPath(fs::AppendPath(backup_root, GetSaveFolder(e)), BuildSavePathName(e, force_id_path));
}

auto BuildSaveBasePath(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath {
    if (IsSystemLikeSave(e.save_data_type)) {
        return BuildSaveBasePathLegacy(e, force_id_path, backup_root);
    }

    return fs::AppendPath(fs::AppendPath(backup_root, BuildSavePathName(e, force_id_path)), GetSaveTypeSubdir(e.save_data_type));
}

auto BuildDbiGameFolderName(const Entry& e) -> fs::FsPath {
    fs::FsPath out{};
    size_t len{};

    if (strcasecmp(e.GetName(), "corrupted")) {
        for (const char* p = e.GetName(); *p && len < sizeof(out) - 1; p++) {
            const char c = *p;
            const bool keep = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ';
            if (keep && !(len == 0 && c == ' ')) {
                out.s[len++] = c;
            }
        }

        while (len && out.s[len - 1] == ' ') {
            len--;
        }
        out.s[len] = '\0';
    }

    if (!len) {
        std::snprintf(out, sizeof(out), "%016lX", e.application_id);
    }

    return out;
}

auto BuildDbiSavePath(const Entry& e, const struct tm& tm, const fs::FsPath& base) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s/%04d%02d%02d/%016lX_%c_%04d%02d%02d%02d%02d%02d_%u.zip",
        base.s, BuildDbiGameFolderName(e).s,
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        e.application_id, GetDbiTypeLetter(e.save_data_type),
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
        (u32)e.save_data_index);
    return path;
}

auto IsDbiBackupName(const Entry& e, const char* name) -> bool {
    char prefix[0x20];
    std::snprintf(prefix, sizeof(prefix), "%016lX_%c_", e.application_id, GetDbiTypeLetter(e.save_data_type));

    if (strncasecmp(name, prefix, std::strlen(prefix))) {
        return false;
    }

    const auto len = std::strlen(name);
    return len > 4 && !strcasecmp(name + len - 4, ".zip");
}

auto DbiBackupMatchesEntry(const fs::FsPath& zip_path, const Entry& e) -> bool {
    if (e.save_data_type != FsSaveDataType_Account && e.save_data_type != FsSaveDataType_Cache) {
        return true;
    }

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_path, &file_func);
    if (!zfile) {
        return true;
    }
    ON_SCOPE_EXIT(unzClose(zfile));

    if (UNZ_END_OF_LIST_OF_FILE == unzLocateFile(zfile, DBI_SAVE_EXTRA_NAME, 2)) {
        return true;
    }
    if (UNZ_OK != unzOpenCurrentFile(zfile)) {
        return true;
    }
    ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

    FsSaveDataExtraData extra{};
    if (sizeof(extra) != unzReadCurrentFile(zfile, &extra, sizeof(extra))) {
        return true;
    }

    if (e.save_data_type == FsSaveDataType_Account) {
        return !std::memcmp(&extra.attr.uid, &e.uid, sizeof(e.uid));
    }
    return extra.attr.save_data_index == e.save_data_index;
}

auto CollectDbiBackups(fs::Fs* fs, const Entry& e) -> std::vector<fs::FsPath> {
    std::vector<fs::FsPath> out;

    const auto sort_desc = [](std::vector<FsDirectoryEntry>& entries) {
        std::ranges::sort(entries, [](const FsDirectoryEntry& a, const FsDirectoryEntry& b) {
            return strcasecmp(a.name, b.name) > 0;
        });
    };

    const auto scan_game_dir = [&](const fs::FsPath& game_dir) {
        filebrowser::FsDirCollection dates{};
        filebrowser::FsView::get_collection(fs, game_dir, "", dates, false, true, false);
        sort_desc(dates.dirs);

        for (const auto& date : dates.dirs) {
            filebrowser::FsDirCollection files{};
            filebrowser::FsView::get_collection(fs, fs::AppendPath(game_dir, date.name), "", files, true, false, false);
            sort_desc(files.files);

            for (const auto& file : files.files) {
                if (IsDbiBackupName(e, file.name)) {
                    out.emplace_back(fs::AppendPath(files.path, file.name));
                }
            }
        }
    };

    const auto dbi_root = fs::AppendPath(fs->Root(), DBI_SAVES_PATH);
    const auto game_folder = BuildDbiGameFolderName(e);
    scan_game_dir(fs::AppendPath(dbi_root, game_folder));

    if (out.empty()) {
        filebrowser::FsDirCollection root{};
        filebrowser::FsView::get_collection(fs, dbi_root, "", root, false, true, false);

        for (const auto& dir : root.dirs) {
            if (!strcasecmp(dir.name, game_folder)) {
                continue;
            }
            scan_game_dir(fs::AppendPath(dbi_root, dir.name));
        }
    }

    return out;
}

auto IsDisaSaveFile(fs::Fs* fs, const fs::FsPath& path) -> bool {
    fs::File file;
    if (R_FAILED(fs->OpenFile(path, FsOpenMode_Read, &file))) {
        return false;
    }
    s64 size{};
    if (R_FAILED(file.GetSize(&size)) || size < 0x200) {
        return false;
    }
    char buf[0x104]{};
    u64 bytes_read{};
    if (R_FAILED(file.Read(0, buf, sizeof(buf), FsReadOption_None, &bytes_read)) || bytes_read < 0x104) {
        return false;
    }
    return std::memcmp(buf + 0x100, "DISF", 4) == 0;
}

auto IsRawSaveCandidate(fs::Fs* fs, const fs::FsPath& path, std::string_view name) -> bool {
    if (name.ends_with(".disa") || name.ends_with(".bin")) {
        return IsDisaSaveFile(fs, path);
    }
    if (name.size() == 16) {
        bool all_hex = true;
        for (char c : name) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                all_hex = false;
                break;
            }
        }
        if (all_hex) {
            return IsDisaSaveFile(fs, path);
        }
    }
    return false;
}

auto NormalizeBackupSearchPath(std::string_view path) -> std::optional<std::string> {
    const auto normalized = path::NormalizeAbsoluteSdPath(path);
    if (!normalized) {
        return std::nullopt;
    }
    if (*normalized == "/" || path::EqualsIC(*normalized, DEFAULT_BACKUP_ROOT) || path::EqualsIC(*normalized, DBI_SAVES_PATH)) {
        return std::nullopt;
    }
    if (normalized->size() >= FS_MAX_PATH) {
        return std::nullopt;
    }
    return normalized;
}

auto GetBackupSearchPaths() -> std::vector<std::string> {
    std::vector<std::string> search_paths;

    ini_browse([](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        if (Section && Value && path::EqualsIC(Section, BACKUP_PATHS_INI_SECTION)) {
            auto* paths = static_cast<std::vector<std::string>*>(UserData);
            const auto normalized = NormalizeBackupSearchPath(Value);
            if (normalized) {
                const bool duplicate = std::any_of(paths->begin(), paths->end(), [&](const std::string& existing) {
                    return path::EqualsIC(existing, *normalized);
                });
                if (!duplicate) {
                    paths->push_back(*normalized);
                }
            }
        }
        return 1;
    }, &search_paths, App::CONFIG_PATH);

    return search_paths;
}

auto GetShareableSaveBackupRoots() -> std::vector<std::string> {
    std::vector<std::string> roots;
    auto add_unique = [&](const std::string& p) {
        if (p.empty()) {
            return;
        }
        for (const auto& existing : roots) {
            if (path::EqualsIC(existing, p)) {
                return;
            }
        }
        roots.push_back(p);
    };

    add_unique(DEFAULT_BACKUP_ROOT);
    add_unique(DBI_SAVES_PATH);
    for (const auto& extra : GetBackupSearchPaths()) {
        add_unique(extra);
    }
    return roots;
}

static auto SaveBackupSearchPaths(const std::vector<std::string>& search_paths) -> bool {
    if (!ini_puts(BACKUP_PATHS_INI_SECTION, nullptr, nullptr, App::CONFIG_PATH)) {
        return false;
    }

    char key[32];
    for (size_t i = 0; i < search_paths.size(); i++) {
        std::snprintf(key, sizeof(key), "path_%zu", i);
        if (!ini_puts(BACKUP_PATHS_INI_SECTION, key, search_paths[i].c_str(), App::CONFIG_PATH)) {
            return false;
        }
    }

    return true;
}

auto AddBackupSearchPath(const fs::FsPath& path) -> bool {
    const auto normalized = NormalizeBackupSearchPath(path.toString());
    if (!normalized) {
        return false;
    }

    auto paths = GetBackupSearchPaths();
    for (const auto& existing : paths) {
        if (path::EqualsIC(existing, *normalized)) {
            return true;
        }
    }

    paths.push_back(*normalized);
    if (!SaveBackupSearchPaths(paths)) {
        return false;
    }

    SignalChange();
    return true;
}

auto RemoveBackupSearchPath(const fs::FsPath& path) -> bool {
    const auto normalized = NormalizeBackupSearchPath(path.toString());
    if (!normalized) {
        return false;
    }

    auto paths = GetBackupSearchPaths();
    const auto it = std::remove_if(paths.begin(), paths.end(), [&](const std::string& existing) {
        return path::EqualsIC(existing, *normalized);
    });

    if (it == paths.end()) {
        return false;
    }

    paths.erase(it, paths.end());
    if (!SaveBackupSearchPaths(paths)) {
        return false;
    }

    SignalChange();
    return true;
}

auto NormalizeBackupRoot(const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> fs::FsPath {
    auto out = path.toString();
    const auto root = fs_entry.root.toString();

    if (fs_entry.type == filebrowser::FsType::Stdio && out.starts_with(root)) {
        out.erase(0, root.size());
    }

    if (out.empty()) {
        out = "/";
    } else if (out.front() != '/') {
        out.insert(out.begin(), '/');
    }

    return out;
}

auto InferBackupIdFromPath(std::string_view full_path) -> u64 {
    u64 parent_hex = 0;
    u64 filename_hex = 0;

    size_t start = 0;
    while (start < full_path.size()) {
        auto slash = full_path.find_first_of("/\\", start);
        auto part = (slash == full_path.npos) ? full_path.substr(start) : full_path.substr(start, slash - start);
        const bool is_last = (slash == full_path.npos);

        if (is_last) {
            if (part.ends_with(".zip") || part.ends_with(".bin") || part.ends_with(".disa")) {
                part = part.substr(0, part.rfind('.'));
            }
            if (part.size() == 16) {
                filename_hex = ParseHex16(part);
            }
        } else {
            if (part.size() == 16) {
                const auto hex = ParseHex16(part);
                if (hex != 0) {
                    parent_hex = hex;
                }
            }
        }

        if (slash == full_path.npos) break;
        start = slash + 1;
    }

    return (parent_hex != 0) ? parent_hex : filename_hex;
}

auto InspectBackupArchive(fs::Fs* fs, const fs::FsPath& path, std::string_view filename, std::string_view dbi_game_dir_name, BackupArchiveInfo& out) -> bool {
    const bool is_zip = path::EndsWithIC(filename, ".zip");
    const bool is_raw = !is_zip && IsRawSaveCandidate(fs, path, filename);
    if (!is_zip && !is_raw) {
        return false;
    }

    out = BackupArchiveInfo{};
    out.path = path;
    out.dbi_game_dir = std::string{dbi_game_dir_name};
    out.timestamp = ParseBackupNameTimestamp(filename);

    if (is_zip) {
        // Precedence 1: valid embedded archive metadata
        zlib_filefunc64_def file_func;
        mz::FileFuncStdio(&file_func);
        auto zfile = unzOpen2_64(path.s, &file_func);
        if (zfile) {
            ON_SCOPE_EXIT(unzClose(zfile));
            bool loaded = false;

            if (UNZ_END_OF_LIST_OF_FILE != unzLocateFile(zfile, DBI_SAVE_EXTRA_NAME, 2)) {
                if (UNZ_OK == unzOpenCurrentFile(zfile)) {
                    ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));
                    FsSaveDataExtraData extra{};
                    if (sizeof(extra) == unzReadCurrentFile(zfile, &extra, sizeof(extra))) {
                        if (extra.attr.application_id != 0) {
                            out.application_id = extra.attr.application_id;
                        }
                        if (extra.attr.system_save_data_id != 0) {
                            out.system_save_data_id = extra.attr.system_save_data_id;
                        }
                        out.save_data_type = extra.attr.save_data_type;
                        out.uid = extra.attr.uid;
                        out.save_data_index = extra.attr.save_data_index;
                        out.save_data_rank = extra.attr.save_data_rank;
                        out.commit_id = extra.commit_id;
                        out.source_timestamp = extra.timestamp;
                        if (out.timestamp == 0 && extra.timestamp != 0) {
                            out.timestamp = PosixToTimestamp(extra.timestamp);
                        }
                        loaded = true;
                    }
                }
            }

            if (!loaded && UNZ_END_OF_LIST_OF_FILE != unzLocateFile(zfile, NX_SAVE_META_NAME, 2)) {
                if (UNZ_OK == unzOpenCurrentFile(zfile)) {
                    ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));
                    NXSaveMeta meta{};
                    if (sizeof(meta) == unzReadCurrentFile(zfile, &meta, sizeof(meta))) {
                        if (meta.magic == NX_SAVE_META_MAGIC && meta.version == NX_SAVE_META_VERSION) {
                            if (meta.attr.application_id != 0) {
                                out.application_id = meta.attr.application_id;
                            }
                            if (meta.attr.system_save_data_id != 0) {
                                out.system_save_data_id = meta.attr.system_save_data_id;
                            }
                            out.save_data_type = meta.attr.save_data_type;
                            out.uid = meta.attr.uid;
                            out.save_data_index = meta.attr.save_data_index;
                            out.save_data_rank = meta.attr.save_data_rank;
                            out.commit_id = meta.commit_id;
                            out.source_timestamp = meta.timestamp;
                            if (out.timestamp == 0 && meta.timestamp != 0) {
                                out.timestamp = PosixToTimestamp(meta.timestamp);
                            }
                            loaded = true;
                        }
                    }
                }
            }
        }

        // Precedence 2: DBI filename fields
        if (out.application_id == 0 && out.system_save_data_id == 0) {
            out.application_id = ParseDbiBackupAppId(filename);
        }
        if (out.save_data_type == 0xFF) {
            if (filename.size() >= 19 && filename[16] == '_' && filename[18] == '_') {
                out.save_data_type = ParseDbiTypeLetter(filename[17]);
            }
        }
        if (out.save_data_index == 0) {
            out.save_data_index = ParseDbiBackupIndex(filename);
        }
    }

    // Precedence 3: explicit DBI directory/folder information
    if (out.application_id == 0 && out.system_save_data_id == 0) {
        if (!dbi_game_dir_name.empty()) {
            out.application_id = ParseHex16(dbi_game_dir_name);
        }
    }

    // Precedence 4: 16-hex component inferred from the full path
    if (out.application_id == 0 && out.system_save_data_id == 0) {
        const auto hex = InferBackupIdFromPath(path.s);
        if (hex != 0) {
            std::string_view p{path.s};
            const bool is_system = (hex & 0x8000000000000000ULL) ||
                                   p.find("Save System") != p.npos ||
                                   (out.save_data_type != 0xFF && IsSystemLikeSave(out.save_data_type));
            if (is_system) {
                out.system_save_data_id = hex;
            } else {
                out.application_id = hex;
            }
        }
    }

    // Save-folder context for save_data_type
    if (out.save_data_type == 0xFF) {
        std::string_view p{path.s};
        if (p.find("Save System BCAT") != p.npos) {
            out.save_data_type = FsSaveDataType_SystemBcat;
        } else if (p.find("Save System") != p.npos) {
            out.save_data_type = FsSaveDataType_System;
        } else if (p.find("Save BCAT") != p.npos || p.find("/BCAT/") != p.npos) {
            out.save_data_type = FsSaveDataType_Bcat;
        } else if (p.find("Save Device") != p.npos || p.find("/Device/") != p.npos) {
            out.save_data_type = FsSaveDataType_Device;
        } else if (p.find("Save Temporary") != p.npos || p.find("/Temporary/") != p.npos) {
            out.save_data_type = FsSaveDataType_Temporary;
        } else if (p.find("Save Cache") != p.npos || p.find("/Cache/") != p.npos) {
            out.save_data_type = FsSaveDataType_Cache;
        } else if (p.find("/Save/") != p.npos || p.find("/Account/") != p.npos) {
            out.save_data_type = FsSaveDataType_Account;
        }
    }

    if (out.application_id == 0 && out.system_save_data_id == 0) {
        return false;
    }

    if (IsSystemLikeSave(out.save_data_type)) {
        if (out.system_save_data_id == 0 && out.application_id != 0) {
            out.system_save_data_id = out.application_id;
            out.application_id = 0;
        }
    } else if (out.save_data_type == 0xFF) {
        if (out.system_save_data_id != 0) {
            out.save_data_type = FsSaveDataType_System;
        } else {
            out.save_data_type = FsSaveDataType_Account;
        }
    }

    return true;
}

auto FormatBackupAccount(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string {
    if (e.save_data_type != FsSaveDataType_Account) {
        return "Not account-bound";
    }

    for (const auto& acc : accounts) {
        if (!std::memcmp(&e.uid, &acc.uid, sizeof(e.uid))) {
            return acc.nickname;
        }
    }

    if (e.uid.uid[0] != 0 || e.uid.uid[1] != 0) {
        char buf[16];
        const u64 display_id = e.uid.uid[0] ? e.uid.uid[0] : e.uid.uid[1];
        std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(display_id & 0xFFFFFFFF));
        return buf;
    }

    return "00000000";
}

auto FormatBackupTimestamp(u64 ts, bool compact) -> std::string {
    if (ts == 0) {
        return "Unknown date";
    }
    const u32 year = static_cast<u32>(ts / 10000000000ULL);
    const u32 mon = static_cast<u32>((ts / 100000000ULL) % 100);
    const u32 day = static_cast<u32>((ts / 1000000ULL) % 100);
    const u32 hour = static_cast<u32>((ts / 10000ULL) % 100);
    const u32 min = static_cast<u32>((ts / 100ULL) % 100);
    const u32 sec = static_cast<u32>(ts % 100);

    char buf[32];
    if (compact) {
        std::snprintf(buf, sizeof(buf), "%04u.%02u.%02u %02u:%02u", year, mon, day, hour, min);
    } else {
        std::snprintf(buf, sizeof(buf), "%04u.%02u.%02u %02u:%02u:%02u", year, mon, day, hour, min, sec);
    }
    return buf;
}

auto GetBackupSecondaryColumns(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> BackupSecondaryColumns {
    const u64 id = IsSystemLikeSave(e.save_data_type) ? e.system_save_data_id : e.application_id;
    char id_str[33];
    std::snprintf(id_str, sizeof(id_str), "%016lX", id);

    BackupSecondaryColumns cols;
    cols.title_id = id_str;
    cols.account = "  •  " + FormatBackupAccount(e, accounts);
    const auto date_str = FormatBackupTimestamp(e.backup_timestamp, false);
    if (!date_str.empty()) {
        cols.timestamp = "  •  " + date_str;
    }
    if (e.backup_count > 1) {
        cols.archive_count = "  •  " + std::to_string(e.backup_count) + " archives";
    }
    return cols;
}

auto FormatBackupSecondaryText(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string {
    const std::string account = FormatBackupAccount(e, accounts);
    const std::string date_str = FormatBackupTimestamp(e.backup_timestamp, true);

    std::string out = account + "  •  " + date_str;
    if (e.backup_count > 1) {
        out += " (" + std::to_string(e.backup_count) + ")";
    }
    return out;
}

auto BackupGroupKey(const BackupArchiveInfo& info) -> std::string {
    char key[0x80];
    if (IsSystemLikeSave(info.save_data_type)) {
        std::snprintf(key, sizeof(key), "backup:system:%u:%016lX:%u",
            info.save_data_type, info.system_save_data_id, info.save_data_index);
    } else {
        std::snprintf(key, sizeof(key), "backup:app:%016lX:%u:%016lX%016lX:%u",
            info.application_id, info.save_data_type, info.uid.uid[0], info.uid.uid[1], info.save_data_index);
    }
    return key;
}

auto BackupGroupKey(const Entry& e) -> std::string {
    char key[0x80];
    if (IsSystemLikeSave(e.save_data_type)) {
        std::snprintf(key, sizeof(key), "backup:system:%u:%016lX:%u",
            e.save_data_type, e.system_save_data_id, e.save_data_index);
    } else {
        std::snprintf(key, sizeof(key), "backup:app:%016lX:%u:%016lX%016lX:%u",
            e.application_id, e.save_data_type, e.uid.uid[0], e.uid.uid[1], e.save_data_index);
    }
    return key;
}

auto VerifyZipIntegrity(const fs::FsPath& path) -> bool {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    auto zfile = unzOpen2_64(path.s, &file_func);
    if (!zfile) {
        return false;
    }
    ON_SCOPE_EXIT(unzClose(zfile));

    unz_global_info64 gi{};
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &gi) || gi.number_entry == 0) {
        return false;
    }

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        return false;
    }

    std::vector<u8> buffer(64 * 1024);
    u64 entries_read = 0;
    while (true) {
        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            return false;
        }

        int read_bytes = 0;
        do {
            read_bytes = unzReadCurrentFile(zfile, buffer.data(), buffer.size());
            if (read_bytes < 0) {
                unzCloseCurrentFile(zfile);
                return false;
            }
        } while (read_bytes > 0);

        if (UNZ_OK != unzCloseCurrentFile(zfile)) {
            return false;
        }

        entries_read++;

        const int next_rc = unzGoToNextFile(zfile);
        if (next_rc == UNZ_END_OF_LIST_OF_FILE) {
            break;
        }
        if (next_rc != UNZ_OK) {
            return false;
        }
    }

    return entries_read == gi.number_entry && entries_read > 0;
}

auto VerifyDisaIntegrity(fs::Fs* fs, const fs::FsPath& path) -> bool {
    if (!IsDisaSaveFile(fs, path)) {
        return false;
    }
    fs::File file;
    if (R_FAILED(fs->OpenFile(path, FsOpenMode_Read, &file))) {
        return false;
    }
    s64 size{};
    if (R_FAILED(file.GetSize(&size)) || size < 0x200) {
        return false;
    }

    std::vector<u8> buffer(64 * 1024);
    s64 offset = 0;
    while (offset < size) {
        const u64 to_read = std::min<s64>(buffer.size(), size - offset);
        u64 bytes_read = 0;
        if (R_FAILED(file.Read(offset, buffer.data(), to_read, FsReadOption_None, &bytes_read)) || bytes_read != to_read) {
            return false;
        }
        offset += bytes_read;
    }
    return true;
}

} // namespace sphaira::ui::menu::save
