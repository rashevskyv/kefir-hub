#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/settings/settings_sources.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/about_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/screensaver.hpp"
#include "ui/steamgriddb_icon.hpp"
#include "app.hpp"
#include "auto_update.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "swkbd.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace sphaira::ui::menu::settings {

auto AutoUpdateModeLabel(long mode) -> std::string {
    switch (mode) {
        case 0: return "Off"_i18n;
        case 1: return "Silent"_i18n;
        case 2: return "Ask"_i18n;
        default: return "Silent"_i18n;
    }
}

auto AutoUpdateModeDescription(long mode) -> std::string {
    switch (mode) {
        case 0: return "Don't check for updates."_i18n;
        case 1: return "Download in the background. Next launch uses the new version."_i18n;
        case 2: return "Popup when a new version is found. Later, skip this version, or update now."_i18n;
        default: return AutoUpdateModeDescription(1);
    }
}

auto UpdateNowValue() -> std::string {
    const auto job = auto_update::GetJob();
    switch (job.state) {
        case auto_update::JobState::Downloading:
        case auto_update::JobState::Installing:
            return "Updating"_i18n;
        case auto_update::JobState::Ready:
            return "Ready — restart"_i18n;
        case auto_update::JobState::Available:
            return job.version.empty() ? "Update"_i18n : job.version;
        case auto_update::JobState::Failed:
            return "Failed"_i18n;
        case auto_update::JobState::Checking:
            return "Checking..."_i18n;
        default:
            return "Up to date"_i18n;
    }
}

auto BuildAutoUpdateItems() -> std::vector<SettingsItem> {
    const auto mode = App::GetAutoUpdateMode();
    std::vector<SettingsItem> items = {
        { "When to install"_i18n, AutoUpdateModeDescription(mode), [](){
            return AutoUpdateModeLabel(App::GetAutoUpdateMode());
        }, [](){
            PopupList::Items choices = {
                "Off"_i18n,
                "Silent"_i18n,
                "Ask"_i18n,
            };
            App::Push<PopupList>("Auto-update"_i18n, std::move(choices), [](std::optional<s64> op_index){
                if (op_index) {
                    App::SetAutoUpdateMode(*op_index);
                }
            }, App::GetAutoUpdateMode());
        }},
        { "Update now"_i18n,
          auto_update::GetJob().state == auto_update::JobState::Ready
              ? "The new version is installed. Tap to restart."_i18n
              : "Download a waiting release, or retry a failed download."_i18n,
          UpdateNowValue, [](){
            const auto job = auto_update::GetJob();
            if (job.state == auto_update::JobState::Ready) {
                App::ExitRestart();
                return;
            }
            if (job.state == auto_update::JobState::Available || job.state == auto_update::JobState::Failed) {
                auto_update::StartDownload();
            }
        }},
    };

    const auto skipped = App::GetAutoUpdateSkip();
    if (!skipped.empty()) {
        items.push_back({
            "Skipped version"_i18n,
            "Tap to ask about this version again."_i18n,
            [](){ return App::GetAutoUpdateSkip(); },
            [](){ App::SetAutoUpdateSkip(""); },
        });
    }

    return items;
}

auto BuildHomebrewSearchPathsItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Add folder"_i18n,
        "Pick a folder on the microSD card to add as a homebrew search path."_i18n,
        [](){ return std::string{}; },
        [](){
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[](const fs::FsPath& path, const filepicker::FsEntry& fs_entry) -> bool {
                    if (fs_entry.type != filepicker::FsType::Sd) {
                        App::Notify("Only microSD folders can be used"_i18n);
                        return false;
                    }
                    if (!homebrew::AddSearchPath(path)) {
                        App::Notify("Failed to add Homebrew search path"_i18n);
                        return false;
                    }
                    App::Notify("Homebrew search path added."_i18n);
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{},
                true
            );
        },
        SettingsItemKind::Folder,
    });

    for (const auto& path_str : homebrew::GetSearchPaths()) {
        const fs::FsPath path{path_str};
        items.emplace_back(SettingsItem{
            path_str,
            "Custom homebrew search path. Select to remove."_i18n,
            [](){ return std::string{}; },
            [path](){
                const auto prompt = "Remove Homebrew Search Path?"_i18n + "\n\n" + path.toString();
                App::Push<OptionBox>(
                    prompt,
                    "Back"_i18n,
                    "Delete"_i18n,
                    0,
                    [path](auto op_index){
                        if (op_index && *op_index == 1) {
                            if (homebrew::RemoveSearchPath(path)) {
                                App::Notify("Homebrew search path removed."_i18n);
                            } else {
                                App::Notify("Failed to remove Homebrew search path"_i18n);
                            }
                        }
                    }
                );
            }
        });
    }

    return items;
}

auto BuildSaveBackupSearchPathsItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Add folder"_i18n,
        "Pick a folder on the microSD card to add as a save backup search path."_i18n,
        [](){ return std::string{}; },
        [](){
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[](const fs::FsPath& path, const filepicker::FsEntry& fs_entry) -> bool {
                    if (fs_entry.type != filepicker::FsType::Sd) {
                        App::Notify("Only microSD folders can be used"_i18n);
                        return false;
                    }
                    if (!save::AddBackupSearchPath(path)) {
                        App::Notify("Failed to add save backup search path"_i18n);
                        return false;
                    }
                    App::Notify("Save backup search path added."_i18n);
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{},
                true
            );
        },
        SettingsItemKind::Folder,
    });

    for (const auto& path_str : save::GetBackupSearchPaths()) {
        const fs::FsPath path{path_str};
        items.emplace_back(SettingsItem{
            path_str,
            "Custom save backup search path. Select to remove."_i18n,
            [](){ return std::string{}; },
            [path](){
                const auto prompt = "Remove Save Backup Search Path?"_i18n + "\n\n" + path.toString();
                App::Push<OptionBox>(
                    prompt,
                    "Back"_i18n,
                    "Delete"_i18n,
                    0,
                    [path](auto op_index){
                        if (op_index && *op_index == 1) {
                            if (save::RemoveBackupSearchPath(path)) {
                                App::Notify("Save backup search path removed."_i18n);
                            } else {
                                App::Notify("Failed to remove save backup search path"_i18n);
                            }
                        }
                    }
                );
            }
        });
    }

    return items;
}

auto BuildFtpItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

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

