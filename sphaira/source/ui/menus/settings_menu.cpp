#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_fancurve.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/themezer.hpp"
#include "ui/menus/uninstaller_menu.hpp"
#include "ui/menus/save/save_locations.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/sidebar.hpp"
#include "utils/devoptab_smb2.hpp"


#include "app.hpp"
#include "location.hpp"
#include "download.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "location.hpp"

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






// marks a Sources row as a user-added network location, so the per-location
// options menu is only offered on rows that actually are one.
constexpr const char* NETWORK_LOCATION_ID = "network_location";

auto MakeHeader(std::string label) -> SettingsItem {
    return { std::move(label), {}, {}, {}, SettingsItemKind::Header };
}

// headers are captions, not rows: the cursor steps over them in the direction
// it was already travelling and wraps around the ends, so a category that
// starts with a header can still be left by pressing up on its first row.
auto ResolveItemIndex(const std::vector<SettingsItem>& items, s64 index, s64 from) -> s64 {
    const auto count = static_cast<s64>(items.size());
    if (count <= 0) {
        return 0;
    }

    auto target = std::clamp<s64>(index, 0, count - 1);
    from = std::clamp<s64>(from, 0, count - 1);

    // the list wraps the index itself; comparing the two ends would read as a
    // move in the opposite direction, so name those two cases outright.
    s64 step;
    if (from == count - 1 && !target) {
        step = 1;
    } else if (!from && target == count - 1) {
        step = -1;
    } else {
        step = target >= from ? 1 : -1;
    }

    for (s64 i = 0; i < count && items[target].kind == SettingsItemKind::Header; i++) {
        target = (target + step + count) % count;
    }

    // all headers: nothing to land on, stay put.
    return items[target].kind == SettingsItemKind::Header ? from : target;
}

// a row that opens its own page in the right pane of the settings menu, with
// the category column left standing. The items are built when the folder is
// opened, so they always show current state.
auto MakeFolderItem(std::string label, std::string description, std::function<std::vector<SettingsItem>()> builder) -> SettingsItem {
    SettingsItem item{
        std::move(label),
        std::move(description),
        [](){ return std::string{}; },
        {},
        SettingsItemKind::Folder,
    };
    item.folder_items = std::move(builder);
    return item;
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

// items for the "MTP storages" settings page (was a side popup). values read
// live state, so toggles/names refresh in place without a rebuild.
auto BuildMtpStorageItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakeBoolItem("Show microSD card"_i18n, "Enable or disable microSD card storage in MTP."_i18n, App::GetMtpShowSd, App::SetMtpShowSd));
    items.emplace_back(MakeBoolItem("Show Install folder"_i18n, "Enable or disable Install folder in MTP."_i18n, App::GetMtpShowInstall, App::SetMtpShowInstall));
    items.emplace_back(MakeBoolItem("Show Saves (read-only)"_i18n, "Show a read-only drive with decrypted game saves. Files can be copied to the PC; writing is disabled."_i18n, App::GetMtpShowSaves, App::SetMtpShowSaves));

    items.emplace_back(SettingsItem{
        "microSD card name"_i18n,
        "Set custom name for microSD card in MTP."_i18n,
        [](){ const auto n = App::GetMtpNameSd(); return n.empty() ? "Default"_i18n : n; },
        [](){
            std::string value = App::GetMtpNameSd();
            if (R_SUCCEEDED(swkbd::ShowText(value, "microSD card name"_i18n.c_str(), value.c_str()))) {
                App::SetMtpNameSd(value);
            }
        }
    });

    items.emplace_back(SettingsItem{
        "Install folder name"_i18n,
        "Set custom name for Install folder in MTP."_i18n,
        [](){ const auto n = App::GetMtpNameInstall(); return n.empty() ? "Default"_i18n : n; },
        [](){
            std::string value = App::GetMtpNameInstall();
            if (R_SUCCEEDED(swkbd::ShowText(value, "Install folder name"_i18n.c_str(), value.c_str()))) {
                App::SetMtpNameInstall(value);
            }
        }
    });

    // one row per user-added folder; selecting it offers to remove it.
    for (const auto& folder : App::GetMtpFolders()) {
        items.emplace_back(SettingsItem{
            folder,
            "Folder exposed over MTP. Select to remove it."_i18n,
            [](){ return std::string{}; },
            [folder](){
                App::Push<OptionBox>(
                    "Remove this folder from MTP?"_i18n + "\n" + folder,
                    "Back"_i18n, "Remove"_i18n, 0, [folder](auto op_index){
                        if (op_index && *op_index) {
                            App::RemoveMtpFolder(folder);
                        }
                    }
                );
            }
        });
    }

    items.emplace_back(SettingsItem{
        "Add folder"_i18n,
        "Pick a folder on the microSD card to expose as its own MTP storage."_i18n,
        [](){ return std::string{}; },
        [](){
            auto browser = std::make_unique<::sphaira::ui::menu::filebrowser::Menu>(MenuFlag_None);
            browser->SetFolderPicker([](const fs::FsPath& folder){
                App::AddMtpFolder(folder.toString());
                App::Notify("Added MTP folder"_i18n);
            });
            App::Push(std::move(browser));
        },
        SettingsItemKind::Folder,
    });

    return items;
}

// items for the "FTP" settings page: the FTP server toggle plus login/port.
// folder restriction is not exposed yet - the server serves the whole SD card.
auto BuildFtpItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakeBoolItem("Enable FTP server"_i18n, "Run the FTP server in the background."_i18n, App::GetFtpEnable, App::SetFtpEnable));
    items.emplace_back(MakeBoolItem("Anonymous (no login)"_i18n, "Allow connecting without a username or password."_i18n, App::GetFtpAnon, App::SetFtpAnon));

    items.emplace_back(SettingsItem{
        "Username"_i18n,
        "FTP username (used when anonymous is off)."_i18n,
        [](){ const auto n = App::GetFtpUser(); return n.empty() ? "Not set"_i18n : n; },
        [](){
            std::string value = App::GetFtpUser();
            if (R_SUCCEEDED(swkbd::ShowText(value, "FTP username"_i18n.c_str(), value.c_str()))) {
                App::SetFtpUser(value);
            }
        }
    });

    items.emplace_back(SettingsItem{
        "Password"_i18n,
        "FTP password (used when anonymous is off)."_i18n,
        [](){ return App::GetFtpPass().empty() ? "Not set"_i18n : std::string("********"); },
        [](){
            std::string value = App::GetFtpPass();
            if (R_SUCCEEDED(swkbd::ShowText(value, "FTP password"_i18n.c_str(), value.c_str()))) {
                App::SetFtpPass(value);
            }
        }
    });

    items.emplace_back(SettingsItem{
        "Port"_i18n,
        "TCP port the FTP server listens on (default 5000)."_i18n,
        [](){ return std::to_string(App::GetFtpPort()); },
        [](){
            s64 value = App::GetFtpPort();
            if (R_SUCCEEDED(swkbd::ShowNumPad(value, "FTP port"_i18n.c_str(), std::to_string(value).c_str())) && value > 0 && value <= 65535) {
                App::SetFtpPort(value);
            }
        }
    });

    return items;
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

