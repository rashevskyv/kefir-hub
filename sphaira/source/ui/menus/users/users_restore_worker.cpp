#include "ui/menus/users_menu.hpp"
#include "ui/menus/users/users_internal.hpp"

#include "account/account_link.hpp"
#include "account/account_restore.hpp"
#include "account/account_user.hpp"
#include "account/nand_transfer.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "utils/utils.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {

auto OfferPendingRestore() -> bool {
    static bool s_offered = false;
    if (s_offered) {
        return false;
    }
    account_restore::ClearReopenHubHint();
    auto pending = account_restore::LoadPending();
    if (pending.rolled_back) {
        s_offered = true;
        const auto rc = account_restore::ClearPending();
        if (R_FAILED(rc)) {
            App::Push<OptionBox>(
                "Failed to clear unfinished operation."_i18n,
                "OK"_i18n);
            return true;
        }
        App::Push<OptionBox>(
            "Account save 0010 was rolled back. Restore was cancelled."_i18n,
            "OK"_i18n);
        return true;
    }
    if (!pending.present || pending.phase == "applied") {
        return false;
    }

    if (pending.phase == "wait_link") {
        s_offered = true;
        fs::FsNativeSd sd;
        if (sd.FileExists(account_restore::LinkAppliedOkPath())) {
            account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok);
            App::Push<OptionBox>(
                "Profile created and Nintendo Account link applied. Reboot is done."_i18n,
                "OK"_i18n);
        } else {
            account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok);
            App::Push<OptionBox>(
                "TegraExplorer did not apply the Nintendo Account link.\n\n"
                "An extra unlinked profile may exist on this console.\n"
                "If the console will not boot: hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te"_i18n,
                "OK"_i18n);
        }
        return true;
    }

    if (pending.phase == "wait_nand_restore") {
        s_offered = true;
        fs::FsNativeSd sd;
        if (!pending.staging_dir.empty()) {
            account_restore::CleanRestoreStagingDir(pending.staging_dir);
        }
        if (sd.FileExists(account_restore::NandRestoredOkPath())) {
            account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok, "");
            App::Push<OptionBox>(
                "Profiles and play hours were restored. Reboot is done."_i18n,
                "OK"_i18n);
        } else {
            account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok, "");
            App::Push<OptionBox>(
                "TegraExplorer did not finish restoring profiles & play hours."_i18n,
                "OK"_i18n);
        }
        return true;
    }

    if (pending.phase == "wait_nand_dump") {
        s_offered = true;
        const auto pack = pending.pack_dirs.empty() ? std::string{} : pending.pack_dirs.front();
        if (NandDumpLooksComplete(pack)) {
            std::string final_archive;
            const auto arc_rc = nand_transfer::FinalizePackArchive(pack, final_archive);
            if (R_SUCCEEDED(arc_rc)) {
                log_write("[NAND] TE dump finalized to %s\n", final_archive.c_str());
                account_restore::SavePending({final_archive}, "applied", true);
                if (R_FAILED(account_restore::CleanDumpHandshake())) {
                    App::Push<OptionBox>("Failed to clear unfinished operation."_i18n, "OK"_i18n);
                    return true;
                }
                App::Push<OptionBox>(
                    "Profiles and play hours dump is done."_i18n,
                    "OK"_i18n);
                return true;
            } else {
                log_write("[NAND] TE dump finalize archive failed 0x%X; keeping staging %s\n", arc_rc, pack.c_str());
                App::Push<OptionBox>(
                    "Failed to create archive from dumped profiles & play hours."_i18n,
                    "OK"_i18n);
                return true;
            }
        }
        App::Push<OptionBox>(
            "TegraExplorer did not finish the dump."_i18n,
            "Later"_i18n, "Cancel operation"_i18n, "Retry"_i18n, 0,
            [pack](auto op) {
                const auto choice = account_restore::ResolveAbandonChoice(op);
                if (choice == account_restore::AbandonChoice::Later) {
                    return;
                }
                if (choice == account_restore::AbandonChoice::CancelOperation) {
                    const auto rc = account_restore::ClearPending();
                    if (R_FAILED(rc)) {
                        App::Push<OptionBox>(
                            "Failed to clear unfinished operation."_i18n,
                            "OK"_i18n);
                    }
                    return;
                }
                // Retry: the user may have deleted the target folder or flags. Re-stage
                // everything (folder, NAND flag, pack path, pending state) before rebooting.
                nand_transfer::Report report;
                report.dir = pack;
                if (R_FAILED(StageNandDump(report))) {
                    App::Push<OptionBox>(
                        "Could not stage the profiles & play hours dump on SD."_i18n,
                        "OK"_i18n);
                    return;
                }
                if (!account_restore::LaunchTegraRomfs(account_restore::NandDumpTeName())) {
                    App::Push<OptionBox>(
                        "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try again."_i18n,
                        "OK"_i18n);
                }
            });
        return true;
    }

    bool upgraded_from_wait_dump = false;
    if (pending.phase == "wait_dump") {
        if (account_restore::SnapshotOk()) {
            account_restore::SavePending(pending.pack_dirs, "ready", true);
            pending.phase = "ready";
            pending.snapshot_ok = true;
            upgraded_from_wait_dump = true;
            log_write("[RESTORE] wait_dump→ready (snapshot on SD)\n");
        } else {
            s_offered = true;
            App::Push<OptionBox>(
                "The account save dump is not on SD yet."_i18n,
                "Later"_i18n, "Cancel operation"_i18n, "Retry"_i18n, 0,
                [](auto op) {
                    const auto choice = account_restore::ResolveAbandonChoice(op);
                    if (choice == account_restore::AbandonChoice::Later) {
                        return;
                    }
                    if (choice == account_restore::AbandonChoice::CancelOperation) {
                        const auto rc = account_restore::ClearPending();
                        if (R_FAILED(rc)) {
                            App::Push<OptionBox>(
                                "Failed to clear unfinished operation."_i18n,
                                "OK"_i18n);
                        }
                        return;
                    }
                    if (choice == account_restore::AbandonChoice::Retry) {
                        if (!account_restore::LaunchTegraDump()) {
                            App::Push<OptionBox>(
                                "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try again."_i18n,
                                "OK"_i18n);
                        }
                    }
                });
            return true;
        }
    }

    if (pending.phase != "ready" || pending.pack_dirs.empty()) {
        return false;
    }
    // Persist ready if state still had has_0010=false from an older Hub.
    if (pending.snapshot_ok) {
        account_restore::SavePending(pending.pack_dirs, "ready", true);
    }

    s_offered = true;
    std::vector<account_user::Pack> packs;
    for (const auto& dir : pending.pack_dirs) {
        auto pack = account_user::FindUserPack(dir);
        if (!pack.dir.empty()) {
            packs.push_back(std::move(pack));
        }
    }
    if (packs.empty()) {
        App::Push<OptionBox>(
            "Pending restore packs are missing from SD."_i18n,
            "Later"_i18n, "Cancel operation"_i18n, 0,
            [](auto op) {
                if (!op || *op != 1) {
                    return;
                }
                const auto rc = account_restore::ClearPending();
                if (R_FAILED(rc)) {
                    App::Push<OptionBox>(
                        "Failed to clear unfinished operation."_i18n,
                        "OK"_i18n);
                }
            });
        return true;
    }

    // User already confirmed Restore before TE; dump is on SD — continue without re-prompt.
    if (upgraded_from_wait_dump) {
        log_write("[RESTORE] auto-continuing StartRestoreBackup after TE dump\n");
        StartRestoreBackup(std::move(packs));
        return true;
    }

    App::Push<OptionBox>(
        "Unfinished restore is ready (0010 snapshot is on SD).\n\n"
        "Continue will create the profile, then reboot the console.\n\n"
        "If it does not boot:\n"
        "• hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te\n"
        "• TegraExplorer is controlled with the power and volume buttons.\n"
        "• That puts the old profiles back and cancels this restore."_i18n,
        "Later"_i18n, "Cancel restore"_i18n, "Continue"_i18n, 2,
        [packs = std::move(packs)](auto op) mutable {
            if (!op) {
                return;
            }
            if (*op == 1) {
                const auto rc = account_restore::ClearPending();
                if (R_FAILED(rc)) {
                    App::Push<OptionBox>(
                        "Failed to clear unfinished operation."_i18n,
                        "OK"_i18n);
                    return;
                }
                App::Push<OptionBox>("Restore cancelled. The 0010 snapshot was removed."_i18n, "OK"_i18n);
                return;
            }
            if (*op == 2) {
                StartRestoreBackup(std::move(packs));
            }
        });
    return true;
}