auto BuildForwarderItems() -> std::vector<SettingsItem> {
    static constexpr const char* ADDRESS_SPACE_LABELS[] = { "Automatic", "36-bit", "39-bit", "32-bit", "32-bit (no alias)" };
    static constexpr const char* CPU_CORE_LABELS[] = { "3 cores", "4 cores" };
    static constexpr const char* SVC_DEBUG_LABELS[] = { "Automatic", "Enabled", "Disabled" };

    auto app = App::GetApp();
    std::vector<SettingsItem> items;

    items.emplace_back(MakeOptionItem("Ask every time"_i18n,
        "Open the forwarder editor when creating a forwarder instead of using the defaults below."_i18n,
        app->m_forwarder_ask));

    items.emplace_back(MakeHeader("Defaults"_i18n));

    items.emplace_back(SettingsItem{
        "Address space"_i18n,
        "How much virtual memory the app gets. Leave Automatic (39-bit). 36-bit: only if an old app does not start. 32-bit and 32-bit (no alias): only when the app itself asks for it (Wine-NX, Box64)."_i18n,
        [](){ return i18n::get(ADDRESS_SPACE_LABELS[App::GetForwarderAddressSpace()]); },
        [](){
            PopupList::Items list;
            for (const auto& label : ADDRESS_SPACE_LABELS) {
                list.push_back(i18n::get(label));
            }
            App::Push<PopupList>("Address space"_i18n, std::move(list), [](std::optional<s64> op_index){
                if (op_index) {
                    App::SetForwarderAddressSpace(*op_index);
                }
            }, App::GetForwarderAddressSpace());
        }
    });

    items.emplace_back(SettingsItem{
        "CPU cores"_i18n,
        "CPU cores available to the forwarder. Default is 3 cores; 4 cores unlocks core 3 for demanding homebrew."_i18n,
        [](){ return i18n::get(CPU_CORE_LABELS[App::GetForwarderCpuCores() == 4 ? 1 : 0]); },
        [](){
            PopupList::Items list;
            for (const auto& label : CPU_CORE_LABELS) {
                list.push_back(i18n::get(label));
            }
            App::Push<PopupList>("CPU cores"_i18n, std::move(list), [](std::optional<s64> op_index){
                if (!op_index) {
                    return;
                }
                if (*op_index == 1) {
                    App::Push<OptionBox>(
                        "Core 3 is shared with system services. Homebrew without proper thread affinity may cause lag or instability."_i18n,
                        "Cancel"_i18n, "Enable"_i18n, 0,
                        [](std::optional<s64> opt) {
                            if (opt && *opt == 1) {
                                App::SetForwarderCpuCores(4);
                            }
                        }
                    );
                } else {
                    App::SetForwarderCpuCores(3);
                }
            }, App::GetForwarderCpuCores() == 4 ? 1 : 0);
        }
    });

    items.emplace_back(MakeOptionItem("Profile selection"_i18n,
        "Prompt for a user profile when the forwarder is launched."_i18n,
        app->m_forwarder_profile_select));

    items.emplace_back(MakeOptionItem("Screenshots"_i18n,
        "Allow the capture button to take screenshots inside the forwarder."_i18n,
        app->m_forwarder_screenshot));

    items.emplace_back(SettingsItem{
        "Video capture"_i18n,
        "Allow holding the capture button to record video inside the forwarder. Requires screenshots."_i18n,
        [](){
            if (!App::GetApp()->m_forwarder_screenshot.Get()) {
                return "Off (needs screenshots)"_i18n;
            }
            return OnOff(App::GetApp()->m_forwarder_video_capture.Get());
        },
        [](){
            if (!App::GetApp()->m_forwarder_screenshot.Get()) {
                App::Notify("Enable screenshots first"_i18n);
                return;
            }
            auto& option = App::GetApp()->m_forwarder_video_capture;
            option.Set(!option.Get());
        }
    });

    items.emplace_back(SettingsItem{
        "svcDebug"_i18n,
        "Kernel debug permission for the forwarder. Automatic enables it on Atmosphere 1.8.0 and newer."_i18n,
        [](){ return i18n::get(SVC_DEBUG_LABELS[std::clamp<long>(App::GetApp()->m_forwarder_svc_debug.Get(), 0, 2)]); },
        [](){
            PopupList::Items list;
            for (const auto& label : SVC_DEBUG_LABELS) {
                list.push_back(i18n::get(label));
            }
            App::Push<PopupList>("svcDebug"_i18n, std::move(list), [](std::optional<s64> op_index){
                if (op_index) {
                    App::GetApp()->m_forwarder_svc_debug.Set(*op_index);
                }
            }, std::clamp<long>(App::GetApp()->m_forwarder_svc_debug.Get(), 0, 2));
        }
    });

    items.emplace_back(MakeHeader("Icons"_i18n));

    items.emplace_back(SettingsItem{
        "SteamGridDB API key"_i18n,
        "Personal key used to look up forwarder icons. Set it from a phone: the console shows a QR code and you paste the key there."_i18n,
        [](){ return ui::steamgriddb::GetApiKey().empty() ? "Not set"_i18n : "Set"_i18n; },
        [](){
            if (ui::steamgriddb::GetApiKey().empty()) {
                ui::steamgriddb::RequestApiKey();
                return;
            }

            App::Push<OptionBox>(
                "SteamGridDB API key"_i18n, "Remove"_i18n, "Replace"_i18n, 1, [](auto op_index){
                    if (!op_index) {
                        return;
                    }
                    if (*op_index) {
                        ui::steamgriddb::RequestApiKey();
                    } else {
                        ui::steamgriddb::SetApiKey("");
                        App::Notify("SteamGridDB key removed"_i18n);
                    }
                }
            );
        }
    });

    return items;
}

