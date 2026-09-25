#include "ui/menus/save/save_folder_discovery.hpp"
#include "ui/menus/save/save_folder_staging.hpp"
#include "save_internal.hpp"
#include "path_util.hpp"
#include "defines.hpp"
#include <cstring>
#include <strings.h>
#include <utility>

namespace sphaira::ui::menu::save {

auto ExtractTitleIdFromDir(std::string_view name) -> u64 {
    if (name.empty()) {
        return 0;
    }
    // 1. Exact 16 hex characters
    if (name.size() == 16) {
        const auto id = ParseHex16(name);
        if (id != 0) return id;
    }
    // 2. Starts with "0x" or "0X" followed by 16 hex
    if ((name.starts_with("0x") || name.starts_with("0X")) && name.size() >= 18) {
        const auto id = ParseHex16(name.substr(2, 16));
        if (id != 0) return id;
    }
    // 3. Leading 16 hex followed by ' ', '_', or '-'
    if (name.size() > 16 && (name[16] == ' ' || name[16] == '_' || name[16] == '-')) {
        const auto id = ParseHex16(name.substr(0, 16));
        if (id != 0) return id;
    }
    // 4. Bracketed "[0100...]" or "[0x0100...]"
    const auto lbracket = name.find('[');
    if (lbracket != std::string_view::npos) {
        const auto rbracket = name.find(']', lbracket);
        if (rbracket != std::string_view::npos && rbracket > lbracket + 1) {
            std::string_view inner = name.substr(lbracket + 1, rbracket - (lbracket + 1));
            if ((inner.starts_with("0x") || inner.starts_with("0X")) && inner.size() >= 18) {
                inner.remove_prefix(2);
            }
            if (inner.size() == 16) {
                const auto id = ParseHex16(inner);
                if (id != 0) return id;
            }
        }
    }
    return 0;
}

auto ExtractTitleNameFromDir(std::string_view name) -> std::string {
    if (name.empty()) {
        return "";
    }
    if ((name.starts_with("0x") || name.starts_with("0X")) && name.size() > 18) {
        if (ParseHex16(name.substr(2, 16)) != 0) {
            std::string_view rest = name.substr(18);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '_' || rest.front() == '-')) {
                rest.remove_prefix(1);
            }
            if (!rest.empty()) return std::string{rest};
        }
    } else if (name.size() > 17 && (name[16] == ' ' || name[16] == '_' || name[16] == '-')) {
        if (ParseHex16(name.substr(0, 16)) != 0) {
            std::string_view rest = name.substr(17);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '_' || rest.front() == '-')) {
                rest.remove_prefix(1);
            }
            if (!rest.empty()) return std::string{rest};
        }
    }
    const auto bracket_pos = name.find('[');
    if (bracket_pos != std::string_view::npos && bracket_pos > 0) {
        std::string_view title_part = name.substr(0, bracket_pos);
        while (!title_part.empty() && (title_part.back() == ' ' || title_part.back() == '_')) {
            title_part.remove_suffix(1);
        }
        if (!title_part.empty()) return std::string{title_part};
    }
    return "";
}

static auto ExtractIdentityFromAncestors(std::string_view path, u64& out_id, std::string& out_name) -> bool {
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
        path.remove_suffix(1);
    }
    const auto leaf_slash = path.find_last_of("/\\");
    if (leaf_slash == std::string_view::npos) return false;
    path = path.substr(0, leaf_slash);

    while (!path.empty()) {
        while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
            path.remove_suffix(1);
        }
        if (path.empty()) break;
        const auto slash = path.find_last_of("/\\");
        const auto comp = (slash == std::string_view::npos) ? path : path.substr(slash + 1);
        const auto id = ExtractTitleIdFromDir(comp);
        if (id != 0) {
            out_id = id;
            out_name = ExtractTitleNameFromDir(comp);
            return true;
        }
        if (slash == std::string_view::npos) break;
        path = path.substr(0, slash);
    }
    return false;
}

