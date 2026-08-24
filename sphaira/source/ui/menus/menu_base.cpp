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
    SetAction(Button::SELECT, Action{App::Exit});
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

void MenuBase::DrawChrome(NVGcontext* vg, Theme* theme) {
    if (!WantsChrome()) {
        return;
    }

    // a panel stacked above claims the region it covers: the chrome that falls
    // inside is skipped entirely rather than drawn under a background that is
    // only *almost* opaque, which is what made the clock, the storage bars and
    // the hint row ghost through an open sidebar.
    const auto occlusion = App::GetChromeOcclusion();
    const auto occluded = [&occlusion](const Vec4& v) {
        return layout::Intersects(v, occlusion);
    };

    // the hint row is drawn by whoever owns the footer (see App::OwnsFooter),
    // so a covered menu draws nothing here.
    Widget::Draw(vg, theme);

    const auto pdata = GetPolledData();

    // --- Status bar layout (top-right) ---
    // Line 0 (y=25): Kefir / firmware version, centered over the IP/Wi-Fi block
    // Line 1 (y=48): IP address / SSID
    // Lines 2-3: SysNAND|EmuNAND / microSD bars
    // Line 4 (y=70 = start_y): Clock + Battery

    const float start_y   = 70;
    const float font_size = 22;
    const float spacing   = 30;
    const float bar_right = 1240.f;
    const float bar_w     = 262.f;
    const float bar_h     = 10.f;
    const float small_font = 15.f;
    // storage rows font: reduced to 15.5px for crisp, overlap-free spacing from system/Kefir version header line
    const float storage_font = 15.5f;
    const float y_ip      = 48.f;

    // Align clock + battery at the rightmost position (bar_right = 1220)
    float start_x = bar_right;
    float bounds[4];

    nvgFontSize(vg, font_size);

    // the whole status block lives in the right half of the header band.
    const bool draw_status = !occluded(Vec4{SCREEN_WIDTH / 2.f, 0.f, SCREEN_WIDTH / 2.f, layout::HEADER_LINE_Y});

    if (draw_status) {

    #define draw(colour, fixed, ...) \
        gfx::drawTextArgs(vg, start_x, start_y, font_size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(colour), __VA_ARGS__); \
        if (fixed) { \
            start_x -= fixed; \
        } else { \
            gfx::textBoundsArgs(vg, 0, 0, bounds, __VA_ARGS__); \
            start_x -= spacing + (bounds[2] - bounds[0]); \
        }

    // ---- Row 4 (start_y=70): Clock + Battery ----
    // Battery: show battery percentage with a small percent symbol. While charging,
    // the percentage and symbol turn green; while discharging, they use the normal text color.
    if (pdata.charger_type != 0) {
        // charging: fixed green color, no animation
        const NVGcolor charge_col = nvgRGBA(100, 230, 100, 255);
        gfx::drawTextArgs(vg, start_x, start_y, font_size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, charge_col, "%u\uFE6A", pdata.battery_percetange);
        start_x -= 94;
    } else {
        // discharging: normal color percentage
        draw(ThemeEntryID_TEXT, 94, "%u\uFE6A", pdata.battery_percetange);
    }

    // Clock
    if (App::Get12HourTimeEnable()) {
        draw(ThemeEntryID_TEXT, 132, "%02u:%02u %s",
            (pdata.tm.tm_hour == 0 || pdata.tm.tm_hour == 12) ? 12 : pdata.tm.tm_hour % 12,
            pdata.tm.tm_min, (pdata.tm.tm_hour < 12) ? "AM" : "PM");
    } else {
        draw(ThemeEntryID_TEXT, 90, "%02u:%02u", pdata.tm.tm_hour, pdata.tm.tm_min);
    }

    if (!App::IsApplication()) {
        draw(ThemeEntryID_ERROR, 0, "[A]");
    }

    #undef draw

    // The storage rows sit on their own band to the left of the clock/battery/applet block.
    // Ensure the right edge never encroaches on the left edge of that block (start_x).
    const float storage_right = std::min(start_x - 10.f, bar_right);

    // ---- Storage row layout: NAND ▓▓▓▓ 4.5 GB ----
    // Both outer edges are fixed and only the size text inside the reserved
    // column changes, so nothing shifts as the cursor moves between titles.
    // The column is sized from six digits plus a space and the two widest
    // unit letters, measured in the current font so it scales with language.
    // In projection mode ("+focus / total"), reserve space for both values.
    const float value_col_w = [&]{
        nvgFontSize(vg, storage_font);
        const char* template_str = (m_storage_projection && m_storage_highlight_active)
            ? "+000000 WW / 000000 WW"
            : "000000 WW";
        gfx::textBounds(vg, 0, 0, bounds, template_str);
        return bounds[2] - bounds[0];
    }();

    const char* nand_bar_label = pdata.is_emummc ? "EmuNAND" : "SysNAND";
    const char* sd_bar_label = "microSD";
    const float label_col_w = [&]{
        nvgFontSize(vg, storage_font);
        float out = 0.f;
        for (const auto* label : {nand_bar_label, sd_bar_label}) {
            gfx::textBounds(vg, 0, 0, bounds, label);
            out = std::max(out, bounds[2] - bounds[0]);
        }
        return out;
    }();

    const float value_x = storage_right - value_col_w;
    const float bar_x   = value_x - 15.f - bar_w;
    const float label_x = bar_x - 8.f - label_col_w;

    // value shown next to a bar: free space normally, the highlighted size in
    // highlight mode, "+size" for a projection (planned install usage).
    auto storage_value_of = [&](s64 free_bytes, u64 highlight_bytes, u64 focus_bytes) -> std::string {
        if (!m_storage_highlight_active || (m_storage_projection && !highlight_bytes)) {
            return utils::formatSizeStorage(free_bytes);
        }
        const auto value = utils::formatSizeStorage(highlight_bytes);
        if (!m_storage_projection) {
            return value;
        }
        // "this package / everything queued" when one package is in focus.
        if (focus_bytes) {
            return "+" + utils::formatSizeStorage(focus_bytes) + " / " + value;
        }
        return "+" + value;
    };

    // Determine the exact rightmost boundary of the NAND storage part on Row 1 (y=48).
    // The network SSID/IP text occupies the region from right after the NAND text to bar_right.
    // If the text would overlap NAND, it scrolls strictly within this window.
    float net_left = start_x;
    if (m_show_storage) {
        m_status_left_x = label_x;
        const auto nand_val = storage_value_of(pdata.nand_free, m_nand_highlight, m_nand_focus);
        nvgFontSize(vg, storage_font);
        gfx::textBounds(vg, 0, 0, bounds, nand_val.c_str());
        const float nand_val_w = bounds[2] - bounds[0];
        const float nand_right = value_x + nand_val_w;
        net_left = nand_right + 12.f;
    } else {
        m_status_left_x = start_x;
    }

    // ---- Row 1 (y=48): access point + IP address ----
    {
        const auto ip_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        std::string network_str;
        if (pdata.ip) {
            // "MyAP · 192.168.1.5" for wi-fi, "LAN · 192.168.1.5" for ethernet.
            if (pdata.type == NifmInternetConnectionType_Ethernet) {
                network_str = "LAN";
            } else if (!pdata.ssid.empty()) {
                network_str = pdata.ssid;
            }
            if (!network_str.empty()) {
                network_str += " · ";
            }
            char ip_buf[32];
            std::snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u",
                pdata.ip & 0xFF, (pdata.ip >> 8) & 0xFF,
                (pdata.ip >> 16) & 0xFF, (pdata.ip >> 24) & 0xFF);
            network_str += ip_buf;
        } else {
            network_str = "No Internet"_i18n;
        }

        const float net_w = std::max(0.f, bar_right - net_left);

        nvgFontSize(vg, small_font);
        gfx::textBounds(vg, 0, 0, bounds, network_str.c_str());
        const float net_text_w = bounds[2] - bounds[0];

        if (net_w > 0.f && net_text_w > net_w) {
            m_scroll_network.Draw(vg, true, net_left, y_ip, net_w, small_font,
                NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, ip_col, network_str);
        } else {
            m_scroll_network.Reset(network_str);
            gfx::drawTextArgs(vg, bar_right, y_ip, small_font,
                NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, ip_col, "%s", network_str.c_str());
        }

        if (!pdata.sys_version.empty()) {
            constexpr float sys_font = 12.f;
            nvgFontSize(vg, sys_font);
            gfx::textBounds(vg, 0, 0, bounds, pdata.sys_version.c_str());
            const float ver_w = bounds[2] - bounds[0];
            const float col_left = (net_w > 0.f && net_text_w > net_w) ? net_left : (bar_right - net_text_w);
            const float col_right = bar_right;
            const float col_w = std::max(0.f, col_right - col_left);
            const float text_mid_y = 25.f;
            const auto ver_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
            if (ver_w <= col_w) {
                gfx::drawTextArgs(vg, col_left + col_w * 0.5f, text_mid_y, sys_font,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, ver_col, "%s", pdata.sys_version.c_str());
            } else {
                gfx::drawTextArgs(vg, col_right, text_mid_y, sys_font,
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, ver_col, "%s", pdata.sys_version.c_str());
            }
        }
    }

    if (m_show_storage) {
        // Left edge of the whole status block, so the header gap can end against
        // it. The labels are drawn left aligned from label_x, so that is already
        // the leftmost pixel of the block.
        m_status_left_x = label_x;

        auto draw_storage_bar = [&](float y, const char* label, s64 free_bytes, s64 total_bytes, u64 highlight_bytes, u64 focus_bytes) {
            if (total_bytes <= 0) return;
            const float used_ratio = 1.f - static_cast<float>(free_bytes) / static_cast<float>(total_bytes);
            const float fill_w     = bar_w * used_ratio;
            const float bar_y      = y - bar_h;

            // Track (background)
            const NVGcolor track_col = nvgRGBA(80, 80, 80, 180);
            gfx::drawRect(vg, bar_x, bar_y, bar_w, bar_h, track_col);

            // Fill
            NVGcolor fill_col;
            if (used_ratio > 0.9f)       fill_col = nvgRGBA(230, 60,  60,  255);
            else if (used_ratio > 0.75f) fill_col = nvgRGBA(230, 180, 60,  255);
            else                         fill_col = nvgRGBA(90,  200, 120, 255);
            gfx::drawRect(vg, bar_x, bar_y, fill_w, bar_h, fill_col);

            // Games may expose how much of the used region belongs to the current
            // title. Anchor the highlighted segment at the end of the used region
            // so it remains proportional without claiming that storage is physically
            // contiguous on disk.
            if (highlight_bytes && m_storage_projection) {
                // planned usage: the segment extends from the end of the used
                // region into the free space. Red when it does not fit.
                const float ratio = static_cast<float>(highlight_bytes) / static_cast<float>(total_bytes);
                const float seg_w = std::min(bar_w - fill_w, std::max(2.f, bar_w * ratio));
                const bool fits = free_bytes > 0 && highlight_bytes <= static_cast<u64>(free_bytes);
                const auto seg_col = fits ? theme->GetColour(ThemeEntryID_HIGHLIGHT_1) : nvgRGBA(230, 60, 60, 255);
                gfx::drawRect(vg, bar_x + fill_w, bar_y, seg_w, bar_h, seg_col);

                // the package in focus takes the head of the segment, so it reads as
                // "of everything queued here, this much is the one you are looking at".
                // Amber is hardcoded like the red above: the theme highlights are
                // often two shades of the same colour, which reads as one segment.
                if (focus_bytes) {
                    const float f_ratio = static_cast<float>(focus_bytes) / static_cast<float>(total_bytes);
                    const float f_w = std::min(seg_w, std::max(2.f, bar_w * f_ratio));
                    gfx::drawRect(vg, bar_x + fill_w, bar_y, f_w, bar_h, nvgRGBA(255, 200, 60, 255));
                }
            } else if (highlight_bytes && fill_w > 0.f) {
                const float highlight_ratio = std::min(
                    used_ratio, static_cast<float>(highlight_bytes) / static_cast<float>(total_bytes));
                const float highlight_w = std::max(2.f, bar_w * highlight_ratio);
                const float highlight_x = bar_x + std::max(0.f, fill_w - highlight_w);
                gfx::drawRect(vg, highlight_x, bar_y, std::min(highlight_w, fill_w), bar_h,
                    theme->GetColour(ThemeEntryID_HIGHLIGHT_1));
            }

            // Normally show free space. Games replace it with the exact size of the
            // focused title or the sum of the current multi-selection.
            const auto value = storage_value_of(free_bytes, highlight_bytes, focus_bytes);
            const auto text_col = theme->GetColour(m_storage_highlight_active && !(m_storage_projection && !highlight_bytes)
                ? ThemeEntryID_HIGHLIGHT_1 : ThemeEntryID_TEXT_INFO);

            nvgFontSize(vg, storage_font);
            gfx::drawTextArgs(vg, label_x + label_col_w * 0.5f, y, storage_font,
                NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM, text_col, "%s", label);

            gfx::drawTextArgs(vg, value_x, y, storage_font, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, text_col,
                "%s", value.c_str());
        };

        // ---- Storage indicators and bars ----
        const float storage_mid  = (y_ip + start_y) * 0.5f;
        const float storage_gap  = 20.f;

        // ---- Service badges, centered over the NAND / microSD block ----
        const float badge_y    = 17.f;
        const float badge_h    = 16.f;
        const float badge_font = 12.f;
        const float badge_gap  = 6.f;

        struct ServiceBadgeItem {
            const char* name;
            bool is_active;
        };

        const float storage_left = label_x;
        const float storage_span_w = std::max(0.f, storage_right - storage_left);

        std::vector<ServiceBadgeItem> badges;
        badges.push_back({"MTP", pdata.mtp_running});
        badges.push_back({"FTP", pdata.ftp_running});
        if (pdata.usb3_enabled) {
            badges.push_back({"USB 3.0", true});
        }

        nvgFontSize(vg, badge_font);
        float block1_w = 0.f;
        for (size_t i = 0; i < badges.size(); ++i) {
            gfx::textBounds(vg, 0, 0, bounds, badges[i].name);
            const float bw = (bounds[2] - bounds[0]) + 24.f;
            block1_w += bw;
            if (i + 1 < badges.size()) {
                block1_w += badge_gap;
            }
        }

        const float margin_m = std::max(4.f, (storage_span_w - block1_w) * 0.5f);

        float cur_badge_x = storage_left + margin_m;
        for (const auto& b : badges) {
            nvgFontSize(vg, badge_font);
            gfx::textBounds(vg, 0, 0, bounds, b.name);
            const float text_w = bounds[2] - bounds[0];
            const float bw = text_w + 24.f;

            const NVGcolor bg_col = b.is_active
                ? nvgRGBA(28, 68, 42, 220)
                : nvgRGBA(55, 55, 58, 160);
            const NVGcolor dot_col = b.is_active
                ? nvgRGBA(76, 210, 120, 255)
                : nvgRGBA(130, 130, 135, 200);
            const NVGcolor text_col = b.is_active
                ? nvgRGBA(225, 255, 235, 255)
                : theme->GetColour(ThemeEntryID_TEXT_INFO);

            // Container
            gfx::drawRect(vg, cur_badge_x, badge_y, bw, badge_h, bg_col, 4.f);

            // Dot
            const float dot_x = cur_badge_x + 8.f;
            const float dot_y = badge_y + badge_h * 0.5f;
            nvgBeginPath(vg);
            nvgCircle(vg, dot_x, dot_y, 3.f);
            nvgFillColor(vg, dot_col);
            nvgFill(vg);

            // Text
            gfx::drawTextArgs(vg, cur_badge_x + 15.f, dot_y, badge_font, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text_col, "%s", b.name);

            cur_badge_x += bw + badge_gap;
        }

        const auto update_job = auto_update::GetJob();
        const bool show_update = update_job.state == auto_update::JobState::Downloading
            || update_job.state == auto_update::JobState::Installing;

        if (show_update) {
            const float y_label = storage_mid - storage_gap * 0.5f;
            const float y_bar = storage_mid + storage_gap * 0.5f;
            const float bar_y = y_bar - bar_h;
            const float pct = std::clamp(update_job.progress, 0.f, 1.f);
            const NVGcolor track_col = nvgRGBA(80, 80, 80, 180);
            const NVGcolor fill_col = nvgRGBA(56, 189, 248, 255);
            const auto text_col = theme->GetColour(ThemeEntryID_TEXT_INFO);

            nvgFontSize(vg, storage_font);
            gfx::drawTextArgs(vg, label_x + label_col_w * 0.5f, y_label, storage_font,
                NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM, text_col, "%s", "Updating"_i18n.c_str());

            gfx::drawRect(vg, bar_x, bar_y, bar_w, bar_h, track_col);
            gfx::drawRect(vg, bar_x, bar_y, bar_w * pct, bar_h, fill_col);
            gfx::drawTextArgs(vg, value_x, y_bar, storage_font, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, text_col,
                "%d%%", static_cast<int>(pct * 100.f + 0.5f));
        } else {
            draw_storage_bar(storage_mid - storage_gap * 0.5f, nand_bar_label, pdata.nand_free, pdata.nand_total, m_nand_highlight, m_nand_focus);
            draw_storage_bar(storage_mid + storage_gap * 0.5f, sd_bar_label, pdata.sd_free, pdata.sd_total, m_sd_highlight, m_sd_focus);
        }
    } else {
        m_status_left_x = start_x;
    }

    } // draw_status

    // the separators fence the content band off; a panel covering part of one
    // redraws its own at the same y, so the line still reads as continuous.
    gfx::drawRect(vg, layout::SIDE_X, layout::HEADER_LINE_Y, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, layout::SIDE_X, layout::FOOTER_LINE_Y, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    // title block: left half of the header band, covered by a left side panel.
    if (occluded(Vec4{0.f, 0.f, SCREEN_WIDTH / 2.f, layout::HEADER_LINE_Y})) {
        return;
    }

    // Measure version text for positioning on the top row
    nvgFontSize(vg, 14.f);
    char ver_buf[64];
    std::snprintf(ver_buf, sizeof(ver_buf), "v%s", APP_VERSION);
    gfx::textBounds(vg, 0, 0, bounds, ver_buf);
    const float ver_w = bounds[2] - bounds[0];

    gfx::drawTextArgs(vg, 80, start_y - 28.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), "v%s", APP_VERSION);

    constexpr float GAP_MARGIN = 30.f;
    constexpr float GAP_INNER = 20.f;
    const auto text_w = SCREEN_WIDTH / 2 - 30;
    const float gap_right = (draw_status ? m_status_left_x : text_w) - GAP_MARGIN;

    float counter_w = 0.f;
    if (!m_sub_heading.empty() && draw_status && gap_right > 80.f + 100.f) {
        // ScrollingText clips from its x rightwards, so x is always the left
        // edge; right alignment is done by measuring and offsetting. A counter
        // wide enough to fill the gap is left aligned and allowed to scroll.
        nvgFontSize(vg, 18.f);
        gfx::textBounds(vg, 0, 0, bounds, m_sub_heading.c_str());
        counter_w = std::min(bounds[2] - bounds[0], gap_right - 80.f - 100.f);

        m_scroll_sub_heading.Draw(vg, true, gap_right - counter_w, start_y, counter_w, 18,
            NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_sub_heading.c_str());
    }

    const float sub_right = counter_w ? gap_right - counter_w - GAP_INNER : gap_right;

    // Optional stat block right after the title: two small stacked lines
    float stat_w = 0.f;
    if (!m_title_stat_top.empty() || !m_title_stat_bottom.empty()) {
        nvgFontSize(vg, 15.f);
        for (const auto* line : {&m_title_stat_top, &m_title_stat_bottom}) {
            if (line->empty()) continue;
            gfx::textBounds(vg, 0, 0, bounds, line->c_str());
            stat_w = std::max(stat_w, bounds[2] - bounds[0]);
        }
    }

    const float avail_title_w = std::max(50.f, sub_right - 80.f - (stat_w > 0.f ? stat_w + 14.f : 0.f));

    // Dynamic title scaling: base 28px, reduce up to 40% (min 16.8px), then scroll
    constexpr float BASE_TITLE_FONT = 28.f;
    constexpr float MIN_TITLE_FONT = BASE_TITLE_FONT * 0.60f;

    nvgFontSize(vg, BASE_TITLE_FONT);
    gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());
    const float title_unscaled_w = bounds[2] - bounds[0];

    float title_font_size = BASE_TITLE_FONT;
    bool title_needs_scroll = false;

    if (title_unscaled_w <= avail_title_w || avail_title_w <= 0.f) {
        title_font_size = BASE_TITLE_FONT;
    } else {
        const float scale = avail_title_w / title_unscaled_w;
        if (scale >= 0.60f) {
            title_font_size = BASE_TITLE_FONT * scale;
        } else {
            title_font_size = MIN_TITLE_FONT;
            title_needs_scroll = true;
        }
    }

    if (title_needs_scroll) {
        m_scroll_title.Draw(vg, true, 80.f, start_y, avail_title_w, title_font_size,
            NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_title);
    } else {
        m_scroll_title.Reset(m_title);
        gfx::drawTextArgs(vg, 80.f, start_y, title_font_size,
            NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), "%s", m_title.c_str());
    }

    nvgFontSize(vg, title_font_size);
    gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());
    const float rendered_title_w = std::min(bounds[2] - bounds[0], avail_title_w);
    float title_sub_x = 80.f + rendered_title_w + 10.f;

    if (!m_title_stat_top.empty() || !m_title_stat_bottom.empty()) {
        const auto stat_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawTextArgs(vg, title_sub_x, start_y - 16.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, stat_col, "%s", m_title_stat_top.c_str());
        gfx::drawTextArgs(vg, title_sub_x, start_y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, stat_col, "%s", m_title_stat_bottom.c_str());
        title_sub_x += stat_w + 14.f;
    }

    const float gap_left = title_sub_x + GAP_MARGIN - 10.f;

    if (!m_title_sub_heading.empty()) {
        if (m_title_sub_heading_top_row) {
            // Upper expanded slot after measured version text (10px gap)
            const float top_left = 80.f + ver_w + 10.f;
            const float top_w = gap_right - top_left;
            if (top_w > 0) {
                m_scroll_title_sub_heading.Draw(vg, true, top_left, start_y - 28.f, top_w, 16,
                    NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading.c_str());
            }
        } else {
            // Lower row slot between title/stats and counter
            const float lower_w = sub_right - gap_left;
            if (lower_w > 0) {
                m_scroll_title_sub_heading.Draw(vg, true, gap_left, start_y, lower_w, 16,
                    NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading.c_str());
            }
        }
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
}

void MenuBase::SetStorageProjection(u64 nand_bytes, u64 sd_bytes, u64 nand_focus, u64 sd_focus) {
    m_nand_highlight = nand_bytes;
    m_sd_highlight = sd_bytes;
    m_nand_focus = std::min(nand_focus, nand_bytes);
    m_sd_focus = std::min(sd_focus, sd_bytes);
    m_storage_highlight_active = true;
    m_storage_projection = true;
}

void MenuBase::ClearStorageHighlight() {
    m_nand_highlight = 0;
    m_sd_highlight = 0;
    m_nand_focus = 0;
    m_sd_focus = 0;
    m_storage_highlight_active = false;
    m_storage_projection = false;
}

} // namespace sphaira::ui::menu