bool IsOptionApplicable(const std::pair<std::string, std::string>& option, SetLanguage console_lang, SetRegion console_region) {
    std::string label = option.first;
    std::string dir = option.second;
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c){ return std::tolower(c); });
    std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c){ return std::tolower(c); });
    std::string opt = label + " " + dir;

    // Detect language of the option
    bool opt_is_english = (opt.find("english") != std::string::npos || opt.find("en-") != std::string::npos || opt == "en" || opt.find(" en") != std::string::npos);
    bool opt_is_russian = (opt.find("russian") != std::string::npos || opt.find("ru-") != std::string::npos || opt == "ru" || opt.find(" ru") != std::string::npos);
    bool opt_is_french = (opt.find("french") != std::string::npos || opt.find("fr-") != std::string::npos || opt == "fr" || opt.find(" fr") != std::string::npos);
    bool opt_is_german = (opt.find("german") != std::string::npos || opt.find("de-") != std::string::npos || opt == "de" || opt.find(" de") != std::string::npos);
    bool opt_is_italian = (opt.find("italian") != std::string::npos || opt.find("it-") != std::string::npos || opt == "it" || opt.find(" it") != std::string::npos);
    bool opt_is_spanish = (opt.find("spanish") != std::string::npos || opt.find("es-") != std::string::npos || opt == "es" || opt.find(" es") != std::string::npos);
    bool opt_is_chinese = (opt.find("chinese") != std::string::npos || opt.find("zh-") != std::string::npos || opt == "zh" || opt.find(" zh") != std::string::npos);
    bool opt_is_korean = (opt.find("korean") != std::string::npos || opt.find("ko-") != std::string::npos || opt == "ko" || opt.find(" ko") != std::string::npos);
    bool opt_is_dutch = (opt.find("dutch") != std::string::npos || opt.find("nl-") != std::string::npos || opt == "nl" || opt.find(" nl") != std::string::npos);
    bool opt_is_portuguese = (opt.find("portuguese") != std::string::npos || opt.find("pt-") != std::string::npos || opt == "pt" || opt.find(" pt") != std::string::npos);
    bool opt_is_japanese = (opt.find("japanese") != std::string::npos || opt.find("ja-") != std::string::npos || opt == "ja" || opt.find(" ja") != std::string::npos);

    // Check if console language matches
    bool lang_matches = false;
    if (opt_is_english && (console_lang == SetLanguage_ENUS || console_lang == SetLanguage_ENGB)) lang_matches = true;
    else if (opt_is_russian && console_lang == SetLanguage_RU) lang_matches = true;
    else if (opt_is_french && (console_lang == SetLanguage_FR || console_lang == SetLanguage_FRCA)) lang_matches = true;
    else if (opt_is_german && console_lang == SetLanguage_DE) lang_matches = true;
    else if (opt_is_italian && console_lang == SetLanguage_IT) lang_matches = true;
    else if (opt_is_spanish && (console_lang == SetLanguage_ES || console_lang == SetLanguage_ES419)) lang_matches = true;
    else if (opt_is_chinese && (console_lang == SetLanguage_ZHCN || console_lang == SetLanguage_ZHTW || console_lang == SetLanguage_ZHHANS || console_lang == SetLanguage_ZHHANT)) lang_matches = true;
    else if (opt_is_korean && console_lang == SetLanguage_KO) lang_matches = true;
    else if (opt_is_dutch && console_lang == SetLanguage_NL) lang_matches = true;
    else if (opt_is_portuguese && (console_lang == SetLanguage_PT || console_lang == SetLanguage_PTBR)) lang_matches = true;
    else if (opt_is_japanese && console_lang == SetLanguage_JA) lang_matches = true;
    else if (!opt_is_english && !opt_is_russian && !opt_is_french && !opt_is_german && !opt_is_italian && 
             !opt_is_spanish && !opt_is_chinese && !opt_is_korean && !opt_is_dutch && !opt_is_portuguese && !opt_is_japanese) {
        lang_matches = true;
    }

    // Detect region of the option
    bool opt_is_usa = (opt.find("american") != std::string::npos || opt.find("usa") != std::string::npos || opt.find("-us") != std::string::npos || opt.find("419") != std::string::npos || opt.find("-br") != std::string::npos);
    bool opt_is_eur = (opt.find("europe") != std::string::npos || opt.find("eur") != std::string::npos || opt.find("-gb") != std::string::npos || opt.find("british") != std::string::npos);
    bool opt_is_jpn = (opt.find("japan") != std::string::npos || opt.find("jpn") != std::string::npos || opt.find("-jp") != std::string::npos);
    bool opt_is_aus = (opt.find("australia") != std::string::npos || opt.find("aus") != std::string::npos);
    bool opt_is_chn = (opt.find("china") != std::string::npos || opt.find("chn") != std::string::npos || opt.find("-cn") != std::string::npos);
    bool opt_is_kor = (opt.find("korea") != std::string::npos || opt.find("kor") != std::string::npos || opt.find("-kr") != std::string::npos);
    bool opt_is_twn = (opt.find("taiwan") != std::string::npos || opt.find("twn") != std::string::npos || opt.find("-tw") != std::string::npos);

    // Check if console region matches
    bool region_matches = false;
    if (opt_is_usa && console_region == SetRegion_USA) region_matches = true;
    else if (opt_is_eur && console_region == SetRegion_EUR) region_matches = true;
    else if (opt_is_jpn && console_region == SetRegion_JPN) region_matches = true;
    else if (opt_is_aus && (console_region == SetRegion_AUS || console_region == SetRegion_EUR)) region_matches = true;
    else if (opt_is_chn && console_region == SetRegion_CHN) region_matches = true;
    else if (opt_is_kor && console_region == SetRegion_HTK) region_matches = true;
    else if (opt_is_twn && console_region == SetRegion_HTK) region_matches = true;
    else if (!opt_is_usa && !opt_is_eur && !opt_is_jpn && !opt_is_aus && !opt_is_chn && !opt_is_kor && !opt_is_twn) {
        region_matches = true;
    }

    return lang_matches && region_matches;
}

