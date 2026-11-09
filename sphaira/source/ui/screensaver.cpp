#include "ui/screensaver.hpp"
#include "ui/screensaver_views.hpp"
#include "ui/nvg_util.hpp"
#include "ui/battery_gauge.hpp"
#include "ui/menus/menu_base.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sphaira::ui {
namespace {

// the drift of every view's centre; the block never fills the panel, so a two hour install does not burn its outline into an OLED.
constexpr float DRIFT_X = 170.f;
constexpr float DRIFT_Y = 90.f;

// fade the panel rather than snapping it, matching the system's own behaviour.
constexpr u64 BACKLIGHT_FADE_NS = 300000000;

// long enough that the Minus press which started the blank, and a finger still
// resting on the panel, do not immediately wake it again.
constexpr u64 WAKE_GRACE_MS = 700;
// the old-television switch-on when the black page appears: a line of light, then it opens to the full screen.
constexpr u64 CRT_ON_MS = 650;

} // namespace

void Screensaver::Start(bool drain) {
    if (m_active) {
        return;
    }

    // a battery drain wants the panel to burn power: the readout page at full brightness, whatever blank mode is set
    m_mode = drain ? BlankMode::Screensaver : static_cast<BlankMode>(App::GetBlankMode());
    m_active = true;
    m_started.Update();
    m_drift.Update();
    m_last_update.Update();
    m_user_offset_x = 0.f;
    m_user_offset_y = 0.f;
    m_drift_speed_mult = 1.0f;
    m_drift_accum = 0.f;

    App::SetAutoSleepDisabled(true);

    m_lbl_ready = R_SUCCEEDED(lblInitialize());
    if (!m_lbl_ready) {
        // no panel control: the screensaver page still works, the other two
        // modes degrade to doing nothing rather than to a stuck display.
        log_write("[saver] lblInitialize failed, brightness is left alone\n");
        return;
    }

    if (m_mode == BlankMode::BacklightOff) {
        lblSwitchBacklightOff(BACKLIGHT_FADE_NS);
        return;
    }

    lblGetCurrentBrightnessSetting(&m_saved_brightness);
    // auto brightness would drag the panel straight back up to whatever the
    // room says, so it has to be off for the level below to hold.
    lblIsAutoBrightnessControlEnabled(&m_saved_auto_brightness);
    if (m_saved_auto_brightness) {
        lblDisableAutoBrightnessControl();
    }

    const bool is_oled = App::IsOledModel();
    if (drain) {
        m_current_brightness = 1.0f;
    } else if (m_mode == BlankMode::Screensaver && is_oled) {
        // On OLED screensavers, black background pixels are physically off (0W),
        // so retain the user's configured brightness to keep the drifting clock and stats crisp.
        if (!m_brightness_dirty) {
            m_current_brightness = m_saved_brightness;
        }
    } else {
        // On LCD models or Dim mode, lower the panel brightness to save power and backlight bleed.
        if (!m_brightness_dirty) {
            m_current_brightness = App::GetBlankBrightness() / 100.f;
        }
    }

    const float target_brightness = m_current_brightness;
    lblSetCurrentBrightnessSetting(target_brightness);
    lblApplyCurrentBrightnessSettingToBacklight();
    log_write("[Screensaver] brightness: saved=%.2f, auto=%d, oled=%d -> target=%.2f\n",
        m_saved_brightness, m_saved_auto_brightness ? 1 : 0, is_oled ? 1 : 0, target_brightness);
}

void Screensaver::Stop() {
    if (!m_active) {
        return;
    }
    m_active = false;
    App::SetAutoSleepDisabled(false);

    if (!m_lbl_ready) {
        return;
    }

    if (m_mode == BlankMode::BacklightOff) {
        lblSwitchBacklightOn(BACKLIGHT_FADE_NS);
        log_write("[Screensaver] restored backlight on\n");
    } else {
        lblSetCurrentBrightnessSetting(m_saved_brightness);
        lblApplyCurrentBrightnessSettingToBacklight();
        if (m_saved_auto_brightness) {
            lblEnableAutoBrightnessControl();
        }
        log_write("[Screensaver] restored brightness: %.2f, auto=%d\n", m_saved_brightness, m_saved_auto_brightness ? 1 : 0);
    }

    lblExit();
    m_lbl_ready = false;
}

