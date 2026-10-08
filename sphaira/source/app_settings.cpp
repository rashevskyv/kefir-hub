#include "app.hpp"
#include "auto_update.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "nro.hpp"
#include "evman.hpp"
#include "fs.hpp"
#include "location.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "nacp_util.hpp"
#include "image.hpp"
#include "owo.hpp"
#include <minIni.h>
#include <switch.h>
#include <cstring>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>

namespace sphaira {

extern App* g_app;
void on_i18n_change();
bool IsKefirHubNacp(const NacpStruct& nacp);

namespace {

auto GetNroIcon(const std::vector<u8>& nro_icon) -> std::vector<u8> {
    auto normalized = ImageNormalizeIcon(nro_icon);
    if (!normalized.empty()) {
        return normalized;
    }
    return ImageGetDefaultIcon();
}

} // namespace

auto App::IsHbmenu() -> bool {
    return !strcasecmp(GetExePath().s, "/hbmenu.nro");
}

auto App::GetLogEnable() -> bool {
    return g_app->m_log_enabled.Get();
}

auto App::GetAutoUpdateEnable() -> bool {
    return GetAutoUpdateMode() == static_cast<long>(auto_update::Mode::Silent);
}

auto App::GetAutoUpdateMode() -> long {
    const auto mode = g_app->m_auto_update.Get();
    if (mode < 0 || mode > 2) {
        return static_cast<long>(auto_update::Mode::Silent);
    }
    return mode;
}

void App::SetAutoUpdateMode(long mode) {
    if (mode < 0 || mode > 2) {
        mode = static_cast<long>(auto_update::Mode::Silent);
    }
    g_app->m_auto_update.Set(mode);
    if (mode == static_cast<long>(auto_update::Mode::Silent)) {
        const auto job = auto_update::GetJob();
        if (job.state == auto_update::JobState::Available) {
            auto_update::StartDownload();
        }
    }
}

auto App::GetAutoUpdateSkip() -> std::string {
    return g_app->m_auto_update_skip.Get();
}

void App::SetAutoUpdateSkip(std::string version) {
    g_app->m_auto_update_skip.Set(std::move(version));
}

auto App::GetAccountLinkPromptSkip() -> bool {
    return g_app->m_account_link_prompt_skip.Get();
}

void App::SetAccountLinkPromptSkip(bool skip) {
    g_app->m_account_link_prompt_skip.Set(skip);
}

auto App::GetReplaceHbmenuEnable() -> bool {
    return g_app->m_replace_hbmenu.Get();
}

auto App::GetInstallEnable() -> bool {
    if (IsEmummc()) {
        return GetInstallEmummcEnable();
    } else {
        return GetInstallSysmmcEnable();
    }
}

auto App::GetInstallSysmmcEnable() -> bool {
    return g_app->m_install_sysmmc.GetOr("install");
}

auto App::GetInstallEmummcEnable() -> bool {
    return g_app->m_install_emummc.GetOr("install");
}

auto App::GetInstallSdEnable() -> bool {
    long loc = g_app->m_install_location.Get();
    if (loc == 0) return true; // SdOnly
    if (loc == 1) return false; // NandOnly
    if (loc == 3) return true; // SdThenNand
    if (loc == 2) return false; // NandThenSd
    if (loc == 4) { // Auto
        s64 free_nand = 0;
        s64 free_sd = 0;
        fs::GetStorageSpaces(&free_nand, nullptr, &free_sd, nullptr);
        const s64 usable_nand = std::max<s64>(0, free_nand - GetInstallReserveMb() * 1024LL * 1024LL);
        const s64 usable_sd = std::max<s64>(0, free_sd - GetInstallReserveSdMb() * 1024LL * 1024LL);
        return usable_sd >= usable_nand;
    }
    return true;
}

auto App::GetInstallLocation() -> long {
    return g_app->m_install_location.Get();
}

auto App::GetInstallReserveMb() -> long {
    return g_app->m_install_reserve_mb.Get();
}

auto App::GetInstallReserveSdMb() -> long {
    return g_app->m_install_reserve_sd_mb.Get();
}

auto App::GetForwarderOptions() -> ForwarderOptions {
    ForwarderOptions out{};
    out.profile_selection = g_app->m_forwarder_profile_select.Get();
    // 0 = auto (39-bit), 1 = 36-bit, 2 = 39-bit, 3 = 32-bit, 4 = 32-bit (no alias).
    switch (GetForwarderAddressSpace()) {
        case 1: out.address_space = ForwarderAddressSpace::Bit36; break;
        case 3: out.address_space = ForwarderAddressSpace::Bit32; break;
        case 4: out.address_space = ForwarderAddressSpace::Bit32NoAlias; break;
        default: out.address_space = ForwarderAddressSpace::Bit39; break;
    }
    out.core_mode = GetForwarderCpuCores() == 4
        ? CpuCoreMode::Four : CpuCoreMode::Three;
    out.screenshot = g_app->m_forwarder_screenshot.Get();
    out.video_capture = g_app->m_forwarder_video_capture.Get();
    switch (g_app->m_forwarder_svc_debug.Get()) {
        case 1: out.svc_debug_mode = ForwarderSvcDebugMode::Enabled; break;
        case 2: out.svc_debug_mode = ForwarderSvcDebugMode::Disabled; break;
        default: out.svc_debug_mode = ForwarderSvcDebugMode::Automatic; break;
    }
    return out;
}

auto App::GetForwarderAsk() -> bool {
    return g_app->m_forwarder_ask.Get();
}

auto App::GetForwarderAddressSpace() -> long {
    return std::clamp<long>(g_app->m_forwarder_address_space.Get(), 0, 4);
}

void App::SetForwarderAddressSpace(long mode) {
    g_app->m_forwarder_address_space.Set(std::clamp<long>(mode, 0, 4));
}

auto App::GetForwarderCpuCores() -> long {
    return g_app->m_forwarder_cpu_cores.Get() == 4 ? 4 : 3;
}

void App::SetForwarderCpuCores(long cores) {
    g_app->m_forwarder_cpu_cores.Set(cores == 4 ? 4 : 3);
}

auto App::GetBlankMode() -> long {
    return std::clamp<long>(g_app->m_blank_mode.Get(), 0, (long)ui::BlankMode::MAX - 1);
}

auto App::GetBlankBrightness() -> long {
    // 0 would be indistinguishable from the backlight-off mode, and a level the
    // user cannot see is a level they cannot get out of by looking at it.
    return std::clamp<long>(g_app->m_blank_brightness.Get(), 1, 100);
}

auto App::GetBlankTimeout() -> long {
    return std::max<long>(0, g_app->m_blank_timeout.Get());
}

auto App::GetSaverOled() -> bool {
    return g_app->m_saver_oled.Get();
}

auto App::GetSaverFields() -> long {
    return g_app->m_saver_fields.Get() & ui::SaverField_ALL;
}

void App::SetBlankMode(long mode) {
    g_app->m_blank_mode.Set(std::clamp<long>(mode, 0, (long)ui::BlankMode::MAX - 1));
}

void App::SetBlankBrightness(long percent) {
    g_app->m_blank_brightness.Set(std::clamp<long>(percent, 1, 100));
}

void App::SetBlankTimeout(long timeout_sec) {
    g_app->m_blank_timeout.Set(std::max<long>(0, timeout_sec));
}

void App::SetSaverOled(bool enable) {
    g_app->m_saver_oled.Set(enable);
}

void App::SetSaverField(long field, bool enable) {
    const auto fields = App::GetSaverFields();
    g_app->m_saver_fields.Set(enable ? (fields | field) : (fields & ~field));
}

auto App::GetAnimatedWavesEnable() -> bool {
    return g_app->m_animated_waves.Get();
}

auto App::GetWaveColorDark() -> std::string {
    return g_app->m_wave_color_dark.Get();
}

auto App::GetWaveColorLight() -> std::string {
    return g_app->m_wave_color_light.Get();
}

auto App::GetLanguage() -> std::string {
    return g_app ? g_app->m_language.Get() : "";
}

auto App::NeedsLanguageSelection() -> bool {
    return g_app ? !g_app->m_language_chosen : false;
}

void App::MarkLanguageChosen() {
    if (g_app) {
        g_app->m_language_chosen = true;
    }
}

void App::OpenLanguageSelectDialog(bool is_initial_setup) {
    const auto languages = i18n::GetSupportedLanguages();
    ui::PopupList::Items items;
    items.reserve(languages.size());

    s64 initial_idx = 0;

    if (is_initial_setup) {
        const std::string sys_match = i18n::MatchSystemLanguage();
        bool found = false;
        if (!sys_match.empty()) {
            for (size_t i = 0; i < languages.size(); ++i) {
                if (languages[i].code == sys_match) {
                    initial_idx = static_cast<s64>(i);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (size_t i = 0; i < languages.size(); ++i) {
                if (languages[i].code == "en") {
                    initial_idx = static_cast<s64>(i);
                    break;
                }
            }
        }
    } else {
        const std::string current_code = App::GetLanguage();
        for (size_t i = 0; i < languages.size(); ++i) {
            if (languages[i].code == current_code) {
                initial_idx = static_cast<s64>(i);
                break;
            }
        }
    }

    for (const auto& lang : languages) {
        items.push_back(lang.name);
    }

    auto popup = std::make_unique<ui::PopupList>(
        "Language"_i18n,
        std::move(items),
        [is_initial_setup](std::optional<s64> op_index){
            if (!op_index) {
                return;
            }
            const auto languages = i18n::GetSupportedLanguages();
            const s64 idx = *op_index;
            if (idx >= 0 && idx < static_cast<s64>(languages.size())) {
                const auto& def = languages[idx];
                const std::string before{i18n::GetCurrentLanguageCode()};
                App::SetLanguage(def.code, !is_initial_setup);
                if (is_initial_setup) {
                    App::MarkLanguageChosen();
                    // The main menu behind the first-start page was built in the console's
                    // language and its tiles cache their labels. The choice is already in
                    // config.ini: a different language restarts once so every tile, hint and
                    // dialog uses it. The user is told first, so the restart is not a crash.
                    if (def.code != before) {
                        App::Push<ui::OptionBox>(
                            "Kefir Hub restarts now to apply the language. If it does not come back, start it from its HOME Menu icon."_i18n,
                            "OK"_i18n, [](auto){ App::ExitRestart(); });
                    }
                }
            }
        },
        initial_idx
    );

    if (is_initial_setup) {
        popup->SetAllowCancel(false);
    }

    App::Push(std::move(popup));
}

void App::ShowInitialLanguageSelection() {
    OpenLanguageSelectDialog(true);
}

auto App::GetTextScrollSpeed() -> long {
    return g_app->m_text_scroll_speed.Get();
}

auto App::GetGodModeEnabled() -> bool {
    return g_app->m_god_mode.Get();
}

static std::atomic<bool> g_progress_active{false};
 
auto App::GetProgressActive() -> bool {
    return g_progress_active;
}
 
void App::SetProgressActive(bool active) {
    g_progress_active = active;
}
 
auto App::Get12HourTimeEnable() -> bool {
    return g_app->m_12hour_time.Get();
}

void App::SetLogEnable(bool enable) {
    if (App::GetLogEnable() != enable) {
        g_app->m_log_enabled.Set(enable);
        if (enable) {
            log_file_init();
        } else {
            log_file_exit();
        }
    }
}

void App::SetAutoUpdateEnable(bool enable) {
    SetAutoUpdateMode(enable
        ? static_cast<long>(auto_update::Mode::Silent)
        : static_cast<long>(auto_update::Mode::Off));
}

void App::SetReplaceHbmenuEnable(bool enable) {
    if (App::GetReplaceHbmenuEnable() != enable) {
        g_app->m_replace_hbmenu.Set(enable);
        if (!enable) {
            // check we have already replaced hbmenu with sphaira
            NacpStruct hbmenu_nacp{};
            if (R_SUCCEEDED(nro_get_nacp("/hbmenu.nro", hbmenu_nacp))) {
                if (!IsKefirHubNacp(hbmenu_nacp)) {
                    return;
                }
            }

            // ask user if they want to restore hbmenu
            App::Push<ui::OptionBox>(
                "Restore hbmenu?"_i18n,
                "Back"_i18n, "Restore"_i18n, 1, [hbmenu_nacp](auto op_index){
                    if (!op_index || *op_index == 0) {
                        return;
                    }

                    NacpStruct actual_hbmenu_nacp;
                    if (R_FAILED(nro_get_nacp("/switch/hbmenu.nro", actual_hbmenu_nacp))) {
                        App::Push<ui::OptionBox>(
                            "Failed to find /switch/hbmenu.nro\n"
                            "Use the Appstore to re-install hbmenu"_i18n,
                            "OK"_i18n
                        );
                        return;
                    }

                    // NOTE: do NOT use rename anywhere here as it's possible
                    // to have a race condition with another app that opens hbmenu as a file
                    // in between the delete + rename.
                    // this would require a sys-module to open hbmenu.nro, such as an ftp server.
                    // a copy means that it opens the file handle, if successfull, then
                    // the full read/write will succeed.
                    fs::FsNativeSd fs;
                    NacpStruct sphaira_nacp;
                    fs::FsPath sphaira_path = "/switch/kefir-hub/kefir-hub.nro";
                    Result rc;

                    // first, try and backup sphaira, its not super important if this fails.
                    rc = nro_get_nacp(sphaira_path, sphaira_nacp);
                    if (R_FAILED(rc) || !IsKefirHubNacp(sphaira_nacp)) {
                        sphaira_path = "/switch/kefir-hub.nro";
                        rc = nro_get_nacp(sphaira_path, sphaira_nacp);
                    }

                    if (R_SUCCEEDED(rc) && IsKefirHubNacp(sphaira_nacp)) {
                        if (IsVersionNewer(sphaira_nacp.display_version, hbmenu_nacp.display_version)) {
                            if (R_FAILED(rc = fs.copy_entire_file(sphaira_path, "/hbmenu.nro"))) {
                                log_write("failed to copy entire file: %s 0x%X module: %u desc: %u\n", sphaira_path.s, rc, R_MODULE(rc), R_DESCRIPTION(rc));
                            } else {
                                log_write("success with updating hbmenu!\n");
                            }
                        }
                    } else {
                        // sphaira doesn't yet exist, create a new file.
                        sphaira_path = "/switch/kefir-hub/kefir-hub.nro";
                        fs.CreateDirectoryRecursively("/switch/kefir-hub/");
                        fs.copy_entire_file(sphaira_path, "/hbmenu.nro");
                    }

                    // this should never fail, if it does, well then the sd card is fucked.
                    if (R_FAILED(rc = fs.copy_entire_file("/hbmenu.nro", "/switch/hbmenu.nro")))  {
                        // try and restore sphaira in a last ditch effort.
                        if (R_FAILED(rc = fs.copy_entire_file("/hbmenu.nro", sphaira_path))) {
                            App::PushErrorBox(rc,
                                "Failed to restore hbmenu, please re-download hbmenu"_i18n
                            );
                        } else {
                            App::Push<ui::OptionBox>(
                                "Failed to restore hbmenu, using Kefir Hub instead"_i18n,
                                "OK"_i18n
                            );
                        }
                        return;
                    }

                    // don't need this any more.
                    fs.DeleteFile("/switch/hbmenu.nro");

                    // if we were hbmenu, exit now (as romfs is gone).
                    if (IsHbmenu()) {
                        App::Push<ui::OptionBox>(
                            "Restored hbmenu, closing Kefir Hub"_i18n,
                            "OK"_i18n, [](auto) {
                                App::Exit();
                            }
                        );
                    } else {
                        App::Notify("Restored hbmenu"_i18n);
                    }
                }
            );
        }
    }
}

void App::SetInstallLocation(long location) {
    g_app->m_install_location.Set(location);
}

void App::SetInstallReserveMb(long reserve_mb) {
    g_app->m_install_reserve_mb.Set(reserve_mb);
}

void App::SetInstallReserveSdMb(long reserve_mb) {
    g_app->m_install_reserve_sd_mb.Set(reserve_mb);
}

void App::SetAnimatedWavesEnable(bool enable) {
    g_app->m_animated_waves.Set(enable);
}

void App::Set12HourTimeEnable(bool enable) {
    g_app->m_12hour_time.Set(enable);
}

void App::SetLanguage(const std::string& code, bool prompt_restart) {
    const bool changed = (App::GetLanguage() != code);
    if (changed || !g_app->m_language_chosen) {
        g_app->m_language.Set(code);
        on_i18n_change();

        if (prompt_restart && changed) {
            App::Push<ui::OptionBox>(
                "Restart Kefir Hub?"_i18n,
                "Back"_i18n, "Restart"_i18n, 1, [](auto op_index){
                    if (op_index && *op_index) {
                        App::ExitRestart();
                    }
                }
            );
        }
    }
}

void App::SetTextScrollSpeed(long index) {
    g_app->m_text_scroll_speed.Set(index);
}

auto App::Install(OwoConfig& config) -> Result {
    config.options = config.options.value_or(GetForwarderOptions());
    App::Push<ui::ProgressBox>(0, "Installing Forwarder"_i18n, config.name, [config](auto pbox) mutable -> Result {
        return Install(pbox, config);
    }, [](Result rc){
        App::PushErrorBox(rc, "Failed to install forwarder"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::PlaySoundEffect(SoundEffect_Install);
            App::Notify("Installed!"_i18n);
        }
    });

    R_SUCCEED();
}

auto App::Install(ui::ProgressBox* pbox, OwoConfig& config) -> Result {
    config.nro_path = nro_add_arg_file(config.nro_path);
    if (config.icon.empty()) {
        config.icon = ImageGetDefaultIcon();
    } else {
        config.icon = GetNroIcon(config.icon);
    }

    if (config.logo.empty()) {
        fs::FsNativeSd().read_entire_file(paths::LOGO + "/NintendoLogo.png", config.logo);
    }

    if (config.gif.empty()) {
        fs::FsNativeSd().read_entire_file(paths::LOGO + "/StartupMovie.gif", config.gif);
    }

    return install_forwarder(pbox, config, GetInstallSdEnable() ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser);
}

auto App::IsOledModel() -> bool {
    static int s_is_oled = -1;
    if (s_is_oled != -1) {
        return s_is_oled == 1;
    }

    s_is_oled = 0;
#ifdef __SWITCH__
    if (R_SUCCEEDED(splInitialize())) {
        u64 hardware_type = 0;
        if (R_SUCCEEDED(splGetConfig(SplConfigItem_HardwareType, &hardware_type))) {
            // 5 = Aula (Nintendo Switch OLED model)
            if (hardware_type == 5) {
                s_is_oled = 1;
            }
        }
        splExit();
    }
#endif
    return s_is_oled == 1;
}

auto App::IsEmummc() -> bool {
    return g_app ? g_app->m_is_emummc : false;
}

auto App::HasEmummc() -> bool {
    if (IsEmummc()) {
        return true;
    }

    // Check configuration files on SD card
    for (const char* ini_path : {"/emummc/emummc.ini", "/emuMMC/emummc.ini"}) {
        if (fs::FileExists(ini_path)) {
            char path_buf[128]{};
            char sector_buf[128]{};
            char nintendo_buf[128]{};
            const long enabled = ini_getl("emummc", "enabled", 0, ini_path);
            ini_gets("emummc", "path", "", path_buf, sizeof(path_buf), ini_path);
            ini_gets("emummc", "sector", "", sector_buf, sizeof(sector_buf), ini_path);
            ini_gets("emummc", "nintendo_path", "", nintendo_buf, sizeof(nintendo_buf), ini_path);

            const bool has_sector = (sector_buf[0] != '\0' && std::strcmp(sector_buf, "0x0") != 0 && std::strcmp(sector_buf, "0") != 0);
            if (enabled != 0 || path_buf[0] != '\0' || has_sector || nintendo_buf[0] != '\0') {
                return true;
            }
        }
    }

    // Check well-known EmuNAND directories on SD card
    for (const char* dir_path : {"/emuMMC/RAW1", "/emuMMC/RAW2", "/emuMMC/SD00", "/emuMMC/SD01", "/emuMMC/ER00",
                                 "/emummc/RAW1", "/emummc/RAW2", "/emummc/SD00", "/emummc/SD01", "/emummc/ER00"}) {
        if (fs::DirExists(dir_path)) {
            return true;
        }
    }

    return false;
}

auto App::IsParitionBaseEmummc() -> bool {
    return g_app && g_app->m_is_emummc && (g_app->m_emummc_type == 1);
}

auto App::IsFileBaseEmummc() -> bool {
    return g_app && g_app->m_is_emummc && (g_app->m_emummc_type == 2);
}

auto App::GetEmummcNintendoPath() -> std::string {
    // exosphere reports the config of the *booted* nand, so this is empty on sysmmc.
    if (!IsEmummc()) {
        return {};
    }

    // the redirected nand folder of the booted emummc, eg "emuMMC/SD00/Nintendo".
    const auto& raw = g_app->m_emummc_paths.nintendo;
    std::string path(raw, strnlen(raw, sizeof(g_app->m_emummc_paths.nintendo)));

    // fallback to the config file in case exosphere gave us nothing.
    if (path.empty()) {
        char buf[FS_MAX_PATH]{};
        if (ini_gets("emummc", "nintendo_path", "", buf, sizeof(buf), "/emummc/emummc.ini") > 0) {
            path = buf;
        }
    }

    // an emummc without a redirect shares /Nintendo with sysmmc.
    if (path.empty()) {
        return {};
    }

    if (path[0] != '/') {
        path.insert(path.begin(), '/');
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    return path;
}

void App::Exit() {
    g_app->m_quit = true;
}

void App::ExitRestart() {
    nro_launch(GetExePath());
    Exit();
}

} // namespace sphaira
