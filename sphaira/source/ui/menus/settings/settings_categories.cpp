#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/settings/settings_sources.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_fancurve.hpp"

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
namespace {

auto MakeSaveSyncLocationItem() -> SettingsItem;

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
            "Custom Homebrew search path. Select to remove."_i18n,
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

void PickSaveDefaultLocationFolder() {
    App::Push<filepicker::Menu>(
        filepicker::LocationCallback{[](const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> bool {
            const auto backup_root = save::NormalizeBackupRoot(path, fs_entry);
            const auto is_stdio = fs_entry.type == filebrowser::FsType::Stdio;
            const save::RecentBackupDir recent{
                is_stdio,
                is_stdio ? fs_entry.root.toString() : "",
                fs_entry.name.toString(),
                backup_root,
            };
            save::PushRecentBackupDir(recent);
            App::SetSaveDefaultLocation(save::MakeLocationKey(recent));
            return true;
        }},
        std::vector<std::string>{},
        fs::FsPath{},
        true
    );
}

auto BuildSavesCategoryItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakeHeader("Show"_i18n));
    items.emplace_back(MakeBoolItem("Installed game saves"_i18n, "Show saves belonging to games that are currently installed."_i18n, App::GetSaveShowInstalled, App::SetSaveShowInstalled));
    items.emplace_back(MakeBoolItem("Deleted game saves"_i18n, "Show orphaned saves whose game is no longer installed."_i18n, App::GetSaveShowDeleted, App::SetSaveShowDeleted));
    items.emplace_back(MakeBoolItem("Backups"_i18n, "Show a tile for every game that has a save backup on the SD card."_i18n, App::GetSaveShowBackups, App::SetSaveShowBackups));

    items.emplace_back(MakeHeader("Backup"_i18n));
    items.emplace_back(SettingsItem{
        "Default location"_i18n,
        "Storage used for Backup and Restore unless you pick another."_i18n,
        [](){
            const auto key = App::GetSaveDefaultLocation();
            const auto choices = save::ListBackupLocationChoices();
            if (key.empty() && !choices.empty()) {
                return choices.front().label;
            }
            for (const auto& c : choices) {
                if (c.key == key) {
                    return c.label;
                }
            }
            return save::MakeSdLocationLabel(save::DEFAULT_BACKUP_ROOT);
        },
        [](){
            const auto choices = save::ListBackupLocationChoices();
            PopupList::Items list;
            s64 current = 0;
            const auto key = App::GetSaveDefaultLocation();
            for (size_t i = 0; i < choices.size(); i++) {
                list.push_back(choices[i].label);
                if (!key.empty() && choices[i].key == key) {
                    current = static_cast<s64>(i);
                }
            }
            const auto picker_index = static_cast<s64>(list.size());
            list.push_back("Choose Folder..."_i18n);

            App::Push<PopupList>("Default location"_i18n, std::move(list), [choices, picker_index](std::optional<s64> op_index){
                if (!op_index) {
                    return;
                }
                if (*op_index == picker_index) {
                    PickSaveDefaultLocationFolder();
                    return;
                }
                if (*op_index >= 0 && *op_index < static_cast<s64>(choices.size())) {
                    App::SetSaveDefaultLocation(choices[static_cast<size_t>(*op_index)].key);
                }
            }, current);
        }
    });
    items.emplace_back(MakeBoolItem("Compress backup"_i18n, "Save backups as compressed ZIP archives to reduce disk space."_i18n, App::GetSaveCompressBackup, App::SetSaveCompressBackup));
    items.emplace_back(MakeBoolItem("Auto backup on restore"_i18n, "Automatically create a backup before restoring a save."_i18n, App::GetSaveAutoBackupOnRestore, App::SetSaveAutoBackupOnRestore));
    items.emplace_back(MakeFolderItem("Save Backup Search Paths"_i18n, "Manage custom folders scanned for save backups."_i18n, BuildSaveBackupSearchPathsItems));

    items.emplace_back(MakeHeader("Remote"_i18n));
    items.emplace_back(MakeBoolItem("Auto-sync after backup"_i18n, "After each Backup, upload the new ZIP to the WebDAV location below."_i18n, App::GetSaveAutosync, App::SetSaveAutosync));
    items.emplace_back(MakeBoolItem("Include remote backups"_i18n, "When restoring, also list backups that exist on WebDAV but not on this console."_i18n, App::GetSaveRestoreIncludeRemote, App::SetSaveRestoreIncludeRemote));
    items.emplace_back(MakeSaveSyncLocationItem());

    return items;
}

