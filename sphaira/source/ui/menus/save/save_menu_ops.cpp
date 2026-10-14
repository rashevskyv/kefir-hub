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
#include "account/account_link.hpp"
#include "ui/menus/filebrowser.hpp"
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

    ShowRestoreConfirm(src_ptr, dst_ptr, location, backup_root);
}

namespace {

// why one save of a restore did not go through, in words.
auto RestoreFailureReason(Result rc) -> std::string {
    const auto value = R_VALUE(rc);
    if (value == R_VALUE(Result_RawSaveRestoreUnsupported)) {
        return "the backup is a raw save file, which cannot be restored"_i18n;
    }
    if (value == FsError_TargetLocked) {
        return "the save is in use: close the game"_i18n;
    }
    if (value == FsError_PathNotFound) {
        return "the backup does not match this game or user"_i18n;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s %04u-%04u", "error"_i18n.c_str(), 2000 + R_MODULE(rc), R_DESCRIPTION(rc));
    return buf;
}

// bytes of a backup archive, or of all the files of a backup folder: the restore order.
auto BackupBytes(fs::Fs* fs, const fs::FsPath& path, bool is_directory) -> s64 {
    FsDirEntryType type{};
    if (is_directory || (R_SUCCEEDED(fs->GetEntryType(path, &type)) && type == FsDirEntryType_Dir)) {
        filebrowser::FsDirCollections collections;
        if (R_FAILED(filebrowser::FsView::get_collections(fs, path, "", collections, true))) {
            return 0;
        }
        s64 total = 0;
        for (const auto& c : collections) {
            for (const auto& f : c.files) {
                total += std::max<s64>(0, f.file_size);
            }
        }
        return total;
    }
    fs::File file;
    s64 size = 0;
    if (R_SUCCEEDED(fs->OpenFile(path, FsOpenMode_Read, &file))) {
        file.GetSize(&size);
    }
    return size;
}

} // namespace

// One confirmation for the whole restore: which game goes to which user.
void Menu::ShowRestoreConfirm(
    std::shared_ptr<std::vector<Entry>> sources,
    std::shared_ptr<std::vector<Entry>> targets,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root) {

    const auto accounts = App::GetAccountList();
    // the game name comes with the control data; without it the list showed bare title ids.
    for (auto& dst : *targets) {
        detail::LoadControlEntry(dst);
    }
    const auto line_of = [&accounts](const Entry& src, const Entry& dst) -> std::string {
        std::string who;
        if (src.save_data_type == FsSaveDataType_Account) {
            who = AccountLabel(dst.uid, accounts);
            if (who.empty()) {
                who = FormatBackupAccount(src, accounts);
            }
        } else {
            who = GetSaveTypeLabel(dst.save_data_type);
        }
        return "• " + std::string{dst.GetName()} + (who.empty() ? std::string{} : " — " + who) + "\n";
    };

    // many saves: the user has just picked them, a list would only repeat it (and could not show all).
    std::string prompt;
    if (sources->size() > 1) {
        prompt = "Saves to restore: "_i18n + std::to_string(sources->size()) + "\n\n";
    } else {
        const auto& src = sources->front();
        prompt = "Restore selected save?"_i18n + "\n\n" + line_of(src, targets->front());
        prompt += "  " + std::string(GetBackupSourceLabel(src.backup_source)) + ", " + FormatBackupTimestamp(src.backup_members.front().ts, false) + "\n\n";
    }

    if (sources->size() > 1) {
        prompt += "If a save cannot be restored, the others still are; the ones that failed are listed at the end."_i18n + "\n";
    }
    const bool replaces_live = std::any_of(targets->begin(), targets->end(), [](const Entry& dst) { return !dst.is_planned_create; });
    if (replaces_live) {
        prompt += "Before a save is replaced, a copy of it is kept on the microSD card (/dumps/recovery)."_i18n + "\n";
    }
    // started as a title, Kefir Hub is the running game itself; only a game suspended under the
    // Homebrew Menu or the Album can hold a save open.
    if (App::IsApplet() && account_link::HasSuspendedApplication()) {
        prompt += "A game is suspended in the background. Close it first: its save cannot be replaced while it runs."_i18n;
    }

    const auto run = [this, sources, targets, location, backup_root](bool make_safety_copy) {
        ExecuteRestore(sources, targets, location, backup_root, make_safety_copy);
    };
    if (replaces_live) {
        App::Push<OptionBox>(prompt, "No"_i18n, "Without a copy"_i18n, "Restore"_i18n, 2, [run](auto choice) {
            if (choice && *choice == 1) {
                run(false);
            } else if (choice && *choice == 2) {
                run(true);
            }
        });
    } else {
        App::Push<OptionBox>(prompt, "No"_i18n, "Restore"_i18n, [run](auto choice) {
            if (choice && *choice == 1) {
                run(true);
            }
        });
    }
}

void Menu::ExecuteRestore(
    std::shared_ptr<std::vector<Entry>> sources,
    std::shared_ptr<std::vector<Entry>> targets,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root,
    bool make_safety_copy) {

    static_cast<void>(location); // every backup path is absolute: the location is not needed here
    static_cast<void>(backup_root);
    App::PopToMenu();
    for (auto& dst : *targets) {
        dst.skip_recovery = !make_safety_copy;
    }

    struct Failure {
        std::string name;
        std::string reason;
    };
    auto restored = std::make_shared<size_t>(0);
    auto failures = std::make_shared<std::vector<Failure>>();
    auto stopped = std::make_shared<bool>(false);
    auto created_slot_kept = std::make_shared<bool>(false);
    auto recovery_paths = std::make_shared<std::vector<fs::FsPath>>();

    // one save that fails is listed and skipped; the others are still restored.
    App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, sources, targets, restored, failures, stopped, created_slot_kept, recovery_paths](auto pbox) mutable -> Result {
        fs::FsStdio stdio_fs;
        fs::FsNativeSd sd_fs;

        // the big saves last: when the console memory runs out, the smaller ones are already in.
        std::vector<std::pair<s64, size_t>> order;
        order.reserve(sources->size());
        for (size_t i = 0; i < sources->size(); i++) {
            const auto& member = (*sources)[i].backup_members.front();
            fs::Fs* fs = member.path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);
            order.emplace_back(BackupBytes(fs, member.path, member.is_directory), i);
        }
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