auto BuildScreenOffItems() -> std::vector<SettingsItem> {
    static constexpr const char* MODE_LABELS[] = {
        "Lower brightness",
        "Turn off backlight",
        "Screensaver",
    };
    static constexpr long BRIGHTNESS_STEPS[] = { 1, 5, 10, 20, 30, 50 };
    static constexpr const char* TIMEOUT_LABELS[] = {
        "Off",
        "30 s",
        "1 min",
        "2 min",
        "5 min",
        "10 min",
    };
    static constexpr long TIMEOUT_STEPS[] = { 0, 30, 60, 120, 300, 600 };

    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Minus button"_i18n,
        "What pressing Minus does while the install queue is running."_i18n,
        [](){ return i18n::get(MODE_LABELS[App::GetBlankMode()]); },
        [](){
            PopupList::Items list;
            for (const auto& label : MODE_LABELS) {
                list.push_back(i18n::get(label));
            }
            App::Push<PopupList>("Minus button"_i18n, std::move(list), [](std::optional<s64> op_index){
                if (op_index) {
                    App::SetBlankMode(*op_index);
                }
            }, App::GetBlankMode());
        }
    });

    items.emplace_back(SettingsItem{
        "Inactivity timeout"_i18n,
        "Automatically start the screen off mode after a period of inactivity during installation."_i18n,
        [](){
            const long timeout = App::GetBlankTimeout();
            for (size_t i = 0; i < std::size(TIMEOUT_STEPS); i++) {
                if (TIMEOUT_STEPS[i] == timeout) {
                    return i18n::get(TIMEOUT_LABELS[i]);
                }
            }
            return i18n::get("Off");
        },
        [](){
            PopupList::Items list;
            s64 index = 0;
            const long timeout = App::GetBlankTimeout();
            for (size_t i = 0; i < std::size(TIMEOUT_STEPS); i++) {
                list.push_back(i18n::get(TIMEOUT_LABELS[i]));
                if (TIMEOUT_STEPS[i] == timeout) {
                    index = i;
                }
            }
            App::Push<PopupList>("Inactivity timeout"_i18n, std::move(list), [](std::optional<s64> op_index){
                if (op_index) {
                    App::SetBlankTimeout(TIMEOUT_STEPS[*op_index]);
                }
            }, index);
        }
    });

    items.emplace_back(SettingsItem{
        "Brightness"_i18n,
        "Panel brightness while the screen is lowered. Ignored when the backlight is turned off."_i18n,
        [](){ return std::to_string(App::GetBlankBrightness()) + "%"; },
        [](){
            PopupList::Items list;
            s64 index = 0;
            for (size_t i = 0; i < std::size(BRIGHTNESS_STEPS); i++) {
                list.push_back(std::to_string(BRIGHTNESS_STEPS[i]) + "%");
                if (BRIGHTNESS_STEPS[i] == App::GetBlankBrightness()) {
                    index = i;
                }
            }
            App::Push<PopupList>("Brightness"_i18n, std::move(list), [](std::optional<s64> op_index){
                if (op_index) {
                    App::SetBlankBrightness(BRIGHTNESS_STEPS[*op_index]);
                }
            }, index);
        }
    });

    items.emplace_back(MakeBoolItem("OLED mode"_i18n,
        "Light only the pixels that carry information: the empty part of the progress bar is left black."_i18n,
        App::GetSaverOled, App::SetSaverOled));

    items.emplace_back(SettingsItem{
        "Preview"_i18n,
        "Show the screensaver at the brightness it will actually run at. Any button exits."_i18n,
        [](){ return std::string{}; },
        [](){ App::Push<SaverPreview>(); }
    });

    items.emplace_back(MakeHeader("Show on screensaver"_i18n));

    const auto field = [&items](SaverField bit, std::string label, std::string description) {
        items.emplace_back(MakeBoolItem(std::move(label), std::move(description),
            [bit](){ return (App::GetSaverFields() & bit) != 0; },
            [bit](bool enable){ App::SetSaverField(bit, enable); }));
    };

    field(SaverField_Clock, "Clock"_i18n, "Show the current time."_i18n);
    field(SaverField_Status, "Status"_i18n, "Show what the queue is doing."_i18n);
    field(SaverField_Counter, "Package counter"_i18n, "Show which package of how many is being installed."_i18n);
    field(SaverField_File, "Current file"_i18n, "Show the package and file being written."_i18n);
    field(SaverField_Bar, "Progress bar"_i18n, "Show the whole-queue progress bar and percentage."_i18n);
    field(SaverField_Speed, "Average speed"_i18n, "Show the average write speed."_i18n);
    field(SaverField_Eta, "Time remaining"_i18n, "Show the estimated time left for the whole queue."_i18n);
    field(SaverField_Elapsed, "Elapsed time"_i18n, "Show how long the queue has been running."_i18n);
    field(SaverField_Battery, "Battery"_i18n, "Show the battery level and whether it is charging."_i18n);
    field(SaverField_Errors, "Errors"_i18n, "Show the failure count, once anything has failed."_i18n);
    field(SaverField_Graph, "Speed graph"_i18n, "Show the live installation read/write speed graph."_i18n);

    return items;
}

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

} // namespace sphaira::ui::menu::settings
