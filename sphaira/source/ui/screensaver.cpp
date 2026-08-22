#include "ui/screensaver.hpp"
#include "ui/nvg_util.hpp"
#include "ui/menus/menu_base.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sphaira::ui {
namespace {

// the block never fills the panel: it drifts inside the margin left over, so a
// two hour install does not burn its outline into an OLED.
constexpr float BLOCK_W = 840.f;
constexpr float DRIFT_X = 170.f;
constexpr float DRIFT_Y = 90.f;

// fade the panel rather than snapping it, matching the system's own behaviour.
constexpr u64 BACKLIGHT_FADE_NS = 300000000;

// long enough that the Minus press which started the blank, and a finger still
// resting on the panel, do not immediately wake it again.
constexpr u64 WAKE_GRACE_MS = 700;

auto FormatElapsed(u64 ns) -> std::string {
    const auto secs = ns / 1000000000ULL;
    char buf[32]{};
    if (secs >= 3600) {
        std::snprintf(buf, sizeof(buf), "%zuh %zum", (size_t)(secs / 3600), (size_t)(secs % 3600 / 60));
    } else {
        std::snprintf(buf, sizeof(buf), "%zum %zus", (size_t)(secs / 60), (size_t)(secs % 60));
    }
    return buf;
}

auto FieldEnabled(SaverField field) -> bool {
    return App::GetSaverFields() & field;
}

} // namespace