auto Screensaver::WantsWake(const Controller* controller, const TouchInfo* touch) const -> bool {
    if (!m_active || m_started.GetMs() < WAKE_GRACE_MS) {
        return false;
    }
    const auto stick_directions = static_cast<u64>(Button::LS_LEFT) | static_cast<u64>(Button::LS_RIGHT) |
        static_cast<u64>(Button::LS_UP) | static_cast<u64>(Button::LS_DOWN) |
        static_cast<u64>(Button::RS_LEFT) | static_cast<u64>(Button::RS_RIGHT) |
        static_cast<u64>(Button::RS_UP) | static_cast<u64>(Button::RS_DOWN);
    return (controller->m_kdown & ~stick_directions) || touch->is_touching || touch->is_clicked;
}

void Screensaver::Update(const Controller* controller, const TouchInfo* touch) {
    if (!m_active) {
        return;
    }

#ifdef __SWITCH__
    appletReportUserIsActive();
#endif

    const double dt = std::clamp(m_last_update.GetSecondsD(), 0.001, 0.1);
    m_last_update.Update();

    if (!controller) {
        return;
    }

    // Left stick: freely move screensaver position across the screen
    constexpr s32 STICK_DEADZONE = 4000;
    const float lx = std::abs(controller->m_stick_l.x) > STICK_DEADZONE
        ? (float)controller->m_stick_l.x / 32767.f : 0.f;
    const float ly = std::abs(controller->m_stick_l.y) > STICK_DEADZONE
        ? (float)controller->m_stick_l.y / 32767.f : 0.f;

    if (lx != 0.f || ly != 0.f) {
        constexpr float MOVE_SPEED = 450.f;
        m_user_offset_x += lx * MOVE_SPEED * (float)dt;
        m_user_offset_y -= ly * MOVE_SPEED * (float)dt;
        m_user_offset_x = std::clamp(m_user_offset_x, -450.f, 450.f);
        m_user_offset_y = std::clamp(m_user_offset_y, -260.f, 260.f);
    }

    // Right stick: Up/Down to adjust brightness, Left/Right to adjust drift speed
    const float rx = std::abs(controller->m_stick_r.x) > STICK_DEADZONE
        ? (float)controller->m_stick_r.x / 32767.f : 0.f;
    const float ry = std::abs(controller->m_stick_r.y) > STICK_DEADZONE
        ? (float)controller->m_stick_r.y / 32767.f : 0.f;

    if (ry != 0.f) {
        m_osd.Update();
        m_osd_on = true;
        const float new_brightness = std::clamp(m_current_brightness + ry * (float)dt * 0.4f, 0.01f, 1.0f);
        if (new_brightness != m_current_brightness) {
            m_current_brightness = new_brightness;
            if (m_lbl_ready) {
                lblSetCurrentBrightnessSetting(m_current_brightness);
                lblApplyCurrentBrightnessSettingToBacklight();
            }
            m_brightness_dirty = true;
        }
    }

    if (rx != 0.f) {
        m_drift_speed_mult = std::clamp(m_drift_speed_mult + rx * (float)dt * 1.5f, 0.1f, 8.0f);
    }

    m_drift_accum += (float)dt * m_drift_speed_mult;
}

void Screensaver::FlushPendingBrightness() {
    if (m_brightness_dirty) {
        m_brightness_dirty = false;
        const long val = std::clamp(static_cast<long>(std::round(m_current_brightness * 100.f)), 1L, 100L);
        App::SetBlankBrightness(val);
    }
}

// a note at the top: "Brightness 40 %" for two seconds after the right stick moved
void Screensaver::DrawBrightnessNote(NVGcontext* vg) {
    if (!m_osd_on || m_osd.GetSeconds() >= 2) {
        return;
    }
    const auto note = "Brightness"_i18n + " " + std::to_string(static_cast<int>(std::round(m_current_brightness * 100.f))) + " %";
    float b[4]{};
    nvgFontSize(vg, 28.f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgTextBounds(vg, 0, 0, note.c_str(), nullptr, b);
    const float w = (b[2] - b[0]) + 40.f;
    gfx::drawRect(vg, Vec4{SCREEN_WIDTH / 2.f - w / 2.f, 24.f, w, 48.f}, nvgRGBA(40, 44, 52, 230), 8.f);
    gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, 48.f, 28.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, nvgRGB(232, 232, 232), "%s", note.c_str());
}

