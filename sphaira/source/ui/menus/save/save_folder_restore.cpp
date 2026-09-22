#include "save_folder_staging.hpp"
#include "save_backup_writer.hpp"
#include "save_restore_zip.hpp"
#include "ui/menus/save_menu.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "haze_helper.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/save/save_paths.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string_view>
#include <vector>
#include <limits>
#include <ctime>
#include <minizip/zip.h>
#include <minizip/unzip.h>

namespace sphaira::ui::menu::save {
namespace {

static Result StageBackupFolderToZip(
    ProgressBox* pbox,
    fs::FsNativeSd& sd_fs,
    const fs::FsPath& owned_stage_zip,
    const std::map<std::string, FolderSourceEntry>& source_files,
    const std::set<std::string>& source_dirs) {

    RecoveryStreamContext rec_ctx{};
    zlib_filefunc64_def file_func{};
    file_func.zopen64_file = RecoveryOpen;
    file_func.zread_file = RecoveryRead;
    file_func.zwrite_file = RecoveryWrite;
    file_func.ztell64_file = RecoveryTell;
    file_func.zseek64_file = RecoverySeek;
    file_func.zclose_file = RecoveryClose;
    file_func.zerror_file = RecoveryError;
    file_func.opaque = &rec_ctx;

    auto zfile = zipOpen2_64(owned_stage_zip.s, APPEND_STATUS_CREATE, NULL, &file_func);
    R_UNLESS(zfile, Result_ZipOpenNewFileInZip);
    bool zfile_open = true;
    ON_SCOPE_EXIT({
        if (zfile_open && zfile) {
            zipClose(zfile, NULL);
        }
    });

    const auto t = std::time(nullptr);
    const auto tm = std::localtime(&t);
    zip_fileinfo zip_info_default{};
    if (tm) {
        zip_info_default.tmz_date.tm_sec = tm->tm_sec;
        zip_info_default.tmz_date.tm_min = tm->tm_min;
        zip_info_default.tmz_date.tm_hour = tm->tm_hour;
        zip_info_default.tmz_date.tm_mday = tm->tm_mday;
        zip_info_default.tmz_date.tm_mon = tm->tm_mon;
        zip_info_default.tmz_date.tm_year = tm->tm_year;
    }

    for (const auto& dir_key : source_dirs) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }
        R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, dir_key.c_str(), &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
        bool dir_open = true;
        ON_SCOPE_EXIT({
            if (dir_open && zfile) {
                zipCloseFileInZip(zfile);
            }
        });
        dir_open = false;
        R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
        R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
    }

    std::vector<u8> buffer(64 * 1024);
    for (const auto& [file_key, file_info] : source_files) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        FsDirEntryType cur_type{};
        R_TRY(sd_fs.GetEntryType(file_info.full_path, &cur_type));
        R_UNLESS(cur_type == FsDirEntryType_File, FsError_TargetLocked);

        fs::File src_file;
        R_TRY(sd_fs.OpenFile(file_info.full_path, FsOpenMode_Read, &src_file));
        s64 cur_size = 0;
        R_TRY(src_file.GetSize(&cur_size));
        R_UNLESS(cur_size == file_info.size, FsError_TargetLocked);

