#include "ui/menus/settings_menu.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/uninstaller_menu.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"

#include "app.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace sphaira::ui::menu::settings {
namespace {

constexpr std::array LANGUAGE_ITEMS{
    "Auto",
    "English",
    "Japanese",
    "French",
    "German",
    "Italian",
    "Spanish",
    "Chinese",
    "Korean",
    "Dutch",
    "Portuguese",
    "Russian",
    "Swedish",
    "Vietnamese",
    "Ukrainian",
};

constexpr std::array TEXT_SCROLL_SPEED_ITEMS{
    "Slow",
    "Normal",
    "Fast",
};

auto ClampIndex(long index, long count) -> long {
    if (count <= 0) {
        return 0;
    }

    index %= count;
    if (index < 0) {
        index += count;
    }
    return index;
}

auto OnOff(bool enabled) -> std::string {
    return enabled ? "On"_i18n : "Off"_i18n;
}

auto MakeBoolItem(std::string label, std::string description, std::function<bool()> get, std::function<void(bool)> set) -> SettingsItem {
    return {
        std::move(label),
        std::move(description),
        [get](){
            return OnOff(get());
        },
        [get, set](){
            set(!get());
        }
    };
}

auto MakeOptionItem(std::string label, std::string description, option::OptionBool& option) -> SettingsItem {
    return MakeBoolItem(
        std::move(label),
        std::move(description),
        [&option](){
            return option.Get();
        },
        [&option](bool enabled){
            option.Set(enabled);
        }
    );
}

void ToggleInstallOption(option::OptionBool& option) {
    if (option.Get()) {
        option.Set(false);
        return;
    }

    App::Push<OptionBox>(
        "WARNING: Installing apps will lead to a ban!"_i18n,
        "Back"_i18n,
        "Enable"_i18n,
        0,
        [&option](auto op_index){
            if (op_index && *op_index) {
                option.Set(true);
                App::Notify("Installing enabled!"_i18n);
            }
        }
    );
}

auto MakeInstallToggle(std::string label, std::string description, option::OptionBool& option) -> SettingsItem {
    return {
        std::move(label),
        std::move(description),
        [&option](){
            return OnOff(option.Get());
        },
        [&option](){
            ToggleInstallOption(option);
        }
    };
}

auto LanguageValue() -> std::string {
    const auto index = ClampIndex(App::GetLanguage(), static_cast<long>(LANGUAGE_ITEMS.size()));
    return i18n::get(LANGUAGE_ITEMS[index]);
}

auto TextScrollSpeedValue() -> std::string {
    const auto index = ClampIndex(App::GetTextScrollSpeed(), static_cast<long>(TEXT_SCROLL_SPEED_ITEMS.size()));
    return i18n::get(TEXT_SCROLL_SPEED_ITEMS[index]);
}

auto ThemeValue() -> std::string {
    const auto themes = App::GetThemeMetaList();
    if (themes.empty()) {
        return "None";
    }

    const auto index = std::clamp<s64>(App::GetThemeIndex(), 0, static_cast<s64>(themes.size() - 1));
    return themes[index].name;
}

} // namespace

Menu::Menu() : MenuBase{"Settings"_i18n, MenuFlag_None} {
    BuildCategories();

    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            OnBack();
        }})
    );

    m_category_list = std::make_unique<List>(1, 8, m_pos, Vec4{76.f, 138.f, 300.f, 56.f});
    m_category_list->SetLayout(List::Layout::GRID);
    m_category_list->SetPageJump(false);

    m_item_list = std::make_unique<List>(1, 7, m_pos, Vec4{420.f, 132.f, 780.f, 66.f});
    m_item_list->SetLayout(List::Layout::GRID);
    m_item_list->SetPageJump(false);

    SetCategoryIndex(0);
}

