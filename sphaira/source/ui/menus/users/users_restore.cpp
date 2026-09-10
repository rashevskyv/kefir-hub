#include "ui/menus/users_menu.hpp"
#include "ui/menus/users/users_internal.hpp"
#include "ui/menus/users/users_restore_library.hpp"
#include "ui/menus/users/users_restore_remote.hpp"
#include "ui/menus/install_share.hpp"

#include "account/account_link.hpp"
#include "account/account_restore.hpp"
#include "account/account_user.hpp"
#include "account/nand_transfer.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/list.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "utils/utils.hpp"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {
namespace {

struct RestoreSourceItem {
    std::string label;
    std::string description;
    std::function<void()> action;
};

struct RestoreSourceMenu final : MenuBase {
    using Callback = std::function<void(std::vector<account_user::Pack>)>;

    RestoreSourceMenu(Callback on_restore)
        : MenuBase{"Restore User Backup"_i18n, MenuFlag_None}
        , m_on_restore{std::move(on_restore)}
    {
        m_items = {
            {
                "Local backup library"_i18n,
                "Restore user backups from the default library (/config/kefir/account_backups)."_i18n,
                [this]() { OpenLocalLibrary(); }
            },
            {
                "Browse folder..."_i18n,
                "Select a folder or user backup package on the microSD card."_i18n,
                [this]() { OpenBrowseFolder(); }
            },
            {
                "Other console..."_i18n,
                "Restore user backups from another console running Share User Backups."_i18n,
                [this]() { ProbeOtherConsole(); }
            },
        };

        this->SetActions(
            std::make_pair(Button::A, Action{"Open"_i18n, [this](){ OnSelect(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
        );

        m_list = std::make_unique<List>(1, 6, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
        m_list->SetLayout(List::Layout::GRID);
        m_list->SetPageJump(false);
        SetIndex(0);
    }

    ~RestoreSourceMenu() = default;

    auto GetShortTitle() const -> const char* override { return "Restore"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);

        // Draw inactive items first so the selection highlight stays on top
        m_list->Draw(vg, theme, m_items.size(), [vg, theme, this](auto*, auto*, Vec4 v, auto i) {
            if (m_index == static_cast<s64>(i)) {
                return;
            }
            const auto& item = m_items[i];
            DrawElement(v, ThemeEntryID_GRID);
            gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f - 10.f, 18.f,
                theme->GetColour(ThemeEntryID_TEXT), item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f + 14.f, 14.f,
                theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        });

        // Draw active item with focus outline on top
        m_list->Draw(vg, theme, m_items.size(), [vg, theme, this](auto*, auto*, Vec4 v, auto i) {
            if (m_index != static_cast<s64>(i)) {
                return;
            }
            const auto& item = m_items[i];
            gfx::drawRectOutline(vg, theme, 4.f, v);
            gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f - 10.f, 18.f,
                theme->GetColour(ThemeEntryID_TEXT_SELECTED), item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f + 14.f, 14.f,
                theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        });
    }

    void OnFocusGained() override {
        MenuBase::OnFocusGained();
        SetIndex(m_index);
        if (m_pending_browse_folder) {
            const auto folder = *m_pending_browse_folder;
            m_pending_browse_folder.reset();
            const auto packs = account_user::ListUserPacks(folder.toString());
            if (packs.empty()) {
                App::Push<OptionBox>("No user backups found in the selected folder."_i18n, "OK"_i18n);
            } else {
                OpenPacksRestore(packs, false);
            }
        }
    }

private:
    void SetIndex(s64 index) {
        if (m_items.empty()) {
            m_index = 0;
            return;
        }
        m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
        if (!m_index) {
            m_list->SetYoff(0);
        }
        SetTitleSubHeading(m_items[m_index].description, true);
        SetSubHeading("");
    }

    void OnSelect() {
        if (!m_items.empty() && m_items[m_index].action) {
            m_items[m_index].action();
        }
    }

    void OpenLocalLibrary() {
        const auto packs = account_user::ListUserPacks();
        if (packs.empty()) {
            App::Push<OptionBox>("No user backups found under /config/kefir/account_backups."_i18n, "OK"_i18n);
            return;
        }
        OpenPacksRestore(packs, true);
    }

    void OpenBrowseFolder() {
        auto browser = std::make_unique<filebrowser::Menu>(MenuFlag_None);
        browser->SetFolderPicker([this](const fs::FsPath& folder) {
            m_pending_browse_folder = folder;
        }, "Select user backup folder"_i18n, "Restore user backups from this folder?"_i18n);
        App::Push(std::move(browser));
    }

    void OpenPacksRestore(std::vector<account_user::Pack> packs, bool allow_delete) {
        OpenRestoreLibrary(std::move(packs), [this](auto picked) {
            if (!picked || picked->empty()) {
                return;
            }
            if (m_on_restore) {
                m_on_restore(std::move(*picked));
            }
        }, allow_delete);
    }

    void ProbeOtherConsole() {
        ConnectConsoleTransfer([this](const std::string& base_url) {
            auto list_ok = std::make_shared<bool>(false);
            auto remote_entries = std::make_shared<std::vector<RemoteUserPacksEntry>>();
            auto on_restore = m_on_restore;

            App::Push<ProgressBox>(
                0,
                "Fetching backup list..."_i18n,
                "",
                [base_url, list_ok, remote_entries](auto pbox) -> Result {
                    curl::Api list_api;
                    list_api.SetOption(curl::Url{base_url + "/list"});
                    list_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                        return !pbox->ShouldExit();
                    }});

                    const auto list_res = curl::ToMemory(list_api);
                    if (pbox->ShouldExit()) {
                        return Result_TransferCancelled;
                    }
                    if (!list_res.success || list_res.data.empty()) {
                        return Result_FsInvalidType;
                    }

                    const std::string list_json(list_res.data.begin(), list_res.data.end());
                    const auto parsed = ParseRemoteListResponse(list_json);
                    if (!parsed) {
                        return Result_FsInvalidType;
                    }

                    *list_ok = true;
                    const auto& candidates = parsed->second;
                    for (const auto& cand : candidates) {
                        if (pbox->ShouldExit()) {
                            return Result_TransferCancelled;
                        }
                        pbox->SetTransfer(cand.name);

                        if (cand.is_archive) {
                            if (cand.size <= 0) {
                                continue;
                            }
                            RemoteUserPacksEntry item;
                            item.pack.folder_name = cand.name;
                            item.pack.dir = cand.remote_path;
                            item.pack.is_archive = true;
                            item.pack.remote_size = cand.size;
                            item.pack.created_label = account_user::FormatPackCreated(cand.name, {});

                            std::string base_name = cand.name;
                            if (base_name.ends_with(".kefir-user.zip")) {
                                base_name.erase(base_name.size() - 15);
                            }
                            const auto first_under = base_name.find('_');
                            const auto last_under = base_name.rfind('_');
                            if (first_under != std::string::npos && last_under != std::string::npos && last_under > first_under) {
                                const auto second_under = base_name.find('_', first_under + 1);
                                if (second_under != std::string::npos && last_under > second_under) {
                                    item.pack.nickname = base_name.substr(second_under + 1, last_under - second_under - 1);
                                } else {
                                    item.pack.nickname = base_name.substr(first_under + 1, last_under - first_under - 1);
                                }
                                std::string status = base_name.substr(last_under + 1);
                                if (status == "linked") {
                                    item.pack.link_valid = true;
                                }
                            }
                            if (item.pack.nickname.empty()) {
                                item.pack.nickname = "User";
                            }
                            remote_entries->push_back(std::move(item));
                            continue;
                        }

                        RemoteUserPacksEntry item;
                        item.pack.folder_name = cand.name;
                        item.pack.dir = cand.remote_path;

                        const std::string prof_url = base_url + "/download?path=" + curl::EscapeString(cand.remote_path + "/profile.json");
                        curl::Api prof_api;
                        prof_api.SetOption(curl::Url{prof_url});
                        prof_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                            return !pbox->ShouldExit();
                        }});
                        const auto prof_res = curl::ToMemory(prof_api);
                        if (prof_res.success && !prof_res.data.empty()) {
                            const std::string prof_json(prof_res.data.begin(), prof_res.data.end());
                            item.pack.nickname = account_user::ReadJsonField(prof_json, "nickname");
                            item.pack.uid_hex = account_user::ReadJsonField(prof_json, "uid");
                            const std::string created = account_user::ReadJsonField(prof_json, "created");
                            item.pack.created_label = account_user::FormatPackCreated(cand.name, created);
                            const std::string link_status = account_user::ReadJsonField(prof_json, "link_status");
                            item.pack.link_valid = (link_status == "linked");
                        }
                        if (item.pack.created_label.empty()) {
                            item.pack.created_label = account_user::FormatPackCreated(cand.name, {});
                        }
                        if (item.pack.nickname.empty()) {
                            item.pack.nickname = "User";
                        }

                        const std::string manifest_url = base_url + "/list-recursive?path=" + curl::EscapeString(cand.remote_path);
                        curl::Api m_api;
                        m_api.SetOption(curl::Url{manifest_url});
                        m_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                            return !pbox->ShouldExit();
                        }});
                        const auto m_res = curl::ToMemory(m_api);
                        if (m_res.success && !m_res.data.empty()) {
                            const std::string m_json(m_res.data.begin(), m_res.data.end());
                            const auto files_opt = ParseManifestResponse(m_json, cand.remote_path);
                            if (files_opt) {
                                for (const auto& f : *files_opt) {
                                    if (f.rel_path == "avatar.jpg") {
                                        item.pack.has_avatar = true;
                                    } else if (f.rel_path == "pdm/PlayEvent.dat" || f.rel_path == "PlayEvent.dat") {
                                        item.pack.has_playtime = true;
                                    } else if (!item.pack.link_valid && (f.rel_path.starts_with("baas/") || f.rel_path.starts_with("nas/"))) {
                                        item.pack.link_valid = true;
                                    }
                                }
                            }
                        }

                        if (item.pack.has_avatar) {
                            const std::string av_url = base_url + "/download?path=" + curl::EscapeString(cand.remote_path + "/avatar.jpg");
                            curl::Api av_api;
                            av_api.SetOption(curl::Url{av_url});
                            av_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                                return !pbox->ShouldExit();
                            }});
                            const auto av_res = curl::ToMemory(av_api);
                            if (av_res.success && !av_res.data.empty()) {
                                item.avatar_bytes = std::move(av_res.data);
                            }
                        }

                        remote_entries->push_back(std::move(item));
                    }

                    return Result_Success;
                },
                [this, base_url, list_ok, remote_entries, on_restore](Result rc) {
                    if (rc == Result_TransferCancelled) {
                        return;
                    }
                    if (R_FAILED(rc) || !*list_ok) {
                        App::Push<OptionBox>(
                            "Could not retrieve the user backup list from the sending console."_i18n,
                            "OK"_i18n
                        );
                        return;
                    }
                    if (remote_entries->empty()) {
                        App::Push<OptionBox>(
                            "No user backups found on the sending console."_i18n,
                            "OK"_i18n
                        );
                        return;
                    }

                    OpenRemoteUserPacks(base_url, std::move(*remote_entries), on_restore);
                }
            );
        });
    }

