#include "app.hpp"
#include "log.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"

#include <switch.h>
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
    // We use a clean, static grid layout with fixed right-aligned positions
    // to prevent any element shifting or jumping when charger states or values change.
    //
    // Fixed Horizontal Anchors (Right Aligned):
    // - Clock: right aligned to 1220 (width max 132 for 12h) -> starts at x=1080 (reserved 140px)
    // - Battery: right aligned to 1070 -> starts at x=980 (reserved 90px)
    // - Storage Bars (NAND/SD): right aligned to 960 (width 160px) -> starts at x=800
    // - Storage Values: right aligned to 790 -> starts at x=690
    // - Storage Labels (NAND/SD): right aligned to 680
    
    const float start_y     = 70;
    const float font_size   = 22;
    const float bar_right   = 960.f;  // Storage bar right anchor
    const float bar_w       = 160.f;
    const float bar_h       = 7.f;
    const float small_font  = 14.f;
    float bounds[4];

    // ---- Clock (Fixed anchor: x = 1220, right-aligned) ----
    nvgFontSize(vg, font_size);
    if (App::Get12HourTimeEnable()) {
        gfx::drawTextArgs(vg, 1220.f, start_y, font_size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT),
            "%02u:%02u %s",
            (pdata.tm.tm_hour == 0 || pdata.tm.tm_hour == 12) ? 12 : pdata.tm.tm_hour % 12,
            pdata.tm.tm_min, (pdata.tm.tm_hour < 12) ? "AM" : "PM");
    } else {
        gfx::drawTextArgs(vg, 1220.f, start_y, font_size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT),
            "%02u:%02u", pdata.tm.tm_hour, pdata.tm.tm_min);
    }

    if (!App::IsApplication()) {
        gfx::drawTextArgs(vg, 1220.f - 132.f, start_y, font_size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_ERROR), "[A]");
    }

    // ---- Battery (Fixed anchor: x = 1070, right-aligned) ----
    // Percentage is always displayed. If charging, % sign is replaced by lightning bolt symbol.
    if (pdata.charger_type != 0) {
        // charging: draw number, and replace the '%' sign with a pulsing lightning bolt.
        const float time = static_cast<float>(armTicksToNs(armGetSystemTick())) / 1'000'000'000.f;
        const float factor = 0.5f + 0.5f * std::sin(time * 6.f); // Pulsing speed
        const NVGcolor pulse_col = nvgRGBA(255, static_cast<u8>(140 + 100 * factor), 0, 255);

        // Draw percentage value (right aligned to 1045.f)
        gfx::drawTextArgs(vg, 1045.f, start_y, font_size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), "%u", pdata.battery_percetange);

        // Draw lightning bolt glyph right after the number (centered vertically with text baseline)
        const float lb_h = 18.f;
        const float lb_w = lb_h * 0.55f;
        const float lb_x = 1051.f;
        const float lb_y = start_y - 20.f;

        nvgBeginPath(vg);
        nvgMoveTo(vg, lb_x + lb_w * 0.55f, lb_y);
        nvgLineTo(vg, lb_x + lb_w * 0.05f, lb_y + lb_h * 0.55f);
        nvgLineTo(vg, lb_x + lb_w * 0.5f,  lb_y + lb_h * 0.55f);
        nvgLineTo(vg, lb_x + lb_w * 0.35f, lb_y + lb_h);
        nvgLineTo(vg, lb_x + lb_w * 0.95f, lb_y + lb_h * 0.45f);
        nvgLineTo(vg, lb_x + lb_w * 0.5f,  lb_y + lb_h * 0.45f);
        nvgClosePath(vg);
        nvgFillColor(vg, pulse_col);
        nvgFill(vg);
    } else {
        // discharging: normal text with percentage symbol (right aligned to 1070.f)
        gfx::drawTextArgs(vg, 1070.f, start_y, font_size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), "%u\uFE6A", pdata.battery_percetange);
    }

    // ---- Row 1 (y=48): IP address ----
    // Align it to the right of the screen (1220.f)
    {
        const float y_ip = 48.f;
        const auto ip_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        if (pdata.ip) {
            gfx::drawTextArgs(vg, 1220.f, y_ip, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, ip_col,
                "%u.%u.%u.%u",
                pdata.ip & 0xFF, (pdata.ip >> 8) & 0xFF,
                (pdata.ip >> 16) & 0xFF, (pdata.ip >> 24) & 0xFF);
        } else {
            gfx::drawTextArgs(vg, 1220.f, y_ip, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, ip_col,
                "%s", ("No Internet"_i18n).c_str());
        }
    }

    // ---- Storage Bars Helper ----
    // Pre-calculate the maximum text width between NAND and SD values to align their labels on the exact same vertical line.
    float nand_val_w = 0.f;
    float sd_val_w = 0.f;
    char temp_nand[32];
    char temp_sd[32];
    std::snprintf(temp_nand, sizeof(temp_nand), "%.1f GB", static_cast<float>(pdata.nand_free) / (1024.f * 1024.f * 1024.f));
    std::snprintf(temp_sd, sizeof(temp_sd), "%.1f GB", static_cast<float>(pdata.sd_free) / (1024.f * 1024.f * 1024.f));
    
    nvgFontSize(vg, small_font);
    if (pdata.nand_total > 0) {
        gfx::textBounds(vg, 0, 0, bounds, temp_nand);
        nand_val_w = bounds[2] - bounds[0];
    }
    if (pdata.sd_total > 0) {
        gfx::textBounds(vg, 0, 0, bounds, temp_sd);
        sd_val_w = bounds[2] - bounds[0];
    }
    const float max_val_w = std::max(nand_val_w, sd_val_w);

    auto draw_storage_bar = [&](float y, const char* label, s64 free_bytes, s64 total_bytes) {
        if (total_bytes <= 0) return;
        const float used_ratio = 1.f - static_cast<float>(free_bytes) / static_cast<float>(total_bytes);
        const float fill_w     = bar_w * used_ratio;
        const float bar_x      = bar_right - bar_w;
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

        // Free space text (right-aligned value at bar_x - 4.f)
        const float free_gb  = static_cast<float>(free_bytes) / (1024.f * 1024.f * 1024.f);
        const auto  text_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawTextArgs(vg, bar_x - 4.f, y, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, text_col,
            "%.1f GB", free_gb);
        
        // Label is right-aligned to a position that is (max_val_w + 10px) to the left of the bar.
        // This ensures NAND and SD labels always stay in one vertical line and shift smoothly if values change.
        gfx::drawTextArgs(vg, bar_x - 4.f - max_val_w - 10.f, y, small_font, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, text_col,
            "%s", label);
    };

    // ---- Rows 2-3 (y=62, y=76): NAND / SD bars ----
    draw_storage_bar(62.f, "NAND", pdata.nand_free, pdata.nand_total);
    draw_storage_bar(76.f, "SD",   pdata.sd_free,   pdata.sd_total);

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
