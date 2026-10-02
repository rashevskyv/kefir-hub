#include "ui/menus/save_menu.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "haze_helper.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/error_box.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_folder_discovery.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cstring>
#include <functional>

namespace sphaira::ui::menu::save {

void Menu::RestoreSaves(std::vector<Entry> entries) {
    RestoreSaves(std::move(entries), MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT);
}

void Menu::RestoreSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    RestoreSaves(entries, entries, location, backup_root);
}

void Menu::RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    if (sources.size() != targets.size()) {
        App::Push<OptionBox>("Source and target count mismatch."_i18n, "OK"_i18n);
        return;
    }

    if (sources.empty()) {
        App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
        return;
    }

    for (const auto& dst : targets) {
        if (dst.is_backup || (dst.save_data_id == 0 && !dst.is_planned_create)) {
            App::Push<OptionBox>("Cannot restore directly to backup or invalid save target."_i18n, "OK"_i18n);
            return;
        }
    }

    for (const auto& src : sources) {
        if (src.backup_members.empty()) {
            App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
            return;
        }
    }

    if (!haze::ReleaseSaveMounts()) { // only while the PC has a save open over MTP
        App::Push<OptionBox>("MTP is currently active. Please close the running game and disable MTP before restoring save data."_i18n, "OK"_i18n);
        return;
    }

    auto src_ptr = std::make_shared<std::vector<Entry>>(std::move(sources));
    auto dst_ptr = std::make_shared<std::vector<Entry>>(std::move(targets));

    ShowRestoreConfirmPage(src_ptr, dst_ptr, location, backup_root, 0, src_ptr->size());
}

void Menu::ShowRestoreConfirmPage(
    std::shared_ptr<std::vector<Entry>> sources,
    std::shared_ptr<std::vector<Entry>> targets,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root,
    size_t page,
    size_t num_pages) {

    const auto& src = (*sources)[page];
    const auto& dst = (*targets)[page];
    const auto accounts = App::GetAccountList();

    const auto& newest = src.backup_members.front();
    std::string type_str = GetSaveTypeLabel(src.save_data_type);
    std::string source_str = GetBackupSourceLabel(src.backup_source);
    std::string date_str = FormatBackupTimestamp(newest.ts, false);
    std::string acc_str;
    if (src.save_data_type == FsSaveDataType_Account) {
        acc_str = this->GetAccountName(dst.uid);
        if (acc_str.empty()) acc_str = FormatBackupAccount(src, accounts);
    }
    const char* slash = std::strrchr(newest.path.s, '/');
    const std::string filename = slash ? (slash + 1) : newest.path.s;

    std::string dest_str;
    if (dst.is_planned_create) {
        dest_str = "New slot ("_i18n + acc_str + ")";
    } else if (dst.save_data_type == FsSaveDataType_Account) {
        dest_str = "Account: "_i18n + acc_str;
        if (dst.save_data_index != 0) {
            dest_str += " [slot "_i18n + std::to_string(dst.save_data_index) + "]";
        }
    } else {
        dest_str = GetSaveTypeLabel(dst.save_data_type);
    }

    std::string prompt;
    if (num_pages > 1) {
        prompt = "Restore selected saves?"_i18n + "\n\n";
        prompt += "Item "_i18n + std::to_string(page + 1) + " of "_i18n + std::to_string(num_pages) + "\n\n";
    } else {
        prompt = "Restore selected save?"_i18n + "\n\n";
    }

    prompt += "• [" + type_str + "] ";
    if (!acc_str.empty()) {
        prompt += acc_str;
        if (src.save_data_index != 0) {
            prompt += " (slot "_i18n + std::to_string(src.save_data_index) + ")";
        }
        prompt += " • ";
    }
    prompt += source_str + " (" + date_str + ")\n";
    prompt += "  " + "Archive: "_i18n + filename + "\n";
    prompt += "  " + "Target: "_i18n + dest_str + "\n\n";

    if (num_pages > 1) {
        prompt += "Restores are performed individually (not atomic: later items may fail if an error occurs).\n"_i18n;
    }
    prompt += "A safety recovery backup will be created on SD before overwriting.\nPlease close the running game and disable MTP."_i18n;

    if (num_pages == 1) {
        App::Push<OptionBox>(prompt, "No"_i18n, "Yes"_i18n, [this, sources, targets, location, backup_root](auto choice) {
            if (choice && *choice == 1) {
                ExecuteRestore(sources, targets, location, backup_root);
            }
        });
        return;
    }

    const bool is_first = (page == 0);
    const bool is_last = (page + 1 == num_pages);

    std::string btn_left = is_first ? "Cancel"_i18n : "Back"_i18n;
    std::string btn_right = is_last ? "Restore"_i18n : "Next"_i18n;

    App::Push<OptionBox>(prompt, btn_left, btn_right, [this, sources, targets, location, backup_root, page, num_pages, is_first, is_last](auto choice) {
        if (!choice) return;
        if (*choice == 1) {
            if (is_last) {
                ExecuteRestore(sources, targets, location, backup_root);
            } else {
                ShowRestoreConfirmPage(sources, targets, location, backup_root, page + 1, num_pages);
            }
        } else if (*choice == 0) {
            if (!is_first) {
                ShowRestoreConfirmPage(sources, targets, location, backup_root, page - 1, num_pages);
            }
        }
    });
}