// defaults baked into every forwarder we build. "Ask every time" swaps them
// for the forwarder editor, see ui/forwarder_editor.hpp.
auto BuildForwarderItems() -> std::vector<SettingsItem> {
    static constexpr const char* ADDRESS_SPACE_LABELS[] = { "Automatic", "36-bit", "39-bit" };
    static constexpr const char* SVC_DEBUG_LABELS[] = { "Automatic", "Enabled", "Disabled" };

    auto app = App::GetApp();
    std::vector<SettingsItem> items;

    items.emplace_back(MakeOptionItem("Ask every time"_i18n,
        "Open the forwarder editor when creating a forwarder instead of using the defaults below."_i18n,
        app->m_forwarder_ask));

    items.emplace_back(MakeHeader("Defaults"_i18n));

    items.emplace_back(SettingsItem{
        "Address space"_i18n,
        "Virtual address space given to the forwarder. Automatic uses 36-bit; 39-bit is for homebrew that needs the wider space."_i18n,
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

    items.emplace_back(MakeOptionItem("Profile selection"_i18n,
        "Prompt for a user profile when the forwarder is launched."_i18n,
        app->m_forwarder_profile_select));

    items.emplace_back(MakeOptionItem("Screenshots"_i18n,
        "Allow the capture button to take screenshots inside the forwarder."_i18n,
        app->m_forwarder_screenshot));

    // recording rides on the capture button, so denying screenshots kills it
    // too. say so in the row instead of silently writing a setting that loses.
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

// items for the "Screen off" settings page: what Minus does while the install
// queue runs, and how the screensaver it can raise is laid out.
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

    // the readout drifts slowly across the panel, so there is nothing to place
    // by hand -- a pinned layout is exactly what burns into an OLED over a long
    // queue. ponytail: which rows show, not where; add a drag-to-place editor
    // (and per-element offsets in the ini) only if the drift proves not enough.
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

auto MtpGamesLayoutLabel(long layout) -> std::string {
    switch (layout) {
        case 0: return "Compatible dump"_i18n;
        case 1: return "Separate files"_i18n;
        default: return "Both"_i18n;
    }
}

auto MtpGamesLayoutDescription(long layout) -> std::string {
    switch (layout) {
        case 0: return "One NSP per game with base, update and DLC together."_i18n;
        case 1: return "A folder per game with each component as its own NSP."_i18n;
        default: return "Merged (one NSP), Separate (folder per game) and Forwarders."_i18n;
    }
}

// items for the "MTP storages" settings page (was a side popup). values read
// live state, so toggles/names refresh in place without a rebuild.
auto BuildMtpStorageItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakeBoolItem("Show microSD card"_i18n, "Enable or disable microSD card storage in MTP."_i18n, App::GetMtpShowSd, App::SetMtpShowSd));
    items.emplace_back(MakeBoolItem("Show Install folder"_i18n, "Enable or disable Install folder in MTP."_i18n, App::GetMtpShowInstall, App::SetMtpShowInstall));
    items.emplace_back(MakeBoolItem("Show Saves (read-only)"_i18n, "Show a read-only drive with decrypted game saves. Files can be copied to the PC; writing is disabled."_i18n, App::GetMtpShowSaves, App::SetMtpShowSaves));
    items.emplace_back(MakeBoolItem("Show NAND Saves (USER:/save)"_i18n, "Show a read/write drive with raw NAND user save files (DISA containers)."_i18n, App::GetMtpShowRawSaves, App::SetMtpShowRawSaves));
    items.emplace_back(MakeBoolItem("Show NAND System Saves (SYSTEM:/save)"_i18n, "Show a read/write drive with raw NAND system save files."_i18n, App::GetMtpShowRawSystemSaves, App::SetMtpShowRawSystemSaves));
    items.emplace_back(MakeBoolItem("Show Games (read-only)"_i18n, "Show a read-only drive with installed games, updates and DLC as NSP files. Copying one to the PC dumps it; nothing is written to the microSD card."_i18n, App::GetMtpShowGames, App::SetMtpShowGames));

    items.emplace_back(SettingsItem{
        "Dump format"_i18n,
        MtpGamesLayoutDescription(App::GetMtpGamesLayout()),
        [](){ return MtpGamesLayoutLabel(App::GetMtpGamesLayout()); },
        [](){
            PopupList::Items choices = {
                "Compatible dump"_i18n,
                "Separate files"_i18n,
                "Both"_i18n,
            };
            App::Push<PopupList>("Dump format"_i18n, std::move(choices), [](std::optional<s64> op_index){
                if (op_index) {
                    App::SetMtpGamesLayout(*op_index);
                }
            }, App::GetMtpGamesLayout());
        }
    });

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

// login/port for the FTP server. The on/off switch lives on the Network page.
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


} // namespace

void Menu::BuildCategories() {
    auto* app = App::GetApp();

    m_categories = {
        {
            "General"_i18n,
            "Language, timing and application flow."_i18n,
            {
                MakeFolderItem("Auto-update"_i18n, "When and how new versions are installed."_i18n, BuildAutoUpdateItems),
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
                { "About"_i18n, "View application version and changelog."_i18n, [](){ return "v" + std::string(APP_VERSION); }, [](){
                    App::Push<AboutBox>();
                }},
                { "Restart Kefir Hub"_i18n, "Close and reopen the application."_i18n, [](){ return std::string{}; }, [](){
                    App::ExitRestart();
                }},
                { "Exit"_i18n, "Close Kefir Hub."_i18n, [](){ return std::string{}; }, [](){
                    App::Exit();
                }},
            }
        },
        {
            "Homebrew"_i18n,
            "Homebrew search paths and application options."_i18n,
            {
                MakeFolderItem("Homebrew Search Paths"_i18n, "Manage custom folders scanned for homebrew applications."_i18n, BuildHomebrewSearchPathsItems),
                MakeFolderItem("Forwarders"_i18n, "Defaults baked into forwarders you create: address space, profile selection, capture and svcDebug."_i18n, BuildForwarderItems),
                MakeBoolItem("Replace hbmenu on exit"_i18n, "Replace /hbmenu.nro with Kefir Hub on exit."_i18n, App::GetReplaceHbmenuEnable, App::SetReplaceHbmenuEnable),
            }
        },
        {
            "Saves"_i18n,
            "What the Saves menu shows, and where backups go."_i18n,
            BuildSavesCategoryItems(),
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
                MakeBoolItem("FTP"_i18n, "Run the FTP server in the background."_i18n, App::GetFtpEnable, App::SetFtpEnable),
                MakeFolderItem("FTP settings"_i18n, "Login, anonymous access and port."_i18n, BuildFtpItems),
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
                MakeFolderItem("Screen off (Minus)"_i18n, "Blank or dim the panel while a long queue runs, and choose what the screensaver shows."_i18n, BuildScreenOffItems),

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


} // namespace sphaira::ui::menu::settings
