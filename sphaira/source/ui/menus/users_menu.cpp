#include "ui/menus/users_menu.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace sphaira::ui::menu::users {

Menu::Menu() : MenuBase{"Users"_i18n, MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Options"_i18n, [this](){ ShowContextMenu(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){ Refresh(); }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){ ShowContextMenu(); }})
    );

    m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 70.f});
    m_list->SetLayout(List::Layout::GRID);
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_items.empty()) {
        Refresh();
    }
}

void Menu::Refresh() {
    m_items = account_link::ListUsers();
    SetIndex(m_index);
}

void Menu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        SetTitleSubHeading("No user profiles"_i18n, true);
        SetSubHeading("");
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetTitleSubHeading(m_items[m_index].uid_hex, true);
    SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_items.size()));
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    if (m_items.empty()) {
        return;
    }
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_items.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No user profiles"_i18n.c_str());
        return;
    }

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& item = m_items[i];
        const auto selected = m_index == i;
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_items.size() - 1) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        gfx::drawTextArgs(vg, x + 20.f, y + h / 2.f - 10.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", item.nickname.c_str());
        gfx::drawTextArgs(vg, x + 20.f, y + h / 2.f + 14.f, 13.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", item.uid_hex.c_str());

        if (item.linked_known) {
            std::string status = "Local"_i18n;
            NVGcolor status_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
            if (item.kind == account_link::LinkKind::Official) {
                status = "Linked"_i18n;
                status_colour = nvgRGBA(76, 190, 120, 255);
            } else if (item.kind == account_link::LinkKind::Offline) {
                status = "Offline stub"_i18n;
                status_colour = nvgRGBA(230, 160, 60, 255);
            }
            gfx::drawTextArgs(vg, x + w - 20.f, y + h / 2.f, 16.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                status_colour, "%s", status.c_str());
        }
    });
}

void Menu::ShowContextMenu() {
    auto options = std::make_unique<Sidebar>(
        m_items.empty() ? "Users"_i18n : m_items[m_index].nickname, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Import official link"_i18n, [this](){
            ConfirmImport(false);
        }, true, "Copy baas/nas (including Nintendo tokens) from a sysNAND dump onto this profile."_i18n);
        options->Add<SidebarEntryCallback>("Unlink Nintendo Account"_i18n, [this](){
            ConfirmUnlink(false);
        }, true, "Remove the Nintendo Account data from this profile."_i18n);
        options->Add<SidebarEntryCallback>("Offline stub (Linkalho)"_i18n, [this](){
            ConfirmOffline(false);
        }, true, "Write fake baas/nas IDs with no Nintendo tokens. Games may retry Nintendo servers."_i18n);
    }
    options->Add<SidebarEntryCallback>("Export account save"_i18n, [this](){
        ConfirmExport();
    }, true, "Write this NAND's baas/nas to SD so you can import them on emuNAND."_i18n);
    options->Add<SidebarEntryCallback>("Import official link to all"_i18n, [this](){
        ConfirmImport(true);
    }, true, "Graft the dumped Nintendo Account onto every profile."_i18n);
    options->Add<SidebarEntryCallback>("Unlink all"_i18n, [this](){
        ConfirmUnlink(true);
    }, true, "Remove Nintendo Account data from every profile."_i18n);
}

void Menu::ConfirmImport(bool all) {
    App::Push<OptionBox>(
        "Pick a folder dumped from a console that was linked officially (usually sysNAND). It must contain baas/ and nas/, including id.token and refresh.token. Y selects the folder. This is the method that does not keep retrying Nintendo servers."_i18n,
        "Cancel"_i18n, "Choose folder"_i18n, 1,
        [this, all](auto op) {
            if (!op || *op != 1) {
                return;
            }
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[this, all](const fs::FsPath& path, const filebrowser::FsEntry&) -> bool {
                    RunImport(all, path.toString());
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{},
                true);
        });
}

void Menu::ConfirmOffline(bool all) {
    App::Push<OptionBox>(
        "Offline stub writes random Nintendo IDs with no real tokens (same as Linkalho). Horizon then retries Nintendo servers, which shows up as Please wait / airplane-mode nags. Prefer Import official link. A reboot is required."_i18n,
        "Cancel"_i18n, "Write stub"_i18n, 0,
        [this, all](auto op) {
            if (op && *op == 1) {
                RunOffline(all);
            }
        });
}

void Menu::ConfirmUnlink(bool all) {
    App::Push<OptionBox>(
        "Remove the Nintendo Account from the selected profile(s)? A reboot is required."_i18n,
        "Cancel"_i18n, "Unlink"_i18n, 1,
        [this, all](auto op) {
            if (op && *op == 1) {
                RunUnlink(all);
            }
        });
}

