#include "ui/menus/users_menu.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "utils/utils.hpp"

#include <algorithm>

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
            const auto status = item.linked ? "Linked"_i18n : "Local"_i18n;
            const auto status_colour = item.linked
                ? nvgRGBA(76, 190, 120, 255)
                : theme->GetColour(ThemeEntryID_TEXT_INFO);
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
        options->Add<SidebarEntryCallback>("Link Nintendo Account"_i18n, [this](){
            ConfirmLink(false);
        }, true, "Write a fake Nintendo Account into this profile (Linkalho method). Not a real eShop login."_i18n);
        options->Add<SidebarEntryCallback>("Unlink Nintendo Account"_i18n, [this](){
            ConfirmUnlink(false);
        }, true, "Remove the injected Nintendo Account from this profile."_i18n);
    }
    options->Add<SidebarEntryCallback>("Link all"_i18n, [this](){
        ConfirmLink(true);
    }, true, "Inject a fake Nintendo Account into every profile."_i18n);
    options->Add<SidebarEntryCallback>("Unlink all"_i18n, [this](){
        ConfirmUnlink(true);
    }, true, "Remove injected Nintendo Accounts from every profile."_i18n);
}

void Menu::ConfirmLink(bool all) {
    App::Push<OptionBox>(
        "This writes a fake Nintendo Account into the profile save (same method as Linkalho). It is not a real eShop login. A reboot is required."_i18n,
        "Cancel"_i18n, "Link"_i18n, 1,
        [this, all](auto op) {
            if (op && *op == 1) {
                Run(true, all);
            }
        });
}

void Menu::ConfirmUnlink(bool all) {
    App::Push<OptionBox>(
        "Remove the injected Nintendo Account from the selected profile(s)? A reboot is required."_i18n,
        "Cancel"_i18n, "Unlink"_i18n, 1,
        [this, all](auto op) {
            if (op && *op == 1) {
                Run(false, all);
            }
        });
}

void Menu::Run(bool link, bool all) {
    std::vector<AccountUid> uids;
    if (all) {
        for (const auto& u : m_items) {
            uids.push_back(u.uid);
        }
    } else if (!m_items.empty()) {
        uids.push_back(m_items[m_index].uid);
    }
    if (uids.empty()) {
        return;
    }

    const auto title = link ? "Link Nintendo Account"_i18n : "Unlink Nintendo Account"_i18n;
    App::Push<ProgressBox>(0, title, title, [link, uids](auto pbox) -> Result {
        pbox->NewTransfer(link ? "Writing account save"_i18n : "Updating account save"_i18n);
        if (link) {
            R_TRY(account_link::LinkUsers(uids));
        } else {
            R_TRY(account_link::UnlinkUsers(uids));
        }
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

} // namespace sphaira::ui::menu::users