namespace {

struct RestoreReport {
    u32 profiles_restored{};
    u32 created_count{};
    u32 replaced_count{};
    u32 links_staged{};
    u32 unlinked_restored{};
    u32 link_malformed_count{};
    u32 failed_creations{};
    bool link_stage_failed{};
    bool needs_te_link{};
};

auto StageCreateLinkForTe(fs::FsNativeSd& sd, const AccountUid& dest_uid, const account_link::LinkPackage& pkg) -> Result {
    R_TRY(sd.CreateDirectoryRecursively(account_restore::LinkBaasDir()));
    R_TRY(sd.CreateDirectoryRecursively(account_restore::LinkNasDir()));

    if (pkg.baas_data.size() < 24) {
        log_write("[USER] StageCreateLinkForTe: baas too small\n");
        return Result_FsInvalidType;
    }
    const auto baas_name = account_link::UidDashedLinkalho(dest_uid) + ".dat";
    const auto baas_path = std::string(account_restore::LinkBaasDir()) + "/" + baas_name;
    R_TRY(sd.write_entire_file(baas_path.c_str(), pkg.baas_data));

    for (const auto& nf : pkg.nas_files) {
        const auto nas_path = std::string(account_restore::LinkNasDir()) + "/" + nf.filename;
        R_TRY(sd.write_entire_file(nas_path.c_str(), nf.data));
    }

    const auto uid_txt =
        "rfc=" + account_link::UidDashedRfc(dest_uid) + "\n" +
        "linkalho=" + account_link::UidDashedLinkalho(dest_uid) + "\n" +
        "baas=" + baas_name + "\n";
    R_TRY(sd.write_entire_file(
        (std::string(account_restore::LinkStagingDir()) + "/uid.txt").c_str(),
        std::vector<u8>(uid_txt.begin(), uid_txt.end())));

    log_write("[USER] staged TE link baas=%s nas_files=%zu\n",
        baas_name.c_str(), pkg.nas_files.size());
    R_SUCCEED();
}

} // namespace

