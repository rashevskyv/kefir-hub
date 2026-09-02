#include "ui/menus/users_menu.hpp"
#include "ui/menus/users/users_internal.hpp"

#include "account/account_restore.hpp"
#include "account/nand_transfer.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {

void Menu::ConfirmNandBackup() {
    App::Push<OptionBox>(
        "Copy every profile on this console plus their play hours to SD (same users, with hours). If the system holds a save, Hub skips it and writes a TegraExplorer script instead of killing services. Not the same as Backup user."_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunNandBackup();
            }
        });
}

void Menu::ConfirmNandRestore() {
    App::Push<OptionBox>(
        "Write a profiles & play hours pack into this console?\n\n"
        "Users and hours here will be replaced. Hub stages the pack, then TegraExplorer writes and signs the system saves.\n\n"
        "Back up SYSTEM in hekate first.\n"
        "If the console will not boot after restore:\n"
        "• hekate > restore SYSTEM backup, or\n"
        "• hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te (only if a raw 0010/00F0 snapshot was taken).\n"
        "Y selects the pack folder."_i18n,
        "Cancel"_i18n, "Choose folder"_i18n, 1,
        [this](auto op) {
            if (!op || *op != 1) {
                return;
            }
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[this](const fs::FsPath& path, const filebrowser::FsEntry&) -> bool {
                    RunNandRestore(path.toString());
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{paths::DATA_ROOT + "/nand_transfer"},
                true);
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
            App::Push<OptionBox>(
                "Horizon could not copy profiles and play hours while the system is running.\n\n"
                "TegraExplorer will dump them, then return to hekate.\n"
                "After the console starts, open Kefir Hub to confirm the dump."_i18n,
                "Cancel"_i18n, "Launch TegraExplorer"_i18n, 1,
                [this](auto op) {
                    if (!op || *op != 1) {
                        account_restore::ClearPending();
                        return;
                    }
                    if (!account_restore::LaunchTegraRomfs(account_restore::NandDumpTeName())) {
                        App::Push<OptionBox>(
                            "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try again."_i18n,
                            "OK"_i18n);
                        return;
                    }
                    Refresh();
                });
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunNandRestore(const std::string& dir) {
    if (!nand_transfer::IsPack(dir)) {
        App::Push<OptionBox>(
            "That folder is not a profiles & play hours pack."_i18n,
            "OK"_i18n);
        return;
    }

    auto pack = std::make_shared<std::string>(dir);
    auto snap = std::make_shared<account_restore::RawSnapshotReport>();
    App::Push<ProgressBox>(0, "Restore profiles & play hours"_i18n, "Preparing TegraExplorer restore"_i18n,
        [pack, snap](auto pbox) -> Result {
            fs::FsNativeSd sd;
            auto resolved = *pack;
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
            R_UNLESS(nand_transfer::IsPack(resolved), Result_FsInvalidType);
            *pack = resolved;

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

            account_restore::TrySnapshotRawSystemSaves(pbox, *snap);
            account_restore::InstallRestoreTeScripts();
            R_TRY(account_restore::SavePending({resolved}, "wait_nand_restore", snap->save_0010));
            R_SUCCEED();
        }, [this, pack, snap](Result rc) {
            if (R_FAILED(rc)) {
                const auto msg = (rc == Result_FsInvalidType)
                    ? "That folder is not a profiles & play hours pack."_i18n
                    : "Could not stage the profiles & play hours restore on SD."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }

            std::string msg =
                "Ready to restore profiles & play hours through TegraExplorer.\n\n"
                "The console will reboot into TegraExplorer, write the pack, then return to hekate.\n"
                "Open Kefir Hub again afterward to confirm the result.\n\n"_i18n;
            if (snap->save_0010 || snap->save_00F0) {
                msg += "A raw undo snapshot was saved on SD. If the console will not boot: hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te\n\n"_i18n;
            } else {
                msg += "Could not snapshot raw 0010/00F0 for Undo. Keep a hekate SYSTEM backup before continuing.\n\n"_i18n;
            }
            msg += "If TegraExplorer does not finish and the console will not boot: restore SYSTEM in hekate, or use Undo if a snapshot exists."_i18n;

            App::Push<OptionBox>(
                msg,
                "Cancel"_i18n, "Launch TegraExplorer"_i18n, 1,
                [this, pack](auto op) {
                    if (!op || *op != 1) {
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
