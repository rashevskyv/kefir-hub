#include "ui/menus/users_menu.hpp"
#include "ui/menus/users/users_internal.hpp"
#include "ui/menus/users/users_nand_library.hpp"

#include "account/account_restore.hpp"
#include "account/nand_transfer.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {

void Menu::ConfirmNandBackup() {
    App::Push<OptionBox>(
        "Copy every profile on this console plus their play hours to SD (same users, with hours). If the system holds a save, Hub skips it and writes a TegraExplorer script instead of killing services."_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunNandBackup();
            }
        });
}

void Menu::ConfirmNandRestore() {
    OpenNandPackLibrary([this](const std::string& dir, bool restore_play_hours) {
        RunNandRestore(dir, restore_play_hours);
    }, NandLibraryMode::Restore);
}

void Menu::ConfirmNandManage() {
    OpenNandPackLibrary([this](const std::string& dir, bool restore_play_hours) {
        RunNandRestore(dir, restore_play_hours);
    }, NandLibraryMode::Manage);
}

void Menu::ReceiveNandFromAnotherConsole() {
    OpenRemoteNandTransfer(NandLibraryMode::Manage, nullptr, [this](){
        Refresh();
    });
}

void Menu::RestoreNandFromAnotherConsole() {
    OpenRemoteNandTransfer(NandLibraryMode::Restore, [this](const std::string& dir, bool restore_play_hours) {
        RunNandRestore(dir, restore_play_hours);
    }, [this](){
        Refresh();
    });
}