        const auto level = Z_DEFAULT_COMPRESSION;
        R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, file_key.c_str(), &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, level), Result_ZipOpenNewFileInZip);
        bool file_open = true;
        ON_SCOPE_EXIT({
            if (file_open && zfile) {
                zipCloseFileInZip(zfile);
            }
        });

        if (file_info.size > 0) {
            s64 remaining = file_info.size;
            s64 offset = 0;
            while (remaining > 0) {
                if (pbox) {
                    R_TRY(pbox->ShouldExitResult());
                }
                const u64 chunk_to_read = std::min<u64>(remaining, buffer.size());
                u64 chunk_read_accum = 0;
                while (chunk_read_accum < chunk_to_read) {
                    if (pbox) {
                        R_TRY(pbox->ShouldExitResult());
                    }
                    u64 read_once = 0;
                    R_TRY(src_file.Read(offset + chunk_read_accum, buffer.data() + chunk_read_accum, chunk_to_read - chunk_read_accum, FsReadOption_None, &read_once));
                    if (read_once == 0 || read_once > (chunk_to_read - chunk_read_accum)) {
                        return FsError_InvalidSize;
                    }
                    chunk_read_accum += read_once;
                }

                const int write_rc = zipWriteInFileInZip(zfile, buffer.data(), chunk_read_accum);
                R_UNLESS(write_rc == ZIP_OK, Result_ZipWriteInFileInZip);
                R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);

                offset += chunk_read_accum;
                remaining -= chunk_read_accum;
            }
        }

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        s64 final_size = 0;
        R_TRY(src_file.GetSize(&final_size));
        R_UNLESS(final_size == file_info.size, FsError_TargetLocked);

        u8 trailing_buf[1]{};
        u64 trailing_read = 0;
        const auto trailing_rc = src_file.Read(file_info.size, trailing_buf, sizeof(trailing_buf), FsReadOption_None, &trailing_read);
        R_TRY(trailing_rc);
        R_UNLESS(trailing_read == 0, FsError_InvalidSize);

        src_file.Close();
        file_open = false;
        R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
        R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
    }

    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    zfile_open = false;
    const int close_rc = zipClose(zfile, NULL);
    R_UNLESS(close_rc == ZIP_OK, Result_ZipWriteInFileInZip);
    R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
    R_UNLESS(!rec_ctx.flush_failed, Result_FsUnknownStdioError);
    R_UNLESS(!rec_ctx.sync_failed, Result_FsUnknownStdioError);
    R_UNLESS(!rec_ctx.close_failed, Result_FsUnknownStdioError);
    R_UNLESS(rec_ctx.fp == nullptr, Result_FsUnknownStdioError);

    const auto sdmc_rc = fsdevCommitDevice("sdmc");
    R_TRY(sdmc_rc);

    const auto sd_commit_rc = sd_fs.Commit();
    if (R_FAILED(sd_commit_rc)) {
        return sd_commit_rc;
    }

    R_SUCCEED();
}

} // namespace

