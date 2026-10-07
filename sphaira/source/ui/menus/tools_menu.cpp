#include "ui/menus/tools_menu.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/game_menu.hpp"
#include "ui/menus/kefir_menu.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_hub_menu.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/menus/uninstaller_menu.hpp"
#include "ui/menus/users_menu.hpp"
#include "ui/menus/wifi_menu.hpp"
#include "ui/menus/settings/settings_fancurve.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "haze_helper.hpp"
#include "system_cleanup.hpp"
#include "system_info.hpp"
#include "ui/menus/file_viewer.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include "ui/nvg_util.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "nro.hpp"
#include "nacp_util.hpp"
#include "stb_image.h"

#include "web.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"

#include <algorithm>
#include <cstddef>

namespace sphaira::ui::menu::tools {
namespace {

constexpr const u8 ICON_UPDATER[]{
    #embed <icons/updater.png>
};

constexpr const u8 ICON_KEFIR_SETTINGS[]{
    #embed <icons/kefir-settings.png>
};

constexpr const u8 ICON_CHEATS[]{
    #embed <icons/cheats.png>
};

constexpr const u8 ICON_FILE_BROWSER[]{
    #embed <icons/file-browser.png>
};

constexpr const u8 ICON_SAVES[]{
    #embed <icons/saves.png>
};

constexpr const u8 ICON_GAMES[]{
    #embed <icons/game-hub.png>
};

constexpr const u8 ICON_SOFTWARE[]{
    #embed <icons/software.png>
};

constexpr const u8 ICON_THEMES[]{
    #embed <icons/themes.png>
};

constexpr const u8 ICON_SETTINGS[]{
    #embed <icons/settings.png>
};

constexpr const u8 ICON_ADVANCED_OPTIONS[]{
    #embed <icons/advanced-options.png>
};

constexpr const u8 ICON_NETWORK[]{
    #embed <icons/network.png>
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

void ComingSoon() {
    App::Push<ui::OptionBox>("Coming soon"_i18n, "OK"_i18n);
}

void OpenSystemInfo() {
    const auto text = system_info::BuildReport();
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursivelyWithPath(system_info::REPORT_PATH);
    const std::vector<u8> data(text.begin(), text.end());
    if (R_FAILED(sd.write_entire_file(system_info::REPORT_PATH, data))) {
        App::Notify("Could not save the report"_i18n);
        return;
    }
    // nullptr: the viewer uses its own sd fs (sd above dies with this function).
    App::Push<fileview::Menu>(nullptr, system_info::REPORT_PATH, fileview::TextMode::View, false);
}

void OpenCleanup() {
    auto opts = std::make_shared<cleanup::Options>();
    auto sidebar = std::make_unique<Sidebar>("Clean system junk"_i18n, Sidebar::Side::RIGHT);
    sidebar->Add<SidebarEntryBool>("Old game updates"_i18n, opts->old_updates, "Updates older than the newest one installed for the same game."_i18n);
    sidebar->Add<SidebarEntryBool>("Lost content on the SD card"_i18n, opts->orphans_sd, "Game files no installed game uses any more."_i18n);
    sidebar->Add<SidebarEntryBool>("Lost content in system memory"_i18n, opts->orphans_nand, "Game files no installed game uses any more."_i18n);
    sidebar->Add<SidebarEntryBool>("Unfinished installs on the SD card"_i18n, opts->placeholders_sd, "Pieces left by installs that stopped half way."_i18n);
    sidebar->Add<SidebarEntryBool>("Unfinished installs in system memory"_i18n, opts->placeholders_nand, "Pieces left by installs that stopped half way."_i18n);
    sidebar->Add<SidebarEntryBool>("Unused tickets"_i18n, opts->unused_tickets, "Tickets of games that are no longer installed."_i18n);
    sidebar->Add<SidebarEntryBool>("Error reports"_i18n, opts->erpt_reports, "Crash reports in /atmosphere/erpt_reports."_i18n);
    sidebar->Add<SidebarEntryBool>("Folders of removed games"_i18n, opts->contents_folders, "Mods and cheats folders in /atmosphere/contents for games that are no longer on the console. Sysmodules are kept."_i18n);
    sidebar->Add<SidebarEntryBool>("Saves of removed users"_i18n, opts->deleted_user_saves, "Saves of users that are no longer on the console. They cannot be restored afterwards."_i18n);
    sidebar->Add<SidebarEntryCallback>("Run selected"_i18n, [opts](){
        auto report = std::make_shared<cleanup::Report>();
        App::Push<ui::ProgressBox>(0, "Clean system junk"_i18n, "", [opts, report](auto pbox) -> Result {
            return cleanup::Run(pbox, *opts, *report);
        }, [report](Result rc){
            if (R_SUCCEEDED(rc)) {
                App::Notify("Cleaned: "_i18n + std::to_string(report->removed) + " · SD +" + grid::FormatBytes(static_cast<u64>(report->freed_sd)) +
                    " · NAND +" + grid::FormatBytes(static_cast<u64>(report->freed_nand)));
            } else if (rc != Result_TransferCancelled) {
                App::PushErrorBox(rc, "Cleanup failed!"_i18n);
            }
        });
    }, true);
    App::Push(std::move(sidebar));
}

void DrawToolsGrid(NVGcontext* vg, Theme* theme, List& list, s64 selected, const std::vector<ToolItem>& items) {
    list.Draw(vg, theme, items.size(), selected, [vg, theme, selected, &items](auto*, auto*, Vec4 v, auto i) {
        const auto& item = items[i];
        const auto is_selected = selected == static_cast<s64>(i);

        DrawElement(v, ThemeEntryID_GRID);
        if (is_selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        const Vec4 icon_box{v.x + 20.f, v.y + 20.f, 115.f, 115.f};
        gfx::drawRect(vg, icon_box, nvgRGBA(52, 52, 52, 255), 5.f);
        if (item.icon_texture) {
            gfx::drawImage(vg, icon_box, item.icon_texture, 5.f);
        } else {
            gfx::drawImage(vg, icon_box, App::GetDefaultImage(), 5.f);
        }

        const auto title_colour = is_selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
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

// a plain row of the system tools list: label, description, action.
auto Tool(std::string label, std::string description, std::function<void()> action) -> settings::SettingsItem {
    return { std::move(label), std::move(description), {}, std::move(action), settings::SettingsItemKind::Normal };
}

} // namespace

Menu::Menu() : MenuBase{"Tools"_i18n, MenuFlag_Tab} {
    m_items = {
        { "File Browser"_i18n, "Browse and manage SD card files."_i18n, 0, [](){
            App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
        }},
        { "Game Tools"_i18n, "Games, saves and cheats."_i18n, 0, [](){
            App::Push<GameToolsMenu>();
        }},
        { "Themes"_i18n, "Download and install theme packs."_i18n, 0, [](){
            App::Push<ui::menu::settings::ThemesMenu>();
        }},
        { "Updater"_i18n, "Update Kefir and firmware packages."_i18n, 0, [](){
            App::Push<ui::menu::kefir::Menu>();
        }},
        { "Software"_i18n, "App Store, DBI and mod utilities."_i18n, 0, [](){
            App::Push<ui::menu::settings::SoftwareMenu>();
        }},
        { "Tools"_i18n, "System tools and sysmodule manager."_i18n, 0, [](){
            App::Push<SystemToolsMenu>();
        }},
        { "Kefir Settings"_i18n, "Console-specific Kefir switches."_i18n, 0, [](){
            App::Push<ui::menu::settings::KefirSettingsMenu>();
        }},
        { "Settings"_i18n, "Open Kefir Hub application settings."_i18n, 0, [](){
            App::Push<ui::menu::settings::Menu>();
        }},
        { "Console Transfer"_i18n, "Share files, backups and installed content with another device."_i18n, 0, [](){
            App::Push<ConsoleTransferMenu>();
        }},
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::START, Action{"Install & Share"_i18n, [this](){
            DisplayConnectionOptions();
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

    DrawToolsGrid(vg, theme, *m_list, m_index, m_items);
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

    struct IconData {
        const u8* data;
        std::size_t size;
    };

    constexpr IconData icons[] = {
        { ICON_FILE_BROWSER, sizeof(ICON_FILE_BROWSER) },
        { ICON_GAMES, sizeof(ICON_GAMES) },
        { ICON_THEMES, sizeof(ICON_THEMES) },
        { ICON_UPDATER, sizeof(ICON_UPDATER) },
        { ICON_SOFTWARE, sizeof(ICON_SOFTWARE) },
        { ICON_ADVANCED_OPTIONS, sizeof(ICON_ADVANCED_OPTIONS) },
        { ICON_KEFIR_SETTINGS, sizeof(ICON_KEFIR_SETTINGS) },
        { ICON_SETTINGS, sizeof(ICON_SETTINGS) },
        { ICON_NETWORK, sizeof(ICON_NETWORK) },
    };

    const auto count = std::min(m_items.size(), std::size(icons));
    for (std::size_t i = 0; i < count; ++i) {
        m_items[i].icon_texture = LoadIcon(vg, icons[i].data, icons[i].size);
    }
}

void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    if (!m_items.empty()) {
        const auto& item = m_items[m_index];
        SetTitleSubHeading(item.label + " - " + item.description, true);
        SetSubHeading("");
    }
    m_scroll_name.Reset();
}

void Menu::OnSelect() {
    if (m_items.empty()) {
        return;
    }

    m_items[m_index].action();
}

void Menu::DisplayConnectionOptions() {
    auto options = std::make_unique<Sidebar>("Install & Share"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    AddInstallShareOptions(options.get());
    AddSettingsOption(options.get());
}

GameToolsMenu::GameToolsMenu() : MenuBase{"Game Tools"_i18n, MenuFlag_None} {
    m_items = {
        { "Games"_i18n, "View, launch and manage installed games."_i18n, 0, [](){
            App::Push<ui::menu::game::Menu>(MenuFlag_None);
        }},
        { "Saves"_i18n, "Backup and restore save data."_i18n, 0, [](){
            App::Push<ui::menu::save::Menu>(MenuFlag_None, 0ULL, ui::menu::save::Category::Installed);
        }},
        { "Cheats"_i18n, "Download and manage cheat databases."_i18n, 0, [](){
            App::Push<ui::menu::hats::CheatsMenu>();
        }},
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){ OnSelect(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
    );

    const Vec2 pad{10.f, 10.f};
    const Vec4 v{75.f, 110.f, 370.f, 155.f};
    m_list = std::make_unique<List>(3, 9, m_pos, v, pad);
    m_list->SetLayout(List::Layout::GRID);
    LoadIcons();
    SetIndex(0);
}

GameToolsMenu::~GameToolsMenu() {
    if (auto* vg = App::GetVg()) {
        for (auto& item : m_items) {
            if (item.icon_texture) {
                nvgDeleteImage(vg, item.icon_texture);
                item.icon_texture = 0;
            }
        }
    }
}

void GameToolsMenu::Update(Controller* controller, TouchInfo* touch) {
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

void GameToolsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    DrawToolsGrid(vg, theme, *m_list, m_index, m_items);
}

void GameToolsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void GameToolsMenu::LoadIcons() {
    auto* vg = App::GetVg();
    if (!vg) {
        return;
    }
    struct IconData { const u8* data; std::size_t size; };
    constexpr IconData icons[] = {
        { ICON_GAMES, sizeof(ICON_GAMES) },
        { ICON_SAVES, sizeof(ICON_SAVES) },
        { ICON_CHEATS, sizeof(ICON_CHEATS) },
    };
    const auto count = std::min(m_items.size(), std::size(icons));
    for (std::size_t i = 0; i < count; ++i) {
        m_items[i].icon_texture = LoadIcon(vg, icons[i].data, icons[i].size);
    }
}

void GameToolsMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
    if (!m_items.empty()) {
        const auto& item = m_items[m_index];
        SetTitleSubHeading(item.label + " - " + item.description, true);
        SetSubHeading("");
    }
}

void GameToolsMenu::OnSelect() {
    if (!m_items.empty()) {
        m_items[m_index].action();
    }
}

SystemToolsMenu::SystemToolsMenu() : MenuBase{"Tools"_i18n, MenuFlag_None} {
    // one list, three captions. The cursor steps over the captions.
    m_items = {
        settings::MakeHeader("Diagnostics"_i18n),
        Tool("System information"_i18n, "Firmware, Atmosphere and console details."_i18n, OpenSystemInfo),
        settings::MakeHeader("Settings"_i18n),
        Tool("Module Manager"_i18n, "Start, stop and configure installed sysmodules."_i18n, [](){
            App::Push<ui::menu::hats::UninstallerMenu>();
        }),
        Tool("Fan curve"_i18n, "Edit Atmosphere tskin fan curves for handheld and docked modes."_i18n, [](){
            ui::menu::settings::OpenFanCurveMenu();
        }),
        Tool("Wi-Fi"_i18n, "Manage wireless connections."_i18n, [](){
            App::Push<ui::menu::wifi::Menu>();
        }),
        Tool("Users"_i18n, "Create, rename, backup and link console user profiles."_i18n, [](){
            App::Push<ui::menu::users::Menu>();
        }),
        settings::MakeHeader("Maintenance"_i18n),
        Tool("Clean system junk"_i18n, "Delete old updates, lost game files, unused tickets and other leftovers."_i18n, OpenCleanup),
        Tool("Remove parental controls"_i18n, "Clear the console parental-control PIN."_i18n, ComingSoon),
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){ OnSelect(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
    );

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

void SystemToolsMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && i < static_cast<s64>(m_items.size()) && m_items[i].kind == settings::SettingsItemKind::Header) {
            return;
        }
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void SystemToolsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        settings::DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void SystemToolsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void SystemToolsMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = settings::ResolveItemIndex(m_items, index, m_index);
    if (!m_index) {
        m_list->SetYoff(0);
    } else {
        m_list->EnsureVisible(m_index, m_items.size());
    }
    SetTitleSubHeading(m_items[m_index].description, true);
    SetSubHeading("");
}

void SystemToolsMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

} // namespace sphaira::ui::menu::tools