private:
    Callback m_on_restore;
    std::vector<RestoreSourceItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    std::optional<fs::FsPath> m_pending_browse_folder;
};

} // namespace

void Menu::ConfirmRestoreBackup() {
    App::Push<RestoreSourceMenu>([this](std::vector<account_user::Pack> picked) {
        ConfirmPickedRestorePacks(std::move(picked));
    });
}

void Menu::ConfirmPickedRestorePacks(std::vector<account_user::Pack> picked) {
    if (picked.empty()) {
        return;
    }
    u32 new_slots = 0;
    u32 replace_slots = 0;
    std::string existing_name;
    for (const auto& p : picked) {
        if (const auto live = FindLiveUidForPack(p)) {
            replace_slots++;
            if (existing_name.empty()) {
                existing_name = LiveNameForUid(*live);
                if (existing_name.empty()) {
                    existing_name = p.nickname;
                }
            }
        } else {
            new_slots++;
        }
    }
    const auto available_slots = ACC_USER_LIST_SIZE - m_items.size();
    if (new_slots > available_slots) {
        App::Push<OptionBox>(
            "Cannot restore: selecting " + std::to_string(new_slots) +
            " new profile(s) would exceed the maximum of 8 users (available: " +
            std::to_string(available_slots) + ")."_i18n, "OK"_i18n);
        return;
    }
    bool any_new_link = false;
    for (const auto& p : picked) {
        if (p.link_valid && !FindLiveUidForPack(p)) {
            any_new_link = true;
            break;
        }
    }
    std::string gate_reason;
    if (any_new_link && account_link::IsLinkGated(gate_reason)) {
        App::Push<OptionBox>(gate_reason, "OK"_i18n);
        return;
    }
    auto picked_packs = std::move(picked);
    auto go = [this, picked_packs](auto op) mutable {
        if (op && *op == 1) {
            RunPrepareRestoreSnapshot(std::move(picked_packs));
        }
    };
    if (replace_slots && !new_slots) {
        App::Push<OptionBox>(
            "This user is already on this console"_i18n +
            (existing_name.empty() ? std::string(".") : (" (" + existing_name + ").")) + "\n\n" +
            "Restore will replace that profile: name and avatar only. The existing Nintendo Account link is left as-is. It will not add a second user.\n\n"
            "We still copy the current account save to SD first, in case something goes wrong."_i18n,
            "Cancel"_i18n, "Replace"_i18n, 1, std::move(go));
        return;
    }
    if (replace_slots && new_slots) {
        App::Push<OptionBox>(
            std::to_string(replace_slots) + " " +
            "backup(s) match accounts already on this console and will replace them. "_i18n +
            std::to_string(new_slots) + " " +
            "will be added as new profiles.\n\n"
            "We copy the current account save to SD first, in case something goes wrong."_i18n,
            "Cancel"_i18n, "Continue"_i18n, 1, std::move(go));
        return;
    }
    App::Push<OptionBox>(
        "Restore will add a profile and may change the account save (0010).\n\n"
        "Before that, we copy the raw 0010 save file to SD. That file is the rollback if something goes wrong.\n\n"
        "• If Hub can read it now, the copy happens here.\n"
        "• If not, TegraExplorer will copy it automatically after OK.\n\n"
        "Then open Kefir Hub yourself to continue the restore."_i18n,
        "Cancel"_i18n, "Continue"_i18n, 1, std::move(go));
}

