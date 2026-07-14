#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_fancurve.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/themezer.hpp"
#include "ui/menus/uninstaller_menu.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/hold_confirm_box.hpp"


#include "app.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <minIni.h>
#include <switch/services/fan.h>
#include <switch/services/pm.h>
#include <switch/services/tc.h>
#include <sys/stat.h>
#include <unistd.h>
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


constexpr const char* ATMOSPHERE_CONFIG = "/atmosphere/config/system_settings.ini";
const auto DBI_TRANSLATIONS_PACKAGE = paths::PACKAGES + "/Software/DBI/Fan Translations/package.ini";


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

using namespace detail;


auto SettingsValueColour(Theme* theme, const std::string& value, bool selected) -> NVGcolor {
    if (value == "On"_i18n) {
        return nvgRGBA(78, 210, 112, 255);
    }
    if (value == "Off"_i18n) {
        return nvgRGBA(135, 138, 148, 255);
    }
    return theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
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

void ToggleKefirSetting(const KefirSetting& setting) {
    const auto enabled = !setting.get();
    auto warning = setting.warning_on;

    if (warning.empty()) {
        warning = enabled
            ? "This setting changes Kefir or Atmosphere files and will reboot the console."
            : "This setting changes Kefir or Atmosphere files and will reboot the console.";
    }

    App::Push<HoldConfirmBox>(
        warning,
        setting.hold_seconds,
        [setting, enabled](bool confirmed){
            if (!confirmed) {
                return;
            }

            App::Push<ProgressBox>(
                0,
                enabled ? "Enabling"_i18n : "Disabling"_i18n,
                setting.label,
                [setting, enabled](auto pbox) -> Result {
                    pbox->NewTransfer(setting.description);
                    return setting.set(enabled);
                },
                [](Result rc){
                    if (R_FAILED(rc)) {
                        App::PushErrorBox(rc, "Failed to apply Kefir setting"_i18n);
                    }
                }
            );
        }
    );
}

auto MakeKefirToggle(KefirSetting setting) -> SettingsItem {
    return {
        setting.label,
        setting.description,
        [setting](){
            return OnOff(setting.get());
        },
        [setting](){
            ToggleKefirSetting(setting);
        }
    };
}

void RunPackageAction(const PackageAction& action) {
    const auto start = [action](){
        App::Push<ProgressBox>(
            0,
            "Running"_i18n,
            action.label,
            [action](auto pbox) -> Result {
                return action.run(pbox);
            },
            [](Result rc){
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Failed to run package action"_i18n);
                } else {
                    App::Notify("Done"_i18n);
                }
            }
        );
    };

    if (!action.hold) {
        start();
        return;
    }

    auto warning = action.warning;
    if (warning.empty()) {
        warning = "This action changes files on the SD card. Hold A to continue."_i18n;
    }

    App::Push<HoldConfirmBox>(
        warning,
        action.hold_seconds,
        [start](bool confirmed){
            if (confirmed) {
                start();
            }
        }
    );
}

auto MakePackageAction(PackageAction action) -> SettingsItem {
    return {
        action.label,
        action.description,
        [](){
            return std::string{};
        },
        [action](){
            RunPackageAction(action);
        },
        SettingsItemKind::Download,
    };
}

auto BuildDbiItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakePackageAction({
        "Download DBI translations list"_i18n,
        "Update the DBI fan translations package list."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading list of translations..."_i18n,
                "https://github.com/rashevskyv/DBI_watcher/raw/main/output/package.ini",
                paths::DOWNLOADS + "/dbi.package.ini"
            ));
            R_TRY(MovePath(paths::DOWNLOADS + "/dbi.package.ini", DBI_TRANSLATIONS_PACKAGE));
            R_SUCCEED();
        },
    }));

    for (const auto& entry : ParseDbiTranslations(DBI_TRANSLATIONS_PACKAGE)) {
        items.emplace_back(MakePackageAction({
            entry.name,
            "Install DBI fan translation."_i18n,
            [entry](auto pbox) -> Result {
                return InstallDbiTranslation(pbox, entry);
            },
        }));
    }

    items.emplace_back(MakePackageAction({
        "Russian latest DBI"_i18n,
        "Download the latest Russian DBI build."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Russian DBI..."_i18n,
                "https://github.com/rashevskyv/DBI/releases/latest/download/DBI.nro",
                "/switch/DBI/DBI_new.nro"
            ));
            R_TRY(MovePath("/switch/DBI/DBI_new.nro", "/switch/DBI/DBI.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Reset DBI config"_i18n,
        "Download a clean DBI config from Kefir."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Resetting DBI Config..."_i18n,
                "https://github.com/rashevskyv/DBI/releases/latest/download/dbi.config",
                "/switch/DBI/dbi.config_new"
            ));
            R_TRY(MovePath("/switch/DBI/dbi.config_new", "/switch/DBI/dbi.config"));
            R_SUCCEED();
        },
        true,
        "This will replace your current DBI config with the default Kefir config."_i18n,
        0.5f,
    }));

    return items;
}