auto InspectBackupFolder(fs::Fs* fs, const fs::FsPath& folder_path, std::string_view folder_name, std::string_view game_dir_name, BackupArchiveInfo& out) -> bool {
    out = BackupArchiveInfo{};
    out.path = folder_path;
    out.is_directory = true;

    if (std::strlen(folder_path.s) == 0) {
        return false;
    }
    if (IsStagingParentOrRelated(folder_path.s)) {
        return false;
    }
    if (path::IsSafeRestoreStagingDir(folder_path.s) || path::IsSafeBackupStagingDir(folder_path.s)) {
        return false;
    }
    std::string_view pview{folder_path.s};
    if (pview.find("_restore_") != std::string_view::npos || pview.find("_staging_") != std::string_view::npos) {
        return false;
    }

    filebrowser::FsDirCollection col{};
    if (R_FAILED(filebrowser::FsView::get_collection(fs, folder_path, "", col, true, true, false))) {
        return false;
    }

    bool has_nx_meta = false;
    s64 payload_files = 0;
    bool has_raw_disa = false;

    for (const auto& file : col.files) {
        if (!strcasecmp(file.name, NX_SAVE_META_NAME)) {
            has_nx_meta = true;
            continue;
        }
        if (IsSaveReservedMetadataRoot(file.name)) {
            continue;
        }
        const auto file_path = fs::AppendPath(folder_path, file.name);
        if (IsRawSaveCandidate(fs, file_path, file.name)) {
            has_raw_disa = true;
        }
        payload_files++;
    }

    if (has_raw_disa) {
        return false;
    }

    // Check if any subdirectories contain child backups (which would make this a parent, not a leaf)
    for (const auto& dir : col.dirs) {
        const auto child_dir_path = fs::AppendPath(folder_path, dir.name);
        fs::File child_meta_file;
        if (R_SUCCEEDED(fs->OpenFile(fs::AppendPath(child_dir_path, NX_SAVE_META_NAME), FsOpenMode_Read, &child_meta_file))) {
            return false;
        }
    }

    // If no direct files, inspect 1 level of subdirectories for payload files
    if (payload_files == 0) {
        for (const auto& dir : col.dirs) {
            filebrowser::FsDirCollection sub_col{};
            const auto sub_path = fs::AppendPath(folder_path, dir.name);
            if (R_SUCCEEDED(filebrowser::FsView::get_collection(fs, sub_path, "", sub_col, true, false, false))) {
                for (const auto& sf : sub_col.files) {
                    if (!strcasecmp(sf.name, NX_SAVE_META_NAME) || IsSaveReservedMetadataRoot(sf.name)) {
                        continue;
                    }
                    const auto sub_file_path = fs::AppendPath(sub_path, sf.name);
                    if (IsRawSaveCandidate(fs, sub_file_path, sf.name)) {
                        has_raw_disa = true;
                    }
                    payload_files++;
                }
            }
        }
    }

    if (has_raw_disa || payload_files == 0) {
        return false;
    }

    out.payload_count = payload_files;
    out.timestamp = ParseBackupNameTimestamp(folder_name);

    u64 derived_id = 0;
    std::string derived_name;
    const bool has_ancestor_id = ExtractIdentityFromAncestors(folder_path.s, derived_id, derived_name);

    if (has_nx_meta) {
        fs::File meta_file;
        const auto meta_path = fs::AppendPath(folder_path, NX_SAVE_META_NAME);
        if (R_FAILED(fs->OpenFile(meta_path, FsOpenMode_Read, &meta_file))) {
            return false;
        }
        s64 meta_size = 0;
        if (R_FAILED(meta_file.GetSize(&meta_size)) || (meta_size != 85 && meta_size != 86 && meta_size != 128)) {
            return false;
        }
        u8 meta_buf[128]{};
        u64 bytes_read = 0;
        if (R_FAILED(meta_file.Read(0, meta_buf, static_cast<u64>(meta_size), FsReadOption_None, &bytes_read)) ||
            bytes_read != static_cast<u64>(meta_size)) {
            return false;
        }
        DecodedSaveMetaInternal decoded{};
        if (!DecodeNxSaveMeta(meta_buf, static_cast<size_t>(meta_size), decoded)) {
            return false;
        }

        out.application_id = decoded.application_id;
        out.system_save_data_id = decoded.system_save_data_id;
        out.save_data_type = decoded.save_data_type;
        out.uid = decoded.uid;
        out.save_data_index = decoded.save_data_index;
        out.save_data_rank = decoded.save_data_rank;
        out.rank_known = true;
        out.commit_id = decoded.commit_id;
        out.source_timestamp = decoded.timestamp;
        if (out.timestamp == 0 && decoded.timestamp != 0) {
            out.timestamp = PosixToTimestamp(decoded.timestamp);
        }
        out.backup_source = BackupSource::Jksv;
    } else {
        u64 title_id = has_ancestor_id ? derived_id : 0;
        if (title_id == 0 && !game_dir_name.empty()) {
            title_id = ExtractTitleIdFromDir(game_dir_name);
        }
        if (title_id == 0) {
            title_id = InferBackupIdFromPath(folder_path.s);
        }
        if (title_id == 0) {
            return false;
        }

        const bool is_system = (title_id & 0x8000000000000000ULL) ||
                               path::HasPathDirComponentIC(folder_path.s, "System Saves") ||
                               path::HasPathDirComponentIC(folder_path.s, "Save System");
        if (is_system) {
            out.system_save_data_id = title_id;
            out.save_data_type = FsSaveDataType_System;
        } else {
            out.application_id = title_id;
            out.save_data_type = FsSaveDataType_Account;
        }

        out.uid = AccountUid{0, 0};
        out.save_data_index = 0;
        out.save_data_rank = 0;
        out.rank_known = false;
        out.commit_id = 0;
        out.source_timestamp = 0;

        if (path::HasPathDirComponentIC(folder_path.s, "checkpoint")) {
            out.backup_source = BackupSource::Checkpoint;
        } else if (path::HasPathDirComponentIC(folder_path.s, "jksv")) {
            out.backup_source = BackupSource::Jksv;
        } else {
            out.backup_source = BackupSource::Other;
        }
    }

    if (out.application_id == 0 && out.system_save_data_id == 0) {
        return false;
    }

    if (!derived_name.empty()) {
        out.dbi_game_dir = std::move(derived_name);
    } else if (!game_dir_name.empty()) {
        std::string name_from_arg = ExtractTitleNameFromDir(game_dir_name);
        if (!name_from_arg.empty()) {
            out.dbi_game_dir = std::move(name_from_arg);
        } else if (!IsHex16(game_dir_name)) {
            out.dbi_game_dir = std::string{game_dir_name};
        }
    }

    if (out.dbi_game_dir.empty()) {
        fs::File title_file;
        if (R_SUCCEEDED(fs->OpenFile(fs::AppendPath(folder_path, "title.txt"), FsOpenMode_Read, &title_file))) {
            char tbuf[128]{};
            u64 tread = 0;
            if (R_SUCCEEDED(title_file.Read(0, tbuf, sizeof(tbuf) - 1, FsReadOption_None, &tread)) && tread > 0) {
                while (tread > 0 && (tbuf[tread - 1] == '\r' || tbuf[tread - 1] == '\n' || tbuf[tread - 1] == ' ')) {
                    tbuf[--tread] = '\0';
                }
                if (tread > 0) {
                    out.dbi_game_dir = tbuf;
                }
            }
        }
    }

    return true;
}

