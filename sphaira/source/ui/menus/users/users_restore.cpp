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
        m_list->Draw(vg, theme, m_items.size(), m_index, [vg, theme, this](auto*, auto*, Vec4 v, auto i) {
            const auto& item = m_items[i];
            const bool focused = (m_index == static_cast<s64>(i));
            if (focused) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }
            gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f - 10.f, 18.f,
                theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
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

                    R_SUCCEED();
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


} // namespace sphaira::ui::menu::users
