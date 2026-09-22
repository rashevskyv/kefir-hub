#include "app.hpp"
#include "haze_helper.hpp"
#include "ftpsrv_helper.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "fs.hpp"
#include <usbhsfs.h>
#include <switch.h>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace sphaira {

extern App* g_app;

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

auto App::GetMtpShowRawSaves() -> bool {
    return g_app->m_mtp_show_raw_saves.Get();
}

auto App::GetMtpShowRawSystemSaves() -> bool {
    return g_app->m_mtp_show_raw_system_saves.Get();
}

auto App::GetMtpShowGames() -> bool {
    return g_app->m_mtp_show_games.Get();
}

auto App::GetMtpGamesLayout() -> long {
    const auto layout = g_app->m_mtp_games_layout.Get();
    if (layout < 0 || layout > 2) {
        return 2;
    }
    return layout;
}

auto App::GetMtpNameSd() -> std::string {
    return g_app->m_mtp_name_sd.Get();
}

auto App::GetMtpNameInstall() -> std::string {
    return g_app->m_mtp_name_install.Get();
}

auto App::GetMtpFolders() -> std::vector<std::string> {
    std::vector<std::string> out;
    const auto raw = g_app->m_mtp_folders.Get();
    size_t start = 0;
    while (start <= raw.size()) {
        const auto end = raw.find('|', start);
        auto part = raw.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            out.push_back(std::move(part));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

void App::SetMtpEnable(bool enable) {
    g_app->ApplyMtpEnable(enable, true);
}

void App::ApplyMtpEnable(bool enable, bool notify_conflict) {
    if (App::GetMtpEnable() != enable) {
        // mutually exclusive with usb host storage -- see SetHddEnable. Free
        // the port before haze grabs it.
        if (enable && App::GetHddEnable()) {
            App::SetHddEnable(false);
            if (notify_conflict) {
                App::Notify("USB storage turned off to free the USB port"_i18n);
            }
        }
        g_app->m_mtp_enabled.Set(enable);
        if (enable) {
            PsmChargerType charger{PsmChargerType_Unconnected};
            psmGetChargerType(&charger);
            // only grab the port as a gadget if a PC is already providing
            // VBUS. otherwise stay in host mode so a flash drive can mount.
            if (charger == PsmChargerType_LowPower) {
                if (haze::Init()) {
                    ui::menu::stream::BackgroundInstaller::RegisterMtpCallbacks();
                } else {
                    g_app->m_mtp_enabled.Set(false);
                }
            } else if (!usbHsFsGetStatusChangeUserEvent()) {
                if (App::GetWriteProtect()) {
                    usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
                }
                usbHsFsInitialize(1);
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

void App::SetMtpShowRawSaves(bool enable) {
    if (App::GetMtpShowRawSaves() != enable) {
        g_app->m_mtp_show_raw_saves.Set(enable);
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetMtpShowRawSystemSaves(bool enable) {
    if (App::GetMtpShowRawSystemSaves() != enable) {
        g_app->m_mtp_show_raw_system_saves.Set(enable);
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetMtpShowGames(bool enable) {
    if (App::GetMtpShowGames() != enable) {
        g_app->m_mtp_show_games.Set(enable);
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::SetMtpGamesLayout(long layout) {
    if (layout < 0 || layout > 2) {
        layout = 2;
    }
    if (App::GetMtpGamesLayout() != layout) {
        g_app->m_mtp_games_layout.Set(layout);
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

void App::SetMtpFolders(const std::vector<std::string>& folders) {
    std::string joined;
    for (const auto& f : folders) {
        if (f.empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined += '|';
        }
        joined += f;
    }

    if (App::GetMtpFolders() != folders) {
        g_app->m_mtp_folders.Set(joined);
        if (App::GetMtpEnable()) {
            SetMtpEnable(false);
            SetMtpEnable(true);
        }
    }
}

void App::AddMtpFolder(const std::string& path) {
    auto folders = App::GetMtpFolders();
    if (std::find(folders.cbegin(), folders.cend(), path) != folders.cend()) {
        return;
    }
    folders.push_back(path);
    App::SetMtpFolders(folders);
}

void App::RemoveMtpFolder(const std::string& path) {
    auto folders = App::GetMtpFolders();
    const auto it = std::find(folders.cbegin(), folders.cend(), path);
    if (it != folders.cend()) {
        folders.erase(it);
        App::SetMtpFolders(folders);
    }
}

// the one folder every network share exposes next to the microSD card. session
// state, not a setting -- see the declaration in app.hpp. read by the web
// server's worker threads on every request, hence the lock: the path is a plain
// char buffer, so an unguarded read racing a mount would return half of each.
static std::vector<fs::FsPath> g_mounted_folders{};
static Mutex g_mounted_folder_mutex{};

auto App::GetMountedFolders() -> std::vector<fs::FsPath> {
    SCOPED_MUTEX(&g_mounted_folder_mutex);
    return g_mounted_folders;
}

void App::SetMountedFolders(std::vector<fs::FsPath> paths) {
    std::vector<std::string> as_strings;
    as_strings.reserve(paths.size());
    for (const auto& p : paths) {
        as_strings.push_back(p.toString());
        log_write("[MOUNT] mounted folder: '%s'\n", p.s);
    }

    if (as_strings.empty()) {
        log_write("[MOUNT] nothing mounted\n");
    }

    {
        SCOPED_MUTEX(&g_mounted_folder_mutex);
        g_mounted_folders = std::move(paths);
    }

    // fan out to whichever transports care. ftp rebuilds its root device list
    // from this (bouncing the server if it is already up); the web server reads
    // it fresh on every request, so it needs no poking. mounting over one of
    // them therefore shows up on the others too, which is the whole point.
    ftpsrv::SetFtpMountedFolders(as_strings);
}

} // namespace sphaira