auto InspectSaveFolderAdmission(const fs::FsPath& folder_path, ProgressBox* pbox, bool allow_empty) -> SaveArchiveAdmissionResult {
    SaveArchiveAdmissionResult result{};

    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = folder_path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

    const char* filename = std::strrchr(folder_path.s, '/');
    filename = filename ? filename + 1 : folder_path.s;

    BackupArchiveInfo info{};
    if (!InspectBackupFolder(probe_fs, folder_path, filename, "", info)) {
        result.rc = FsError_PathNotFound;
        return result;
    }

    if (!allow_empty && info.payload_count == 0) {
        result.rc = FsError_PathNotFound;
        return result;
    }

    const auto meta_path = fs::AppendPath(folder_path, NX_SAVE_META_NAME);
    fs::File meta_file;
    if (R_SUCCEEDED(probe_fs->OpenFile(meta_path, FsOpenMode_Read, &meta_file))) {
        s64 meta_size = 0;
        if (R_SUCCEEDED(meta_file.GetSize(&meta_size)) && (meta_size == 85 || meta_size == 86 || meta_size == 128)) {
            u8 meta_buf[128]{};
            u64 bytes_read = 0;
            if (R_SUCCEEDED(meta_file.Read(0, meta_buf, static_cast<u64>(meta_size), FsReadOption_None, &bytes_read)) &&
                bytes_read == static_cast<u64>(meta_size)) {
                DecodedSaveMetaInternal decoded{};
                if (DecodeNxSaveMeta(meta_buf, static_cast<size_t>(meta_size), decoded)) {
                    if (decoded.owner_id != 0 &&
                        decoded.data_size > 0 && (decoded.data_size % 0x4000 == 0) &&
                        decoded.journal_size >= 0 && (decoded.journal_size % 0x4000 == 0) &&
                        decoded.save_data_type == FsSaveDataType_Account &&
                        decoded.save_data_rank == FsSaveDataRank_Primary &&
                        decoded.save_data_index == 0) {
                        result.sizing.has_sizing = true;
                        result.sizing.data_size = decoded.data_size;
                        result.sizing.journal_size = decoded.journal_size;
                        result.sizing.has_metadata = true;
                        result.sizing.owner_id = decoded.owner_id;
                        result.sizing.attr.application_id = decoded.application_id;
                        result.sizing.attr.save_data_type = decoded.save_data_type;
                        result.sizing.attr.save_data_rank = decoded.save_data_rank;
                        result.sizing.attr.save_data_index = decoded.save_data_index;
                        result.sizing.attr.uid = decoded.uid;
                    }
                }
            }
        }
    }

    result.admitted = true;
    result.rc = 0;
    result.payload_file_count = info.payload_count;
    return result;
}

} // namespace sphaira::ui::menu::save
