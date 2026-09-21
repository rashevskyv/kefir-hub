#include "ui/menus/save/save_paths.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "path_util.hpp"
#include "i18n.hpp"
#include <minIni.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <utility>
#include <vector>

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

auto IsSystemLikeSave(u8 data_type) -> bool {
    return data_type == FsSaveDataType_System || data_type == FsSaveDataType_SystemBcat;
}

auto DisplayEntryKey(const Entry& e) -> std::string {
    if (e.is_backup) {
        return BackupGroupKey(e);
    }
    char key[0x80];
    if (IsSystemLikeSave(e.save_data_type)) {
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

auto NormalizeBackupSearchPath(std::string_view path) -> std::optional<std::string> {
    const auto normalized = path::NormalizeAbsoluteSdPath(path);
    if (!normalized) {
        return std::nullopt;
    }
    if (*normalized == "/" || path::EqualsIC(*normalized, DEFAULT_BACKUP_ROOT) || path::EqualsIC(*normalized, DBI_SAVES_PATH) || path::EqualsIC(*normalized, DBI_SAVES_ROOT_PATH)) {
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
    add_unique(DBI_SAVES_ROOT_PATH);
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

auto GetRawRestoreUnsupportedMessage() -> std::string {
    return "RAW container restore is unsupported."_i18n;
}

} // namespace sphaira::ui::menu::save