auto BuildSoftwareItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Homebrew App Store"_i18n,
        "Download and update homebrew apps."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(SettingsItem{
        "DBI"_i18n,
        "DBI installer and translations."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<DbiMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(MakePackageAction({
        "UAModDownloader"_i18n,
        "Ukrainian mods."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading UAModDownloader..."_i18n,
                "https://github.com/pro100luk/UAModDownloader/releases/latest/download/UAModDownloader.nro",
                "/switch/UAModDownloader/UAModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/UAModDownloader/UAModDownloader_new.nro", "/switch/UAModDownloader/UAModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "ModCD"_i18n,
        "ECLIPS graphic mods."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading ModCD..."_i18n,
                "https://github.com/kawaii-flesh/ModCD/releases/latest/download/ModCD.nro",
                "/switch/ModCD/ModCD_new.nro"
            ));
            R_TRY(MovePath("/switch/ModCD/ModCD_new.nro", "/switch/ModCD/ModCD.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "SimpleModDownloader"_i18n,
        "Game mods from GameBanana."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading SimpleModDownloader..."_i18n,
                "https://github.com/PoloNX/SimpleModDownloader/releases/latest/download/SimpleModDownloader.nro",
                "/switch/SimpleModDownloader/SimpleModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/SimpleModDownloader/SimpleModDownloader_new.nro", "/switch/SimpleModDownloader/SimpleModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    return items;
}

auto MakeFavoriteThemeItem(ui::menu::themezer::PackListEntry entry) -> SettingsItem {
    return {
        entry.details.name,
        entry.details.description.empty() ? "Install favorite theme." : entry.details.description,
        [](){
            return std::string{};
        },
        [entry](){
            App::Push<OptionBox>(
                "Download theme?"_i18n,
                "Back"_i18n, "Download"_i18n, 1, [entry](auto op_index){
                    if (op_index && *op_index) {
                        App::Push<ProgressBox>(0, "Downloading "_i18n, entry.details.name, [entry](auto pbox) -> Result {
                            return ui::menu::themezer::InstallTheme(pbox, entry);
                        }, [entry](Result rc){
                            App::PushErrorBox(rc, "Failed to download theme"_i18n);

                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Downloaded "_i18n + entry.details.name);
                            }
                        });
                    }
                }
            );
        },
        SettingsItemKind::Favorite,
        entry.id
    };
}

auto BuildThemeItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Themezer",
        "Browse, download and install theme packs from themezer.net.",
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::themezer::Menu>(MenuFlag_None);
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(MakePackageAction({
        "Mario BG Dark",
        "Download and extract Mario BG Modern theme.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Mario BG Dark...",
                "https://github.com/rashevskyv/mario_bg_theme/releases/latest/download/Mario.BG.Modern.zip",
                paths::DOWNLOADS + "/theme.zip"
            ));
            R_TRY(UnzipFile(pbox, paths::DOWNLOADS + "/theme.zip", "/themes/"));
            R_TRY(DeletePath(paths::DOWNLOADS + "/theme.zip"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Switch 2 Theme by alexwak",
        "Download and extract Switch 2 theme.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Switch 2 Theme...",
                "https://github.com/alexwak/Switch-2-Switch-Theme/releases/latest/download/Switch-2-Switch-Banned.zip",
                paths::DOWNLOADS + "/theme.zip"
            ));
            R_TRY(UnzipFile(pbox, paths::DOWNLOADS + "/theme.zip", "/themes/"));
            R_TRY(DeletePath(paths::DOWNLOADS + "/theme.zip"));
            R_SUCCEED();
        },
    }));

    for (const auto& entry : ui::menu::themezer::GetFavorites()) {
        items.emplace_back(MakeFavoriteThemeItem(entry));
    }

    return items;
}

