#include "app.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "nro.hpp"
#include "evman.hpp"
#include "nxlink.h"
#include "fs.hpp"
#include "location.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "ftpsrv_helper.hpp"
#include "haze_helper.hpp"
#include "nacp_util.hpp"
#include "image.hpp"
#include "owo.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include <usbhsfs.h>
#include <switch.h>
#include <cstring>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>

namespace sphaira {

extern App* g_app;
void nxlink_callback(const NxlinkCallbackData *data);
void on_i18n_change();
bool IsKefirHubNacp(const NacpStruct& nacp);

namespace {

auto NormalizeWebdavUrl(std::string url) -> std::string {

    constexpr const char* whitespace = " \t\r\n";
    const auto first = url.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return {};
    }
    const auto last = url.find_last_not_of(whitespace);
    url = url.substr(first, last - first + 1);

    if (url.find("://") == std::string::npos) {
        url.insert(0, "webdav://");
    } else if (url.starts_with("https://")) {
        url.replace(0, std::strlen("https"), "webdav");
    }

    const auto scheme_end = url.find("://");
    while (url.ends_with('/') && scheme_end != std::string::npos && url.size() > scheme_end + 3) {
        url.pop_back();
    }
    return url;
}

// this will try to decompress the icon and then re-convert it to jpg
// in order to strip exif data.
// this doesn't take long at all, but it's very overkill.
// todo: look into jpeg/exif spec to manually strip data
auto GetNroIcon(const std::vector<u8>& nro_icon) -> std::vector<u8> {
    auto image = ImageLoadFromMemory(nro_icon);
    if (!image.data.empty()) {
        if (image.w != 256 || image.h != 256) {
            image = ImageResize(image.data, image.w, image.h, 256, 256);
        }
        if (!image.data.empty()) {
            image = ImageConvertToJpg(image.data, image.w, image.h);
            if (!image.data.empty()) {
                return image.data;
            }
        }
    }
    return nro_icon;
}

} // namespace

auto App::IsHbmenu() -> bool {
    return !strcasecmp(GetExePath().s, "/hbmenu.nro");
}

auto App::GetNxlinkEnable() -> bool {
    return g_app->m_nxlink_enabled.Get();
}

auto App::GetHddEnable() -> bool {
    return g_app->m_hdd_enabled.Get();
}

auto App::GetWriteProtect() -> bool {
    return g_app->m_hdd_write_protect.Get();
}

auto App::GetWebdavUrlName() -> std::string {
    return g_app->m_webdav_url.Get();
}

auto App::GetWebdavUrl() -> std::string {
    const auto raw_val = g_app->m_webdav_url.Get();
    const auto locations = location::Load();
    for (const auto& loc : locations) {
        if (loc.name == raw_val) {
            return NormalizeWebdavUrl(loc.url);
        }
    }
    return NormalizeWebdavUrl(raw_val);
}

auto App::GetWebdavUser() -> std::string {
    const auto raw_val = g_app->m_webdav_url.Get();
    const auto locations = location::Load();
    for (const auto& loc : locations) {
        if (loc.name == raw_val) {
            return loc.user;
        }
    }
    return g_app->m_webdav_user.Get();
}

auto App::GetWebdavPass() -> std::string {
    const auto raw_val = g_app->m_webdav_url.Get();
    const auto locations = location::Load();
    for (const auto& loc : locations) {
        if (loc.name == raw_val) {
            return loc.pass;
        }
    }
    return g_app->m_webdav_pass.Get();
}

auto App::GetLogEnable() -> bool {
    return g_app->m_log_enabled.Get();
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
        return free_sd > free_nand;
    }
    return true;
}

auto App::GetInstallLocation() -> long {
    return g_app->m_install_location.Get();
}