void Screensaver::Draw(NVGcontext* vg, Theme* theme, const SaverInfo& info) {
    const auto ms = m_started.GetMs();
    if (ms >= CRT_ON_MS) {
        DrawContent(vg, theme, info);
        return;
    }

    const auto ease_out = [](float x) { x = std::clamp(x, 0.f, 1.f); return 1.f - (1.f - x) * (1.f - x) * (1.f - x); };
    const float p = static_cast<float>(ms) / static_cast<float>(CRT_ON_MS);
    // first fifth: the line of light grows from the middle to the full width; then it opens up and the readout fades in.
    const float line = ease_out(p / 0.2f);
    const float open = ease_out((p - 0.2f) / 0.8f);
    const float mid = SCREEN_HEIGHT / 2.f;
    const float band = std::max(3.f, open * SCREEN_HEIGHT);

    gfx::drawRect(vg, Vec4{0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT}, nvgRGB(0, 0, 0));

    nvgSave(vg);
    nvgIntersectScissor(vg, 0.f, mid - band / 2.f, SCREEN_WIDTH, band);
    nvgGlobalAlpha(vg, open);
    DrawContent(vg, theme, info);
    nvgRestore(vg);

    // the glowing edge of the opening band: bright at first, gone when the screen is open.
    const float glow = 1.f - open;
    const float w = SCREEN_WIDTH * line;
    const auto edge = nvgRGBA(235, 245, 255, static_cast<u8>(255.f * glow));
    gfx::drawRect(vg, Vec4{(SCREEN_WIDTH - w) / 2.f, mid - band / 2.f - 1.5f, w, 3.f}, edge);
    gfx::drawRect(vg, Vec4{(SCREEN_WIDTH - w) / 2.f, mid + band / 2.f - 1.5f, w, 3.f}, edge);
}

void Screensaver::DrawContent(NVGcontext* vg, Theme* theme, const SaverInfo& info) {
    gfx::drawRect(vg, Vec4{0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT}, nvgRGB(0, 0, 0));

    // two incommensurate periods, so the block never retraces the same path.
    const auto t = m_drift_accum > 0.f ? m_drift_accum : (float)m_drift.GetSecondsD();
    if (info.bright) {
        gfx::drawDrainFill(vg, SCREEN_WIDTH, SCREEN_HEIGHT, t);
    }

    // whole pixels: the font puts every glyph on a whole pixel while bars and lines take fractions,
    // so at this slow drift the text stepped a pixel at a time and the rest slid on its own.
    float cx = std::round(SCREEN_WIDTH / 2.f + std::sin(t * 0.021f) * DRIFT_X + m_user_offset_x);
    float cy = std::round(SCREEN_HEIGHT / 2.f + std::cos(t * 0.013f) * DRIFT_Y + m_user_offset_y);

    // every view is drawn in the same frame: the rectangle it asks for, kept whole on the panel.
    const auto pdata = menu::MenuBase::GetPolledData();
    const auto block = FitSaverFrame(saver_view::Measure(info, pdata), SCREEN_WIDTH, SCREEN_HEIGHT, cx, cy, m_user_offset_x, m_user_offset_y);
    saver_view::Draw(vg, info, block, pdata);

    if (!info.hint.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT - 40.f, 19.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP,
            info.bright ? nvgRGB(56, 60, 70) : nvgRGB(109, 127, 136), "%s", info.hint.c_str());
    }
    DrawBrightnessNote(vg);
}

SaverPreview::SaverPreview() {
    m_saver.Start();
}

void SaverPreview::Update(Controller* controller, TouchInfo* touch) {
    m_saver.Update(controller, touch);
    if (m_saver.WantsWake(controller, touch)) {
        m_saver.Stop();
        m_saver.FlushPendingBrightness();
        SetPop();
    }
}

void SaverPreview::Draw(NVGcontext* vg, Theme* theme) {
    if (!m_saver.OwnsScreen()) {
        // dim / backlight-off have nothing to paint: the point of previewing
        // them is the panel itself, with the settings page still underneath.
        return;
    }

    SaverInfo info{};
    info.status = "Installing"_i18n;
    info.file = "Sample Game [0100000000010000][v0].nsp";
    info.package = 3;
    info.package_count = 12;
    info.installed = 2;
    info.ratio = 0.42;
    info.speed_mib = 27.5;
    info.eta = "8m 12s";
    info.elapsed_ns = 11ULL * 60 * 1000000000ULL;

    info.has_graph = true;
    info.history_count = 48;
    info.history_index = 48;
    for (size_t i = 0; i < 48; i++) {
        info.read_history[i] = static_cast<s64>((28.0 + 4.0 * std::sin(i * 0.25)) * 1024 * 1024);
        info.write_history[i] = static_cast<s64>((24.0 + 3.5 * std::cos(i * 0.25)) * 1024 * 1024);
    }

    m_saver.Draw(vg, theme, info);
    gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT - 44.f, 18.f,
        NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nvgRGB(90, 94, 104), "%s", "Press any button to exit"_i18n.c_str());
}

} // namespace sphaira::ui