void Menu::ExecuteRestore(
    std::shared_ptr<std::vector<Entry>> sources,
    std::shared_ptr<std::vector<Entry>> targets,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root) {

    App::PopToMenu();
    auto restored = std::make_shared<size_t>(0);
    auto skipped = std::make_shared<size_t>(0);
    auto recovery_paths = std::make_shared<std::vector<fs::FsPath>>();
    auto last_mutation_started = std::make_shared<bool>(false);
    auto last_item_is_raw = std::make_shared<bool>(false);
    auto last_item_created_slot_retained = std::make_shared<bool>(false);

    App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, sources, targets, location, backup_root, restored, skipped, recovery_paths, last_mutation_started, last_item_is_raw, last_item_created_slot_retained](auto pbox) mutable -> Result {
        fs::FsStdio stdio_fs;
        fs::FsNativeSd sd_fs;

        for (size_t i = 0; i < sources->size(); i++) {
            auto& src = (*sources)[i];
            auto& dst = (*targets)[i];
            detail::LoadControlEntry(dst);
            pbox->SetTitle(dst.GetName());
            if (dst.image) {
                pbox->SetImage(dst.image);
            } else if (auto data = title::Get(dst.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }
            pbox->UpdateTransfer(i + 1, sources->size());

            const fs::FsPath file_path = src.backup_members.front().path;
            const auto& member = src.backup_members.front();
            fs::Fs* probe_fs = file_path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

            FsDirEntryType entry_type{};
            const bool is_dir_on_disk = (R_SUCCEEDED(probe_fs->GetEntryType(file_path, &entry_type)) && entry_type == FsDirEntryType_Dir);
            const bool is_folder = member.is_directory || is_dir_on_disk;

            const bool is_raw = !is_folder && IsDisaSaveFile(probe_fs, file_path);
            *last_item_is_raw = is_raw;
            *last_mutation_started = false;
            *last_item_created_slot_retained = false;

            if (is_raw) {
                return Result_RawSaveRestoreUnsupported;
            }

            const char* filename = std::strrchr(file_path.s, '/');
            filename = filename ? filename + 1 : file_path.s;
            BackupArchiveInfo check_info{};

            if (is_folder) {
                if (!is_dir_on_disk || !InspectBackupFolder(probe_fs, file_path, filename, src.dbi_game_dir, check_info) ||
                    BackupGroupKey(check_info) != BackupGroupKey(src)) {
                    log_write("Backup folder reinspection failed or identity mismatch for %s\n", file_path.s);
                    return FsError_PathNotFound;
                }
            } else {
                if (!InspectBackupArchive(probe_fs, file_path, filename, src.dbi_game_dir, check_info) ||
                    BackupGroupKey(check_info) != BackupGroupKey(src)) {
                    log_write("Backup archive reinspection failed or identity mismatch for %s\n", file_path.s);
                    return FsError_PathNotFound;
                }
            }

            if (!MatchesRestoreDestination(check_info, dst)) {
                log_write("Destination slot identity mismatch for %s\n", file_path.s);
                return FsError_PathNotFound;
            }

            *last_mutation_started = false;
            pbox->SetActionName("Restore"_i18n);
            fs::FsPath item_recovery_path;
            bool item_mutation_started = false;
            bool item_created_slot_retained = false;
            const Result restore_rc = RestoreSaveInternal(pbox, dst, file_path, &item_recovery_path, &item_mutation_started, &item_created_slot_retained);
            if (!item_recovery_path.empty()) {
                recovery_paths->push_back(item_recovery_path);
            }
            if (R_FAILED(restore_rc)) {
                *last_mutation_started = item_mutation_started;
                *last_item_created_slot_retained = item_created_slot_retained;
                return restore_rc;
            }
            (*restored)++;
        }

        R_SUCCEED();
    }, [restored, skipped, recovery_paths, last_mutation_started, last_item_is_raw, last_item_created_slot_retained](Result rc){
        if (R_FAILED(rc)) {
            if (*last_item_created_slot_retained) {
                App::Push<OptionBox>("Save slot was created, but restore did not finish.\nThe save slot remains available for retry or manual management.\nNo safety recovery archive was created because the slot was newly created."_i18n, "OK"_i18n);
            } else if (*last_item_is_raw) {
                App::Push<OptionBox>(save::GetRawRestoreUnsupportedMessage(), "OK"_i18n);
            } else if (rc == Result_TransferCancelled) {
                App::Notify("Restore cancelled."_i18n); // the user stopped it: not an error
            } else {
                App::PushErrorBox(rc, "Restore failed!"_i18n);
            }
        } else {
            if (*restored) {
                App::Notify("Restore successful!"_i18n);
            } else {
                App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
            }

            if (*skipped) {
                App::Notify(std::to_string(*skipped) + " saves skipped");
            }
        }

        if (!recovery_paths->empty()) {
            std::string prefix;
            if (R_SUCCEEDED(rc)) {
                prefix = (recovery_paths->size() == 1)
                    ? "Restore completed.\nSafety recovery archive:\n"_i18n
                    : "Restore completed.\nSafety recovery archive(s):\n"_i18n;
            } else if (*last_item_is_raw) {
                prefix = "Restore stopped.\nSafety recovery archive(s) retained:\n"_i18n;
            } else if (!*last_item_is_raw && *last_mutation_started) {
                prefix = (recovery_paths->size() == 1)
                    ? "Restore stopped: target save may have changed and restored contents are unverified.\nSafety recovery archive retained:\n"_i18n
                    : "Restore stopped: current target save may have changed and restored contents are unverified.\nSafety recovery archive(s) retained:\n"_i18n;
            } else {
                prefix = (recovery_paths->size() == 1)
                    ? "Restore stopped before target save was modified.\nSafety recovery archive retained:\n"_i18n
                    : "Restore stopped before current target save was modified.\nSafety recovery archive(s) retained:\n"_i18n;
            }
            std::string rec_msg = prefix;
            for (const auto& rp : *recovery_paths) {
                rec_msg += rp.s;
                rec_msg += "\n";
            }
            rec_msg += "\n" + "Manual recovery: open File Browser -> select recovery.zip -> Restore to confirmed target slot."_i18n;
            App::Push<OptionBox>(rec_msg, "OK"_i18n);
        } else if (R_FAILED(rc)) {
            if (!*last_item_is_raw && !*last_mutation_started && !*last_item_created_slot_retained) {
                App::Push<OptionBox>("Restore stopped before current target save was modified."_i18n, "OK"_i18n);
            }
        }
    });
}