auto App::GetInstallReserveMb() -> long {
    return g_app->m_install_reserve_mb.Get();
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

auto App::GetMtpEnable() -> bool {
    return g_app->m_mtp_enabled.Get();
}

auto App::GetMtpShowSd() -> bool {
    return g_app->m_mtp_show_sd.Get();
}

auto App::GetMtpShowInstall() -> bool {
    return g_app->m_mtp_show_install.Get();
}

auto App::GetMtpShowSaves() -> bool {
    return g_app->m_mtp_show_saves.Get();
}

auto App::GetMtpNameSd() -> std::string {
    return g_app->m_mtp_name_sd.Get();
}

auto App::GetMtpNameInstall() -> std::string {
    return g_app->m_mtp_name_install.Get();
}

auto App::GetFtpEnable() -> bool {
    return g_app->m_ftp_enabled.Get();
}

auto App::GetLanguage() -> long {
    return g_app->m_language.Get();
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

void App::SetNxlinkEnable(bool enable) {
    if (App::GetNxlinkEnable() != enable) {
        g_app->m_nxlink_enabled.Set(enable);
        if (enable) {
            nxlinkInitialize(nxlink_callback);
        } else {
            nxlinkExit();
        }
    }
}

void App::SetHddEnable(bool enable) {
    if (App::GetHddEnable() != enable) {
        g_app->m_hdd_enabled.Set(enable);
        if (enable) {
            if (App::GetWriteProtect()) {
                usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
            }
            usbHsFsInitialize(1);
        } else {
            usbHsFsExit();
        }
    }
}

void App::SetWriteProtect(bool enable) {
    if (App::GetWriteProtect() != enable) {
        g_app->m_hdd_write_protect.Set(enable);

        if (enable) {
            usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
        } else {
            usbHsFsSetFileSystemMountFlags(0);
        }
    }
}

void App::SetWebdavUrl(std::string value) {
    g_app->m_webdav_url.Set(NormalizeWebdavUrl(std::move(value)));
}

void App::SetWebdavUser(std::string value) {
    g_app->m_webdav_user.Set(std::move(value));
}

void App::SetWebdavPass(std::string value) {
    g_app->m_webdav_pass.Set(std::move(value));
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

void App::SetInstallSysmmcEnable(bool enable) {
    g_app->m_install_sysmmc.Set(enable);
}

void App::SetInstallEmummcEnable(bool enable) {
    g_app->m_install_emummc.Set(enable);
}

void App::SetInstallSdEnable(bool enable) {
    g_app->m_install_location.Set(enable ? 0 : 1);
}

void App::SetInstallLocation(long location) {
    g_app->m_install_location.Set(location);
}

void App::SetInstallReserveMb(long reserve_mb) {
    g_app->m_install_reserve_mb.Set(reserve_mb);
}

void App::SetAnimatedWavesEnable(bool enable) {
    g_app->m_animated_waves.Set(enable);
}

void App::Set12HourTimeEnable(bool enable) {
    g_app->m_12hour_time.Set(enable);
}

void App::SetMtpEnable(bool enable) {
    if (App::GetMtpEnable() != enable) {
        g_app->m_mtp_enabled.Set(enable);
        if (enable) {
            if (haze::Init()) {
                ui::menu::stream::BackgroundInstaller::RegisterMtpCallbacks();
            } else {
                // e.g. every storage is disabled in "MTP storages" - keep the
                // toggle honest, otherwise settings would show a running
                // server that never started.
                g_app->m_mtp_enabled.Set(false);
            }
        } else {
            haze::Exit();
        }
    }
}

void App::SetMtpShowSd(bool enable) {
    if (App::GetMtpShowSd() != enable) {
        g_app->m_mtp_show_sd.Set(enable);
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetMtpShowInstall(bool enable) {
    if (App::GetMtpShowInstall() != enable) {
        g_app->m_mtp_show_install.Set(enable);
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetMtpShowSaves(bool enable) {
    if (App::GetMtpShowSaves() != enable) {
        g_app->m_mtp_show_saves.Set(enable);
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetMtpNameSd(std::string value) {
    if (App::GetMtpNameSd() != value) {
        g_app->m_mtp_name_sd.Set(std::move(value));
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetMtpNameInstall(std::string value) {
    if (App::GetMtpNameInstall() != value) {
        g_app->m_mtp_name_install.Set(std::move(value));
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetFtpEnable(bool enable) {
    if (App::GetFtpEnable() != enable) {
        g_app->m_ftp_enabled.Set(enable);
        if (enable) {
            ftpsrv::Init();
        } else {
            ftpsrv::Exit();
        }
    }
}

void App::SetLanguage(long index, bool prompt_restart) {
    if (App::GetLanguage() != index) {
        g_app->m_language.Set(index);
        on_i18n_change();

        if (prompt_restart) {
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

void App::SetGodModeEnable(bool enable) {
    g_app->m_god_mode.Set(enable);
}

auto App::Install(OwoConfig& config) -> Result {
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
    if (!config.icon.empty()) {
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

auto App::IsEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] != '\0') || (paths.nintendo[0] != '\0');
}

auto App::IsParitionBaseEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] == '\0') && (paths.nintendo[0] != '\0');
}

auto App::IsFileBaseEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] != '\0') && (paths.nintendo[0] != '\0');
}

void App::Exit() {
    g_app->m_quit = true;
}

void App::ExitRestart() {
    nro_launch(GetExePath());
    Exit();
}

} // namespace sphaira