auto BuildTranslateItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    const bool downloaded = fs::FileExists(TRANSLATE_PACKAGE);

    items.emplace_back(MakePackageAction({
        downloaded ? "Update language packs"_i18n : "Download language packs"_i18n,
        downloaded ? "Update the UltraHand language package list."_i18n : "Download the UltraHand language package list."_i18n,
        [downloaded](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                downloaded ? "Updating language packs..."_i18n : "Downloading language packs..."_i18n,
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

                u64 languageCode{};
                SetLanguage console_lang = SetLanguage_ENGB;
                if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
                    setMakeLanguage(languageCode, &console_lang);
                }
                SetRegion console_region = SetRegion_EUR;
                setGetRegionCode(&console_region);

                std::vector<std::pair<std::string, std::string>> applicable_options;
                for (const auto& opt : options) {
                    if (IsOptionApplicable(opt, console_lang, console_region)) {
                        applicable_options.push_back(opt);
                    }
                }

                if (applicable_options.empty()) {
                    std::string msg = "To apply this translation, you must select one of the required variations in the console settings:\n"_i18n;
                    for (const auto& opt : options) {
                        msg += "- " + opt.first + "\n";
                    }
                    App::Push<OptionBox>(msg, "OK"_i18n);
                    return;
                }

                PopupList::Items labels;
                labels.reserve(applicable_options.size());
                for (const auto& [label, dir] : applicable_options) {
                    labels.push_back(label);
                }

                App::Push<PopupList>(
                    "Replace language"_i18n,
                    labels,
                    [entry, applicable_options](auto index){
                        if (!index) {
                            return;
                        }

                        const auto dir = applicable_options[*index].second;
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
                                        if (R_SUCCEEDED(rc)) {
                                            return;
                                        }

                                        if (rc == Result_TranslationRemoveExistingFailed) {
                                            App::Push<OptionBox>(
                                                "The installed translation could not be replaced.\nRemove it and reboot the console?\nAfter the reboot, install the translation again."_i18n,
                                                "Cancel"_i18n, "Remove and reboot"_i18n, 1,
                                                [](auto op_index){
                                                    if (op_index && *op_index) {
                                                        App::Push<ProgressBox>(
                                                            0,
                                                            "Removing"_i18n,
                                                            "",
                                                            [](auto pbox) -> Result {
                                                                return RemoveInterfaceTranslationAndReboot(pbox);
                                                            },
                                                            [](Result remove_rc){
                                                                if (R_FAILED(remove_rc)) {
                                                                    App::PushErrorBox(remove_rc, "Failed to remove translation"_i18n);
                                                                }
                                                            }
                                                        );
                                                    }
                                                }
                                            );
                                            return;
                                        }

                                        App::PushErrorBox(rc, "Failed to install translation"_i18n);
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

// items for the "Kefir Hub theme options" page (was a side popup).
auto BuildThemeOptionItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Select Theme"_i18n,
        "Customise the look of Kefir Hub by changing the theme"_i18n,
        ThemeValue,
        [](){
            const auto themes = App::GetThemeMetaList();
            if (themes.empty()) {
                return;
            }

            PopupList::Items list;
            for (const auto& theme : themes) {
                list.push_back(theme.name);
            }
            App::Push<PopupList>("Select Theme"_i18n, std::move(list), [](std::optional<s64> op_index){
                if (op_index) {
                    App::SetTheme(*op_index);
                }
            }, App::GetThemeIndex());
        }
    });

    items.emplace_back(MakeBoolItem("12 Hour Time"_i18n, "Changes the clock to 12 hour"_i18n, App::Get12HourTimeEnable, App::Set12HourTimeEnable));

    return items;
}

// adds a location from the sync picker itself, so an empty list is a dead end
// no longer: the new WebDAV location becomes the sync target and its edit page
// opens right away, because a fresh location is only a name and "webdav://".
void AddSaveSyncLocationInteractive() {
    std::vector<std::string> before;
    for (const auto& loc : save::GetWebdavLocations()) {
        before.push_back(loc.name);
    }

    // deferred by a frame: the picker that started this is still on the widget
    // stack and about to pop itself, and pushing over it would draw both.
    evman::push(evman::FunctionalEventData{[before](){
        filebrowser::AddNetworkLocationInteractive([before](){
            // any protocol can be added here; only a WebDAV one can be synced
            // to, so a new SMB/FTP location is added and simply not selected.
            for (const auto& loc : save::GetWebdavLocations()) {
                if (std::find(before.cbegin(), before.cend(), loc.name) != before.cend()) {
                    continue;
                }

                App::SetWebdavUrl(loc.name);
                App::Push<SourceEditMenu>(loc.name);
                return;
            }
        });
    }});
}

// picks which of the WebDAV locations added above receives save backups. Not a
// folder: it is a single choice, and a page holding one row is just a detour.
auto MakeSaveSyncLocationItem() -> SettingsItem {
    return {
        "Save sync location"_i18n,
        "Network location that save backups are uploaded to. Only WebDAV locations can be used."_i18n,
        [](){
            const auto name = App::GetWebdavUrlName();
            return name.empty() ? "None"_i18n : name;
        },
        [](){
            const auto locations = save::GetWebdavLocations();

            // nothing to choose between yet: go straight to adding one.
            if (locations.empty()) {
                AddSaveSyncLocationInteractive();
                return;
            }

            PopupList::Items list;
            list.push_back("None"_i18n);

            s64 current = 0;
            for (size_t i = 0; i < locations.size(); i++) {
                list.push_back(locations[i].name);
                if (locations[i].name == App::GetWebdavUrlName()) {
                    current = static_cast<s64>(i) + 1;
                }
            }

            // last row, the way the Sources category offers it.
            const auto add_index = static_cast<s64>(list.size());
            list.push_back("+ Add network location"_i18n);

            App::Push<PopupList>("Save sync location"_i18n, std::move(list), [locations, add_index](std::optional<s64> op_index){
                if (!op_index) {
                    return;
                }
                if (*op_index == add_index) {
                    AddSaveSyncLocationInteractive();
                } else if (!*op_index) {
                    App::SetWebdavUrl("");
                } else {
                    App::SetWebdavUrl(locations[*op_index - 1].name);
                }
            }, current);
        }
    };
}

// Two blocks: the sources themselves at the top (add one, then the ones you
// have), and the knobs that govern them below the fold. Opening this category
// should put "add a source" under the cursor, not a checkbox.
auto BuildSourcesCategoryItems(Menu* menu) -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "+ Add network location"_i18n,
        "Configure a new network location (supported protocols: SMB, WebDAV, FTP, HTTP)."_i18n,
        [](){ return std::string{}; },
        [menu](){
            sphaira::ui::menu::filebrowser::AddNetworkLocationInteractive([menu](){
                menu->OnFocusGained();
            });
        }
    });

    const auto network_locations = location::Load();
    for (const auto& loc : network_locations) {
        items.emplace_back(SettingsItem{
            loc.name,
            loc.url,
            [](){ return std::string{}; },
            [loc](){
                if (loc.IsConfigured()) {
                    App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None, &loc);
                } else {
                    App::Push<SourceEditMenu>(loc.name);
                }
            },
            SettingsItemKind::Folder,
            // tags the row as an editable network location. The category also
            // holds usb storage toggles and WebDAV, which the per-location
            // options menu must not offer itself on.
            NETWORK_LOCATION_ID
        });
    }

    items.emplace_back(MakeHeader("Source settings"_i18n));

    // usb mass storage is a file source like any other, so it is configured
    // here next to the network locations rather than under "Network".
    items.emplace_back(MakeBoolItem(
        "USB storage"_i18n,
        "Mount connected USB drives next to the microSD card. Shares the USB port with MTP, so turning this on turns MTP off."_i18n,
        App::GetHddEnable, App::SetHddEnable));

    items.emplace_back(MakeBoolItem(
        "USB storage read-only"_i18n,
        "Protect connected USB drives from changes. Turn this off to allow writing, renaming, deleting and installing to them."_i18n,
        App::GetWriteProtect, App::SetWriteProtect));

    // the drives plugged in right now, so the two toggles above can be seen to
    // have taken effect without leaving the screen.
    for (const auto& e : location::GetStdio(false)) {
        items.emplace_back(SettingsItem{
            e.name,
            e.write_protect() ? "Connected, mounted read-only."_i18n : "Connected, writable."_i18n,
            [](){ return std::string{}; },
            [](){},
        });
    }

    // the save-sync target is a property of the sources above, so it sits with
    // them rather than under Network.
    items.emplace_back(MakeSaveSyncLocationItem());

    return items;
}

} // namespace