void Menu::ConfirmExport() {
    App::Push<OptionBox>(
        "Export baas/ and nas/ from this NAND to SD. On a clean sysNAND with a real Nintendo Account, export here, then import that folder on emuNAND."_i18n,
        "Cancel"_i18n, "Export"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunExport();
            }
        });
}

auto Menu::SelectedUids(bool all) const -> std::vector<AccountUid> {
    std::vector<AccountUid> uids;
    if (all) {
        for (const auto& u : m_items) {
            uids.push_back(u.uid);
        }
    } else if (!m_items.empty()) {
        uids.push_back(m_items[m_index].uid);
    }
    return uids;
}

void Menu::RunUnlink(bool all) {
    const auto uids = SelectedUids(all);
    if (uids.empty()) {
        return;
    }

    App::Push<ProgressBox>(0, "Unlink Nintendo Account"_i18n, "Unlink Nintendo Account"_i18n, [uids](auto pbox) -> Result {
        pbox->NewTransfer("Updating account save"_i18n);
        R_TRY(account_link::UnlinkUsers(uids));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>(
                "Could not update the account save. Close other homebrew and try again."_i18n,
                "OK"_i18n);
            return;
        }
        App::Push<OptionBox>(
            "Account data updated. Reboot for the change to apply."_i18n,
            "Later"_i18n, "Reboot"_i18n, 1,
            [](auto op) {
                if (op && *op == 1) {
                    utils::requestForcedReboot();
                }
            });
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunOffline(bool all) {
    const auto uids = SelectedUids(all);
    if (uids.empty()) {
        return;
    }

    App::Push<ProgressBox>(0, "Offline stub (Linkalho)"_i18n, "Offline stub (Linkalho)"_i18n, [uids](auto pbox) -> Result {
        pbox->NewTransfer("Writing account save"_i18n);
        R_TRY(account_link::LinkUsers(uids));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>(
                "Could not update the account save. Close other homebrew and try again."_i18n,
                "OK"_i18n);
            return;
        }
        App::Push<OptionBox>(
            "Offline stub written. Games that only check for a linked account may work, but Horizon can keep retrying Nintendo. Reboot required."_i18n,
            "Later"_i18n, "Reboot"_i18n, 1,
            [](auto op) {
                if (op && *op == 1) {
                    utils::requestForcedReboot();
                }
            });
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunImport(bool all, const std::string& dump_dir) {
    const auto uids = SelectedUids(all);
    if (uids.empty()) {
        return;
    }

    auto had_tokens = std::make_shared<bool>(false);
    App::Push<ProgressBox>(0, "Import official link"_i18n, "Import official link"_i18n,
        [uids, dump_dir, had_tokens](auto pbox) -> Result {
            pbox->NewTransfer("Importing account save"_i18n);
            R_TRY(account_link::ImportOfficialLink(uids, dump_dir, *had_tokens));
            R_SUCCEED();
        }, [this, had_tokens](Result rc) {
            if (R_FAILED(rc)) {
                const auto msg = (rc == Result_FsInvalidType)
                    ? "No baas/nas dump in that folder. Dump system save 8000000000000010 (JKSV or TegraExplorer) from a console that was linked officially."_i18n
                    : "Could not update the account save. Close other homebrew and try again."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }
            const auto msg = *had_tokens
                ? "Official Nintendo tokens imported. Reboot for the change to apply. Keep emuNAND off Nintendo servers (dns.mitm / prodinfo blank)."_i18n
                : "baas/nas imported, but this dump has no id.token / refresh.token. Horizon can still retry Nintendo servers. Prefer a dump from an officially linked sysNAND."_i18n;
            App::Push<OptionBox>(
                msg,
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op) {
                    if (op && *op == 1) {
                        utils::requestForcedReboot();
                    }
                });
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunExport() {
    auto out_dir = std::make_shared<std::string>();
    App::Push<ProgressBox>(0, "Export account save"_i18n, "Export account save"_i18n,
        [out_dir](auto pbox) -> Result {
            pbox->NewTransfer("Exporting account save"_i18n);
            R_TRY(account_link::ExportAccountSave(*out_dir));
            R_SUCCEED();
        }, [this, out_dir](Result rc) {
            if (R_FAILED(rc)) {
                const auto msg = (rc == Result_FsEmpty)
                    ? "Nothing to export. This NAND has no baas/nas account data."_i18n
                    : "Could not export the account save. Close other homebrew and try again."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }
            App::Push<OptionBox>(
                "Exported to "_i18n + *out_dir,
                "OK"_i18n);
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

} // namespace sphaira::ui::menu::users