void Menu::RunRestoreBackup(std::vector<account_user::Pack> picked_packs) {
    StartRestoreBackup(std::move(picked_packs));
}

void StartRestoreBackup(std::vector<account_user::Pack> picked_packs) {
    auto report = std::make_shared<RestoreReport>();
    App::Push<ProgressBox>(0, "Restore Backup"_i18n, "Restoring profiles..."_i18n,
        [picked_packs = std::move(picked_packs), report](auto pbox) mutable -> Result {
            fs::FsNativeSd sd;
            std::vector<account_link::TargetLink> links_to_stage;
            std::vector<std::string> temp_extracted_dirs;

            ON_SCOPE_EXIT({
                for (const auto& td : temp_extracted_dirs) {
                    sd.DeleteDirectoryRecursively(td.c_str());
                }
            });

            // Fresh staging dir; never leave stale baas/nas for TE.
            if (sd.DirExists(account_restore::LinkStagingDir())) {
                sd.DeleteDirectoryRecursively(account_restore::LinkStagingDir());
            }
            sd.DeleteFile(account_restore::LinkAppliedOkPath());

            for (size_t i = 0; i < picked_packs.size(); i++) {
                const auto& p = picked_packs[i];
                pbox->NewTransfer("Restoring user profile"_i18n);

                std::string effective_dir = p.dir;
                if (p.is_archive) {
                    const std::string staging_dir = paths::DATA_ROOT + "/restore_pending/staging_pack_" + std::to_string(i);
                    sd.DeleteDirectoryRecursively(staging_dir.c_str());
                    R_TRY(account_user::ExtractPackToDirectory(p, staging_dir, pbox));
                    temp_extracted_dirs.push_back(staging_dir);
                    effective_dir = staging_dir;
                }

                std::vector<u8> jpeg;
                sd.read_entire_file((effective_dir + "/avatar.jpg").c_str(), jpeg);
                const std::string name = !p.nickname.empty() ? p.nickname : "User";

                account_link::LinkPackage pkg;
                const auto link_load_rc = account_link::LoadUserPackLinkPackage(effective_dir, pkg);
                AccountUid dest_uid{};
                bool have_dest = false;
                bool created = false;

                if (const auto live = FindLiveUidForPack(p)) {
                    dest_uid = *live;
                    have_dest = true;
                    log_write("[USER] Replace onto live uid %s (pack uid %s nas %llx); name/avatar only, no TE link\n",
                        account_link::UidHex(dest_uid).c_str(),
                        p.uid_hex.c_str(),
                        static_cast<unsigned long long>(p.nas_id));
                    const auto rename_rc = account_user::Rename(dest_uid, name);
                    if (R_FAILED(rename_rc)) {
                        log_write("[USER] rename existing 0x%X\n", rename_rc);
                    }
                    if (!jpeg.empty()) {
                        const auto av_rc = account_user::SetImageJpeg(dest_uid, jpeg);
                        if (R_FAILED(av_rc)) {
                            log_write("[USER] avatar existing 0x%X\n", av_rc);
                        }
                    }
                    report->profiles_restored++;
                    report->replaced_count++;
                }

                if (!have_dest) {
                    log_write("[USER] Create new uid for pack %s nas %llx\n",
                        p.uid_hex.c_str(),
                        static_cast<unsigned long long>(p.nas_id));
                    const auto create_rc = account_user::Create(name, dest_uid, jpeg);
                    if (R_FAILED(create_rc)) {
                        log_write("[USER] Create user failed 0x%X\n", create_rc);
                        report->failed_creations++;
                        continue;
                    }
                    have_dest = true;
                    created = true;
                    report->profiles_restored++;
                    report->created_count++;
                }

                // Create + valid baas/nas → SD staging for TE. Never ApplyLinkPackages / Horizon 0010 write.
                // Replace (proven nas): name/avatar only — no TE link, no 0010 write.
                if (created && R_SUCCEEDED(link_load_rc) && pkg.nas_id != 0) {
                    links_to_stage.push_back({dest_uid, std::move(pkg)});
                } else if (have_dest && !created) {
                    // Replace path intentionally skips link rewrite.
                } else if (have_dest && !sd.DirExists((effective_dir + "/baas").c_str())) {
                    report->unlinked_restored++;
                } else if (have_dest) {
                    log_write("[USER] Link package invalid in %s (0x%X)\n", effective_dir.c_str(), link_load_rc);
                    report->link_malformed_count++;
                    report->unlinked_restored++;
                }
                // Pack may still contain pdm/playtime; Restore Backup ignores it.
            }

            if (!links_to_stage.empty()) {
                pbox->NewTransfer("Staging Nintendo Account link for TegraExplorer"_i18n);
                for (const auto& target : links_to_stage) {
                    const auto stage_rc = StageCreateLinkForTe(sd, target.uid, target.pkg);
                    if (R_FAILED(stage_rc)) {
                        log_write("[USER] StageCreateLinkForTe failed 0x%X\n", stage_rc);
                        report->link_stage_failed = true;
                        R_TRY(stage_rc);
                    }
                    report->links_staged++;
                }
                auto pending = account_restore::LoadPending();
                const auto pack_dirs = pending.present ? pending.pack_dirs : std::vector<std::string>{};
                const bool snap_ok = pending.present ? pending.snapshot_ok : account_restore::SnapshotOk();
                if (!pack_dirs.empty()) {
                    R_TRY(account_restore::SavePending(pack_dirs, "wait_link", snap_ok));
                } else {
                    // Snapshot prep should have written packs; keep wait_link even if state was cleared.
                    std::vector<std::string> dirs;
                    for (const auto& p : picked_packs) {
                        dirs.push_back(p.dir);
                    }
                    R_TRY(account_restore::SavePending(dirs, "wait_link", snap_ok));
                }
                report->needs_te_link = true;
                log_write("[USER] Create link staged; wait_link for TE apply (%u)\n", report->links_staged);
            }

            R_SUCCEED();
        },
        [report](Result /*rc*/) {
            if (report->profiles_restored == 0) {
                App::Push<OptionBox>("Could not restore user profiles."_i18n, "OK"_i18n);
                return;
            }

            if (report->needs_te_link && !report->link_stage_failed) {
                log_write("[USER] launching account_0010_apply_link.te\n");
                if (!account_restore::LaunchTegraRomfs(account_restore::ApplyLinkTeName())) {
                    App::Push<OptionBox>(
                        "Profile was created, but TegraExplorer could not start to apply the Nintendo Account link.\n\n"
                        "Put TegraExplorer.bin in /bootloader/payloads/ and open Kefir Hub again, or run Undo if the console will not boot."_i18n,
                        "OK"_i18n);
                }
                return;
            }

            auto pending = account_restore::LoadPending();
            if (pending.present) {
                account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok);
            }

            if (report->link_stage_failed || report->link_malformed_count) {
                std::string msg = "Restored " + std::to_string(report->profiles_restored) + " user profile(s)."_i18n;
                msg += " " + "Nintendo Account link data was invalid or could not be applied."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }

            // Create without link: reboot for a clean user list. Replace-only: stay in Hub.
            if (report->created_count > 0) {
                log_write("[USER] Create without TE link; rebooting for clean user list\n");
                utils::requestForcedReboot();
                return;
            }

            std::string msg = "Restored " + std::to_string(report->profiles_restored) + " user profile(s)."_i18n;
            if (report->replaced_count > 0) {
                msg += " " + "Name and avatar updated on existing profile(s)."_i18n;
            }
            if (report->unlinked_restored > 0) {
                msg += " " + std::to_string(report->unlinked_restored) + " profile(s) restored without link."_i18n;
            }
            App::Push<OptionBox>(msg, "OK"_i18n);
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}


} // namespace sphaira::ui::menu::users
