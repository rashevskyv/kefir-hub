#include "app.hpp"
#include "log.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"

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

        const auto t = std::time(NULL);
        localtime_r(&t, &data.tm);
        psmGetBatteryChargePercentage(&data.battery_percetange);
        psmGetChargerType(&data.charger_type);
        nifmGetInternetConnectionStatus(&data.type, &data.strength, &data.status);
        nifmGetCurrentIpAddress(&data.ip);

        timestamp.Update();
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

    Widget::Draw(vg, theme);

    const auto pdata = GetPolledData();

    // --- Status bar layout (top-right) ---
    // Line 1 (y=48): IP address
    // Lines 2-3: NAND / SD storage bars, vertically centered between line 1 and line 4
    // Line 4 (y=70 = start_y): Clock + Battery

    const float start_y   = 70;
    const float font_size = 22;
    const float spacing   = 30;
    const float bar_right = 1220;
    const float bar_w     = 172.f;
    const float bar_h     = 8.f;
    const float small_font = 15.f;
    const float y_ip      = 48.f;

    // Align clock + battery at the rightmost position (bar_right = 1220)
    float start_x = bar_right;
    float bounds[4];

    nvgFontSize(vg, font_size);

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
        start_x -= 64;
    } else {
        // discharging: normal color percentage
        draw(ThemeEntryID_TEXT, 64, "%u\uFE6A", pdata.battery_percetange);
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

    // The storage bars should be shifted to the left of the clock/battery.
    // We add a gap to start_x (which currently sits to the left of the leftmost element in Row 4).
    const float storage_right = start_x - 10.f;

    // ---- Row 1 (y=48): IP address ----
    {
        const auto ip_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        if (pdata.ip) {
            gfx::drawTextArgs(vg, bar_right, y_ip, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, ip_col,
                "%u.%u.%u.%u",
                pdata.ip & 0xFF, (pdata.ip >> 8) & 0xFF,
                (pdata.ip >> 16) & 0xFF, (pdata.ip >> 24) & 0xFF);
        } else {
            gfx::drawTextArgs(vg, bar_right, y_ip, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, ip_col,
                "%s", ("No Internet"_i18n).c_str());
        }
    }

    // ---- Helper: compute the left-aligned x of a storage row's label ----
    // storage_right-aligned; label on left of bar
    const float bar_x = storage_right - bar_w;

    auto label_x_of = [&](s64 free_bytes, s64 total_bytes) -> float {
        if (total_bytes <= 0) return bar_x - 4.f;
        const float free_gb = static_cast<float>(free_bytes) / (1024.f * 1024.f * 1024.f);
        char temp_buf[32];
        std::snprintf(temp_buf, sizeof(temp_buf), "%.1f GB", free_gb);
        nvgFontSize(vg, small_font);
        gfx::textBounds(vg, 0, 0, bounds, temp_buf);
        const float value_w = bounds[2] - bounds[0];
        return bar_x - 4.f - value_w - 10.f;
    };

    // NAND and SD labels must line up under one another: use whichever row's
    // label position sits further left (i.e. has the wider value text).
    const float label_x = std::min(
        label_x_of(pdata.nand_free, pdata.nand_total),
        label_x_of(pdata.sd_free, pdata.sd_total));

    auto draw_storage_bar = [&](float y, const char* label, s64 free_bytes, s64 total_bytes) {
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

        // Free space text (right-aligned value next to the bar)
        const float free_gb  = static_cast<float>(free_bytes) / (1024.f * 1024.f * 1024.f);
        const auto  text_col = theme->GetColour(ThemeEntryID_TEXT_INFO);

        nvgFontSize(vg, small_font);
        gfx::drawTextArgs(vg, bar_x - 4.f, y, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, text_col,
            "%.1f GB", free_gb);

        // Label shares the same x across both rows so NAND/SD sit one above the other.
        gfx::drawTextArgs(vg, label_x, y, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, text_col,
            "%s", label);
    };

    // ---- Rows 2-3: NAND / SD bars, vertically centered between the IP row and the clock row ----
    const float storage_mid  = (y_ip + start_y) * 0.5f;
    const float storage_gap  = 15.f;
    draw_storage_bar(storage_mid - storage_gap * 0.5f, "NAND", pdata.nand_free, pdata.nand_total);
    draw_storage_bar(storage_mid + storage_gap * 0.5f, "SD",   pdata.sd_free,   pdata.sd_total);

    gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, 30.f, 646.0f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    nvgFontSize(vg, 28);
    gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());

    const auto text_w = SCREEN_WIDTH / 2 - 30;
    const auto title_sub_x = 80 + (bounds[2] - bounds[0]) + 10;

    gfx::drawTextArgs(vg, 80, start_y - 28.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), "v%s", APP_VERSION);
    gfx::drawTextArgs(vg, 80, start_y, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
    m_scroll_title_sub_heading.Draw(vg, true, title_sub_x, start_y, text_w - title_sub_x, 16, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading.c_str());
    m_scroll_sub_heading.Draw(vg, true, 80, 683, text_w - 160, 18, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), m_sub_heading.c_str());
}

void MenuBase::SetTitle(std::string title) {
    m_title = title;
}

void MenuBase::SetTitleSubHeading(std::string sub_heading) {
    m_title_sub_heading = sub_heading;
}

void MenuBase::SetSubHeading(std::string sub_heading) {
    m_sub_heading = sub_heading;
}

} // namespace sphaira::ui::menu
