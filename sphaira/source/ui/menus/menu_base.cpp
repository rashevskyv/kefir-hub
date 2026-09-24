#include "app.hpp"
#include "auto_update.hpp"
#include "log.hpp"
#include "ntp.hpp"
#include "haze_helper.hpp"
#include "ftpsrv_helper.hpp"
#include "hats_version.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/layout.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"

#include <switch.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sys/statvfs.h>

namespace {
NVGcolor ParseColor(const std::string& str, NVGcolor fallback) {
    if (str.empty()) return fallback;
    std::string_view val = str;
    if (val.starts_with("0x")) {
        val = val.substr(2);
    } else {
        return fallback;
    }
    char* end;
    u32 c = std::strtoul(val.data(), &end, 16);
    if (!c && val.data() == end) {
        return fallback;
    }
    if (val.length() <= 6) {
        c <<= 8;
        c |= 0xFF;
    }
    return nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}
}

namespace sphaira::ui::menu {

auto MenuBase::GetPolledData(bool force_refresh) -> PolledData {
    static PolledData data{};
    static TimeStamp timestamp{};
    static TimeStamp storage_timestamp{};
    static bool has_init = false;

    if (!has_init) {
        has_init = true;
        force_refresh = true;
    }

    // update every second, do this in Draw because Update() isn't called if it
    // doesn't have focus.
    if (force_refresh || timestamp.GetSeconds() >= 1) {
        data.tm = {};
        data.battery_percetange = {};
        data.charger_type = {};
        data.type = {};
        data.status = {};
        data.strength = {};
        data.ip = {};
        data.mtp_running = haze::IsRunning();
        data.ftp_running = ftpsrv::IsRunning();

        static std::string s_cached_sys_version{};
        static bool s_sys_version_loaded = false;
        if (!s_sys_version_loaded) {
            s_cached_sys_version = hats::getSystemVersionString();
            s_sys_version_loaded = true;
        }
        data.sys_version = s_cached_sys_version;

        static bool s_cached_usb3_enabled = false;
        static bool s_usb3_loaded = false;
        if (!s_usb3_loaded || force_refresh) {
            s_cached_usb3_enabled = !settings::detail::IniValueEquals("/atmosphere/config/system_settings.ini", "usb", "usb30_force_enabled", "u8!0x0");
            s_usb3_loaded = true;
        }
        data.usb3_enabled = s_cached_usb3_enabled;
        data.is_emummc = App::IsEmummc();

        const auto t = std::time(NULL) + ntp::GetDisplayOffset();
        localtime_r(&t, &data.tm);
        psmGetBatteryChargePercentage(&data.battery_percetange);
        psmGetChargerType(&data.charger_type);
        nifmGetInternetConnectionStatus(&data.type, &data.strength, &data.status);
        nifmGetCurrentIpAddress(&data.ip);

        timestamp.Update();
    }

    // the access point name changes rarely and the profile fetch is a heavier
    // ipc than the ones above, poll it on its own slower cadence.
    static TimeStamp ssid_timestamp{};
    if (force_refresh || ssid_timestamp.GetSeconds() >= 5) {
        data.ssid.clear();
        if (data.ip && data.type == NifmInternetConnectionType_WiFi) {
            NifmNetworkProfileData profile{};
            if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&profile))) {
                auto& wireless = profile.wireless_setting_data;
                const auto len = std::min<size_t>(wireless.ssid_len, sizeof(wireless.ssid) - 1);
                wireless.ssid[len] = '\0';
                data.ssid = wireless.ssid;
            }
        }
        ssid_timestamp.Update();
    }

    if (force_refresh || storage_timestamp.GetSeconds() >= 15) {
        fs::GetStorageSpaces(&data.nand_free, &data.nand_total, &data.sd_free, &data.sd_total);
        storage_timestamp.Update();
    }

    return data;
}

MenuBase::MenuBase(const std::string& title, u32 flags) : m_title{title}, m_flags{flags} {
    // this->SetParent(this);
    this->SetPos(30, 87, 1220 - 30, 646 - 87);
    SetAction(Button::SELECT, Action{App::HandleMinus});
}

MenuBase::~MenuBase() {
}

void MenuBase::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
}