auto TestLocationConnection(const location::Entry& loc) -> Result {
    if (loc.IsSmb()) {
#ifdef BUILD_SMB2
        CSMB2FS test_smb(loc.url, "test_smb", "test_smb");
        return test_smb.CheckConnection() ? 0 : -1;
#else
        return -1;
#endif
    }

    curl::Api api(CURL_LOCATION_TO_API(loc));
    curl::ProbeType type = curl::ProbeType::Webdav;
    if (loc.url.starts_with("ftp://") || loc.url.starts_with("ftps://") || loc.protocol == "ftp") {
        type = curl::ProbeType::Ftp;
    } else if (loc.protocol == "http") {
        type = curl::ProbeType::Http;
    }

    const auto result = curl::Probe(api, type);
    log_write("[SOURCE] connection probe for %s: success=%d code=%ld\n", loc.name.c_str(), result.success, result.code);
    return result.success ? 0 : -1;
}


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

    m_category_list = std::make_unique<List>(1, 8, Vec4{76.f, 138.f, 300.f, 448.f}, Vec4{76.f, 138.f, 300.f, 56.f});
    m_category_list->SetLayout(List::Layout::GRID);
    // up/down wrap around the ends; L/R (page) and ZL/ZR (ends) are driven
    // manually in Update() instead of via the List flags, because in a
    // single-column GRID those flags also capture dpad LEFT/RIGHT, which this
    // menu uses to move between the category and item panes.
    m_category_list->SetPageJump(false);
    m_category_list->SetFastScroll(false);

    m_item_list = std::make_unique<List>(1, 7, Vec4{420.f, 132.f, 780.f, 462.f}, Vec4{420.f, 132.f, 780.f, 66.f});
    m_item_list->SetLayout(List::Layout::GRID);
    m_item_list->SetPageJump(false);
    m_item_list->SetFastScroll(false);

    SetCategoryIndex(0);
}

Menu::~Menu() = default;

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    // an open folder owns the right pane: rebuild its rows in place (a popup
    // may have changed what they read) and leave the category pane alone.
    if (m_folder_open) {
        BuildCategories();
        m_category_index = std::clamp<s64>(m_category_index, 0, static_cast<s64>(m_categories.size()) - 1);

        if (m_folder_builder) {
            const auto yoff = m_item_list->GetYoff();
            m_folder_items = m_folder_builder();
            m_item_list->SetYoff(yoff);
        }

        SetFolderIndex(m_folder_index);
        return;
    }

    std::string category_label;
    std::string item_label;
    if (!m_categories.empty()) {
        category_label = m_categories[m_category_index].label;
        const auto& items = m_categories[m_category_index].items;
        if (!items.empty() && m_item_index >= 0 && m_item_index < items.size()) {
            item_label = items[m_item_index].label;
        }
    }

    BuildCategories();
    auto it = std::find_if(m_categories.cbegin(), m_categories.cend(), [&](const auto& category) {
        return category.label == category_label;
    });

    s64 new_cat_index = (it == m_categories.cend()) ? m_category_index : std::distance(m_categories.cbegin(), it);
    float saved_yoff = m_item_list->GetYoff();
    s64 saved_item_index = m_item_index;

    SetCategoryIndex(new_cat_index);

    if (new_cat_index < m_categories.size()) {
        const auto& new_items = m_categories[new_cat_index].items;
        if (new_items.empty()) {
            m_item_index = 0;
            m_item_list->SetYoff(0.f);
            return;
        }
        auto item_it = std::find_if(new_items.cbegin(), new_items.cend(), [&](const auto& item) {
            return item.label == item_label;
        });
        if (item_it != new_items.cend()) {
            SetItemIndex(std::distance(new_items.cbegin(), item_it));
        } else {
            SetItemIndex(std::clamp<s64>(saved_item_index, 0, static_cast<s64>(new_items.size() - 1)));
        }

        if (new_cat_index == m_category_index) {
            m_item_list->SetYoff(saved_yoff);
        }
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_categories.empty()) return;
    bool focus_changed = false;
    if (touch->is_clicked) {
        if (touch->in_range(m_category_list->GetPos())) {
            if (m_focus_pane != FocusPane::Categories) {
                SetFocusPane(FocusPane::Categories);
                focus_changed = true;
            }
        } else if (touch->in_range(m_item_list->GetPos())) {
            if (m_focus_pane != FocusPane::Items) {
                SetFocusPane(FocusPane::Items);
                focus_changed = true;
            }
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

    // fast navigation of the focused pane: L/R jump a page, ZL/ZR jump to the
    // first/last entry. done here (not through List's page-jump/fast-scroll
    // flags) so it doesn't steal dpad LEFT/RIGHT from the pane switch above.
    {
        const bool cats = m_focus_pane == FocusPane::Categories;
        List* list = cats ? m_category_list.get() : m_item_list.get();
        s64 idx = cats ? CategoryRow() : CurrentItemIndex();
        const s64 cnt = cats ? CategoryRowCount() : static_cast<s64>(CurrentItems().size());

        bool moved = false;
        if (controller->GotDown(Button::R2)) {
            moved = list->ScrollToEnd(idx, cnt);
        } else if (controller->GotDown(Button::L2)) {
            moved = list->ScrollToStart(idx, cnt);
        } else if (controller->GotDown(Button::R)) {
            moved = list->ScrollPageDown(idx, cnt);
        } else if (controller->GotDown(Button::L)) {
            moved = list->ScrollPageUp(idx, cnt);
        }

        if (moved) {
            App::PlaySoundEffect(SoundEffect_Focus);
            if (cats) {
                SetCategoryRow(idx);
            } else {
                SetCurrentItemIndex(idx);
            }
        }
    }

    if (m_focus_pane == FocusPane::Categories) {
        m_category_list->OnUpdate(controller, touch, CategoryRow(), CategoryRowCount(), [this, focus_changed](bool touch, auto i) {
            if (touch && CategoryRow() == i) {
                if (!focus_changed) {
                    FireAction(Button::A);
                }
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetCategoryRow(i);
            }
        }, this);
    } else {
        m_item_list->OnUpdate(controller, touch, CurrentItemIndex(), CurrentItems().size(), [this, focus_changed](bool touch, auto i) {
            if (touch && CurrentItemIndex() == i) {
                if (!focus_changed) {
                    FireAction(Button::A);
                }
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetCurrentItemIndex(i);
            }
        }, this);
    }

    // a finger scrolls the pane it is over, not the pane that happens to hold
    // the cursor: reaching for the right hand list should not need a focus
    // change first. The cursor stays where it is either way.
    if (m_focus_pane == FocusPane::Categories) {
        m_item_list->OnUpdateTouchOnly(touch, CurrentItems().size());
    } else {
        m_category_list->OnUpdateTouchOnly(touch, CategoryRowCount());
    }

    // the pane may have changed category above, so read the live one.
    const auto& current_items = CurrentItems();
    const bool on_network_location = !m_folder_open
        && m_item_index >= 0
        && m_item_index < static_cast<s64>(current_items.size())
        && current_items[m_item_index].id == NETWORK_LOCATION_ID;

    if (m_focus_pane == FocusPane::Items && on_network_location) {
        const auto location_name = current_items[m_item_index].label;
        SetAction(Button::START, Action{"Options"_i18n, [this, location_name](){
            auto network_locations = location::Load();
            auto it = std::find_if(network_locations.begin(), network_locations.end(), [&](const auto& e) {
                return e.name == location_name;
            });
            if (it != network_locations.end()) {
                location::Entry loc = *it;
                auto options = std::make_unique<Sidebar>(loc.name, Sidebar::Side::RIGHT);

                options->Add<SidebarEntryCallback>("Enter/Connect"_i18n, [this, loc](){
                    if (loc.IsConfigured()) {
                        if (loc.IsSmb()) {
                            App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None, &loc);
                        } else {
                            App::Push<OptionBox>("Browsing is not supported for this protocol yet."_i18n, "OK"_i18n);
                        }
                    } else {
                        App::Push<SourceEditMenu>(loc.name);
                    }
                }, true, "Connect to this network location."_i18n);

                options->Add<SidebarEntryCallback>("Edit"_i18n, [loc](){
                    App::Push<SourceEditMenu>(loc.name);
                }, true, "Configure connection settings."_i18n);

                options->Add<SidebarEntryCallback>("Test Connection"_i18n, [loc](){
                    App::Push<ProgressBox>(0, "Testing Connection..."_i18n, loc.name, [loc](auto pbox) -> Result {
                        return TestLocationConnection(loc);
                    }, [loc](Result rc) {
                        filebrowser::SetSourceConnectionStatus(loc.url, R_SUCCEEDED(rc));
                        if (R_SUCCEEDED(rc)) {
                            App::Notify("Connection test successful!"_i18n);
                        } else {
                            App::Push<OptionBox>("Connection test failed!"_i18n, "OK"_i18n);
                        }
                    });
                }, true, "Test connection with current settings."_i18n);

                options->Add<SidebarEntryCallback>("Rename"_i18n, [this, loc](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Rename Network Location"_i18n.c_str(), loc.name.c_str()))) {
                        if (!out.empty() && out != loc.name) {
                            location::Remove(loc.name);
                            location::Entry new_loc = loc;
                            new_loc.name = out;
                            location::Add(new_loc);
                            App::Notify("Location renamed successfully!"_i18n);
                            OnFocusGained();
                        }
                    }
                }, true, "Rename this network location."_i18n);

                options->Add<SidebarEntryCallback>("Properties"_i18n, [loc](){
                    std::string props = "Name: "_i18n + loc.name + "\n";
                    std::string proto = loc.protocol;
                    if (proto.empty()) {
                        if (loc.IsSmb()) proto = "smb";
                        else if (loc.url.starts_with("ftp://")) proto = "ftp";
                        else if (loc.url.starts_with("http://") || loc.url.starts_with("https://")) proto = "webdav"; // fallback
                        else if (loc.url.starts_with("webdav://") || loc.url.starts_with("webdavs://")) proto = "webdav";
                    }
                    props += "Protocol: "_i18n + proto + "\n";
                    props += "URL: "_i18n + loc.url + "\n";
                    if (!loc.user.empty()) {
                        props += "Username: "_i18n + loc.user + "\n";
                    }
                    if (loc.port) {
                        props += "Port: "_i18n + std::to_string(loc.port) + "\n";
                    }
                    App::Push<OptionBox>(props, "OK"_i18n);
                }, true, "View network location properties."_i18n);

                options->Add<SidebarEntryCallback>("Delete"_i18n, [this, loc](){
                    App::Push<OptionBox>(
                        "Delete this network location?"_i18n,
                        "No"_i18n, "Yes"_i18n, 0, [this, loc](auto op_delete_idx) {
                            if (op_delete_idx && *op_delete_idx) {
                                if (loc.name == App::GetWebdavUrlName()) {
                                    App::SetWebdavUrl("");
                                }
                                location::Remove(loc.name);
                                App::Notify("Location deleted successfully!"_i18n);
                                OnFocusGained();
                            }
                        }
                    );
                }, true, "Delete this network location."_i18n);

                App::Push(std::move(options));
            }
        }});
    } else {
        RemoveAction(Button::START);
    }
}