void Menu::RunPrepareRestoreSnapshot(std::vector<account_user::Pack> packs) {
    if (packs.empty()) {
        return;
    }
    auto dirs = std::make_shared<std::vector<std::string>>();
    for (const auto& p : packs) {
        dirs->push_back(p.dir);
    }
    auto live_ok = std::make_shared<bool>(false);
    App::Push<ProgressBox>(0, "Snapshot account save"_i18n, "Snapshot account save"_i18n,
        [dirs, live_ok](auto pbox) -> Result {
            pbox->NewTransfer("Copying 0010"_i18n);
            account_restore::InstallRestoreTeScripts();
            const auto dump_rc = account_restore::Dump0010ReadOnly(pbox);
            if (R_SUCCEEDED(dump_rc) && account_restore::SnapshotOk()) {
                *live_ok = true;
                R_TRY(account_restore::SavePending(*dirs, "ready", true));
                R_SUCCEED();
            }
            log_write("[RESTORE] live 0010 dump failed 0x%X, TE fallback\n", dump_rc);
            R_TRY(account_restore::WriteNandFlag());
            R_TRY(account_restore::SavePending(*dirs, "wait_dump", false));
            R_SUCCEED();
        },
        [live_ok, packs = std::move(packs)](Result rc) mutable {
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Could not prepare the 0010 snapshot."_i18n, "OK"_i18n);
                return;
            }
            if (*live_ok) {
                log_write("[RESTORE] live dump ok; continuing restore in same session\n");
                StartRestoreBackup(std::move(packs));
                return;
            }
            App::Push<OptionBox>(
                "Hub could not copy the raw account save while the system is running. Horizon is holding it.\n\n"
                "We still need that file before restore. It is the rollback if the console later fails to boot.\n\n"
                "After OK:\n"
                "• TegraExplorer starts and copies 8000000000000010 by itself.\n"
                "• When it finishes, the console returns to CFW.\n"
                "• Open Kefir Hub yourself. We will continue the restore."_i18n,
                "OK"_i18n,
                [](auto op) {
                    if (!op) {
                        return;
                    }
                    if (!account_restore::LaunchTegraDump()) {
                        App::Push<OptionBox>(
                            "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try Restore Backup again."_i18n,
                            "OK"_i18n);
                    }
                });
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

auto OfferPendingRestore() -> bool {
    static bool s_offered = false;
    if (s_offered) {
        return false;
    }
    account_restore::ClearReopenHubHint();
    auto pending = account_restore::LoadPending();
    if (pending.rolled_back) {
        s_offered = true;
        account_restore::ClearPending();
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
                account_restore::CleanDumpHandshake();
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
            "TegraExplorer did not finish the dump.\n\nAfter OK, TegraExplorer will try again."_i18n,
            "OK"_i18n,
            [](auto op) {
                if (!op) {
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
                "The account save dump is not on SD yet.\n\n"
                "After OK, TegraExplorer will dump 0010 automatically.\n"
                "When it finishes, open Kefir Hub yourself to continue the restore."_i18n,
                "OK"_i18n,
                [](auto op) {
                    if (!op) {
                        return;
                    }
                    if (!account_restore::LaunchTegraDump()) {
                        App::Push<OptionBox>(
                            "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try again."_i18n,
                            "OK"_i18n);
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
        App::Push<OptionBox>("Pending restore packs are missing from SD."_i18n, "OK"_i18n);
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
                account_restore::ClearPending();
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
