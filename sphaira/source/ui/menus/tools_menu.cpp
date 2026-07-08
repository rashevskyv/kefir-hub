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

#include "web.hpp"
#include "ui/progress_box.hpp"

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

constexpr const u8 ICON_NETWORK[]{
    #embed <icons/network.png>
};

constexpr const u8 ICON_GAME_HUB[]{
    #embed <icons/game-hub.png>
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

void StartShareServerFromTools(bool screenshots) {
    WebShareResult result;
    Result rc;
    if (screenshots) {
        rc = WebShareScreenshots(result);
    } else {
        rc = WebShareFolder("/", result);
    }

    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, screenshots ? "Failed to start screenshot server"_i18n : "Failed to start web server"_i18n);
        return;
    }

    const auto url = result.url;
    const auto qr_image = result.qr_image;
    App::Push<ProgressBox>(qr_image, screenshots ? "Screenshot Gallery"_i18n : "Web Sharing Server"_i18n, url,
        [url](auto pbox) -> Result {
            pbox->NewTransferForce("Press B to Stop Server"_i18n);
            WebSetProgressBox(pbox);
            std::string last_name;
            while (!pbox->ShouldExit() && WebShareIsRunning()) {
                const auto state = WebGetUploadState();
                if (state.active) {
                    if (state.name != last_name) {
                        last_name = state.name;
                        pbox->NewTransferForce(state.name);
                    }
                    pbox->UpdateTransferForce(state.bytes, state.total);
                } else if (!last_name.empty()) {
                    const std::string completed_name = last_name;
                    last_name.clear();
                    pbox->ResetTransferProgress();
                    pbox->SetTitle(url);
                    if (completed_name.starts_with("Installing:")) {
                        pbox->NewTransferForce("Installation completed"_i18n);
                    } else {
                        pbox->NewTransferForce("Upload completed"_i18n);
                    }
                    for (int i = 0; i < 30 && !WebGetUploadState().active && !pbox->ShouldExit(); ++i) {
                        svcSleepThread(100'000'000LL);
                    }
                    if (!WebGetUploadState().active && !pbox->ShouldExit()) {
                        pbox->NewTransferForce("Press B to Stop Server"_i18n);
                    }
                }
                svcSleepThread(100'000'000LL);
            }
            WebSetProgressBox(nullptr);
            WebShareStop();
            R_SUCCEED();
        },
        [qr_image](Result) {
            nvgDeleteImage(App::GetVg(), qr_image);
        }
    );
}

} // namespace

Menu::Menu() : MenuBase{"Tools"_i18n, MenuFlag_Tab} {
    m_items = {
        { "Updater"_i18n, "Update Kefir, firmware and network downloads."_i18n, 0, [](){
            App::Push<ui::menu::kefir::Menu>();
        }},
        { "Kefir Settings"_i18n, "Fan curves and console-specific Kefir switches."_i18n, 0, [](){
            App::Push<ui::menu::settings::KefirSettingsMenu>();
        }},
        { "Cheats"_i18n, "Download and manage cheat databases."_i18n, 0, [](){
            App::Push<ui::menu::hats::CheatsMenu>();
        }},
        { "App Store"_i18n, "Download and update homebrew apps."_i18n, 0, [](){
            App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
        }},
        { "File Browser"_i18n, "Browse and manage SD card files."_i18n, 0, [](){
            App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
        }},
        { "Saves"_i18n, "Backup and restore save data."_i18n, 0, [](){
            App::Push<ui::menu::save::Menu>(MenuFlag_None);
        }},
        { "Software"_i18n, "Install DBI and mod utilities."_i18n, 0, [](){
            App::Push<ui::menu::settings::SoftwareMenu>();
        }},
        { "Themes"_i18n, "Download and install theme packs."_i18n, 0, [](){
            App::Push<ui::menu::settings::ThemesMenu>();
        }},
        { "Settings"_i18n, "Open Sphaira application settings."_i18n, 0, [](){
            App::Push<ui::menu::settings::Menu>();
        }},
        { "Start Web Server"_i18n, "Start a local web server to upload/download files."_i18n, 0, [](){
            StartShareServerFromTools(false);
        }},
        { "Screenshots"_i18n, "View and manage console screenshots in a browser."_i18n, 0, [](){
            StartShareServerFromTools(true);
        }},
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }})
    );

    const Vec2 pad{10.f, 10.f};
    const Vec4 v{75.f, 110.f, 370.f, 155.f};
    m_list = std::make_unique<List>(3, 11, m_pos, v, pad);
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
        const float text_x = v.x + 148.f;
        const float text_w = v.w - 178.f;
        const float icon_top = icon_box.y;
        const float icon_h = icon_box.h;

        // measure title block height
        nvgFontSize(vg, 20.f);
        nvgTextLineHeight(vg, 1.0f);
        float title_bounds[4];
        nvgTextBoxBounds(vg, text_x, 0, text_w, item.label.c_str(), nullptr, title_bounds);
        const float title_h = title_bounds[3] - title_bounds[1];

        // measure description block height
        nvgFontSize(vg, 13.f);
        nvgTextLineHeight(vg, 1.0f);
        float desc_bounds[4];
        nvgTextBoxBounds(vg, text_x, 0, text_w, item.description.c_str(), nullptr, desc_bounds);
        const float desc_h = desc_bounds[3] - desc_bounds[1];

        // equal spacing: gap = (icon_h - title_h - desc_h) / 3
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
    m_items[9].icon_texture = LoadIcon(vg, ICON_NETWORK, sizeof(ICON_NETWORK));
    m_items[10].icon_texture = LoadIcon(vg, ICON_GAME_HUB, sizeof(ICON_GAME_HUB));
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