auto BuildTranslateItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakePackageAction({
        "Download language packs"_i18n,
        "Download the UltraHand language package list."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading language packs..."_i18n,
                "https://github.com/rashevskyv/switch-translations-mirrors/raw/main/lang_packs_ultra.zip",
                paths::DOWNLOADS + "/lang_packs.zip"
            ));
            R_TRY(MovePath(TRANSLATE_PACKAGE, TRANSLATE_PACKAGE_BACKUP));
            R_TRY(DeletePath(TRANSLATE_PACKAGE_DIR));
            fs::FsNativeSd fs;
            R_TRY(fs.CreateDirectoryRecursively(TRANSLATE_PACKAGE_DIR));
            if (fs::FileExists(TRANSLATE_PACKAGE_BACKUP)) {
                R_TRY(CopyFileSimple(TRANSLATE_PACKAGE_BACKUP, std::string{TRANSLATE_PACKAGE_DIR} + "/package.ini.bkp"));
            }
            R_TRY(UnzipFile(pbox, paths::DOWNLOADS + "/lang_packs.zip", TRANSLATE_PACKAGE_DIR));
            R_TRY(DeletePath(paths::DOWNLOADS + "/lang_packs.zip"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Remove installed translation"_i18n,
        "Delete installed interface translations and reboot."_i18n,
        [](auto pbox) -> Result {
            return RemoveInterfaceTranslation(pbox);
        },
        true,
        "This removes installed system interface translation files and reboots the console."_i18n,
        0.5f,
    }));

    for (const auto& entry : ParseInterfaceTranslations(TRANSLATE_PACKAGE)) {
        items.emplace_back(SettingsItem{
            entry.name,
            "Install interface translation."_i18n,
            [](){
                return std::string{};
            },
            [entry](){
                const auto options = ReadInterfaceReplacementOptions(entry);
                if (options.empty()) {
                    App::PushErrorBox(Result_FsEmpty, "No replacement languages found"_i18n);
                    return;
                }

                PopupList::Items labels;
                labels.reserve(options.size());
                for (const auto& [label, dir] : options) {
                    labels.push_back(label);
                }

                App::Push<PopupList>(
                    "Replace language"_i18n,
                    labels,
                    [entry, options](auto index){
                        if (!index) {
                            return;
                        }

                        const auto dir = options[*index].second;
                        App::Push<HoldConfirmBox>(
                            "This will replace the selected system interface language and reboot the console."_i18n,
                            0.5f,
                            [entry, dir](bool confirmed){
                                if (!confirmed) {
                                    return;
                                }

                                App::Push<ProgressBox>(
                                    0,
                                    "Installing"_i18n,
                                    entry.name,
                                    [entry, dir](auto pbox) -> Result {
                                        return InstallInterfaceTranslation(pbox, entry, dir);
                                    },
                                    [](Result rc){
                                        if (R_FAILED(rc)) {
                                            App::PushErrorBox(rc, "Failed to install translation"_i18n);
                                        }
                                    }
                                );
                            }
                        );
                    }
                );
            },
            SettingsItemKind::Folder,
        });
    }

    return items;
}