Result RestoreSaveFolder(ProgressBox* pbox, const Entry& e, const fs::FsPath& folder_path, fs::FsPath* out_recovery_path, bool* out_mutation_started) {
    if (out_mutation_started) {
        *out_mutation_started = false;
    }
    if (haze::IsRunning()) {
        return FsError_TargetLocked;
    }
    R_UNLESS(!e.is_backup && e.save_data_id != 0, FsError_PathNotFound);

    // 1. Length bounds check with strnlen
    const size_t raw_len = strnlen(folder_path.s, sizeof(folder_path.s));
    if (raw_len == 0 || raw_len >= sizeof(folder_path.s)) {
        return FsError_TooLongPath;
    }

    const std::string_view raw_view{folder_path.s, raw_len};

    // 2. Must be absolute SD path starting with '/' and no mount prefixes (colons)
    if (raw_view.front() != '/' || raw_view.find(':') != std::string_view::npos) {
        return FsError_InvalidCharacter;
    }

    // 3. Reject duplicate slashes (//) anywhere in path
    if (raw_view.find("//") != std::string_view::npos) {
        return FsError_InvalidCharacter;
    }

    // 4. Character validation: no control chars, no 0x7F, no backslashes, no invalid save characters
    for (const char c : raw_view) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\' || IsInvalidSavePathChar(c)) {
            return FsError_InvalidCharacter;
        }
    }

    // 5. Canonical browser-compatible trailing-slash handling:
    // Single trailing slash on non-root paths is trimmed to canonical form without altering authority.
    std::string_view canonical_view = raw_view;
    if (canonical_view.size() > 1 && canonical_view.back() == '/') {
        canonical_view.remove_suffix(1);
    }

    if (canonical_view == "/" || canonical_view.empty()) {
        return FsError_PathNotFound;
    }

    // 6. Validate every component in canonical path
    {
        size_t start = 1;
        while (start < canonical_view.size()) {
            size_t end = canonical_view.find('/', start);
            if (end == std::string_view::npos) {
                end = canonical_view.size();
            }
            const std::string_view comp = canonical_view.substr(start, end - start);
            if (comp.empty() || comp == "." || comp == "..") {
                return FsError_InvalidCharacter;
            }
            start = end + 1;
        }
    }

    // 7. Reject SD root and staging ancestry / equality / descendant on canonical path BEFORE creating any directories
    if (IsStagingParentOrRelated(canonical_view)) {
        return FsError_PathAlreadyExists;
    }

    fs::FsPath canonical_folder_path;
    if (canonical_view.size() >= sizeof(canonical_folder_path.s)) {
        return FsError_TooLongPath;
    }
    std::memcpy(canonical_folder_path.s, canonical_view.data(), canonical_view.size());
    canonical_folder_path.s[canonical_view.size()] = '\0';

    // Independent FsNativeSd handle
    fs::FsNativeSd sd_fs;
    R_TRY(sd_fs.GetFsOpenResult());

    // Verify source exists and is a directory
    FsDirEntryType src_type{};
    R_TRY(sd_fs.GetEntryType(canonical_folder_path, &src_type));
    R_UNLESS(src_type == FsDirEntryType_Dir, FsError_PathNotFound);

    // Iterative worklist scan to checked EOF
    std::map<std::string, FolderSourceEntry> source_files;
    std::set<std::string> source_dirs;
    s64 total_source_file_bytes = 0;

    std::vector<std::string> dir_worklist;
    dir_worklist.push_back("");

    while (!dir_worklist.empty()) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        const std::string rel_dir = std::move(dir_worklist.back());
        dir_worklist.pop_back();

        fs::FsPath cur_full_dir;
        if (rel_dir.empty()) {
            cur_full_dir = canonical_folder_path;
        } else {
            R_TRY(CheckedJoinPath(cur_full_dir, canonical_folder_path.s, rel_dir));
        }

        fs::Dir dir;
        R_TRY(sd_fs.OpenDirectory(cur_full_dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir));

        constexpr size_t BATCH_SIZE = 32;
        FsDirectoryEntry batch[BATCH_SIZE];
        s64 read_count = 0;

        while (true) {
            if (pbox) {
                R_TRY(pbox->ShouldExitResult());
            }

            R_TRY(dir.Read(&read_count, BATCH_SIZE, batch));
            if (read_count == 0) {
                break;
            }
            if (read_count < 0 || static_cast<size_t>(read_count) > BATCH_SIZE) {
                return FsError_InvalidSize;
            }

            for (s64 i = 0; i < read_count; i++) {
                const auto& de = batch[i];
                const auto name_len = strnlen(de.name, sizeof(de.name));
                if (name_len == 0 || name_len >= sizeof(de.name)) {
                    return FsError_InvalidCharacter;
                }

                if (!std::strcmp(de.name, ".") || !std::strcmp(de.name, "..")) {
                    continue;
                }

                const std::string_view name_view{de.name, name_len};
                for (size_t c_idx = 0; c_idx < name_len; c_idx++) {
                    const char c = de.name[c_idx];
                    const auto uc = static_cast<unsigned char>(c);
                    if (uc < 0x20 || uc == 0x7F || c == '/' || c == '\\' || c == ':' || IsInvalidSavePathChar(c)) {
                        return FsError_InvalidCharacter;
                    }
                }

                if (de.type != FsDirEntryType_Dir && de.type != FsDirEntryType_File) {
                    return FsError_InvalidCharacter;
                }

                const size_t rel_entry_len = (rel_dir.empty() ? 0 : (rel_dir.size() + 1)) + name_len;
                if (de.type == FsDirEntryType_Dir) {
                    if (rel_entry_len + 1 >= sizeof(fs::FsPath)) {
                        return FsError_TooLongPath;
                    }
                } else {
                    if (rel_entry_len >= sizeof(fs::FsPath)) {
                        return FsError_TooLongPath;
                    }
                }

                const std::string rel_entry = rel_dir.empty() ? std::string(name_view) : (rel_dir + "/" + std::string(name_view));

                fs::FsPath full_entry_path;
                R_TRY(CheckedJoinPath(full_entry_path, cur_full_dir.s, name_view));

                if (de.type == FsDirEntryType_Dir) {
                    if (rel_dir.empty() && IsSaveReservedMetadataRoot(de.name)) {
                        return FsError_PathAlreadyExists;
                    }

                    const std::string dir_key = rel_entry + "/";
                    if (source_files.contains(rel_entry) || source_dirs.contains(dir_key)) {
                        return FsError_PathAlreadyExists;
                    }

                    size_t slash_pos = rel_entry.find('/');
                    while (slash_pos != std::string::npos) {
                        const std::string parent = rel_entry.substr(0, slash_pos);
                        if (source_files.contains(parent)) {
                            return FsError_PathAlreadyExists;
                        }
                        slash_pos = rel_entry.find('/', slash_pos + 1);
                    }

                    source_dirs.insert(dir_key);
                    dir_worklist.push_back(rel_entry);
                } else {
                    if (de.file_size < 0) {
                        return FsError_InvalidSize;
                    }
                    if (std::numeric_limits<s64>::max() - total_source_file_bytes < de.file_size) {
                        return FsError_InvalidSize;
                    }

                    if (source_files.contains(rel_entry) || source_dirs.contains(rel_entry + "/")) {
                        return FsError_PathAlreadyExists;
                    }

                    size_t slash_pos = rel_entry.find('/');
                    while (slash_pos != std::string::npos) {
                        const std::string parent = rel_entry.substr(0, slash_pos);
                        if (source_files.contains(parent)) {
                            return FsError_PathAlreadyExists;
                        }
                        slash_pos = rel_entry.find('/', slash_pos + 1);
                    }

                    source_files.emplace(rel_entry, FolderSourceEntry{full_entry_path, de.file_size});
                    total_source_file_bytes += de.file_size;
                }
            }
        }
    }

    R_UNLESS(source_files.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);
    R_UNLESS(source_dirs.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);

    // Ensure staging parent directory exists
    const auto stage_parent_rc = sd_fs.CreateDirectoryRecursively("/dumps/save-import");
    if (R_FAILED(stage_parent_rc) && stage_parent_rc != FsError_PathAlreadyExists) {
        return stage_parent_rc;
    }

    // Reserve owned directory with checked native primitive and explicit commit
    const auto now = std::time(nullptr);
    const auto tm = *std::localtime(&now);
    fs::FsPath owned_dir;
    bool owned_dir_created = false;
    for (u32 counter = 0; counter < 1000; counter++) {
        R_TRY(pbox->ShouldExitResult());
        char dir_buf[sizeof(fs::FsPath)];
        const int n = std::snprintf(dir_buf, sizeof(dir_buf), "/dumps/save-import/%04d%02d%02d_%02d%02d%02d_%016lX_%03u",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            e.save_data_id, counter);
        R_UNLESS(n > 0 && static_cast<size_t>(n) < sizeof(dir_buf), FsError_TooLongPath);

        const auto prim_rc = fsFsCreateDirectory(&sd_fs.m_fs, dir_buf);
        if (R_SUCCEEDED(prim_rc)) {
            owned_dir = dir_buf;
            owned_dir_created = true;
            const auto commit_rc = sd_fs.Commit();
            if (R_FAILED(commit_rc)) {
                sd_fs.DeleteDirectory(owned_dir);
                return commit_rc;
            }
            break;
        }
        if (prim_rc != FsError_PathAlreadyExists) {
            return prim_rc;
        }
    }
    R_UNLESS(owned_dir_created, FsError_PathAlreadyExists);

    fs::FsPath owned_stage_zip;
    R_TRY(CheckedJoinPath(owned_stage_zip, owned_dir.s, "source.zip.temp"));

    bool stage_published = false;
    ON_SCOPE_EXIT({
        if (!stage_published) {
            if (!owned_stage_zip.empty()) {
                sd_fs.DeleteFile(owned_stage_zip);
            }
            if (owned_dir_created && !owned_dir.empty()) {
                sd_fs.DeleteDirectory(owned_dir);
            }
        }
    });

    // Stage source folder to temporary zip archive
    pbox->NewTransfer("Staging save backup..."_i18n);
    R_TRY(pbox->ShouldExitResult());
    R_TRY(StageBackupFolderToZip(pbox, sd_fs, owned_stage_zip, source_files, source_dirs));

    // Reopen finalized stage with SaveReaderContext for preflight and admission
    SaveReaderContext stage_reader_ctx;
    zlib_filefunc64_def stage_file_func;
    stage_reader_ctx.InitFileFunc(&stage_file_func);
    auto stage_zfile = unzOpen2_64(owned_stage_zip.s, &stage_file_func);
    R_UNLESS(stage_zfile, Result_UnzOpen2_64);
    bool stage_reader_open = true;
    ON_SCOPE_EXIT({
        if (stage_reader_open && stage_zfile) {
            unzClose(stage_zfile);
        }
    });

    const auto save_filter = [](const fs::FsPath& name, fs::FsPath& /*path*/) -> bool {
        if (IsSaveReservedMetadataRoot(name.s)) {
            return false;
        }
        return true;
    };

    thread::UnzipPayloadSummary stage_summary{};
    thread::UnzipPayloadInventory stage_inventory{};
    pbox->NewTransfer("Validating staged save..."_i18n);
    R_TRY(thread::TransferUnzipPreflight(pbox, stage_zfile, "/", save_filter, true, &stage_summary, &stage_inventory, true));

    DecodedSaveMetadata stage_meta{};
    Result stage_meta_rc = 0;
    const auto stage_meta_status = ReadArchiveSaveMetadata(stage_zfile, pbox, stage_meta, &stage_meta_rc);
    if (stage_meta_status == ArchiveMetaStatus::Invalid) {
        R_THROW(R_FAILED(stage_meta_rc) ? stage_meta_rc : Result_UnzOpen2_64);
    }

    // Compare exact staged payload inventory/sizes/dirs to source inventory with ONLY shared reserved ROOT metadata filtering
    size_t expected_payload_file_count = 0;
    for (const auto& [file_key, file_info] : source_files) {
        if (IsSaveReservedMetadataRoot(file_key)) {
            continue;
        }
        expected_payload_file_count++;
        const std::string staged_key = "/" + file_key;
        auto it = stage_inventory.files.find(staged_key);
        R_UNLESS(it != stage_inventory.files.end(), FsError_PathNotFound);
        R_UNLESS(it->second == file_info.size, FsError_InvalidSize);
    }
    R_UNLESS(stage_inventory.files.size() == expected_payload_file_count, FsError_PathNotFound);

    for (const auto& dir_key : source_dirs) {
        std::string dir_no_slash = dir_key;
        if (dir_no_slash.ends_with('/')) {
            dir_no_slash.pop_back();
        }
        const std::string staged_dir_key = "/" + dir_no_slash;
        R_UNLESS(stage_inventory.directories.contains(staged_dir_key), FsError_PathNotFound);
    }
    R_UNLESS(static_cast<s64>(source_dirs.size()) == stage_summary.directory_count, FsError_PathNotFound);

    stage_reader_open = false;
    R_UNLESS(UNZ_OK == unzClose(stage_zfile), Result_UnzOpen2_64);
    R_UNLESS(!stage_reader_ctx.HasError(), Result_UnzOpen2_64);

    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    // Invoke shared RestoreSaveZip for sole destructive restore ownership
    const auto restore_rc = RestoreSaveZip(pbox, e, owned_stage_zip, out_recovery_path, out_mutation_started, true);

    // Exact owned stage cleanup
    const auto del_file_rc = sd_fs.DeleteFile(owned_stage_zip);
    const auto del_dir_rc = sd_fs.DeleteDirectory(owned_dir);
    stage_published = true;

    if (R_FAILED(restore_rc)) {
        return restore_rc;
    }
    if (R_FAILED(del_file_rc)) {
        return del_file_rc;
    }
    if (R_FAILED(del_dir_rc)) {
        return del_dir_rc;
    }

    R_SUCCEED();
}

} // namespace sphaira::ui::menu::save