void MenuBase::Draw(NVGcontext* vg, Theme* theme) {
    DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);

    if (App::GetAnimatedWavesEnable()) {
        // Draw animated waves at the bottom of the screen
        const float time = static_cast<float>(armTicksToNs(armGetSystemTick())) / 1'000'000'000.f;

        // Determine if light theme or dark theme based on background color brightness
        auto bg_color = theme->GetColour(ThemeEntryID_BACKGROUND);
        float brightness = 0.299f * bg_color.r + 0.587f * bg_color.g + 0.114f * bg_color.b;
        bool is_light = brightness > 0.5f;

        NVGcolor col1, col2;
        if (is_light) {
            std::string custom_color_str = App::GetWaveColorLight();
            if (!custom_color_str.empty()) {
                col1 = ParseColor(custom_color_str, theme->GetColour(ThemeEntryID_HIGHLIGHT_1));
                col2 = col1;
            } else {
                col1 = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
                col2 = theme->GetColour(ThemeEntryID_HIGHLIGHT_2);
            }
        } else {
            std::string custom_color_str = App::GetWaveColorDark();
            if (!custom_color_str.empty()) {
                col1 = ParseColor(custom_color_str, theme->GetColour(ThemeEntryID_HIGHLIGHT_1));
                col2 = col1;
            } else {
                col1 = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
                col2 = theme->GetColour(ThemeEntryID_HIGHLIGHT_2);
            }
        }

        auto draw_wave = [&](float base_y, float amp, float freq, float speed, NVGcolor color) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, 0.f, SCREEN_HEIGHT);
            const float phase = time * speed;
            for (float x = 0.f; x <= SCREEN_WIDTH; x += 15.f) {
                float y = base_y + amp * std::sin(x * freq + phase);
                nvgLineTo(vg, x, y);
            }
            nvgLineTo(vg, SCREEN_WIDTH, SCREEN_HEIGHT);
            nvgClosePath(vg);
            nvgFillColor(vg, color);
            nvgFill(vg);
        };

        // Layer 1 (Back wave)
        col1.a = 0.15f;
        draw_wave(670.f, 15.f, 0.004f, 1.2f, col1);

        // Layer 2 (Middle wave)
        col2.a = 0.20f;
        draw_wave(685.f, 10.f, 0.007f, -0.8f, col2);

        // Layer 3 (Front wave)
        col1.a = 0.30f;
        draw_wave(695.f, 8.f, 0.005f, 1.6f, col1);
    }

}



void MenuBase::SetTitle(std::string title) {
    if (m_title != title) {
        m_scroll_title.Reset(title);
    }
    m_title = std::move(title);
}

void MenuBase::SetTitleStats(std::string top, std::string bottom) {
    m_title_stat_top = std::move(top);
    m_title_stat_bottom = std::move(bottom);
}

void MenuBase::SetTitleSubHeading(std::string sub_heading, bool top_row) {
    if (m_title_sub_heading_top_row != top_row || sub_heading.empty()) {
        m_scroll_title_sub_heading.Reset(sub_heading);
    }
    m_title_sub_heading = std::move(sub_heading);
    m_title_sub_heading_top_row = top_row;
}

void MenuBase::SetSubHeading(std::string sub_heading) {
    m_sub_heading = sub_heading;
}

void MenuBase::SetStorageHighlight(u64 nand_bytes, u64 sd_bytes) {
    m_nand_highlight = nand_bytes;
    m_sd_highlight = sd_bytes;
    m_nand_focus = 0;
    m_sd_focus = 0;
    m_storage_highlight_active = true;
    m_storage_projection = false;
    m_storage_install_progress = false;
}

void MenuBase::SetStorageProjection(u64 nand_bytes, u64 sd_bytes, u64 nand_focus, u64 sd_focus) {
    m_nand_highlight = nand_bytes;
    m_sd_highlight = sd_bytes;
    m_nand_focus = std::min(nand_focus, nand_bytes);
    m_sd_focus = std::min(sd_focus, sd_bytes);
    m_storage_highlight_active = true;
    m_storage_projection = true;
    m_storage_install_progress = false;
}

void MenuBase::SetStorageInstallProgress(u64 nand_written, u64 nand_total, u64 sd_written, u64 sd_total) {
    m_nand_highlight = nand_total;
    m_sd_highlight = sd_total;
    m_nand_focus = std::min(nand_written, nand_total);
    m_sd_focus = std::min(sd_written, sd_total);
    m_storage_highlight_active = (nand_total > 0 || sd_total > 0);
    m_storage_projection = false;
    m_storage_install_progress = (nand_total > 0 || sd_total > 0);
}

void MenuBase::ClearStorageHighlight() {
    m_nand_highlight = 0;
    m_sd_highlight = 0;
    m_nand_focus = 0;
    m_sd_focus = 0;
    m_storage_highlight_active = false;
    m_storage_projection = false;
    m_storage_install_progress = false;
}

} // namespace sphaira::ui::menu
