#include "ui/menus/save/save_paths.hpp"
#include "minizip_helper.hpp"
#include <minizip/unzip.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <utility>

namespace sphaira::ui::menu::save {


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
    std::unreachable();
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

    constexpr auto tail_len = std::string_view{"YYYY.MM.DD @ HH.MM.SS.zip"}.size();
    if (name.size() >= tail_len) {
        char buf[tail_len + 1]{};
        std::memcpy(buf, name.data() + name.size() - tail_len, tail_len);

        u32 year, mon, day, hour, min, sec;
        if (6 == std::sscanf(buf, "%4u.%2u.%2u @ %2u.%2u.%2u", &year, &mon, &day, &hour, &min, &sec)) {
            return (u64)year * 10000000000ULL + (u64)mon * 100000000ULL + (u64)day * 1000000ULL
                + (u64)hour * 10000ULL + (u64)min * 100ULL + sec;
        }
    }

    return 0;
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
    char key[0x40];
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

auto BuildDbiSavePath(const Entry& e, const struct tm& tm) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s/%04d%02d%02d/%016lX_%c_%04d%02d%02d%02d%02d%02d_%u.zip",
        DBI_SAVES_PATH, BuildDbiGameFolderName(e).s,
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

} // namespace sphaira::ui::menu::save
