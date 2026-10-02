#include "ui/menus/save/save_paths.hpp"
#include "save_internal.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "path_util.hpp"
#include "minizip_helper.hpp"
#include <minizip/unzip.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace sphaira::ui::menu::save {

auto DbiBackupMatchesEntry(const fs::FsPath& zip_path, const Entry& e) -> bool {
    SaveReaderContext reader_ctx;
    zlib_filefunc64_def file_func;
    reader_ctx.InitFileFunc(&file_func);

    auto zfile = unzOpen2_64(zip_path.s, &file_func);
    if (!zfile) {
        return false;
    }
    bool zfile_open = true;
    ON_SCOPE_EXIT({
        if (zfile_open && zfile) {
            unzClose(zfile);
        }
    });

    DecodedSaveMetadata archive_meta{};
    Result meta_rc = 0;
    const auto meta_status = ReadArchiveSaveMetadata(zfile, nullptr, archive_meta, &meta_rc);

    zfile_open = false;
    const int close_res = unzClose(zfile);
    if (close_res != UNZ_OK || reader_ctx.HasError()) {
        return false;
    }

    if (meta_status == ArchiveMetaStatus::Invalid) {
        return false;
    }
    if (meta_status == ArchiveMetaStatus::Valid) {
        const auto& meta = archive_meta.meta;
        if (meta.attr.save_data_type != e.save_data_type) {
            return false;
        }
        if (meta.attr.application_id != e.application_id) {
            return false;
        }
        if (e.save_data_type == FsSaveDataType_Account) {
            if (std::memcmp(&meta.attr.uid, &e.uid, sizeof(e.uid)) != 0) {
                return false;
            }
        }
        if (e.save_data_type == FsSaveDataType_Cache) {
            if (meta.attr.save_data_index != e.save_data_index) {
                return false;
            }
        }
        return true;
    }

    // Absent metadata -> existing metadata-free DBI behavior preserved
    if (e.save_data_type != FsSaveDataType_Account && e.save_data_type != FsSaveDataType_Cache) {
        return true;
    }
    return true;
}