namespace {

auto SettingsItemTextX(const SettingsItem& item, float x) -> float {
    switch (item.kind) {
        case SettingsItemKind::Normal:
        case SettingsItemKind::Header:
            return x + 18.f;
        default:
            return x + 74.f;
    }
}

void DrawSettingsItemKindIcon(NVGcontext* vg, Theme* theme, const SettingsItem& item, Vec4 v, bool selected) {
    if (item.kind == SettingsItemKind::Normal || item.kind == SettingsItemKind::Header) {
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

    m_category_list->Draw(vg, theme, CategoryRowCount(), [this](auto* vg, auto* theme, Vec4 v, auto row) {
        // while a folder is open it sits on its own row under its category,
        // indented, so the column shows where the right pane came from.
        const bool folder_row = m_folder_open && row == m_category_index + 1;
        const s64 category = (m_folder_open && row > m_category_index) ? row - 1 : row;
        const bool parent_row = m_folder_open && row == m_category_index;
        const bool selected = m_folder_open ? folder_row : (m_category_index == row);
        const auto focused = selected && m_focus_pane == FocusPane::Categories;
        const auto text_id = (selected || parent_row) ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const auto& label = folder_row ? m_folder_label : m_categories[category].label;

        if (selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        }
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        {
            const float indent = folder_row ? 30.f : 0.f;
            const float text_x = v.x + 18.f + indent;
            const float text_w = v.w - 36.f - indent;
            const float font_size = folder_row ? 18.f : 20.f;
            nvgFontSize(vg, font_size);
            nvgTextLineHeight(vg, 1.0f);
            float label_bounds[4];
            nvgTextBoxBounds(vg, text_x, 0, text_w, label.c_str(), nullptr, label_bounds);
            const float label_h = label_bounds[3] - label_bounds[1];
            const float label_y = v.y + (v.h - label_h) / 2.f;

            // a short elbow tying the indented row back to its category above.
            if (folder_row) {
                const float elbow_x = v.x + 26.f;
                const float mid_y = v.y + v.h / 2.f;
                nvgBeginPath(vg);
                nvgMoveTo(vg, elbow_x, v.y + 6.f);
                nvgLineTo(vg, elbow_x, mid_y);
                nvgLineTo(vg, elbow_x + 10.f, mid_y);
                nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_TEXT_INFO));
                nvgStrokeWidth(vg, 2.f);
                nvgLineCap(vg, NVG_ROUND);
                nvgLineJoin(vg, NVG_ROUND);
                nvgStroke(vg);
            }

            gfx::drawTextBox(
                vg, text_x, label_y, font_size, text_w,
                theme->GetColour(text_id), label.c_str()
            );
        }
    });

    if (m_categories.empty()) {
        return;
    }

    const auto& items = CurrentItems();
    m_item_list->Draw(vg, theme, items.size(), [this, &items](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto selected = CurrentItemIndex() == i;
        const auto focused = selected && m_focus_pane == FocusPane::Items;
        DrawItemRow(vg, theme, v, items[i], selected, focused);
    });
}