Menu::~Menu() = default;

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (controller->GotDown(Button::RIGHT) && m_focus_pane == FocusPane::Categories) {
        SetFocusPane(FocusPane::Items);
        App::PlaySoundEffect(SoundEffect_Focus);
        return;
    }

    if (controller->GotDown(Button::LEFT) && m_focus_pane == FocusPane::Items) {
        SetFocusPane(FocusPane::Categories);
        App::PlaySoundEffect(SoundEffect_Focus);
        return;
    }

    if (touch->is_clicked) {
        if (touch->in_range(m_category_list->GetPos())) {
            SetFocusPane(FocusPane::Categories);
        } else if (touch->in_range(m_item_list->GetPos())) {
            SetFocusPane(FocusPane::Items);
        }
    }

    MenuBase::Update(controller, touch);

    if (m_focus_pane == FocusPane::Categories) {
        m_category_list->OnUpdate(controller, touch, m_category_index, m_categories.size(), [this](bool touch, auto i) {
            if (touch && m_category_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetCategoryIndex(i);
            }
        });
    } else {
        const auto& category = m_categories[m_category_index];
        m_item_list->OnUpdate(controller, touch, m_item_index, category.items.size(), [this](bool touch, auto i) {
            if (touch && m_item_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetItemIndex(i);
            }
        });
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    gfx::drawRect(vg, 392.f, 118.f, 1.f, 504.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    m_category_list->Draw(vg, theme, m_categories.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto selected = m_category_index == i;
        const auto focused = selected && m_focus_pane == FocusPane::Categories;
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;

        if (selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        }
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        gfx::drawTextBox(
            vg, v.x + 18.f, v.y + 16.f, 20.f, v.w - 36.f,
            theme->GetColour(text_id), m_categories[i].label.c_str()
        );
    });

    if (m_categories.empty()) {
        return;
    }

    const auto& category = m_categories[m_category_index];
    m_item_list->Draw(vg, theme, category.items.size(), [this, &category](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& item = category.items[i];
        const auto selected = m_item_index == i;
        const auto focused = selected && m_focus_pane == FocusPane::Items;
        const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;

        if (selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        } else {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        gfx::drawTextBox(
            vg, v.x + 18.f, v.y + 10.f, 20.f, v.w - 260.f,
            theme->GetColour(label_id), item.label.c_str()
        );
        if (!item.description.empty()) {
            gfx::drawTextBox(
                vg, v.x + 18.f, v.y + 37.f, 14.f, v.w - 230.f,
                theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
            );
        }

        if (item.value) {
            const auto value = item.value();
            gfx::drawText(
                vg, v.x + v.w - 20.f, v.y + 21.f, 18.f,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO),
                value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP
            );
        }
    });
}

