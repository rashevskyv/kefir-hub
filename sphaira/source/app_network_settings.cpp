#include "app.hpp"
#include "nxlink.h"
#include "ntp.hpp"
#include "i18n.hpp"
#include "ftpsrv_helper.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include <usbhsfs.h>
#include <switch.h>
#include <cstring>
#include <string>
#include <utility>

namespace sphaira {

extern App* g_app;
void nxlink_callback(const NxlinkCallbackData *data);

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

// restarts the FTP server if it is running, so a config change takes effect.
void RestartFtpIfRunning() {
    if (App::GetFtpEnable()) {
        ftpsrv::Exit();
        ftpsrv::Init();
    }
}

} // namespace

auto App::GetNxlinkEnable() -> bool {
    return g_app->m_nxlink_enabled.Get();
}

auto App::GetNtpEnable() -> bool {
    return g_app->m_ntp_enabled.Get();
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

auto App::GetFtpEnable() -> bool {
    return g_app->m_ftp_enabled.Get();
}

auto App::GetFtpAnon() -> bool {
    return g_app->m_ftp_anon.Get();
}

auto App::GetFtpUser() -> std::string {
    return g_app->m_ftp_user.Get();
}

auto App::GetFtpPass() -> std::string {
    return g_app->m_ftp_pass.Get();
}

auto App::GetFtpPort() -> long {
    return g_app->m_ftp_port.Get();
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

void App::SetNtpEnable(bool enable) {
    if (App::GetNtpEnable() != enable) {
        g_app->m_ntp_enabled.Set(enable);
        // the worker keeps running either way and re-checks the option each
        // cycle, so turning it back on picks up without a restart.
        if (enable) {
            ntp::Start();
        }
    }
}

void App::SetHddEnable(bool enable) {
    if (App::GetHddEnable() != enable) {
        // MTP and usb host storage both want to own the usb port; only one can.
        // Turning the drive on turns MTP off so the port is actually free for
        // usbhsfs to claim, instead of silently doing nothing.
        if (enable && App::GetMtpEnable()) {
            App::SetMtpEnable(false);
            App::Notify("MTP turned off to free the USB port"_i18n);
        }
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

void App::SetFtpEnable(bool enable) {
    if (App::GetFtpEnable() != enable) {
        g_app->m_ftp_enabled.Set(enable);
        if (enable) {
            ftpsrv::Init();
            // enables background install for files dropped into the FTP "install" folder.
            ui::menu::stream::BackgroundInstaller::RegisterMtpCallbacks();
        } else {
            ftpsrv::Exit();
        }
    }
}

void App::SetFtpAnon(bool enable) {
    if (App::GetFtpAnon() != enable) {
        g_app->m_ftp_anon.Set(enable);
        RestartFtpIfRunning();
    }
}

void App::SetFtpUser(std::string value) {
    if (App::GetFtpUser() != value) {
        g_app->m_ftp_user.Set(std::move(value));
        RestartFtpIfRunning();
    }
}

void App::SetFtpPass(std::string value) {
    if (App::GetFtpPass() != value) {
        g_app->m_ftp_pass.Set(std::move(value));
        RestartFtpIfRunning();
    }
}

void App::SetFtpPort(long port) {
    if (App::GetFtpPort() != port) {
        g_app->m_ftp_port.Set(port);
        RestartFtpIfRunning();
    }
}

} // namespace sphaira