void Menu::RestoreSavesPicked(Entry e, const Entry& group, const dump::DumpLocation& location, const fs::FsPath& backup_root, fs::FsPath chosen) {
    auto it = std::ranges::find_if(group.backup_members, [&](const auto& m) {
        return m.path == chosen;
    });
    if (it == group.backup_members.end()) {
        App::Push<OptionBox>("Selected backup archive has changed or is no longer available."_i18n, "OK"_i18n);
        return;
    }

    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = chosen.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

    FsDirEntryType entry_type{};
    const bool is_dir_on_disk = (R_SUCCEEDED(probe_fs->GetEntryType(chosen, &entry_type)) && entry_type == FsDirEntryType_Dir);
    const bool is_folder = it->is_directory || is_dir_on_disk;

    const bool is_raw = !is_folder && IsDisaSaveFile(probe_fs, chosen);

    if (is_raw) {
        App::Push<OptionBox>(save::GetRawRestoreUnsupportedMessage(), "OK"_i18n);
        return;
    }

    const char* filename = std::strrchr(chosen.s, '/');
    filename = filename ? filename + 1 : chosen.s;
    BackupArchiveInfo check_info{};

    // identity (BackupGroupKey) is what must still hold. the backup source is not compared:
    // a group built from a live save spans every source, so that check rejected valid archives.
    const bool inspected = is_folder
        ? (is_dir_on_disk && InspectBackupFolder(probe_fs, chosen, filename, group.dbi_game_dir, check_info))
        : InspectBackupArchive(probe_fs, chosen, filename, group.dbi_game_dir, check_info);
    if (!inspected || BackupGroupKey(check_info) != BackupGroupKey(group)) {
        log_write("[SAVE] restore revalidation failed for %s (inspected=%d)\n", chosen.s, inspected ? 1 : 0);
        App::Push<OptionBox>("Selected backup archive has changed or is no longer available."_i18n, "OK"_i18n);
        return;
    }

    if (!MatchesRestoreDestination(check_info, e)) {
        App::Push<OptionBox>("Selected backup archive does not match destination save slot."_i18n, "OK"_i18n);
        return;
    }

    if (!haze::ReleaseSaveMounts()) { // only while the PC has a save open over MTP
        App::Push<OptionBox>("MTP is currently active. Please close the running game and disable MTP before restoring save data."_i18n, "OK"_i18n);
        return;
    }

    // the only confirmation of a restore: name the game and, for an account save, the user.
    std::string target_name = e.GetName() ? e.GetName() : "";
    if (const auto account = e.save_data_type == FsSaveDataType_Account ? GetAccountName(e.uid) : std::string{}; !account.empty()) {
        target_name += " (" + account + ")";
    }
    const std::string prompt = "Restore save data to\n"_i18n + target_name + "?\n\n" + "A safety recovery backup will be created on SD before overwriting.\nPlease close the running game and disable MTP."_i18n;

    auto start_restore = [this, e = std::move(e), location, backup_root, chosen = std::move(chosen), is_raw]() mutable {
        auto recovery_path = std::make_shared<fs::FsPath>();
        auto mutation_started = std::make_shared<bool>(false);
        auto created_slot_retained = std::make_shared<bool>(false);

        App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, e, location, backup_root, chosen, recovery_path, mutation_started, created_slot_retained, is_raw](auto pbox) mutable -> Result {
            detail::LoadControlEntry(e);

            pbox->SetActionName("Restore"_i18n);
            R_TRY(RestoreSaveInternal(pbox, e, chosen, recovery_path.get(), mutation_started.get(), created_slot_retained.get()));
            R_SUCCEED();
        }, [recovery_path, mutation_started, created_slot_retained, is_raw](Result rc){
            if (R_FAILED(rc)) {
                if (*created_slot_retained) {
                    App::Push<OptionBox>("Save slot was created, but restore did not finish.\nThe save slot remains available for retry or manual management.\nNo safety recovery archive was created because the slot was newly created."_i18n, "OK"_i18n);
                    return;
                }
                if (rc == Result_TransferCancelled) {
                    App::Notify("Restore cancelled."_i18n); // the user stopped it: not an error
                } else {
                    App::PushErrorBox(rc, "Restore failed!"_i18n);
                }
            } else {
                App::Notify("Restore successful!"_i18n);
            }

            if (!is_raw && !*created_slot_retained) {
                if (!recovery_path->empty()) {
                    std::string prefix;
                    if (R_SUCCEEDED(rc)) {
                        prefix = "Restore completed.\nSafety recovery archive:\n"_i18n;
                    } else if (*mutation_started) {
                        prefix = "Restore stopped: target save may have changed and restored contents are unverified.\nSafety recovery archive retained:\n"_i18n;
                    } else {
                        prefix = "Restore stopped before target save was modified.\nSafety recovery archive retained:\n"_i18n;
                    }
                    const std::string msg = prefix + recovery_path->toString() + "\n\n" + "Manual recovery: open File Browser -> select recovery.zip -> Restore to confirmed target slot."_i18n;
                    App::Push<OptionBox>(msg, "OK"_i18n);
                } else if (R_FAILED(rc)) {
                    if (!*mutation_started) {
                        App::Push<OptionBox>("Restore stopped before target save was modified."_i18n, "OK"_i18n);
                    }
                }
            }
        });
    };

    if (e.is_planned_create) {
        start_restore();
        return;
    }

    App::Push<OptionBox>(prompt, "No"_i18n, "Yes"_i18n, 0, [start_restore](auto op_index) mutable {
        if (!op_index || *op_index != 1) return;
        start_restore();
    });
}