void Menu::DrawItemRow(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected, bool focused) {
    {
        // a section caption: a dimmed label with a rule running off to the
        // right, never highlighted because the cursor cannot land on it.
        if (item.kind == SettingsItemKind::Header) {
            const auto colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
            const float text_x = v.x + 18.f;
            const float text_y = v.y + v.h - 22.f;
            gfx::drawTextArgs(vg, text_x, text_y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", item.label.c_str());
            float bounds[4];
            nvgFontSize(vg, 15.f);
            gfx::textBounds(vg, 0, 0, bounds, item.label.c_str());
            const float rule_x = text_x + (bounds[2] - bounds[0]) + 12.f;
            gfx::drawRect(vg, rule_x, text_y + 9.f, std::max(0.f, v.x + v.w - 20.f - rule_x), 1.f,
                theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            return;
        }

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

        // one line each, never wrapped: a wrapped label would land on top of
        // the description below it. Long text scrolls while the row is
        // selected and is clipped otherwise.
        m_scroll_label.Draw(
            vg, selected, text_x, v.y + 10.f, v.w - 242.f - text_offset, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(label_id), item.label
        );
        if (!item.description.empty()) {
            m_scroll_description.Draw(
                vg, selected, text_x, v.y + 37.f, v.w - 212.f - text_offset, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), item.description
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
    }
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
                // clock sync sits with the other clock settings rather than
                // under Network: it is an outbound client, not a server.
                MakeBoolItem("Clock sync"_i18n, "Correct the console clock from an internet time server in the background."_i18n, App::GetNtpEnable, App::SetNtpEnable),
                MakeBoolItem("Logging"_i18n, "Write logs to /config/kefir/log.txt."_i18n, App::GetLogEnable, App::SetLogEnable),
                MakeBoolItem("Replace hbmenu on exit"_i18n, "Replace /hbmenu.nro with Kefir Hub on exit."_i18n, App::GetReplaceHbmenuEnable, App::SetReplaceHbmenuEnable),
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
            "Theme and visual options."_i18n,
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
                MakeBoolItem("Animated waves"_i18n, "Enable animated background waves in the bottom bar."_i18n, App::GetAnimatedWavesEnable, App::SetAnimatedWavesEnable),
                MakeFolderItem("Kefir Hub theme options"_i18n, "Select the Kefir Hub interface theme and visual options."_i18n, BuildThemeOptionItems),
            }
        },
        {
            // servers only: things a pc connects *to*. Clients that merely use
            // the network (clock sync) and file sources (WebDAV) live elsewhere.
            "Network"_i18n,
            "Servers that let a PC reach this console."_i18n,
            {
                MakeFolderItem("FTP"_i18n, "Configure the FTP server: enable, login and port."_i18n, BuildFtpItems),
                MakeBoolItem("MTP"_i18n, "Run the MTP server in the background. Shares the USB port with USB storage, so turning this on turns USB storage off."_i18n, App::GetMtpEnable, App::SetMtpEnable),
                MakeFolderItem("MTP storages"_i18n, "Configure which folders are visible over MTP and their names."_i18n, BuildMtpStorageItems),
                MakeBoolItem("Nxlink"_i18n, "Receive .nro files from a PC."_i18n, App::GetNxlinkEnable, App::SetNxlinkEnable),
            }
        },
        {
            "Sources"_i18n,
            "Storage and network locations to browse and install from."_i18n,
            BuildSourcesCategoryItems(this)
        },
        {
            "Install"_i18n,
            "Install behavior and safety switches."_i18n,
            {
                MakeHeader("Where and when"_i18n),
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
                { "Skip if already installed"_i18n, "Skip or prompt for titles or NCAs that are already installed."_i18n, [](){
                    const auto val = App::GetApp()->m_skip_if_already_installed.Get();
                    if (val >= 0 && val < 3) {
                        static constexpr const char* labels[] = {
                            "Reinstall",
                            "Skip",
                            "Prompt"
                        };
                        return i18n::get(labels[val]);
                    }
                    return std::string{};
                }, [](){
                    PopupList::Items items;
                    items.push_back("Reinstall"_i18n);
                    items.push_back("Skip"_i18n);
                    items.push_back("Prompt"_i18n);

                    App::Push<PopupList>("Already installed behaviour"_i18n, std::move(items), [](std::optional<s64> op_index){
                        if (op_index) {
                            App::GetApp()->m_skip_if_already_installed.Set(*op_index);
                        }
                    }, App::GetApp()->m_skip_if_already_installed.Get());
                }},
                MakeOptionItem("Save options globally"_i18n, "Save install options globally or locally for session."_i18n, app->m_save_settings_globally),
                MakeOptionItem("Boost CPU during transfer"_i18n, "Enable CPU boost during transfers."_i18n, app->m_progress_boost_mode),

                MakeHeader("What to install"_i18n),
                MakeOptionItem("Install tickets only"_i18n, "Install tickets without any title content."_i18n, app->m_ticket_only),
                MakeOptionItem("Skip base game"_i18n, "Skip installing base applications."_i18n, app->m_skip_base),
                MakeOptionItem("Skip game updates"_i18n, "Skip installing title updates."_i18n, app->m_skip_patch),
                MakeOptionItem("Skip DLC"_i18n, "Skip installing DLC content."_i18n, app->m_skip_addon),
                MakeOptionItem("Skip DLC updates"_i18n, "Skip installing updates for DLC (data patches)."_i18n, app->m_skip_data_patch),
                MakeOptionItem("Skip tickets"_i18n, "Skip installing tickets."_i18n, app->m_skip_ticket),

                // these used to sit under "Advanced", one category away from
                // everything else that affects an install.
                MakeHeader("Verification and conversion"_i18n),
                MakeOptionItem("Skip NCA hash verify"_i18n, "Skip SHA-256 verification over NCA content."_i18n, app->m_skip_nca_hash_verify),
                MakeOptionItem("Skip RSA header verify"_i18n, "Skip RSA NCA fixed-key header verification."_i18n, app->m_skip_rsa_header_fixed_key_verify),
                MakeOptionItem("Skip RSA NPDM verify"_i18n, "Skip RSA NPDM fixed-key verification."_i18n, app->m_skip_rsa_npdm_fixed_key_verify),
                MakeOptionItem("Ignore origin flag"_i18n, "Ignore the NCA distribution bit that marks content as gamecard or digital."_i18n, app->m_ignore_distribution_bit),
                MakeOptionItem("Convert ticket on install"_i18n, "Convert a personalized ticket to a common one while installing."_i18n, app->m_convert_to_common_ticket),
                MakeOptionItem("Convert to standard crypto"_i18n, "Convert titlekey to standard crypto."_i18n, app->m_convert_to_standard_crypto),
                MakeOptionItem("Re-encrypt to master key 0"_i18n, "Encrypt key area keys with master key 0 so older firmware can read them."_i18n, app->m_lower_master_key),
                MakeOptionItem("Lower required firmware"_i18n, "Lower the required system version recorded in the metadata."_i18n, app->m_lower_system_version),
            }
        },
        {
            "Dump"_i18n,
            "Game dump naming and transfer options."_i18n,
            {
                MakeOptionItem("Create nested folder"_i18n, "Create a nested folder for each game dump."_i18n, app->m_dump_app_folder),
                MakeOptionItem("Name XCI folder like the file"_i18n, "Append .xci to the dump folder name; some devices only read the dump when the folder matches the file exactly."_i18n, app->m_dump_append_folder_with_xci),
                MakeOptionItem("Trim XCI"_i18n, "Remove unused data from XCI dumps."_i18n, app->m_dump_trim_xci),
                MakeOptionItem("Label trimmed XCI"_i18n, "Mark trimmed XCI output names."_i18n, app->m_dump_label_trim_xci),
                MakeOptionItem("USB transfer stream"_i18n, "Stream dump output over USB."_i18n, app->m_dump_usb_transfer_stream),
                // deliberately not called "Convert to common ticket": the
                // install category has a separate option with that meaning and
                // the two used to share a label.
                MakeOptionItem("Convert ticket on dump"_i18n, "Convert a personalized ticket to a common one while dumping."_i18n, app->m_dump_convert_to_common_ticket),
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

    // a category may open on a section header; step past it.
    SetItemIndex(0);

    SetSubHeading(m_categories[m_category_index].description);
}

void Menu::SetItemIndex(s64 index) {
    const auto& items = m_categories[m_category_index].items;
    if (items.empty()) {
        m_item_index = 0;
        return;
    }

    m_item_index = ResolveItemIndex(items, index, m_item_index);
    // the cursor may have stepped over a caption or wrapped around an end,
    // neither of which the list scrolled for.
    m_item_list->EnsureVisible(m_item_index, static_cast<s64>(items.size()));
}

auto Menu::CategoryRowCount() const -> s64 {
    return static_cast<s64>(m_categories.size()) + (m_folder_open ? 1 : 0);
}

auto Menu::CategoryRow() const -> s64 {
    return m_folder_open ? m_category_index + 1 : m_category_index;
}

void Menu::SetCategoryRow(s64 row) {
    const auto count = CategoryRowCount();
    if (count <= 0) {
        return;
    }

    row = std::clamp<s64>(row, 0, count - 1);

    if (m_folder_open) {
        // still on the open folder's own row: nothing changes.
        if (row == m_category_index + 1) {
            m_category_list->EnsureVisible(row, count);
            return;
        }

        // leaving the folder's row closes it, and the rows below it shift back
        // up by one now that it is gone.
        const auto category = row <= m_category_index ? row : row - 1;
        CloseFolder();
        SetCategoryIndex(category);
    } else {
        SetCategoryIndex(row);
    }

    m_category_list->EnsureVisible(CategoryRow(), CategoryRowCount());
}

auto Menu::CurrentItems() const -> const std::vector<SettingsItem>& {
    return m_folder_open ? m_folder_items : m_categories[m_category_index].items;
}

auto Menu::CurrentItemIndex() const -> s64 {
    return m_folder_open ? m_folder_index : m_item_index;
}

void Menu::SetCurrentItemIndex(s64 index) {
    if (m_folder_open) {
        SetFolderIndex(index);
    } else {
        SetItemIndex(index);
    }
}

void Menu::SetFolderIndex(s64 index) {
    if (m_folder_items.empty()) {
        m_folder_index = 0;
        return;
    }

    m_folder_index = ResolveItemIndex(m_folder_items, index, m_folder_index);
    m_item_list->EnsureVisible(m_folder_index, static_cast<s64>(m_folder_items.size()));
}

void Menu::OpenFolder(const SettingsItem& item) {
    m_saved_item_index = m_item_index;
    m_saved_item_yoff = m_item_list->GetYoff();

    m_folder_builder = item.folder_items;
    m_folder_items = m_folder_builder();
    m_folder_label = item.label;
    m_folder_open = true;
    m_folder_index = 0;
    m_item_list->SetYoff(0);

    SetFolderIndex(0);
    SetSubHeading(item.description);
    SetFocusPane(FocusPane::Items);
    m_category_list->EnsureVisible(CategoryRow(), CategoryRowCount());
}

void Menu::CloseFolder() {
    if (!m_folder_open) {
        return;
    }

    m_folder_open = false;
    m_folder_items.clear();
    m_folder_builder = {};
    m_folder_label.clear();
    m_folder_index = 0;

    m_item_index = m_saved_item_index;
    m_item_list->SetYoff(m_saved_item_yoff);
    m_item_list->EnsureVisible(m_item_index, static_cast<s64>(m_categories[m_category_index].items.size()));
    SetSubHeading(m_categories[m_category_index].description);
}

void Menu::OnSelect() {
    if (m_categories.empty()) {
        return;
    }

    if (m_focus_pane == FocusPane::Categories) {
        SetFocusPane(FocusPane::Items);
        return;
    }

    const auto& items = CurrentItems();
    const auto index = CurrentItemIndex();
    if (index < 0 || index >= static_cast<s64>(items.size())) {
        return;
    }

    // by value: opening a folder replaces the vector this row lives in.
    const auto item = items[index];
    if (item.kind == SettingsItemKind::Folder && item.folder_items) {
        OpenFolder(item);
        return;
    }

    if (item.action) {
        item.action();
    }
}

void Menu::OnBack() {
    // a folder page hands the right pane back to its category, one step at a
    // time, instead of dropping straight out of the settings menu.
    if (m_folder_open) {
        const auto pane = m_focus_pane;
        CloseFolder();
        SetFocusPane(pane);
        m_category_list->EnsureVisible(CategoryRow(), CategoryRowCount());
        return;
    }

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

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
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

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
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

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
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

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
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

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

TranslateMenu::~TranslateMenu() = default;

void TranslateMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildTranslateItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
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

namespace {

auto GetLocationProtocol(const location::Entry& loc) -> std::string {
    if (!loc.protocol.empty()) {
        return loc.protocol;
    }
    if (loc.url.starts_with("smb://")) {
        return "smb";
    }
    if (loc.url.starts_with("ftp://") || loc.url.starts_with("ftps://")) {
        return "ftp";
    }
    if (loc.url.starts_with("http://") || loc.url.starts_with("https://")) {
        return "webdav";
    }
    return "webdav";
}

auto GetLocationProtocolLabel(const std::string& protocol) -> std::string {
    if (protocol == "smb") {
        return "Samba (SMB)";
    }
    if (protocol == "ftp") {
        return "FTP";
    }
    if (protocol == "http") {
        return "HTTP";
    }
    return "WebDAV";
}

void ChangeLocationProtocol(location::Entry& loc, const std::string& protocol) {
    std::string address;
    if (const auto scheme = loc.url.find("://"); scheme != std::string::npos) {
        address = loc.url.substr(scheme + 3);
    }

    loc.protocol = protocol;
    loc.bearer.clear();
    loc.pub_key.clear();
    loc.priv_key.clear();

    if (protocol == "smb") {
        loc.url = "smb://" + address;
        loc.port = 0;
    } else if (protocol == "webdav") {
        loc.url = "webdav://" + address;
        loc.port = 0;
    } else if (protocol == "ftp") {
        loc.url = "ftp://" + address;
        loc.port = 21;
    } else {
        loc.url = "http://" + address;
        loc.port = 0;
        loc.user.clear();
        loc.pass.clear();
    }
}

} // namespace

SourceEditMenu::SourceEditMenu(std::string name) : MenuBase{name, MenuFlag_None}, m_loc_name{name} {
    m_items = BuildEditItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

SourceEditMenu::~SourceEditMenu() = default;

void SourceEditMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    std::string item_label;
    if (!m_items.empty() && m_index >= 0 && m_index < m_items.size()) {
        item_label = m_items[m_index].label;
    }

    float saved_yoff = m_list->GetYoff();
    s64 saved_index = m_index;

    m_items = BuildEditItems();

    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });

    s64 new_index = (it == m_items.cend()) ? saved_index : std::distance(m_items.cbegin(), it);
    if (m_items.empty()) {
        m_index = 0;
        m_list->SetYoff(0.f);
        return;
    }
    new_index = std::clamp<s64>(new_index, 0, static_cast<s64>(m_items.size() - 1));
    SetIndex(new_index);
    m_list->SetYoff(saved_yoff);
}

void SourceEditMenu::Update(Controller* controller, TouchInfo* touch) {
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

void SourceEditMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void SourceEditMenu::SetIndex(s64 index) {
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

void SourceEditMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

std::vector<SettingsItem> SourceEditMenu::BuildEditItems() {
    std::vector<SettingsItem> items;
    auto network_locations = location::Load();
    auto it = std::find_if(network_locations.begin(), network_locations.end(), [&](const auto& e) {
        return e.name == m_loc_name;
    });
    if (it == network_locations.end()) {
        return items;
    }
    location::Entry loc = *it;

    const auto proto = GetLocationProtocol(loc);

    items.emplace_back(SettingsItem{
        "Protocol"_i18n,
        "Change the network protocol and edit the fields required by the new source type."_i18n,
        [proto](){ return GetLocationProtocolLabel(proto); },
        [this, proto]() {
            PopupList::Items protocols = {"Samba (SMB)", "WebDAV", "FTP", "HTTP"};
            App::Push<PopupList>("Select Protocol"_i18n, protocols, [this, proto](auto op_index) {
                if (!op_index) {
                    return;
                }

                constexpr std::array protocol_values{"smb", "webdav", "ftp", "http"};
                const auto selected = protocol_values[*op_index];
                if (proto == selected) {
                    return;
                }

                auto locations = location::Load();
                const auto entry = std::ranges::find_if(locations, [this](const auto& candidate) {
                    return candidate.name == m_loc_name;
                });
                if (entry == locations.end()) {
                    return;
                }

                auto new_loc = *entry;
                ChangeLocationProtocol(new_loc, selected);
                location::Remove(new_loc.name);
                location::Add(new_loc);
                if (new_loc.name == App::GetWebdavUrlName() && new_loc.protocol != "webdav") {
                    App::SetWebdavUrl("");
                }
                m_items = BuildEditItems();
                SetIndex(0);
                App::Notify("Source protocol changed."_i18n);
            });
        }
    });

    if (proto == "smb") {
        std::string server, share;
        if (loc.url.rfind("smb://", 0) == 0) {
            size_t host_start = 6;
            size_t slash_pos = loc.url.find('/', host_start);
            if (slash_pos == std::string::npos) {
                server = loc.url.substr(host_start);
                share = "";
            } else {
                server = loc.url.substr(host_start, slash_pos - host_start);
                share = loc.url.substr(slash_pos + 1);
            }
        }

        items.emplace_back(SettingsItem{
            "Server IP / Hostname"_i18n,
            "Samba server IP address or hostname."_i18n,
            [server](){ return server; },
            [this, loc, server, share]() mutable {
                std::string out = server;
                if (R_SUCCEEDED(swkbd::ShowText(out, "Enter server IP or hostname"_i18n.c_str(), server.c_str()))) {
                    location::Entry new_loc = loc;
                    new_loc.url = "smb://" + out + "/" + share;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                }
            }
        });

        items.emplace_back(SettingsItem{
            "Share Name"_i18n,
            "Samba shared folder name."_i18n,
            [share](){ return share; },
            [this, loc, server, share]() mutable {
                std::string out = share;
                if (R_SUCCEEDED(swkbd::ShowText(out, "Enter share name"_i18n.c_str(), share.c_str()))) {
                    location::Entry new_loc = loc;
                    new_loc.url = "smb://" + server + "/" + out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                }
            }
        });
    }
    else if (proto == "ftp") {
        std::string server;
        if (loc.url.rfind("ftp://", 0) == 0) {
            server = loc.url.substr(6);
            if (!server.empty() && server.back() == '/') {
                server.pop_back();
            }
        }

        items.emplace_back(SettingsItem{
            "Server IP / Hostname"_i18n,
            "FTP server IP address or hostname."_i18n,
            [server](){ return server; },
            [this, loc, server]() mutable {
                std::string out = server;
                if (R_SUCCEEDED(swkbd::ShowText(out, "Enter server IP or hostname"_i18n.c_str(), server.c_str()))) {
                    location::Entry new_loc = loc;
                    new_loc.url = "ftp://" + out + "/";
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                }
            }
        });

        items.emplace_back(SettingsItem{
            "Port"_i18n,
            "FTP port."_i18n,
            [loc](){ return std::to_string(loc.port); },
            [this, loc]() mutable {
                std::string out = std::to_string(loc.port);
                if (R_SUCCEEDED(swkbd::ShowText(out, "Enter port"_i18n.c_str(), std::to_string(loc.port).c_str()))) {
                    location::Entry new_loc = loc;
                    const auto parsed = std::strtoul(out.c_str(), nullptr, 10);
                    if (parsed >= 1 && parsed <= 65535) {
                        new_loc.port = static_cast<u16>(parsed);
                        location::Add(new_loc);
                        m_items = BuildEditItems();
                    }
                }
            }
        });
    }
    else { // webdav / http
        items.emplace_back(SettingsItem{
            "Server URL"_i18n,
            "Server connection URL address."_i18n,
            [loc](){ return loc.url; },
            [this, loc]() mutable {
                std::string out = loc.url;
                if (R_SUCCEEDED(swkbd::ShowText(out, "Enter Server URL"_i18n.c_str(), loc.url.c_str()))) {
                    location::Entry new_loc = loc;
                    new_loc.url = out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                }
            }
        });
    }

    if (proto != "http") {
        items.emplace_back(SettingsItem{
            "Username"_i18n,
            "Username for network connection (optional)."_i18n,
            [loc](){ return loc.user; },
            [this, loc]() mutable {
                std::string out = loc.user;
                if (R_SUCCEEDED(swkbd::ShowText(out, "Enter username"_i18n.c_str(), loc.user.c_str()))) {
                    location::Entry new_loc = loc;
                    new_loc.user = out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                }
            }
        });

        items.emplace_back(SettingsItem{
            "Password"_i18n,
            "Password for network connection (optional)."_i18n,
            [loc](){ return loc.pass.empty() ? "" : "********"; },
            [this, loc]() mutable {
                std::string out = loc.pass;
                if (R_SUCCEEDED(swkbd::ShowText(out, "Enter password"_i18n.c_str(), loc.pass.c_str()))) {
                    location::Entry new_loc = loc;
                    new_loc.pass = out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                }
            }
        });
    }

    items.emplace_back(SettingsItem{
        "Test Connection"_i18n,
        "Test connection with current settings."_i18n,
        [](){ return std::string{}; },
        [loc]() mutable {
            App::Push<ProgressBox>(0, "Testing Connection..."_i18n, loc.name, [loc](auto pbox) -> Result {
                return TestLocationConnection(loc);
            }, [loc](Result rc) {
                filebrowser::SetSourceConnectionStatus(loc.url, R_SUCCEEDED(rc));
                if (R_SUCCEEDED(rc)) {
                    App::Notify("Connection test successful!"_i18n);
                } else {
                    App::Push<OptionBox>("Connection test failed!"_i18n, "OK"_i18n);
                }
            });
        }
    });

    return items;
}

} // namespace sphaira::ui::menu::settings