auto BuildKefirItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;
    items.emplace_back(MakeKefirToggle({
        "Overclock status"_i18n,
        "Enable or disable Kefir overclock files."_i18n,
        [](){
            return fs::FileExists("/atmosphere/kips/kefir.kip");
        },
        ApplyOverclock,
        "",
        0.5f,
    }));
    items.emplace_back(MakeKefirToggle({
        "40MB Memory"_i18n,
        "Toggle the 40MB applet memory patch."_i18n,
        [](){
            return IniValueEquals(ATMOSPHERE_CONFIG, "atmosphere", "force_40mb_applet", "u8!0x1");
        },
        Apply40Mb,
        "",
        0.5f,
    }));

    if (IsEmummcEnabled()) {
        items.emplace_back(MakeKefirToggle({
            "Redirect Emunand saves to SD"_i18n,
            "Experimental save redirection for emuMMC."_i18n,
            [](){
                return IniValueEquals(ATMOSPHERE_CONFIG, "atmosphere", "fsmitm_redirect_saves_to_sd", "u8!0x1");
            },
            ApplyRedirectSaves,
            "Experimental option.\n\nThis redirects emuMMC saves to the SD card. Use it only if you understand the risk; changing save paths can make saves appear missing until the setting is reverted."_i18n,
            0.5f,
        }));
    }

    items.emplace_back(MakeKefirToggle({
        "8GB DRAM status"_i18n,
        "Only for consoles with physically soldered 8GB RAM."_i18n,
        [](){
            return fs::FileExists("/tegraexplorer/scripts/Remove_8GB-RAM_config.te");
        },
        Apply8GbDram,
        "Only for consoles with physically soldered 8GB RAM. Other consoles will not boot correctly.\n\nTo disable it if the console does not boot:\nhekate > payloads > TegraExplorer > Remove_8GB-RAM_config.te"_i18n,
        3.f,
    }));

    items.emplace_back(SettingsItem{
        "Fan curve"_i18n,
        "Edit Atmosphere tskin fan curves for handheld and docked modes."_i18n,
        [](){
            return "";
        },
        [](){
            App::Push<FanCurveMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(SettingsItem{
        "Module Manager"_i18n,
        "Start, stop and configure installed sysmodules."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::hats::UninstallerMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(SettingsItem{
        "Translate Interface"_i18n,
        "Interface translation package tools."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::settings::TranslateMenu>();
        },
        SettingsItemKind::Folder,
    });

    return items;
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
    m_category_list->SetFastScroll(false);

    m_item_list = std::make_unique<List>(1, 7, m_pos, Vec4{420.f, 132.f, 780.f, 66.f});
    m_item_list->SetLayout(List::Layout::GRID);
    m_item_list->SetPageJump(false);
    m_item_list->SetFastScroll(false);

    SetCategoryIndex(0);
}

Menu::~Menu() = default;

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    std::string category_label;
    if (!m_categories.empty()) {
        category_label = m_categories[m_category_index].label;
    }

    BuildCategories();
    auto it = std::find_if(m_categories.cbegin(), m_categories.cend(), [&](const auto& category) {
        return category.label == category_label;
    });
    SetCategoryIndex(it == m_categories.cend() ? m_category_index : std::distance(m_categories.cbegin(), it));
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (touch->is_clicked) {
        if (touch->in_range(m_category_list->GetPos())) {
            SetFocusPane(FocusPane::Categories);
        } else if (touch->in_range(m_item_list->GetPos())) {
            SetFocusPane(FocusPane::Items);
        }
    }

    if (m_focus_pane == FocusPane::Categories) {
        if (controller->GotDown(Button::RIGHT)) {
            SetFocusPane(FocusPane::Items);
            App::PlaySoundEffect(SoundEffect_Focus);
        }
    } else {
        if (controller->GotDown(Button::LEFT)) {
            SetFocusPane(FocusPane::Categories);
            App::PlaySoundEffect(SoundEffect_Focus);
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
        }, this);
    } else {
        const auto& category = m_categories[m_category_index];
        m_item_list->OnUpdate(controller, touch, m_item_index, category.items.size(), [this](bool touch, auto i) {
            if (touch && m_item_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetItemIndex(i);
            }
        }, this);
    }
}

namespace {

auto SettingsItemTextX(const SettingsItem& item, float x) -> float {
    return item.kind == SettingsItemKind::Normal ? x + 18.f : x + 74.f;
}

void DrawSettingsItemKindIcon(NVGcontext* vg, Theme* theme, const SettingsItem& item, Vec4 v, bool selected) {
    if (item.kind == SettingsItemKind::Normal) {
        return;
    }

    const auto colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
    const auto x = v.x + 18.f;
    const auto y = v.y + 15.f;

    nvgSave(vg);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 3.f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);

    if (item.kind == SettingsItemKind::Folder) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y + 8.f, 42.f, 28.f, 5.f);
        nvgMoveTo(vg, x + 5.f, y + 9.f);
        nvgLineTo(vg, x + 5.f, y + 4.f);
        nvgLineTo(vg, x + 19.f, y + 4.f);
        nvgLineTo(vg, x + 24.f, y + 9.f);
        nvgLineTo(vg, x + 37.f, y + 9.f);
        nvgStroke(vg);
    } else if (item.kind == SettingsItemKind::Download) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 21.f, y + 3.f);
        nvgLineTo(vg, x + 21.f, y + 25.f);
        nvgMoveTo(vg, x + 10.f, y + 16.f);
        nvgLineTo(vg, x + 21.f, y + 27.f);
        nvgLineTo(vg, x + 32.f, y + 16.f);
        nvgMoveTo(vg, x + 8.f, y + 36.f);
        nvgLineTo(vg, x + 34.f, y + 36.f);
        nvgStroke(vg);
    } else if (item.kind == SettingsItemKind::Favorite) {
        const float cx = x + 21.f;
        const float cy = y + 20.f;
        const float rOut = 15.f;
        const float rIn = 7.f;
        nvgBeginPath(vg);
        for (int i = 0; i < 10; ++i) {
            float r = (i % 2 == 0) ? rOut : rIn;
            float angle = -3.14159265f / 2.f + i * 3.14159265f / 5.f;
            float px = cx + r * std::cos(angle);
            float py = cy + r * std::sin(angle);
            if (i == 0) {
                nvgMoveTo(vg, px, py);
            } else {
                nvgLineTo(vg, px, py);
            }
        }
        nvgClosePath(vg);
        nvgStroke(vg);
    }

    nvgRestore(vg);
}

} // namespace

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

        {
            const float text_x = v.x + 18.f;
            const float text_w = v.w - 36.f;
            nvgFontSize(vg, 20.f);
            nvgTextLineHeight(vg, 1.0f);
            float label_bounds[4];
            nvgTextBoxBounds(vg, text_x, 0, text_w, m_categories[i].label.c_str(), nullptr, label_bounds);
            const float label_h = label_bounds[3] - label_bounds[1];
            const float label_y = v.y + (v.h - label_h) / 2.f;
            gfx::drawTextBox(
                vg, text_x, label_y, 20.f, text_w,
                theme->GetColour(text_id), m_categories[i].label.c_str()
            );
        }
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

        DrawSettingsItemKindIcon(vg, theme, item, v, selected);
        const auto text_x = SettingsItemTextX(item, v.x);
        const auto text_offset = text_x - v.x;

        gfx::drawTextBox(
            vg, text_x, v.y + 10.f, 20.f, v.w - 242.f - text_offset,
            theme->GetColour(label_id), item.label.c_str()
        );
        if (!item.description.empty()) {
            gfx::drawTextBox(
                vg, text_x, v.y + 37.f, 14.f, v.w - 212.f - text_offset,
                theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
            );
        }

        if (item.value) {
            const auto value = item.value();
            gfx::drawText(
                vg, v.x + v.w - 20.f, v.y + 21.f, 18.f,
                SettingsValueColour(theme, value, selected),
                value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP
            );
        }

        if (item.kind == SettingsItemKind::Folder) {
            const float x1 = v.x + v.w - 24.f;
            const float y1 = v.y + v.h / 2.f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, x1 - 8.f, y1 - 8.f);
            nvgLineTo(vg, x1, y1);
            nvgLineTo(vg, x1 - 8.f, y1 + 8.f);
            nvgStrokeColor(vg, theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));
            nvgStrokeWidth(vg, 3.f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
        }
    });
}