Result Menu::RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool* out_created_slot_retained) const {
    if (out_mutation_started) {
        *out_mutation_started = false;
    }
    if (out_created_slot_retained) {
        *out_created_slot_retained = false;
    }
    R_UNLESS(!e.is_backup && (e.save_data_id != 0 || e.is_planned_create), FsError_PathNotFound);
    pbox->SetTitle(e.GetName());
    if (e.image) {
        pbox->SetImage(e.image);
    } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
        pbox->SetImageDataConst(data->icon);
    } else {
        pbox->SetImage(0);
    }

    log_write("restoring save: %s\n", path.s);

    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

    FsDirEntryType path_type{};
    const bool is_folder = (R_SUCCEEDED(probe_fs->GetEntryType(path, &path_type)) && path_type == FsDirEntryType_Dir);

    if (is_folder) {
        const char* filename = std::strrchr(path.s, '/');
        filename = filename ? filename + 1 : path.s;
        BackupArchiveInfo check_info{};
        if (!InspectBackupFolder(probe_fs, path, filename, e.dbi_game_dir, check_info)) {
            log_write("Folder backup reinspection failed for %s\n", path.s);
            return FsError_PathNotFound;
        }
        if (!MatchesRestoreDestination(check_info, e)) {
            log_write("Folder backup destination identity mismatch for %s\n", path.s);
            return FsError_PathNotFound;
        }
        return RestoreSaveFolder(pbox, e, path, out_recovery_path, out_mutation_started, out_created_slot_retained);
    }

    if (IsDisaSaveFile(probe_fs, path)) {
        log_write("refusing unsupported raw DISA save restore: %s\n", path.s);
        return Result_RawSaveRestoreUnsupported;
    }

    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, false, out_created_slot_retained);
}

} // namespace sphaira::ui::menu::save