auto CollectDbiBackups(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root) -> std::vector<fs::FsPath> {
    std::vector<fs::FsPath> out;

    const auto sort_desc = [](std::vector<FsDirectoryEntry>& entries) {
        std::ranges::sort(entries, [](const FsDirectoryEntry& a, const FsDirectoryEntry& b) {
            return strcasecmp(a.name, b.name) > 0;
        });
    };

    const auto scan_game_dir = [&](const fs::FsPath& game_dir) {
        filebrowser::FsDirCollection direct{};
        filebrowser::FsView::get_collection(fs, game_dir, "", direct, true, true, false);
        sort_desc(direct.files);
        for (const auto& file : direct.files) {
            if (IsDbiBackupName(e, file.name)) {
                const auto p = fs::AppendPath(direct.path, file.name);
                bool duplicate = false;
                for (const auto& existing : out) {
                    if (path::EqualsIC(existing.s, p.s)) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    out.emplace_back(p);
                }
            }
        }
        sort_desc(direct.dirs);
        for (const auto& date : direct.dirs) {
            filebrowser::FsDirCollection files{};
            filebrowser::FsView::get_collection(fs, fs::AppendPath(game_dir, date.name), "", files, true, false, false);
            sort_desc(files.files);

            for (const auto& file : files.files) {
                if (IsDbiBackupName(e, file.name)) {
                    const auto p = fs::AppendPath(files.path, file.name);
                    bool duplicate = false;
                    for (const auto& existing : out) {
                        if (path::EqualsIC(existing.s, p.s)) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        out.emplace_back(p);
                    }
                }
            }
        }
    };

    std::vector<fs::FsPath> dbi_roots;
    auto add_root = [&](std::string_view r) {
        if (r.empty()) return;
        for (const auto& existing : dbi_roots) {
            if (path::EqualsIC(existing.s, r)) return;
        }
        dbi_roots.emplace_back(r);
    };
    if (!backup_root.empty()) {
        add_root(backup_root.s);
    }
    add_root(DEFAULT_BACKUP_ROOT);
    add_root(GetDbiSavesPath());
    add_root(DBI_SAVES_ROOT_PATH);
    for (const auto& custom : GetBackupSearchPaths()) {
        add_root(custom);
    }

    const auto game_folder = BuildDbiGameFolderName(e);
    for (const auto& root_str : dbi_roots) {
        const auto dbi_root = fs::AppendPath(fs->Root(), root_str);
        scan_game_dir(fs::AppendPath(dbi_root, game_folder));
    }

    if (out.empty()) {
        for (const auto& root_str : dbi_roots) {
            const auto dbi_root = fs::AppendPath(fs->Root(), root_str);
            filebrowser::FsDirCollection root{};
            filebrowser::FsView::get_collection(fs, dbi_root, "", root, false, true, false);

            for (const auto& dir : root.dirs) {
                if (!strcasecmp(dir.name, game_folder)) {
                    continue;
                }
                scan_game_dir(fs::AppendPath(dbi_root, dir.name));
            }
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

    bool loaded = false;

    if (is_zip) {
        // Precedence 1: valid embedded archive metadata
        SaveReaderContext reader_ctx;
        zlib_filefunc64_def file_func;
        reader_ctx.InitFileFunc(&file_func);
        auto zfile = unzOpen2_64(path.s, &file_func);
        if (!zfile) {
            return false;
        }
        bool zfile_open = true;
        ON_SCOPE_EXIT({
            if (zfile_open && zfile) {
                unzClose(zfile);
            }
        });

        DecodedSaveMetadata archive_meta{};
        Result meta_rc = 0;
        const auto meta_status = ReadArchiveSaveMetadata(zfile, nullptr, archive_meta, &meta_rc);

        zfile_open = false;
        const int close_res = unzClose(zfile);
        if (close_res != UNZ_OK || reader_ctx.HasError()) {
            return false;
        }

        if (meta_status == ArchiveMetaStatus::Invalid) {
            // Present invalid metadata: fail closed, NO fallback!
            return false;
        }

        if (archive_meta.payload_count == 0) {
            // Metadata-only archive without kept payload: fail closed, omit from library!
            return false;
        }

        out.payload_count = archive_meta.payload_count;
        out.owner_name = archive_meta.account_name;

        if (meta_status == ArchiveMetaStatus::Valid) {
            const auto& meta = archive_meta.meta;
            out.application_id = meta.attr.application_id;
            out.system_save_data_id = meta.attr.system_save_data_id;
            out.save_data_type = meta.attr.save_data_type;
            out.uid = meta.attr.uid;
            out.save_data_index = meta.attr.save_data_index;
            out.save_data_rank = meta.attr.save_data_rank;
            out.rank_known = true;
            out.commit_id = meta.commit_id;
            out.source_timestamp = meta.timestamp;
            if (out.timestamp == 0 && meta.timestamp != 0) {
                out.timestamp = PosixToTimestamp(meta.timestamp);
            }
            loaded = true;
        }

        if (!loaded) {
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

        if (archive_meta.has_kefir_comment) {
            out.backup_source = BackupSource::KefirHub;
        } else if (archive_meta.has_dbi_extra || archive_meta.has_dbi_info ||
                   path::HasPathDirComponentIC(path.s, "dbi") ||
                   path::HasPathDirComponentIC(path.s, "dbisaves")) {
            out.backup_source = BackupSource::Dbi;
        } else if (archive_meta.has_nx_meta || path::HasPathDirComponentIC(path.s, "jksv")) {
            out.backup_source = BackupSource::Jksv;
        } else if (path::HasPathDirComponentIC(path.s, "checkpoint")) {
            out.backup_source = BackupSource::Checkpoint;
        } else {
            out.backup_source = BackupSource::Other;
        }
    }

    if (!loaded) {
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

    if (!is_zip) {
        if (path::HasPathDirComponentIC(path.s, "dbi") ||
            path::HasPathDirComponentIC(path.s, "dbisaves")) {
            out.backup_source = BackupSource::Dbi;
        } else if (path::HasPathDirComponentIC(path.s, "jksv")) {
            out.backup_source = BackupSource::Jksv;
        } else if (path::HasPathDirComponentIC(path.s, "checkpoint")) {
            out.backup_source = BackupSource::Checkpoint;
        } else {
            out.backup_source = BackupSource::Other;
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
            // two users with one nickname get the same tag as in the "Restore for user" list.
            const auto same_name = std::ranges::count_if(accounts, [&](const auto& a) { return !std::strncmp(a.nickname, acc.nickname, sizeof(acc.nickname)); });
            if (same_name < 2) {
                return acc.nickname;
            }
            char tagged[64];
            std::snprintf(tagged, sizeof(tagged), "%.32s (%04X)", acc.nickname, static_cast<unsigned>(acc.uid.uid[1] & 0xFFFF));
            return tagged;
        }
    }
    if (!e.backup_owner_name.empty()) {
        return e.backup_owner_name; // a user of another console: the name saved in the archive
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

namespace {

auto FormatBackupRankMarker(const Entry& e) -> std::string {
    if (!e.backup_rank_known) {
        return "rk:?";
    }
    return (e.save_data_rank == FsSaveDataRank_Secondary) ? "rk:1" : "rk:0";
}

} // namespace

auto GetBackupSecondaryColumns(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> BackupSecondaryColumns {
    const u64 id = IsSystemLikeSave(e.save_data_type) ? e.system_save_data_id : e.application_id;
    char id_str[33];
    std::snprintf(id_str, sizeof(id_str), "%016lX", id);

    BackupSecondaryColumns cols;
    cols.title_id = id_str;
    cols.account = "  •  " + FormatBackupAccount(e, accounts) + "  •  " + FormatBackupRankMarker(e);
    const auto date_str = FormatBackupTimestamp(e.backup_timestamp, false);
    if (!date_str.empty()) {
        cols.timestamp = "  •  " + date_str;
    }
    if (e.backup_count > 1) {
        cols.archive_count = "  •  " + std::to_string(e.backup_count) + " archives";
    }
    if (e.backup_source == BackupSource::Dbi) {
        cols.archive_count += "  •  DBI";
    }
    return cols;
}

auto FormatBackupSecondaryText(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string {
    const std::string account = FormatBackupAccount(e, accounts);
    const std::string rank_str = FormatBackupRankMarker(e);
    const std::string date_str = FormatBackupTimestamp(e.backup_timestamp, true);

    std::string out = account + "  •  " + rank_str + "  •  " + date_str;
    if (e.backup_count > 1) {
        out += " (" + std::to_string(e.backup_count) + ")";
    }
    if (e.backup_source == BackupSource::Dbi) {
        out += "  •  DBI";
    }
    return out;
}

static auto FormatRankKeyPart(bool rank_known, u8 rank) -> const char* {
    if (!rank_known) {
        return "rk:?";
    }
    return (rank == FsSaveDataRank_Secondary) ? "rk:1" : "rk:0";
}

auto BackupGroupKey(const BackupArchiveInfo& info) -> std::string {
    char key[0x80];
    const char* rk = FormatRankKeyPart(info.rank_known, info.save_data_rank);
    if (IsSystemLikeSave(info.save_data_type)) {
        std::snprintf(key, sizeof(key), "backup:system:%u:%016lX:%u:%s",
            info.save_data_type, info.system_save_data_id, info.save_data_index, rk);
    } else {
        std::snprintf(key, sizeof(key), "backup:app:%016lX:%u:%016lX%016lX:%u:%s",
            info.application_id, info.save_data_type, info.uid.uid[0], info.uid.uid[1], info.save_data_index, rk);
    }
    return key;
}

auto BackupGroupKey(const Entry& e) -> std::string {
    char key[0x80];
    const bool rank_known = e.is_backup ? e.backup_rank_known : true;
    const char* rk = FormatRankKeyPart(rank_known, e.save_data_rank);
    if (IsSystemLikeSave(e.save_data_type)) {
        std::snprintf(key, sizeof(key), "backup:system:%u:%016lX:%u:%s",
            e.save_data_type, e.system_save_data_id, e.save_data_index, rk);
    } else {
        std::snprintf(key, sizeof(key), "backup:app:%016lX:%u:%016lX%016lX:%u:%s",
            e.application_id, e.save_data_type, e.uid.uid[0], e.uid.uid[1], e.save_data_index, rk);
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

} // namespace sphaira::ui::menu::save
