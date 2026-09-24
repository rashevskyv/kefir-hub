#include "ui/menus/users_menu.hpp"
#include "ui/menus/users/users_nand_library.hpp"
#include "ui/menus/users/users_manage_internal.hpp"

#include "account/account_user.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {
namespace {

struct ManageBackupsSourceItem {
    std::string label;
    std::string description;
    std::function<void()> action;
};

struct ManageBackupsSourceMenu final : MenuBase {
    ManageBackupsSourceMenu(std::function<void(std::vector<account_user::Pack>)> on_restore_user,
                            std::function<void(const std::string&, bool)> on_restore_nand)
        : MenuBase{"Manage Backups"_i18n, MenuFlag_None}
        , m_on_restore_user{std::move(on_restore_user)}
        , m_on_restore_nand{std::move(on_restore_nand)}
    {
        m_items = {
            {
                "Backup user"_i18n,
                "Inspect, restore, rename, duplicate, delete or share user backup packs."_i18n,
                [this]() { OpenUserLibrary(m_on_restore_user); }
            },
            {
                "Backup profiles & play hours"_i18n,
                "Browse profiles & play hours packs under /config/kefir/nand_transfer."_i18n,
                [this]() { OpenNandLibrary(); }
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

    auto GetShortTitle() const -> const char* override { return "Backups"; }

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

    void OpenNandLibrary() {
        OpenNandPackLibrary(m_on_restore_nand);
    }

    std::vector<ManageBackupsSourceItem> m_items;
    std::function<void(std::vector<account_user::Pack>)> m_on_restore_user;
    std::function<void(const std::string&, bool)> m_on_restore_nand;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace

void Menu::OpenManageBackups() {
    App::Push<ManageBackupsSourceMenu>(
        [this](std::vector<account_user::Pack> picked) {
            ConfirmPickedRestorePacks(std::move(picked));
        },
        [this](const std::string& dir, bool restore_play_hours) {
            RunNandRestore(dir, restore_play_hours);
        });
}

} // namespace sphaira::ui::menu::users
