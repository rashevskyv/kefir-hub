#include "ui/menus/tools_menu.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/kefir_menu.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/settings_menu.hpp"

#include "ui/nvg_util.hpp"

#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "stb_image.h"

#include <cstddef>

namespace sphaira::ui::menu::tools {
namespace {

constexpr const u8 ICON_KEFIR[]{
    #embed <icons/update-hats.png>
};

constexpr const u8 ICON_CHEATS[]{
    #embed <icons/cheats.png>
};

constexpr const u8 ICON_APPSTORE[]{
    #embed <icons/app-shop.png>
};

constexpr const u8 ICON_FILE_BROWSER[]{
    #embed <icons/file-browser.png>
};

constexpr const u8 ICON_SAVES[]{
    #embed <icons/default.png>
};

constexpr const u8 ICON_SETTINGS[]{
    #embed <icons/advanced-options.png>
};

auto LoadIcon(NVGcontext* vg, const u8* data, std::size_t size) -> int {
    int width{};
    int height{};
    int channels{};
    auto* decoded = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (!decoded) {
        log_write("failed to decode tools icon\n");
        return 0;
    }

    const auto texture = nvgCreateImageRGBA(vg, width, height, 0, decoded);
    stbi_image_free(decoded);

    if (!texture) {
        log_write("failed to create tools icon texture\n");
    }

    return texture;
}

} // namespace

Menu::Menu() : MenuBase{"Tools", MenuFlag_Tab} {
    m_items = {
        { "Updater", "Update Kefir, firmware and network downloads.", 0, [](){
            App::Push<ui::menu::kefir::Menu>();
        }},
        { "Kefir Settings", "Fan curves and console-specific Kefir switches.", 0, [](){
            App::Push<ui::menu::settings::KefirSettingsMenu>();
        }},
        { "Cheats", "Download and manage cheat databases.", 0, [](){
            App::Push<ui::menu::hats::CheatsMenu>();
        }},
        { "App Store", "Download and update homebrew apps.", 0, [](){
            App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
        }},
        { "File Browser", "Browse and manage SD card files.", 0, [](){
            App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
        }},
        { "Saves", "Backup and restore save data.", 0, [](){
            App::Push<ui::menu::save::Menu>(MenuFlag_None);
        }},
        { "Software", "Install DBI and mod utilities.", 0, [](){
            App::Push<ui::menu::settings::SoftwareMenu>();
        }},
        { "Themes", "Download and install theme packs.", 0, [](){
            App::Push<ui::menu::settings::ThemesMenu>();
        }},
        { "Settings", "Open Sphaira application settings.", 0, [](){
            App::Push<ui::menu::settings::Menu>();
        }},
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }})
    );

    const Vec2 pad{10.f, 10.f};
    const Vec4 v{75.f, 110.f, 370.f, 155.f};
    m_list = std::make_unique<List>(3, 9, m_pos, v, pad);
    m_list->SetLayout(List::Layout::GRID);

    LoadIcons();
    SetIndex(0);
}

Menu::~Menu() {
    if (auto* vg = App::GetVg()) {
        for (auto& item : m_items) {
            if (item.icon_texture) {
                nvgDeleteImage(vg, item.icon_texture);
                item.icon_texture = 0;
            }
        }
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
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

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& item = m_items[i];
        const auto selected = m_index == i;

        DrawElement(v, ThemeEntryID_GRID);
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        const Vec4 icon_box{v.x + 20.f, v.y + 20.f, 115.f, 115.f};
        if (item.icon_texture) {
            gfx::drawImage(vg, icon_box, item.icon_texture, 5.f);
        } else {
            gfx::drawImage(vg, icon_box, App::GetDefaultImage(), 5.f);
        }

        const auto title_colour = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        gfx::drawTextBox(
            vg, v.x + 148.f, v.y + 45.f, 20.f, v.w - 178.f,
            theme->GetColour(title_colour), item.label.c_str()
        );
        gfx::drawTextBox(
            vg, v.x + 148.f, v.y + 80.f, 13.f, v.w - 178.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
        );
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void Menu::LoadIcons() {
    auto* vg = App::GetVg();
    if (!vg) {
        return;
    }

    m_items[0].icon_texture = LoadIcon(vg, ICON_KEFIR, sizeof(ICON_KEFIR));
    m_items[1].icon_texture = LoadIcon(vg, ICON_SETTINGS, sizeof(ICON_SETTINGS));
    m_items[2].icon_texture = LoadIcon(vg, ICON_CHEATS, sizeof(ICON_CHEATS));
    m_items[3].icon_texture = LoadIcon(vg, ICON_APPSTORE, sizeof(ICON_APPSTORE));
    m_items[4].icon_texture = LoadIcon(vg, ICON_FILE_BROWSER, sizeof(ICON_FILE_BROWSER));
    m_items[5].icon_texture = LoadIcon(vg, ICON_SAVES, sizeof(ICON_SAVES));
    m_items[6].icon_texture = LoadIcon(vg, ICON_APPSTORE, sizeof(ICON_APPSTORE));
    m_items[7].icon_texture = LoadIcon(vg, ICON_SETTINGS, sizeof(ICON_SETTINGS));
    m_items[8].icon_texture = LoadIcon(vg, ICON_SETTINGS, sizeof(ICON_SETTINGS));
}

void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    if (!m_items.empty()) {
        const auto& item = m_items[m_index];
        SetSubHeading(item.label + " - " + item.description);
    }
    m_scroll_name.Reset();
}

void Menu::OnSelect() {
    if (m_items.empty()) {
        return;
    }

    m_items[m_index].action();
}

} // namespace sphaira::ui::menu::tools