void Menu::BuildCategories() {
    auto* app = App::GetApp();

    m_categories = {
        {
            "General"_i18n,
            "Language, timing and application flow."_i18n,
            {
                { "Language"_i18n, "Select the active interface language."_i18n, LanguageValue, [](){
                    PopupList::Items items;
                    for (const auto& lang : LANGUAGE_ITEMS) {
                        items.push_back(i18n::get(lang));
                    }
                    App::Push<PopupList>("Language"_i18n, std::move(items), [](std::optional<s64> op_index){
                        if (op_index) {
                            App::SetLanguage(*op_index);
                        }
                    }, App::GetLanguage());
                }},
                { "Text scroll speed"_i18n, "Select how fast long labels scroll."_i18n, TextScrollSpeedValue, [](){
                    PopupList::Items items;
                    for (const auto& speed : TEXT_SCROLL_SPEED_ITEMS) {
                        items.push_back(i18n::get(speed));
                    }
                    App::Push<PopupList>("Text scroll speed"_i18n, std::move(items), [](std::optional<s64> op_index){
                        if (op_index) {
                            App::SetTextScrollSpeed(*op_index);
                        }
                    }, App::GetTextScrollSpeed());
                }},
                MakeBoolItem("12 Hour Time"_i18n, "Use 12 hour clock format."_i18n, App::Get12HourTimeEnable, App::Set12HourTimeEnable),
                { "Restart Kefir Hub"_i18n, "Close and reopen the application."_i18n, [](){ return std::string{}; }, [](){
                    App::ExitRestart();
                }},
                { "Exit"_i18n, "Close Kefir Hub."_i18n, [](){ return std::string{}; }, [](){
                    App::Exit();
                }},
            }
        },
        {
            "Appearance"_i18n,
            "Theme and audio options."_i18n,
            {
                { "Theme"_i18n, "Select the active Kefir Hub theme."_i18n, ThemeValue, [](){
                    const auto themes = App::GetThemeMetaList();
                    if (!themes.empty()) {
                        PopupList::Items items;
                        for (const auto& theme : themes) {
                            items.push_back(theme.name);
                        }
                        App::Push<PopupList>("Theme"_i18n, std::move(items), [](std::optional<s64> op_index){
                            if (op_index) {
                                App::SetTheme(*op_index);
                            }
                        }, App::GetThemeIndex());
                    }
                }},
                MakeBoolItem("Music"_i18n, "Enable background music from the current theme."_i18n, App::GetThemeMusicEnable, App::SetThemeMusicEnable),
                MakeBoolItem("Animated waves"_i18n, "Enable animated background waves in the bottom bar."_i18n, App::GetAnimatedWavesEnable, App::SetAnimatedWavesEnable),
                { "Kefir Hub theme options"_i18n, "Select the Kefir Hub interface theme and music options."_i18n, [](){ return std::string{}; }, [](){
                    App::DisplayThemeOptions(false);
                }, SettingsItemKind::Folder },
            }
        },
        {
            "Network"_i18n,
            "Background services and network downloads."_i18n,
            {
                MakeBoolItem("FTP"_i18n, "Run the FTP server in the background."_i18n, App::GetFtpEnable, App::SetFtpEnable),
                MakeBoolItem("MTP"_i18n, "Run the MTP server in the background."_i18n, App::GetMtpEnable, App::SetMtpEnable),
                { "MTP storages"_i18n, "Configure which folders are visible over MTP and their names."_i18n, [](){ return std::string{}; }, [](){
                    App::DisplayMtpStorageOptions(false);
                }, SettingsItemKind::Folder },
                MakeBoolItem("Nxlink"_i18n, "Receive .nro files from a PC."_i18n, App::GetNxlinkEnable, App::SetNxlinkEnable),
                MakeBoolItem("HDD"_i18n, "Mount connected USB/HDD devices."_i18n, App::GetHddEnable, App::SetHddEnable),
                MakeBoolItem("HDD write protect"_i18n, "Make connected HDD storage read-only."_i18n, App::GetWriteProtect, App::SetWriteProtect),
                { "WebDAV"_i18n, "Configure WebDAV server for save synchronization."_i18n, [](){ return std::string{}; }, [](){
                    App::DisplayWebdavOptions(false);
                }, SettingsItemKind::Folder },
            }
        },
        {
            "Homebrew"_i18n,
            "Shortcuts for core Kefir Hub tools."_i18n,
            {
                { "Homebrew App Store"_i18n, "Download and update homebrew apps."_i18n, [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
                }},
                { "File Browser"_i18n, "Browse and manage files on the SD card."_i18n, [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
                }},
            }
        },

        {
            "Install"_i18n,
            "Install behavior and safety switches."_i18n,
            {
                MakeInstallToggle("Enable sysMMC"_i18n, "Allow installing while running sysMMC."_i18n, app->m_install_sysmmc),
                MakeInstallToggle("Enable emuMMC"_i18n, "Allow installing while running emuMMC."_i18n, app->m_install_emummc),
                { "Install location"_i18n, "Choose system memory or microSD card."_i18n, [](){
                    const auto loc = App::GetInstallLocation();
                    if (loc >= 0 && loc < 5) {
                        static constexpr const char* labels[] = {
                            "microSD card only",
                            "System memory only",
                            "System first, then SD",
                            "SD first, then system",
                            "Automatic"
                        };
                        return i18n::get(labels[loc]);
                    }
                    return std::string{};
                }, [](){
                    PopupList::Items items;
                    items.push_back("microSD card only"_i18n);
                    items.push_back("System memory only"_i18n);
                    items.push_back("System first, then SD"_i18n);
                    items.push_back("SD first, then system"_i18n);
                    items.push_back("Automatic"_i18n);

                    App::Push<PopupList>("Install location"_i18n, std::move(items), [](std::optional<s64> op_index){
                        if (op_index) {
                            App::SetInstallLocation(*op_index);
                        }
                    }, App::GetInstallLocation());
                }},
                MakeOptionItem("Allow downgrade"_i18n, "Allow lower title updates to be installed."_i18n, app->m_allow_downgrade),
                MakeOptionItem("Skip if already installed"_i18n, "Skip titles or NCAs that are already installed."_i18n, app->m_skip_if_already_installed),
                MakeOptionItem("Ticket only"_i18n, "Install tickets without title contents."_i18n, app->m_ticket_only),
                MakeOptionItem("Skip base"_i18n, "Skip installing base applications."_i18n, app->m_skip_base),
                MakeOptionItem("Skip patch"_i18n, "Skip installing title updates."_i18n, app->m_skip_patch),
                MakeOptionItem("Skip DLC"_i18n, "Skip installing DLC content."_i18n, app->m_skip_addon),
                MakeOptionItem("Skip data patch"_i18n, "Skip installing DLC updates."_i18n, app->m_skip_data_patch),
                MakeOptionItem("Skip ticket"_i18n, "Skip installing tickets."_i18n, app->m_skip_ticket),
            }
        },
        {
            "Dump"_i18n,
            "Game dump naming and transfer options."_i18n,
            {
                MakeOptionItem("Created nested folder"_i18n, "Create a nested folder for each game dump."_i18n, app->m_dump_app_folder),
                MakeOptionItem("Append folder with .xci"_i18n, "Append .xci to XCI dump folders."_i18n, app->m_dump_append_folder_with_xci),
                MakeOptionItem("Trim XCI"_i18n, "Remove unused data from XCI dumps."_i18n, app->m_dump_trim_xci),
                MakeOptionItem("Label trimmed XCI"_i18n, "Mark trimmed XCI output names."_i18n, app->m_dump_label_trim_xci),
                MakeOptionItem("USB transfer stream"_i18n, "Stream dump output over USB."_i18n, app->m_dump_usb_transfer_stream),
                MakeOptionItem("Convert to common ticket"_i18n, "Convert personalized tickets during dump."_i18n, app->m_dump_convert_to_common_ticket),
            }
        },
        {
            "Advanced"_i18n,
            "Power-user options and verification controls."_i18n,
            {
                MakeBoolItem("Logging"_i18n, "Write logs to /config/kefir/log.txt."_i18n, App::GetLogEnable, App::SetLogEnable),
                MakeBoolItem("Replace hbmenu on exit"_i18n, "Replace /hbmenu.nro with Kefir Hub on exit."_i18n, App::GetReplaceHbmenuEnable, App::SetReplaceHbmenuEnable),
                MakeOptionItem("Boost CPU during transfer"_i18n, "Enable CPU boost during transfers."_i18n, app->m_progress_boost_mode),
                MakeOptionItem("Skip NCA hash verify"_i18n, "Skip SHA-256 verification over NCA content."_i18n, app->m_skip_nca_hash_verify),
                MakeOptionItem("Skip RSA header verify"_i18n, "Skip RSA NCA fixed-key header verification."_i18n, app->m_skip_rsa_header_fixed_key_verify),
                MakeOptionItem("Skip RSA NPDM verify"_i18n, "Skip RSA NPDM fixed-key verification."_i18n, app->m_skip_rsa_npdm_fixed_key_verify),
                MakeOptionItem("Ignore distribution bit"_i18n, "Ignore the NCA distribution bit."_i18n, app->m_ignore_distribution_bit),
                MakeOptionItem("Convert to common ticket"_i18n, "Convert personalized tickets to common tickets."_i18n, app->m_convert_to_common_ticket),
                MakeOptionItem("Convert to standard crypto"_i18n, "Convert titlekey to standard crypto."_i18n, app->m_convert_to_standard_crypto),
                MakeOptionItem("Lower master key"_i18n, "Encrypt key area keys with master key 0."_i18n, app->m_lower_master_key),
                MakeOptionItem("Lower system version"_i18n, "Lower the system firmware field in metadata."_i18n, app->m_lower_system_version),
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

namespace {



void DrawActionListItem(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected) {
    const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
    const auto value = item.value ? item.value() : std::string{};
    const auto value_width = value.empty() ? 0.f : 224.f;

    if (selected) {
        gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        gfx::drawRectOutline(vg, theme, 4.f, v);
    } else {
        gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    }

    DrawSettingsItemKindIcon(vg, theme, item, v, selected);
    const auto text_x = SettingsItemTextX(item, v.x);
    const auto text_offset = text_x - v.x;

    gfx::drawTextBox(
        vg, text_x, v.y + 10.f, 20.f, v.w - 18.f - value_width - text_offset,
        theme->GetColour(label_id), item.label.c_str()
    );
    if (!item.description.empty()) {
        gfx::drawTextBox(
            vg, text_x, v.y + 39.f, 14.f, v.w - 18.f - value_width - text_offset,
            theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
        );
    }

    if (!value.empty()) {
        gfx::drawText(
            vg, v.x + v.w - 20.f, v.y + 21.f, 18.f,
            SettingsValueColour(theme, value, selected),
            value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP
        );
    }

    if (item.kind == SettingsItemKind::Folder) {
        const float x1 = v.x + v.w - 24.f;
        const float y1 = v.y + v.h / 2.f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1 - 8.f, y1 - 8.f);
        nvgLineTo(vg, x1, y1);
        nvgLineTo(vg, x1 - 8.f, y1 + 8.f);
        nvgStrokeColor(vg, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));
        nvgStrokeWidth(vg, 3.f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
}

} // namespace


SoftwareMenu::SoftwareMenu() : MenuBase{"Software", MenuFlag_None} {
    m_items = BuildSoftwareItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

SoftwareMenu::~SoftwareMenu() = default;

void SoftwareMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildSoftwareItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void SoftwareMenu::Update(Controller* controller, TouchInfo* touch) {
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

void SoftwareMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void SoftwareMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);
}

void SoftwareMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

DbiMenu::DbiMenu() : MenuBase{"DBI", MenuFlag_None} {
    m_items = BuildDbiItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

DbiMenu::~DbiMenu() = default;

void DbiMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildDbiItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void DbiMenu::Update(Controller* controller, TouchInfo* touch) {
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

void DbiMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void DbiMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);
}

void DbiMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

KefirSettingsMenu::KefirSettingsMenu() : MenuBase{"Kefir Settings", MenuFlag_None} {
    m_items = BuildKefirItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

KefirSettingsMenu::~KefirSettingsMenu() = default;

void KefirSettingsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildKefirItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void KefirSettingsMenu::Update(Controller* controller, TouchInfo* touch) {
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

void KefirSettingsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void KefirSettingsMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);
}

void KefirSettingsMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

ThemesMenu::ThemesMenu() : MenuBase{"Themes", MenuFlag_None} {
    m_items = BuildThemeItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

ThemesMenu::~ThemesMenu() = default;

void ThemesMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    m_items = BuildThemeItems();
    SetIndex(m_index);
}

void ThemesMenu::Update(Controller* controller, TouchInfo* touch) {
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

void ThemesMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void ThemesMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        RemoveAction(Button::R3);
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);

    const auto& item = m_items[m_index];
    if (item.kind == SettingsItemKind::Favorite) {
        SetAction(Button::R3, Action{"Unstar"_i18n, [this, id = item.id](){
            ini_puts("themezer_favorites", id.c_str(), nullptr, App::CONFIG_PATH);
            ini_puts("themezer_favorites", (id + "_name").c_str(), nullptr, App::CONFIG_PATH);
            ini_puts("themezer_favorites", (id + "_creator").c_str(), nullptr, App::CONFIG_PATH);
            ini_puts("themezer_favorites", (id + "_themes").c_str(), nullptr, App::CONFIG_PATH);
            App::Notify("Removed from Favorites"_i18n);

            m_items = BuildThemeItems();
            SetIndex(m_index);
        }});
    } else {
        RemoveAction(Button::R3);
    }
}

void ThemesMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

TranslateMenu::TranslateMenu() : MenuBase{"Translate Interface"_i18n, MenuFlag_None} {
    m_items = BuildTranslateItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

TranslateMenu::~TranslateMenu() = default;

void TranslateMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void TranslateMenu::Update(Controller* controller, TouchInfo* touch) {
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

void TranslateMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void TranslateMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);
}

void TranslateMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

} // namespace sphaira::ui::menu::settings
