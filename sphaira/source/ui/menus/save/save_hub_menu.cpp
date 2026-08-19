#include "ui/menus/save/save_hub_menu.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "stb_image.h"
#include <algorithm>

namespace sphaira::ui::menu::save {
namespace {

constexpr const u8 ICON_INSTALLED[]{
    #embed <icons/game-hub.png>
};

constexpr const u8 ICON_DELETED[]{
    #embed <icons/software.png>
};

constexpr const u8 ICON_BACKUPS[]{
    #embed <icons/saves.png>
};

auto CreateTexture(NVGcontext* vg, const u8* data, std::size_t size) -> int {
    if (!data || !size) {
        return 0;
    }

    int width{};
    int height{};
    int channels{};
    auto* decoded = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (!decoded) {
        return 0;
    }

    const auto texture = nvgCreateImageRGBA(vg, width, height, 0, decoded);
    stbi_image_free(decoded);
    return texture;
}

} // namespace

SaveHubMenu::SaveHubMenu(u32 flags) : MenuBase{"Saves"_i18n, flags} {
    m_items = {
        { "Installed Games"_i18n, "View and manage saves for currently installed games."_i18n, 0, [](){
            App::Push<ui::menu::save::Menu>(MenuFlag_None, 0, ui::menu::save::Category::Installed);
        }},
        { "Deleted Games"_i18n, "View and manage saves for uninstalled games."_i18n, 0, [](){
            App::Push<ui::menu::save::Menu>(MenuFlag_None, 0, ui::menu::save::Category::Deleted);
        }},
        { "Backups"_i18n, "Browse and restore save backups on SD and external storage."_i18n, 0, [](){
            App::Push<ui::menu::save::Menu>(MenuFlag_None, 0, ui::menu::save::Category::Backups);
        }},
    };

    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }})
    );

    const Vec2 pad{10.f, 10.f};
    const Vec4 v{75.f, 110.f, 370.f, 155.f};
    m_list = std::make_unique<List>(3, 3, m_pos, v, pad);
    m_list->SetLayout(List::Layout::GRID);

    LoadIcons();
    SetIndex(0);
}

SaveHubMenu::~SaveHubMenu() {
    if (auto* vg = App::GetVg()) {
        for (auto& item : m_items) {
            if (item.icon_texture) {
                nvgDeleteImage(vg, item.icon_texture);
                item.icon_texture = 0;
            }
        }
    }
}

void SaveHubMenu::Update(Controller* controller, TouchInfo* touch) {
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

void SaveHubMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& item = m_items[i];
        const auto selected = m_index == i;

        DrawElement(v, ThemeEntryID_GRID);
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        const Vec4 icon_box{v.x + 20.f, v.y + 20.f, 115.f, 115.f};
        gfx::drawRect(vg, icon_box, nvgRGBA(52, 52, 52, 255), 5.f);
        if (item.icon_texture) {
            gfx::drawImage(vg, icon_box, item.icon_texture, 5.f);
        } else {
            gfx::drawImage(vg, icon_box, App::GetDefaultImage(), 5.f);
        }

        const auto title_colour = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const float text_x = v.x + 148.f;
        const float text_w = v.w - 178.f;
        const float icon_top = icon_box.y;
        const float icon_h = icon_box.h;

        nvgFontSize(vg, 20.f);
        nvgTextLineHeight(vg, 1.0f);
        float title_bounds[4];
        nvgTextBoxBounds(vg, text_x, 0, text_w, item.label.c_str(), nullptr, title_bounds);
        const float title_h = title_bounds[3] - title_bounds[1];

        nvgFontSize(vg, 13.f);
        nvgTextLineHeight(vg, 1.0f);
        float desc_bounds[4];
        nvgTextBoxBounds(vg, text_x, 0, text_w, item.description.c_str(), nullptr, desc_bounds);
        const float desc_h = desc_bounds[3] - desc_bounds[1];

        const float gap = (icon_h - title_h - desc_h) / 3.f;
        const float title_y = icon_top + gap;
        const float desc_y = title_y + title_h + gap;

        gfx::drawTextBox(
            vg, text_x, title_y, 20.f, text_w,
            theme->GetColour(title_colour), item.label.c_str()
        );
        gfx::drawTextBox(
            vg, text_x, desc_y, 13.f, text_w,
            theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
        );
    });
}

void SaveHubMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void SaveHubMenu::LoadIcons() {
    auto* vg = App::GetVg();
    if (!vg) {
        return;
    }

    struct IconData {
        const u8* data;
        std::size_t size;
    };

    constexpr IconData icons[] = {
        { ICON_INSTALLED, sizeof(ICON_INSTALLED) },
        { ICON_DELETED, sizeof(ICON_DELETED) },
        { ICON_BACKUPS, sizeof(ICON_BACKUPS) },
    };

    const auto count = std::min(m_items.size(), std::size(icons));
    for (std::size_t i = 0; i < count; i++) {
        m_items[i].icon_texture = CreateTexture(vg, icons[i].data, icons[i].size);
    }
}

void SaveHubMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }

    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (m_index < static_cast<s64>(m_items.size())) {
        const auto& item = m_items[m_index];
        SetSubHeading(item.label + " - " + item.description);
    }
    m_scroll_name.Reset();
}

void SaveHubMenu::OnSelect() {
    if (m_items.empty()) {
        return;
    }

    m_items[m_index].action();
}

} // namespace sphaira::ui::menu::save
