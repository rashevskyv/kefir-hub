#include "save_restore_zip.hpp"
#include "ui/menus/save_menu.hpp"
#include "save_backup_writer.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "haze_helper.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
#include "ui/menus/filebrowser.hpp"
#include <utility>
#include <cstring>
#include <algorithm>
#include <set>
#include <map>
#include <limits>
#include <ctime>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::ui::menu::save {

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty, bool* out_created_slot_retained);

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool* out_created_slot_retained) {
    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, false, out_created_slot_retained);
}

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started) {
    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, false, nullptr);
}

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty) {
    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, allow_empty, nullptr);
}

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty, bool* out_created_slot_retained) {
    if (out_mutation_started) {
        *out_mutation_started = false;
    }
    if (out_created_slot_retained) {
        *out_created_slot_retained = false;
    }
    if (haze::IsRunning()) {
        return FsError_TargetLocked;
    }
    R_UNLESS(!e.is_backup, FsError_PathNotFound);
    R_UNLESS(e.save_data_id != 0 || e.is_planned_create, FsError_PathNotFound);

    SaveReaderContext source_reader_ctx;
    zlib_filefunc64_def file_func;
    source_reader_ctx.InitFileFunc(&file_func);

    auto zfile = unzOpen2_64(path, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    bool source_reader_open = true;
    ON_SCOPE_EXIT({
        if (source_reader_open && zfile) {
            unzClose(zfile);
        }
    });
    log_write("opened zip\n");

    const auto save_filter = [](const fs::FsPath& name, fs::FsPath& /*path*/) -> bool {
        // skip restoring the reserved meta files (sphaira and dbi).
        if (IsSaveReservedMetadataRoot(name.s)) {
            log_write("skipping meta\n");
            return false;
        }

        // restore everything else.
        return true;
    };

    thread::UnzipPayloadSummary summary{};
    thread::UnzipPayloadInventory source_inventory{};
    pbox->NewTransfer("Validating save..."_i18n);
    R_TRY(thread::TransferUnzipPreflight(pbox, zfile, "/", save_filter, true, &summary, &source_inventory, allow_empty));
    log_write("save preflight payload: %lld bytes, %lld files, %lld dirs\n",
        static_cast<long long>(summary.file_bytes),
        static_cast<long long>(summary.file_count),
        static_cast<long long>(summary.directory_count));

    DecodedSaveMetadata archive_meta{};
    Result meta_rc = 0;
    const auto meta_status = ReadArchiveSaveMetadata(zfile, pbox, archive_meta, &meta_rc);
    if (meta_status == ArchiveMetaStatus::Invalid) {
        R_THROW(R_FAILED(meta_rc) ? meta_rc : Result_UnzOpen2_64);
    }

    Entry target_entry = e;
    bool was_newly_created = false;
    if (e.is_planned_create) {
        R_TRY(pbox->ShouldExitResult());
        const auto create_res = CreateSaveDataChecked(e.creation_request, [pbox]() {
            return pbox && R_FAILED(pbox->ShouldExitResult());
        });
        if (create_res.create_succeeded) {
            if (out_mutation_started) *out_mutation_started = true;
            if (out_created_slot_retained) *out_created_slot_retained = true;
        }
        if (!create_res.verified) {
            return R_FAILED(create_res.rc) ? create_res.rc : FsError_PathNotFound;
        }
        was_newly_created = true;
        static_cast<FsSaveDataInfo&>(target_entry) = create_res.verified_info;
        target_entry.is_planned_create = false;
    }

    if (target_entry.save_data_id == 0) {
        return FsError_PathNotFound;
    }

    R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);

    FsSaveDataAttribute attr{};
    attr.application_id = target_entry.application_id;
    attr.uid = target_entry.uid;
    attr.system_save_data_id = target_entry.system_save_data_id;
    attr.save_data_type = target_entry.save_data_type;
    attr.save_data_rank = target_entry.save_data_rank;
    attr.save_data_index = target_entry.save_data_index;

    const auto save_data_space_id = static_cast<FsSaveDataSpaceId>(target_entry.save_data_space_id);
    const u64 target_save_data_id = target_entry.save_data_id;

    // Check if save filesystem already exists
    bool save_exists = false;
    {
        fs::FsNativeSave check_save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, false};
        const auto check_rc = check_save_fs.GetFsOpenResult();
        if (target_entry.save_data_id != 0) {
            R_TRY(check_rc);
            save_exists = true;
        } else {
            save_exists = R_SUCCEEDED(check_rc);
        }
    }
    R_UNLESS(save_exists, FsError_PathNotFound);

    FsSaveDataExtraData live{};
    R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&live, sizeof(live), save_data_space_id, target_save_data_id));

    if (live.attr.application_id != attr.application_id ||
        live.attr.uid.uid[0] != attr.uid.uid[0] ||
        live.attr.uid.uid[1] != attr.uid.uid[1] ||
        live.attr.system_save_data_id != attr.system_save_data_id ||
        live.attr.save_data_type != attr.save_data_type ||
        live.attr.save_data_rank != attr.save_data_rank ||
        live.attr.save_data_index != attr.save_data_index) {
        return FsError_PathNotFound;
    }

    if (live.data_size <= 0 || live.journal_size < 0) {
        return FsError_InvalidSize;
    }

    // ponytail: payload bytes are a rejection lower bound; implicit
    // directories, allocation, and journal overhead are not accounted for;
    // verified sizing/growth remains queued.
    if (summary.file_bytes > live.data_size) {
        return FsError_InvalidSize;
    }

    // Lexical RW scope: open the save file system for writing
    {
        fs::FsNativeSave save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, false};
        R_TRY(save_fs.GetFsOpenResult());

        filebrowser::FsDirCollections collections;
        if (target_entry.save_data_id != 0 && !was_newly_created) {
            filebrowser::FsDirCollections live_collections;
            R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", live_collections, true));

            // Validate live entries against reserved metadata and invalid characters
            for (const auto& col : live_collections) {
                if (col.path != "/") {
                    for (const char c : std::string_view{col.path.s}) {
                        if (IsInvalidSavePathChar(c)) {
                            return FsError_InvalidCharacter;
                        }
                    }
                }
                for (const auto& f : col.files) {
                    if (col.path == "/") {
                        if (!strcasecmp(f.name, NX_SAVE_META_NAME) ||
                            !strcasecmp(f.name, DBI_SAVE_INFO_NAME) ||
                            !strcasecmp(f.name, DBI_SAVE_EXTRA_NAME)) {
                            return FsError_PathAlreadyExists;
                        }
                    }
                    for (const char c : std::string_view{f.name}) {
                        if (IsInvalidSavePathChar(c)) {
                            return FsError_InvalidCharacter;
                        }
                    }
                }
            }

            // Build live inventory maps for bijection verification
            struct LiveFileInfo {
                fs::FsPath full_fs_path;
                s64 size = 0;
                bool verified = false;
            };
            std::map<std::string, LiveFileInfo> live_files;
            std::map<std::string, bool> live_dirs;
            s64 live_bytes_sum = 0;

            for (const auto& col : live_collections) {
                if (col.path != "/") {
                    const char* r = col.path.s;
                    while (*r == '/') r++;
                    if (*r != '\0') {
                        std::string dir_key{r};
                        if (!dir_key.ends_with('/')) dir_key += '/';
                        auto [dit, dinserted] = live_dirs.try_emplace(dir_key, false);
                        R_UNLESS(dinserted, FsError_PathAlreadyExists);
                    }
                }
                for (const auto& lf : col.files) {
                    R_UNLESS(lf.file_size >= 0, FsError_InvalidSize);
                    R_UNLESS(std::numeric_limits<s64>::max() - live_bytes_sum >= lf.file_size, FsError_InvalidSize);
                    fs::FsPath full_path = fs::AppendPath(col.path, lf.name);
                    const char* rf = full_path.s;
                    while (*rf == '/') rf++;
                    auto [fit, finserted] = live_files.try_emplace(std::string{rf}, LiveFileInfo{full_path, lf.file_size, false});
                    R_UNLESS(finserted, FsError_PathAlreadyExists);
                    live_bytes_sum += lf.file_size;
                }
            }

            R_UNLESS(live_dirs.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);
            R_UNLESS(live_files.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);

            // Collision-safe directory reservation on SD
            fs::FsNativeSd sd_fs;
            R_TRY(sd_fs.GetFsOpenResult());
            const auto rec_parent_rc = sd_fs.CreateDirectoryRecursively("/dumps/recovery");
            if (R_FAILED(rec_parent_rc) && rec_parent_rc != FsError_PathAlreadyExists) {
                return rec_parent_rc;
            }

            const auto now = std::time(nullptr);
            const auto tm = *std::localtime(&now);
            fs::FsPath owned_dir;
            bool reserved = false;
            for (u32 counter = 0; counter < 1000; counter++) {
                R_TRY(pbox->ShouldExitResult());
                char dir_buf[sizeof(fs::FsPath)];
                const int n = std::snprintf(dir_buf, sizeof(dir_buf), "/dumps/recovery/%04d%02d%02d_%02d%02d%02d_%016lX_%03u",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    e.save_data_id, counter);
                R_UNLESS(n > 0 && static_cast<size_t>(n) < sizeof(dir_buf), FsError_TooLongPath);

                const auto create_rc = sd_fs.CreateDirectory(dir_buf);
                if (R_SUCCEEDED(create_rc)) {
                    owned_dir = dir_buf;
                    reserved = true;
                    break;
                }
                if (create_rc != FsError_PathAlreadyExists) {
                    return create_rc;
                }
            }
            R_UNLESS(reserved, FsError_PathAlreadyExists);

            const fs::FsPath recovery_temp_path = owned_dir + "/recovery.zip.temp";
            const fs::FsPath recovery_final_path = owned_dir + "/recovery.zip";

            enum class RecoveryPubState {
                Unpublished,
                Renamed,
                Published
            };
            RecoveryPubState pub_state = RecoveryPubState::Unpublished;

            ON_SCOPE_EXIT({
                if (pub_state == RecoveryPubState::Unpublished) {
                    sd_fs.DeleteFile(recovery_temp_path);
                    sd_fs.DeleteDirectory(owned_dir);
                } else if (pub_state == RecoveryPubState::Renamed) {
                    sd_fs.DeleteFile(recovery_final_path);
                    sd_fs.DeleteDirectory(owned_dir);
                }
            });

            pbox->NewTransfer("Creating recovery backup..."_i18n);
            R_TRY(pbox->ShouldExitResult());
            R_TRY(WriteSaveBackupZip(pbox, &sd_fs, recovery_temp_path, &save_fs, e, live, live_collections, {}, false, true, true));

            // Reopen recovery backup and preflight
            SaveReaderContext rec_reader_ctx;
            zlib_filefunc64_def rec_file_func;
            rec_reader_ctx.InitFileFunc(&rec_file_func);
            auto rec_zfile = unzOpen2_64(recovery_temp_path, &rec_file_func);
            R_UNLESS(rec_zfile, Result_UnzOpen2_64);
            bool rec_reader_open = true;
            ON_SCOPE_EXIT({
                if (rec_reader_open && rec_zfile) {
                    unzClose(rec_zfile);
                }
            });

            thread::UnzipPayloadSummary rec_summary{};
            thread::UnzipPayloadInventory rec_inventory{};
            pbox->NewTransfer("Validating recovery backup..."_i18n);
            R_TRY(thread::TransferUnzipPreflight(pbox, rec_zfile, "/", save_filter, true, &rec_summary, &rec_inventory, true));

            const s64 live_dir_count = static_cast<s64>(live_dirs.size());
            const s64 live_file_count = static_cast<s64>(live_files.size());
            R_UNLESS(rec_summary.directory_count == live_dir_count, FsError_PathNotFound);
            R_UNLESS(rec_summary.file_count == live_file_count, FsError_PathNotFound);
            R_UNLESS(rec_summary.file_bytes == live_bytes_sum, FsError_InvalidSize);

            // Verified recovery archive check against native held mount
            R_TRY(thread::VerifyArchiveAgainstNative(pbox, rec_zfile, &save_fs, "/", rec_inventory, save_filter, true));

            DecodedSaveMetadata rec_meta{};
            Result rec_meta_rc = 0;
            const auto rec_meta_status = ReadArchiveSaveMetadata(rec_zfile, pbox, rec_meta, &rec_meta_rc);
            R_UNLESS(rec_meta_status == ArchiveMetaStatus::Valid, R_FAILED(rec_meta_rc) ? rec_meta_rc : Result_UnzOpen2_64);

            // Explicit checked close of candidate recovery reader BEFORE re-enumeration/publication
            rec_reader_open = false;
            R_UNLESS(UNZ_OK == unzClose(rec_zfile), Result_UnzOpen2_64);
            R_UNLESS(!rec_reader_ctx.HasError(), Result_UnzOpen2_64);

            // Re-enumerate live inventory from save_fs immediately before clear
            filebrowser::FsDirCollections current_collections;
            R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", current_collections, true));

            std::map<std::string, s64> curr_files;
            std::set<std::string> curr_dirs;
            for (const auto& col : current_collections) {
                if (col.path != "/") {
                    const char* r = col.path.s;
                    while (*r == '/') r++;
                    if (*r != '\0') {
                        std::string dir_key{r};
                        if (!dir_key.ends_with('/')) dir_key += '/';
                        auto [dit, dinserted] = curr_dirs.insert(dir_key);
                        R_UNLESS(dinserted, FsError_TargetLocked);
                    }
                }
                for (const auto& cf : col.files) {
                    R_UNLESS(cf.file_size >= 0, FsError_TargetLocked);
                    fs::FsPath c_full = fs::AppendPath(col.path, cf.name);
                    const char* rf = c_full.s;
                    while (*rf == '/') rf++;
                    auto [fit, finserted] = curr_files.try_emplace(std::string{rf}, cf.file_size);
                    R_UNLESS(finserted, FsError_TargetLocked);
                }
            }

            R_UNLESS(curr_dirs.size() == live_dirs.size(), FsError_TargetLocked);
            for (const auto& [d, _] : live_dirs) {
                R_UNLESS(curr_dirs.contains(d), FsError_TargetLocked);
            }
            R_UNLESS(curr_files.size() == live_files.size(), FsError_TargetLocked);
            for (const auto& [f, info] : live_files) {
                auto it = curr_files.find(f);
                R_UNLESS(it != curr_files.end(), FsError_TargetLocked);
                R_UNLESS(it->second == info.size, FsError_TargetLocked);
            }

            // Publish validated recovery archive using native primitive to separate rename from commit
            const auto rename_rc = fsFsRenameFile(&sd_fs.m_fs, recovery_temp_path, recovery_final_path);
            R_TRY(rename_rc);
            pub_state = RecoveryPubState::Renamed;

            const auto sdmc_rc = fsdevCommitDevice("sdmc");
            if (R_FAILED(sdmc_rc)) {
                return sdmc_rc;
            }

            const auto sd_commit_rc = sd_fs.Commit();
            if (R_FAILED(sd_commit_rc)) {
                return sd_commit_rc;
            }

            pub_state = RecoveryPubState::Published;
            if (out_recovery_path) {
                *out_recovery_path = recovery_final_path;
            }

            collections = std::move(current_collections);
        } else if (!was_newly_created) {
            R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", collections));
        }

        R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);
        R_TRY(pbox->ShouldExitResult());

        // Conservative mutation flag: mark mutation started immediately before clear
        if (out_mutation_started) {
            *out_mutation_started = true;
        }

        // ponytail: held mount and byte comparison verify correspondence at check time, but do not provide snapshot isolation against concurrent external mutation; true atomic save transactions require filesystem-level snapshot support.
        if (!was_newly_created) {
            R_TRY(filebrowser::FsView::DeleteAllCollections(pbox, &save_fs, collections));
            R_TRY(save_fs.Commit());
        }

        log_write("opened save file\n");
        // restore save data from zip.
        pbox->NewTransfer("Restoring save..."_i18n);
        const s64 target_journal_size = live.journal_size;
        if (!allow_empty || !source_inventory.files.empty() || !source_inventory.directories.empty()) {
            R_TRY(thread::TransferUnzipAll(pbox, zfile, &save_fs, "/", save_filter, thread::Mode::SingleThreadedIfSmaller, true, true, target_journal_size));
        }

        R_TRY(save_fs.Commit());
        log_write("finished save restore commit\n");
    } // End lexical RW scope: save_fs is destroyed here!

    // Reread live extra data to verify save identity after restore
    if (target_entry.save_data_id != 0) {
        FsSaveDataExtraData post_live{};
        R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&post_live, sizeof(post_live), save_data_space_id, target_save_data_id));
        if (post_live.attr.application_id != attr.application_id ||
            post_live.attr.uid.uid[0] != attr.uid.uid[0] ||
            post_live.attr.uid.uid[1] != attr.uid.uid[1] ||
            post_live.attr.system_save_data_id != attr.system_save_data_id ||
            post_live.attr.save_data_type != attr.save_data_type ||
            post_live.attr.save_data_rank != attr.save_data_rank ||
            post_live.attr.save_data_index != attr.save_data_index) {
            return FsError_PathNotFound;
        }
        if (post_live.data_size <= 0 || post_live.journal_size < 0) {
            return FsError_InvalidSize;
        }
    }

    // Open fresh read-only mount to verify restored save contents
    {
        pbox->NewTransfer("Verifying restored save..."_i18n);
        fs::FsNativeSave ro_save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, true};
        R_TRY(ro_save_fs.GetFsOpenResult());
        if (allow_empty) {
            R_TRY(thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, "/", source_inventory, save_filter, true, allow_empty));
        } else {
            R_TRY(thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, "/", source_inventory, save_filter, true));
        }
    }

    // Explicit checked close of source reader
    source_reader_open = false;
    R_UNLESS(UNZ_OK == unzClose(zfile), Result_UnzOpen2_64);
    R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);

    log_write("finished save restore\n");
    R_SUCCEED();
}

} // namespace sphaira::ui::menu::save