void Menu::BuildCategories() {
    auto* app = App::GetApp();

    m_categories = {
        {
            "General",
            "Language, timing and application flow.",
            {
                { "Language", "Cycle the active interface language.", LanguageValue, [](){
                    App::SetLanguage(ClampIndex(App::GetLanguage() + 1, static_cast<long>(LANGUAGE_ITEMS.size())));
                }},
                { "Text scroll speed", "Change how fast long labels scroll.", TextScrollSpeedValue, [](){
                    App::SetTextScrollSpeed(ClampIndex(App::GetTextScrollSpeed() + 1, static_cast<long>(TEXT_SCROLL_SPEED_ITEMS.size())));
                }},
                MakeBoolItem("12 Hour Time", "Use 12 hour clock format.", App::Get12HourTimeEnable, App::Set12HourTimeEnable),
                { "Restart Sphaira", "Close and reopen the application.", [](){ return std::string{}; }, [](){
                    App::ExitRestart();
                }},
                { "Exit", "Close Sphaira.", [](){ return std::string{}; }, [](){
                    App::Exit();
                }},
            }
        },
        {
            "Appearance",
            "Theme and audio options.",
            {
                { "Theme", "Cycle installed Sphaira themes.", ThemeValue, [](){
                    const auto themes = App::GetThemeMetaList();
                    if (!themes.empty()) {
                        App::SetTheme(ClampIndex(App::GetThemeIndex() + 1, static_cast<long>(themes.size())));
                    }
                }},
                MakeBoolItem("Music", "Enable background music from the current theme.", App::GetThemeMusicEnable, App::SetThemeMusicEnable),
            }
        },
        {
            "Network",
            "Background services and network downloads.",
            {
                MakeBoolItem("FTP", "Run the FTP server in the background.", App::GetFtpEnable, App::SetFtpEnable),
                MakeBoolItem("MTP", "Run the MTP server in the background.", App::GetMtpEnable, App::SetMtpEnable),
                MakeBoolItem("Nxlink", "Receive .nro files from a PC.", App::GetNxlinkEnable, App::SetNxlinkEnable),
                MakeBoolItem("HDD", "Mount connected USB/HDD devices.", App::GetHddEnable, App::SetHddEnable),
                MakeBoolItem("HDD write protect", "Make connected HDD storage read-only.", App::GetWriteProtect, App::SetWriteProtect),
            }
        },
        {
            "Homebrew",
            "Shortcuts for core Sphaira tools.",
            {
                { "Homebrew App Store", "Download and update homebrew apps.", [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
                }},
                { "File Browser", "Browse and manage files on the SD card.", [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
                }},
                { "Component Manager", "Manage installed packages and modules.", [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::hats::UninstallerMenu>();
                }},
            }
        },
        {
            "Install",
            "Install behavior and safety switches.",
            {
                MakeInstallToggle("Enable sysMMC", "Allow installing while running sysMMC.", app->m_install_sysmmc),
                MakeInstallToggle("Enable emuMMC", "Allow installing while running emuMMC.", app->m_install_emummc),
                { "Install location", "Choose system memory or microSD card.", [](){
                    return App::GetInstallSdEnable() ? "microSD card"_i18n : "System memory"_i18n;
                }, [](){
                    App::SetInstallSdEnable(!App::GetInstallSdEnable());
                }},
                MakeOptionItem("Allow downgrade", "Allow lower title updates to be installed.", app->m_allow_downgrade),
                MakeOptionItem("Skip if already installed", "Skip titles or NCAs that are already installed.", app->m_skip_if_already_installed),
                MakeOptionItem("Ticket only", "Install tickets without title contents.", app->m_ticket_only),
                MakeOptionItem("Skip base", "Skip installing base applications.", app->m_skip_base),
                MakeOptionItem("Skip patch", "Skip installing title updates.", app->m_skip_patch),
                MakeOptionItem("Skip DLC", "Skip installing DLC content.", app->m_skip_addon),
                MakeOptionItem("Skip data patch", "Skip installing DLC updates.", app->m_skip_data_patch),
                MakeOptionItem("Skip ticket", "Skip installing tickets.", app->m_skip_ticket),
            }
        },
        {
            "Dump",
            "Game dump naming and transfer options.",
            {
                MakeOptionItem("Created nested folder", "Create a nested folder for each game dump.", app->m_dump_app_folder),
                MakeOptionItem("Append folder with .xci", "Append .xci to XCI dump folders.", app->m_dump_append_folder_with_xci),
                MakeOptionItem("Trim XCI", "Remove unused data from XCI dumps.", app->m_dump_trim_xci),
                MakeOptionItem("Label trimmed XCI", "Mark trimmed XCI output names.", app->m_dump_label_trim_xci),
                MakeOptionItem("USB transfer stream", "Stream dump output over USB.", app->m_dump_usb_transfer_stream),
                MakeOptionItem("Convert to common ticket", "Convert personalized tickets during dump.", app->m_dump_convert_to_common_ticket),
            }
        },
        {
            "Advanced",
            "Power-user options and verification controls.",
            {
                MakeBoolItem("Logging", "Write logs to /config/sphaira/log.txt.", App::GetLogEnable, App::SetLogEnable),
                MakeBoolItem("Replace hbmenu on exit", "Replace /hbmenu.nro with Sphaira on exit.", App::GetReplaceHbmenuEnable, App::SetReplaceHbmenuEnable),
                MakeOptionItem("Boost CPU during transfer", "Enable CPU boost during transfers.", app->m_progress_boost_mode),
                MakeOptionItem("Skip NCA hash verify", "Skip SHA-256 verification over NCA content.", app->m_skip_nca_hash_verify),
                MakeOptionItem("Skip RSA header verify", "Skip RSA NCA fixed-key header verification.", app->m_skip_rsa_header_fixed_key_verify),
                MakeOptionItem("Skip RSA NPDM verify", "Skip RSA NPDM fixed-key verification.", app->m_skip_rsa_npdm_fixed_key_verify),
                MakeOptionItem("Ignore distribution bit", "Ignore the NCA distribution bit.", app->m_ignore_distribution_bit),
                MakeOptionItem("Convert to common ticket", "Convert personalized tickets to common tickets.", app->m_convert_to_common_ticket),
                MakeOptionItem("Convert to standard crypto", "Convert titlekey to standard crypto.", app->m_convert_to_standard_crypto),
                MakeOptionItem("Lower master key", "Encrypt key area keys with master key 0.", app->m_lower_master_key),
                MakeOptionItem("Lower system version", "Lower the system firmware field in metadata.", app->m_lower_system_version),
            }
        },
    };
}

void Menu::SetFocusPane(FocusPane pane) {
    m_focus_pane = pane;
}

void Menu::SetCategoryIndex(s64 index) {
    if (m_categories.empty()) {
        m_category_index = 0;
        m_item_index = 0;
        return;
    }

    m_category_index = std::clamp<s64>(index, 0, static_cast<s64>(m_categories.size() - 1));
    m_item_index = 0;
    m_item_list->SetYoff(0);
    if (!m_category_index) {
        m_category_list->SetYoff(0);
    }

    SetSubHeading(m_categories[m_category_index].description);
}

void Menu::SetItemIndex(s64 index) {
    const auto& items = m_categories[m_category_index].items;
    if (items.empty()) {
        m_item_index = 0;
        return;
    }

    m_item_index = std::clamp<s64>(index, 0, static_cast<s64>(items.size() - 1));
    if (!m_item_index) {
        m_item_list->SetYoff(0);
    }
}

void Menu::OnSelect() {
    if (m_categories.empty()) {
        return;
    }

    if (m_focus_pane == FocusPane::Categories) {
        SetFocusPane(FocusPane::Items);
        return;
    }

    const auto& item = m_categories[m_category_index].items[m_item_index];
    if (item.action) {
        item.action();
    }
}

void Menu::OnBack() {
    if (m_focus_pane == FocusPane::Items) {
        SetFocusPane(FocusPane::Categories);
        return;
    }

    SetPop();
}

} // namespace sphaira::ui::menu::settings
