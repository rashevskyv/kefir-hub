#include "app.hpp"
#include "log.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"

#include <switch.h>
#include <cmath>

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

    return data;
}

MenuBase::MenuBase(const std::string& title, u32 flags) : m_title{title}, m_flags{flags} {
    // this->SetParent(this);
    this->SetPos(30, 87, 1220 - 30, 646 - 87);
    SetAction(Button::START, Action{[this](){
        FireAction(Button::X);
    }});
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

    const float start_y = 70;
    const float font_size = 22;
    const float spacing = 30;

    float start_x = 1220;
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

    draw(ThemeEntryID_TEXT, 90, "%u\uFE6A", pdata.battery_percetange);

    if (App::Get12HourTimeEnable()) {
        draw(ThemeEntryID_TEXT, 132, "%02u:%02u %s", (pdata.tm.tm_hour == 0 || pdata.tm.tm_hour == 12) ? 12 : pdata.tm.tm_hour % 12, pdata.tm.tm_min, (pdata.tm.tm_hour < 12) ? "AM" : "PM");
    } else {
        draw(ThemeEntryID_TEXT, 90, "%02u:%02u", pdata.tm.tm_hour, pdata.tm.tm_min);
    }

    if (pdata.ip) {
        draw(ThemeEntryID_TEXT, 0, "%u.%u.%u.%u", pdata.ip&0xFF, (pdata.ip>>8)&0xFF, (pdata.ip>>16)&0xFF, (pdata.ip>>24)&0xFF);
    } else {
        draw(ThemeEntryID_TEXT, 0, ("No Internet"_i18n).c_str());
    }
    if (!App::IsApplication()) {
        draw(ThemeEntryID_ERROR, 0, "[A]");
    }
    draw(ThemeEntryID_TEXT_INFO, 0, "v%s", APP_VERSION_HASH);

    #undef draw

    gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, 30.f, 646.0f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    nvgFontSize(vg, 28);
    gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());

    const auto text_w = SCREEN_WIDTH / 2 - 30;
    const auto title_sub_x = 80 + (bounds[2] - bounds[0]) + 10;

    gfx::drawTextArgs(vg, 80, start_y, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
    m_scroll_title_sub_heading.Draw(vg, true, title_sub_x, start_y, text_w - title_sub_x, 16, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading.c_str());
    m_scroll_sub_heading.Draw(vg, true, 80, 685, text_w - 160, 18, NVG_ALIGN_LEFT, theme->GetColour(ThemeEntryID_TEXT), m_sub_heading.c_str());
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