void Menu::RunNandBackup() {
    auto report = std::make_shared<nand_transfer::Report>();
    App::Push<ProgressBox>(0, "Backup profiles & play hours"_i18n, "Backup profiles & play hours"_i18n,
        [report](auto pbox) -> Result {
            const auto rc = nand_transfer::Export(pbox, *report);
            if (R_SUCCEEDED(rc) && report->complete) {
                R_SUCCEED();
            }
            pbox->NewTransfer("Preparing TegraExplorer dump"_i18n);
            R_TRY(StageNandDump(*report));
            R_SUCCEED();
        }, [this, report](Result rc) {
            if (R_SUCCEEDED(rc) && report->complete) {
                App::Push<OptionBox>(
                    "Profiles and play hours are copied. On the other console: Restore profiles & play hours."_i18n,
                    "OK"_i18n);
                Refresh();
                return;
            }
            if (R_FAILED(rc) || report->dir.empty()) {
                App::Push<OptionBox>(
                    "Could not stage the profiles & play hours dump on SD."_i18n,
                    "OK"_i18n);
                return;
            }
            if (!account_restore::LaunchTegraRomfs(account_restore::NandDumpTeName())) {
                account_restore::ClearPending();
                App::Push<OptionBox>(
                    "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try again."_i18n,
                    "OK"_i18n);
                return;
            }
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunNandRestore(const std::string& dir, bool restore_play_hours) {
    if (!nand_transfer::IsPack(dir)) {
        App::Push<OptionBox>(
            "That folder is not a profiles & play hours pack."_i18n,
            "OK"_i18n);
        return;
    }

    auto selected_pack = std::make_shared<std::string>(dir);
    auto staging = std::make_shared<std::string>();
    auto is_archive = std::make_shared<bool>(false);
    auto snap = std::make_shared<account_restore::RawSnapshotReport>();
    auto hours = restore_play_hours;
    App::Push<ProgressBox>(0, "Restore profiles & play hours"_i18n, "Preparing TegraExplorer restore"_i18n,
        [selected_pack, staging, is_archive, snap, hours](auto pbox) -> Result {
            fs::FsNativeSd sd;
            auto resolved = *selected_pack;
            if (sd.FileExists(resolved.c_str()) && (nand_transfer::IsPackArchive(resolved) || std::string_view{resolved}.ends_with(".zip"))) {
                *is_archive = true;
                pbox->NewTransfer("Extracting backup archive"_i18n);
                R_TRY(nand_transfer::StagePackArchiveForRestore(resolved, *staging, pbox));
                resolved = *staging;
            } else {
                *is_archive = false;
                *staging = "";
                if (!sd.DirExists((resolved + "/80000000000000F0").c_str()) &&
                    !sd.DirExists((resolved + "/8000000000000010").c_str()) &&
                    !sd.FileExists((resolved + "/manifest.json").c_str())) {
                    // Picker may land inside a save subfolder; climb one level.
                    auto parent = resolved;
                    while (!parent.empty() && (parent.back() == '/' || parent.back() == '\\')) {
                        parent.pop_back();
                    }
                    const auto slash = parent.find_last_of("/\\");
                    if (slash != std::string::npos && slash > 0) {
                        parent = parent.substr(0, slash);
                    }
                    if (nand_transfer::IsPack(parent)) {
                        resolved = parent;
                    }
                }
            }
            R_UNLESS(nand_transfer::IsPack(resolved), Result_FsInvalidType);

            pbox->NewTransfer("Staging restore"_i18n);
            R_TRY(sd.CreateDirectoryRecursively(account_restore::PendingDir()));
            sd.DeleteFile(account_restore::NandRestoredOkPath());
            sd.DeleteFile(account_restore::LinkAppliedOkPath());
            sd.DeleteFile(account_restore::DumpedOkPath());
            sd.DeleteFile(account_restore::RolledBackPath());

            R_TRY(account_restore::WriteNandFlag());
            {
                const std::vector<u8> body(resolved.begin(), resolved.end());
                R_TRY(sd.write_entire_file(account_restore::NandPackPath(), body));
            }
            {
                const char* flag = hours ? "1" : "0";
                const std::vector<u8> body(flag, flag + 1);
                R_TRY(sd.write_entire_file(account_restore::Restore00F0Path(), body));
            }

            // Horizon may not be able to read raw SYSTEM saves while services are active.
            // TegraExplorer performs the same mandatory snapshot before its first write.
            account_restore::TrySnapshotRawSystemSaves(pbox, *snap, resolved, hours);
            account_restore::InstallRestoreTeScripts();
            R_TRY(account_restore::SavePending({*selected_pack}, "wait_nand_restore", snap->save_0010, *staging));
            R_SUCCEED();
        }, [this, selected_pack, staging, is_archive, snap](Result rc) {
            if (R_FAILED(rc)) {
                if (*is_archive && !staging->empty()) {
                    account_restore::CleanRestoreStagingDir(*staging);
                }
                const auto msg = (rc == Result_FsInvalidType)
                    ? "That backup is not a valid profiles & play hours pack."_i18n
                    : (rc == Result_SaveSyncFailed)
                        ? "Safety backup of existing console saves could not be completed. Restore aborted to protect NAND."_i18n
                        : "Could not stage the profiles & play hours restore on SD."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }

            std::string msg =
                "Ready to restore profiles & play hours through TegraExplorer.\n\n"
                "The console will reboot into TegraExplorer, write the pack, then return to hekate.\n"
                "Open Kefir Hub again afterward to confirm the result.\n\n"_i18n;
            msg += snap->complete
                ? "A raw undo snapshot was verified and saved on SD. If the console will not boot: hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te\n\n"_i18n
                : "TegraExplorer will create and verify the mandatory safety backup before writing any save.\n\n"_i18n;
            msg += "If TegraExplorer does not finish and the console will not boot: restore SYSTEM in hekate, or use Undo if a snapshot exists."_i18n;

            App::Push<OptionBox>(
                msg,
                "Cancel"_i18n, "Launch TegraExplorer"_i18n, 1,
                [this, staging, is_archive](auto op) {
                    if (!op || *op != 1) {
                        if (*is_archive && !staging->empty()) {
                            account_restore::CleanRestoreStagingDir(*staging);
                        }
                        account_restore::ClearPending();
                        App::Push<OptionBox>("Restore cancelled."_i18n, "OK"_i18n);
                        return;
                    }
                    if (!account_restore::LaunchTegraRomfs(account_restore::NandRestoreTeName())) {
                        App::Push<OptionBox>(
                            "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try again."_i18n +
                            "\n\n" +
                            "Pending restore stays on SD (phase wait_nand_restore)."_i18n,
                            "OK"_i18n);
                        return;
                    }
                    Refresh();
                });
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

} // namespace sphaira::ui::menu::users