        for (size_t n = 0; n < order.size(); n++) {
            const size_t i = order[n].second;
            pbox->SetOverall(static_cast<s64>(n), static_cast<s64>(order.size()));
            if (R_FAILED(pbox->ShouldExitResult())) {
                *stopped = true;
                break;
            }
            auto& src = (*sources)[i];
            auto& dst = (*targets)[i];
            detail::LoadControlEntry(dst);
            const std::string name = dst.GetName();
            pbox->SetTitle(name);
            if (dst.image) {
                pbox->SetImage(dst.image);
            } else if (auto data = title::Get(dst.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }
            const fs::FsPath file_path = src.backup_members.front().path;
            const auto& member = src.backup_members.front();
            fs::Fs* probe_fs = file_path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

            FsDirEntryType entry_type{};
            const bool is_dir_on_disk = (R_SUCCEEDED(probe_fs->GetEntryType(file_path, &entry_type)) && entry_type == FsDirEntryType_Dir);
            const bool is_folder = member.is_directory || is_dir_on_disk;

            if (!is_folder && IsDisaSaveFile(probe_fs, file_path)) {
                failures->push_back({name, RestoreFailureReason(Result_RawSaveRestoreUnsupported)});
                continue;
            }

            const char* filename = std::strrchr(file_path.s, '/');
            filename = filename ? filename + 1 : file_path.s;
            BackupArchiveInfo check_info{};
            const bool inspected = is_folder
                ? (is_dir_on_disk && InspectBackupFolder(probe_fs, file_path, filename, src.dbi_game_dir, check_info))
                : InspectBackupArchive(probe_fs, file_path, filename, src.dbi_game_dir, check_info);
            if (!inspected || !MatchesSelectedBackup(check_info, src) || !MatchesRestoreDestination(check_info, dst)) {
                log_write("Backup reinspection failed or identity mismatch for %s\n", file_path.s);
                failures->push_back({name, RestoreFailureReason(FsError_PathNotFound)});
                continue;
            }

            pbox->SetActionName("Restore"_i18n);
            fs::FsPath item_recovery_path;
            bool item_mutation_started = false;
            bool item_created_slot_retained = false;
            const Result restore_rc = RestoreSaveInternal(pbox, dst, file_path, &item_recovery_path, &item_mutation_started, &item_created_slot_retained);
            if (!item_recovery_path.empty()) {
                recovery_paths->push_back(item_recovery_path);
            }
            if (R_FAILED(restore_rc)) {
                if (restore_rc == Result_TransferCancelled) {
                    *stopped = true;
                    break;
                }
                auto reason = RestoreFailureReason(restore_rc);
                if (item_mutation_started) {
                    reason += " " + "(the save may be changed; its copy is in /dumps/recovery)"_i18n;
                }
                failures->push_back({name, reason});
                *created_slot_kept = *created_slot_kept || item_created_slot_retained;
                continue;
            }
            (*restored)++;
        }
        if (!*stopped) {
            pbox->SetOverall(static_cast<s64>(order.size()), static_cast<s64>(order.size()));
        }

        R_SUCCEED();
    }, [sources, restored, failures, stopped, created_slot_kept, recovery_paths](Result rc) {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Restore failed!"_i18n);
            return;
        }
        const auto total = sources->size();
        if (total == 1 && *restored == 1 && recovery_paths->empty()) {
            App::Notify("Restore successful!"_i18n);
            return;
        }

        std::string msg = "Restored: "_i18n + std::to_string(*restored) + " / " + std::to_string(total) + "\n";
        if (*stopped) {
            msg += "Stopped before the end."_i18n + "\n";
        }
        if (!failures->empty()) {
            msg += "\n" + "Not restored:"_i18n + "\n";
            constexpr size_t MAX_LINES = 6; // the box has no scrolling
            for (size_t i = 0; i < failures->size() && i < MAX_LINES; i++) {
                msg += "• " + (*failures)[i].name + ": " + (*failures)[i].reason + "\n";
            }
            if (failures->size() > MAX_LINES) {
                msg += "… +" + std::to_string(failures->size() - MAX_LINES) + "\n";
            }
        }
        if (*created_slot_kept) {
            msg += "\n" + "A new save was created for a failed restore; it stays empty."_i18n + "\n";
        }
        if (!recovery_paths->empty()) {
            msg += "\n" + "Copies of the replaced saves are in /dumps/recovery. To undo one: open its recovery.zip in the File Browser, press + and choose Restore save data."_i18n;
        }
        App::Push<OptionBox>(msg, "OK"_i18n);
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

    // Exact archive identity and creator provenance must match the selected backup before mutation:
    const bool inspected = is_folder
        ? (is_dir_on_disk && InspectBackupFolder(probe_fs, chosen, filename, group.dbi_game_dir, check_info))
        : InspectBackupArchive(probe_fs, chosen, filename, group.dbi_game_dir, check_info);
    if (!inspected || !MatchesSelectedBackup(check_info, group)) {
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
                    const std::string msg = prefix + recovery_path->toString() + "\n\n" + "To undo: open File Browser, select recovery.zip, press + and choose Restore save data."_i18n;
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