void Screensaver::Start() {
    if (m_active) {
        return;
    }

    m_mode = static_cast<BlankMode>(App::GetBlankMode());
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
    if (m_mode == BlankMode::Screensaver && is_oled) {
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

void Screensaver::Draw(NVGcontext* vg, Theme* theme, const SaverInfo& info) {
    // the readout is drawn on a black page, so it uses its own palette rather
    // than the theme: a light theme would paint black text onto it.
    const auto text_col = nvgRGB(232, 232, 232);
    const auto dim_col = nvgRGB(140, 144, 154);
    const auto accent_col = nvgRGB(80, 170, 255);
    const auto bad_col = nvgRGB(235, 90, 80);

    const bool oled = App::GetSaverOled();
    const auto pdata = menu::MenuBase::GetPolledData();

    gfx::drawRect(vg, Vec4{0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT}, nvgRGB(0, 0, 0));

    // two incommensurate periods, so the block never retraces the same path.
    const auto t = m_drift_accum > 0.f ? m_drift_accum : (float)m_drift.GetSecondsD();
    float cx = SCREEN_WIDTH / 2.f + std::sin(t * 0.021f) * DRIFT_X + m_user_offset_x;
    float cy = SCREEN_HEIGHT / 2.f + std::cos(t * 0.013f) * DRIFT_Y + m_user_offset_y;
    const float left = cx - BLOCK_W / 2.f;

    // speed / eta / elapsed / battery share one line, so how many of them are
    // on decides whether that line takes any height at all.
    std::string stats;
    const auto add_stat = [&stats](const std::string& value) {
        if (!value.empty()) {
            stats += stats.empty() ? value : "   ·   " + value;
        }
    };

    if (FieldEnabled(SaverField_Speed)) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%.1f MiB/s", info.speed_mib);
        add_stat(buf);
    }
    if (FieldEnabled(SaverField_Eta) && !info.eta.empty()) {
        add_stat(info.eta + " " + "left"_i18n);
    }
    if (FieldEnabled(SaverField_Elapsed) && info.elapsed_ns) {
        add_stat(FormatElapsed(info.elapsed_ns));
    }
    if (FieldEnabled(SaverField_Battery)) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%s%u%%",
            pdata.charger_type != 0 ? "+" : "", pdata.battery_percetange);
        add_stat(buf);
    }

    const bool show_clock = FieldEnabled(SaverField_Clock);
    const bool show_completion = info.is_complete;
    const bool show_status = FieldEnabled(SaverField_Status) && !info.status.empty() && !show_completion;
    const bool show_counter = FieldEnabled(SaverField_Counter) && info.package_count;
    const bool show_file = FieldEnabled(SaverField_File) && !info.file.empty();
    const bool show_bar = FieldEnabled(SaverField_Bar);
    const bool show_stats = !stats.empty();
    const bool show_errors = FieldEnabled(SaverField_Errors) && info.failed;
    const bool show_graph = FieldEnabled(SaverField_Graph) && info.has_graph && info.history_count >= 2 && !show_completion;

    const float clock_h = 86.f, status_h = 32.f, counter_h = 34.f;
    const float file_h = 32.f, bar_h = 52.f, stats_h = 32.f, errors_h = 30.f;
    const float graph_h = 115.f, completion_h = 50.f;

    float total = 0.f;
    total += show_clock ? clock_h : 0.f;
    total += show_status ? status_h : 0.f;
    total += show_counter ? counter_h : 0.f;
    total += show_file ? file_h : 0.f;
    total += show_bar ? bar_h : 0.f;
    total += show_stats ? stats_h : 0.f;
    total += show_errors ? errors_h : 0.f;
    total += show_graph ? graph_h + 10.f : 0.f;
    total += show_completion ? completion_h + 10.f : 0.f;

    float y = cy - total / 2.f;

    // the clock is deliberately the biggest thing on the panel: it is what a
    // glance from across the room can actually resolve.
    if (show_clock) {
        if (App::Get12HourTimeEnable()) {
            gfx::drawTextArgs(vg, cx, y, 76.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, text_col, "%02u:%02u %s",
                (pdata.tm.tm_hour == 0 || pdata.tm.tm_hour == 12) ? 12 : pdata.tm.tm_hour % 12,
                pdata.tm.tm_min, pdata.tm.tm_hour < 12 ? "AM" : "PM");
        } else {
            gfx::drawTextArgs(vg, cx, y, 76.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, text_col, "%02u:%02u",
                pdata.tm.tm_hour, pdata.tm.tm_min);
        }
        y += clock_h;
    }

    if (show_status) {
        gfx::drawTextArgs(vg, cx, y, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP,
            info.failed ? bad_col : dim_col, "%s", info.status.c_str());
        y += status_h;
    }

    if (show_counter) {
        gfx::drawTextArgs(vg, cx, y, 26.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, text_col, "%s %zu / %zu",
            "Package"_i18n.c_str(), info.package, info.package_count);
        y += counter_h;
    }

    if (show_file) {
        nvgSave(vg);
        nvgIntersectScissor(vg, left, y, BLOCK_W, file_h);

        float font_sz = 22.f;
        nvgFontSize(vg, font_sz);
        float bounds[4]{};
        gfx::textBounds(vg, 0, 0, bounds, info.file.c_str());
        float text_w = bounds[2] - bounds[0];

        // If text is slightly too long for BLOCK_W, adapt font size down to 18px to fit
        if (text_w > BLOCK_W && font_sz > 18.f) {
            font_sz = std::max(18.f, font_sz * (BLOCK_W / text_w));
            nvgFontSize(vg, font_sz);
            gfx::textBounds(vg, 0, 0, bounds, info.file.c_str());
            text_w = bounds[2] - bounds[0];
        }

        if (text_w <= BLOCK_W) {
            // Fits within BLOCK_W: center it nicely
            gfx::drawTextArgs(vg, cx, y, font_sz, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, dim_col, "%s", info.file.c_str());
        } else {
            // Still exceeds BLOCK_W: align left from left margin so the beginning of the title is never clipped
            gfx::drawTextArgs(vg, left, y, font_sz, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, dim_col, "%s", info.file.c_str());
        }
        nvgRestore(vg);
        y += file_h;
    }

    if (show_bar) {
        const Vec4 bar{left, y + 28.f, BLOCK_W, 14.f};
        // OLED mode lights only the pixels that carry information, so the empty
        // part of the track stays unlit; the filled length still reads.
        if (!oled) {
            gfx::drawRect(vg, bar, nvgRGBA(255, 255, 255, 40), 7.f);
        }
        gfx::drawRect(vg, bar.x, bar.y, std::max(4.f, bar.w * (float)info.ratio), bar.h, accent_col, 7.f);
        gfx::drawTextArgs(vg, left, y, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, dim_col, "%s: %zu",
            "Installed"_i18n.c_str(), info.installed);
        gfx::drawTextArgs(vg, left + BLOCK_W, y, 22.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, accent_col,
            "%.0f%%", info.ratio * 100.0);
        y += bar_h;
    }

    if (show_stats) {
        gfx::drawTextArgs(vg, cx, y, 22.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, dim_col, "%s", stats.c_str());
        y += stats_h;
    }

    if (show_errors) {
        gfx::drawTextArgs(vg, cx, y, 22.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, bad_col, "%s: %zu",
            "Failed"_i18n.c_str(), info.failed);
        y += errors_h;
    }

    if (show_completion) {
        y += 10.f;
        const auto comp_col = info.is_failed ? bad_col : accent_col;
        const auto& comp_text = !info.status.empty() ? info.status : (info.is_failed ? "Finished with errors"_i18n : "Finished"_i18n);
        gfx::drawTextArgs(vg, cx, y, 32.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, comp_col, "%s", comp_text.c_str());
        y += completion_h;
    }

    if (show_graph) {
        y += 6.f;
        const auto red = nvgRGBA(231, 76, 60, 255);
        const auto blue = nvgRGBA(52, 152, 219, 255);
        const float graph_w = 600.f;
        const Vec4 plot{(SCREEN_WIDTH - graph_w) / 2.f, y, graph_w, 95.f};
        const float pad = 4.f;

        gfx::drawRect(vg, plot, nvgRGBA(20, 20, 25, 220), 5.f);

        s64 peak = 1;
        for (size_t i = 0; i < info.history_count; i++) {
            const auto idx = (info.history_index + 96 - info.history_count + i) % 96;
            peak = std::max({peak, info.read_history[idx], info.write_history[idx]});
        }
        const double peak_mib = (double)peak / (1024.0 * 1024.0);

        const auto nice_step = [](double range) -> double {
            double s = 1.0;
            while (true) {
                if (range <= s) return s;
                if (range <= s * 2.0) return s * 2.0;
                if (range <= s * 5.0) return s * 5.0;
                s *= 10.0;
            }
        };
        const double step_mib = nice_step(std::max(peak_mib, 1.0) / 4.0);
        int steps = 1;
        while (step_mib * steps < peak_mib) steps++;
        const double top_mib = step_mib * steps;
        const double top = top_mib * 1024.0 * 1024.0;

        nvgSave(vg);
        nvgIntersectScissor(vg, plot.x, plot.y, plot.w, plot.h);

        auto grid_col = nvgRGBA(255, 255, 255, 30);
        const auto label_col = nvgRGBA(140, 144, 154, 255);
        const float inner_h = plot.h - pad * 2.f;
        for (int k = 0; k <= steps; k++) {
            const float gy = plot.y + plot.h - pad - inner_h * (float)k / (float)steps;
            nvgBeginPath(vg);
            nvgMoveTo(vg, plot.x + pad, gy);
            nvgLineTo(vg, plot.x + plot.w - pad, gy);
            nvgStrokeColor(vg, grid_col);
            nvgStrokeWidth(vg, 1.f);
            nvgStroke(vg);
            if (k > 0) {
                const double val = step_mib * k;
                if (k == steps) {
                    gfx::drawTextArgs(vg, plot.x + pad + 4.f, gy + 2.f, 11.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_col, "%g MiB/s", val);
                }
            }
        }

        const auto draw_line = [&](const std::array<s64, 96>& history, NVGcolor colour) {
            nvgBeginPath(vg);
            for (size_t i = 0; i < info.history_count; i++) {
                const auto idx = (info.history_index + 96 - info.history_count + i) % 96;
                const auto slot = 96 - info.history_count + i;
                const float px = plot.x + pad + (plot.w - pad * 2.f) * slot / 95.f;
                const double frac = std::clamp((double)history[idx] / top, 0.0, 1.0);
                const float py = plot.y + plot.h - pad - inner_h * (float)frac;
                if (i == 0) nvgMoveTo(vg, px, py);
                else nvgLineTo(vg, px, py);
            }
            nvgStrokeColor(vg, colour);
            nvgStrokeWidth(vg, 2.f);
            nvgStroke(vg);
        };

        nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
        draw_line(info.read_history, red);
        draw_line(info.write_history, blue);

        nvgRestore(vg);
        y += graph_h;
    }
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
