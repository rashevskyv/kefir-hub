#include "battery_drain_menu.hpp"
#include "battery_calib_ui.hpp"

namespace sphaira::battery {

void DrainMenu::Chart(NVGcontext* vg, float x, float y, float w, float h, const std::string& title, const std::string& value,
           std::initializer_list<Line> lines, float lo, float hi, NVGcolor text, NVGcolor info, float mark,
           const std::vector<std::pair<NVGcolor, std::string>>& legend) const {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, 12.f);
    nvgFillColor(vg, nvgTransRGBAf(info, 0.08f));
    nvgFill(vg);
    ui::gfx::drawTextArgs(vg, x + 16.f, y + 22.f, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", title.c_str());
    ui::gfx::drawTextArgs(vg, x + w - 16.f, y + 22.f, 24.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, text, "%s", value.c_str());

    // the legend sits between the title and the plot: two columns, a dot and a text each
    const float legend_h = legend.empty() ? 0.f : 24.f * static_cast<float>((legend.size() + 1) / 2) + 4.f;
    for (size_t k = 0; k < legend.size(); k++) {
        const float lx = x + 16.f + static_cast<float>(k % 2) * (w - 32.f) / 2.f;
        const float ly = y + 56.f + static_cast<float>(k / 2) * 24.f;
        nvgBeginPath(vg);
        nvgCircle(vg, lx + 5.f, ly, 5.f);
        nvgFillColor(vg, legend[k].first);
        nvgFill(vg);
        ui::gfx::drawTextArgs(vg, lx + 18.f, ly, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", legend[k].second.c_str());
    }
    const float px = x + 12.f, py = y + 44.f + legend_h, pw = w - 24.f, ph = h - 56.f - legend_h;
    const auto Y = [&](float v) { return py + ph * (1.f - std::clamp((v - lo) / (hi - lo), 0.f, 1.f)); };
    nvgStrokeWidth(vg, 1.f);
    nvgStrokeColor(vg, nvgTransRGBAf(info, 0.15f));
    for (int l = 0; l <= 4; l++) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, px, py + ph * l / 4.f);
        nvgLineTo(vg, px + pw, py + ph * l / 4.f);
        nvgStroke(vg);
    }
    if (mark >= lo) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, px, Y(mark));
        nvgLineTo(vg, px + pw, Y(mark));
        nvgStrokeColor(vg, nvgRGBA(0xE5, 0x39, 0x35, 160));
        nvgStrokeWidth(vg, 2.f);
        nvgStroke(vg);
    }

    const float frac = std::min(1.f, m_live.GetMs() / static_cast<float>(LIVE_MS));
    const float step = pw / (LIVE_N - 1);
    nvgScissor(vg, px, py - 2.f, pw, ph + 4.f);
    for (const auto& line : lines) {
        const auto& d = *line.d;
        const size_t n = d.size();
        if (n < 2) {
            continue;
        }
        const auto X = [&](size_t k) { return px + pw - (n - 1 - k + frac) * step; };
        nvgBeginPath(vg);
        nvgMoveTo(vg, X(0), py + ph);
        for (size_t k = 0; k < n; k++) {
            nvgLineTo(vg, X(k), Y(d[k]));
        }
        nvgLineTo(vg, px + pw, Y(d[n - 1]));
        nvgLineTo(vg, px + pw, py + ph);
        nvgClosePath(vg);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, py, 0, py + ph, nvgTransRGBAf(line.c, 0.35f), nvgTransRGBAf(line.c, 0.02f)));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgMoveTo(vg, X(0), Y(d[0]));
        for (size_t k = 1; k < n; k++) {
            nvgLineTo(vg, X(k), Y(d[k]));
        }
        nvgLineTo(vg, px + pw, Y(d[n - 1]));
        nvgStrokeColor(vg, line.c);
        nvgStrokeWidth(vg, 3.f);
        nvgStroke(vg);
    }
    nvgResetScissor(vg);
}

auto DrainMenu::Last(const std::deque<float>& d) -> float {
    return d.empty() ? 0.f : d.back();
}

auto DrainMenu::HoursMinutes(u64 sec) -> std::string {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lu:%02lu", sec / 3600, (sec / 60) % 60);
    return buf;
}

auto DrainMenu::HoursMinutesSeconds(u64 sec) -> std::string {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", sec / 3600, (sec / 60) % 60, sec % 60);
    return buf;
}

void DrainMenu::DrawCalib(NVGcontext* vg, Theme* theme) {
    const auto text = theme->GetColour(ThemeEntryID_TEXT);
    const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto warn = nvgRGB(0xF5, 0x9E, 0x0B);
    const float pct = std::clamp(m_pct, 0, 100) / 100.f;
    const auto level = pct > 0.5f ? nvgLerpRGBA(nvgRGB(0xF5, 0x9E, 0x0B), nvgRGB(0x4C, 0xAF, 0x50), (pct - 0.5f) * 2.f)
                                  : nvgLerpRGBA(nvgRGB(0xE5, 0x39, 0x35), nvgRGB(0xF5, 0x9E, 0x0B), pct * 2.f);

    // the battery
    const float bx = 70.f, by = 108.f, bw = 360.f, bh = 150.f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx, by, bw, bh, 18.f);
    nvgStrokeColor(vg, text);
    nvgStrokeWidth(vg, 6.f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx + bw, by + bh / 2.f - 30.f, 22.f, 60.f, 6.f);
    nvgFillColor(vg, text);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx + 12.f, by + 12.f, (bw - 24.f) * pct, bh - 24.f, 10.f);
    nvgFillColor(vg, level);
    nvgFill(vg);
    ui::gfx::drawTextArgs(vg, bx + bw / 2.f, by + bh / 2.f, 72.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, text, "\u2248 %d%%", m_pct);
    calib_ui::DrawWrapped(vg, bx, by + bh + 10.f, 16.f, bw, info, "The percent is approximate: the console may switch off earlier."_i18n);

    // the voltage and the state
    const float fx = 520.f;
    ui::gfx::drawTextArgs(vg, fx, 118.f, 64.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%.2f V", m_mv / 1000.0);
    const bool attention = m_charger || m_docked || m_hot;
    calib_ui::DrawWrapped(vg, fx, 158.f, 18.f, 690.f, attention ? warn : info, m_status);

    // the chips
    const auto Chip = [&](float x, float y, const std::string& label, const std::string& value) {
        float b[4]{};
        const auto s = label + " " + value;
        nvgFontSize(vg, 18.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, b);
        const float w = (b[2] - b[0]) + 28.f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, 36.f, 18.f);
        nvgFillColor(vg, nvgTransRGBAf(info, 0.14f));
        nvgFill(vg);
        ui::gfx::drawTextArgs(vg, x + 14.f, y + 18.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", label.c_str());
        nvgFontSize(vg, 18.f);
        float lb[4]{};
        nvgTextBounds(vg, 0, 0, (label + " ").c_str(), nullptr, lb);
        ui::gfx::drawTextArgs(vg, x + 14.f + (lb[2] - lb[0]) + 8.f, y + 18.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s", value.c_str());
        return w;
    };
    if (!m_calib->charge) {
        const std::string load_text = m_calib->charge || m_charger || !g_load ? (m_calib->tail && !m_charger ? "Minimum"_i18n : "Off"_i18n) : "Maximum"_i18n;
        const int brightness = m_calib->charge ? static_cast<int>(m_saved_brightness * 100.f + 0.5f)
            : m_calib_mode == 1 ? static_cast<int>(m_saved_brightness * 100.f + 0.5f) : m_calib_mode == 2 ? 1 : 100;
        const float w1 = Chip(fx, 232.f, "Load:"_i18n, load_text);
        Chip(fx + w1 + 12.f, 232.f, "Brightness:"_i18n, std::to_string(brightness) + " %");

    }

    // the figures
    char v[64];
    std::snprintf(v, sizeof(v), "%d mA", std::abs(m_ma));
    ui::gfx::drawTextArgs(vg, fx, 292.f, 19.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", (m_calib->charge ? "Charging current"_i18n : "Consumption from the battery"_i18n).c_str());
    ui::gfx::drawTextArgs(vg, fx + 690.f, 292.f, 19.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, text, "%s", v);
    ui::gfx::drawTextArgs(vg, fx, 322.f, 19.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", (m_calib->charge ? "The charge has lasted"_i18n : "The drain has lasted"_i18n).c_str());
    ui::gfx::drawTextArgs(vg, fx + 690.f, 322.f, 19.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, text, "%s", HoursMinutesSeconds(DrainSeconds()).c_str());

    if (m_calib->charge) {
        const auto good = nvgRGB(0x4C, 0xAF, 0x50);
        float y = 372.f;
        if (m_charger) {
            calib_ui::DrawTick(vg, 70.f, y + 3.f, 24.f, good);
            ui::gfx::drawTextArgs(vg, 104.f, y, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, good, "%s", "Charger is plugged in."_i18n.c_str());
        } else {
            ui::gfx::drawTextArgs(vg, 70.f, y, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, warn, "%s", "Plug the charger in."_i18n.c_str());
        }
        y += 46.f;
        if (m_calib->reset) {
            return; // the status above says what comes next; the cycle texts below do not apply to the reset
        }
        if (m_charger && m_pct >= 100) {
            ui::gfx::drawTextBox(vg, 70.f, y, 22.f, 1140.f, warn, "Full. Now the drain: take the charger out."_i18n.c_str());
            y += 44.f;
        }
        calib_ui::DrawWrapped(vg, 92.f, y, 21.f, 1110.f, text, "You can leave the console like this. You can also use it while it charges. When the charge reaches 100 %, come back to Kefir Hub and open Battery calibration."_i18n);
        y += 76.f;
        calib_ui::DrawWrapped(vg, 92.f, y, 21.f, 1110.f, text, "The screen stays on: so the console charges to the end. The screensaver saves the screen."_i18n);
        return;
    }
    // one row of three charts
    const float cy = 356.f, ch = 270.f, gap = 18.f, cw = (1280.f - 2.f * 60.f - 2.f * gap) / 3.f;
    std::snprintf(v, sizeof(v), "%.2f V", m_mv / 1000.0);
    Chart(vg, 60.f, cy, cw, ch, "Voltage"_i18n, v, {{&m_volts, nvgRGB(0x60, 0xA5, 0xFA)}}, 3.0f, 4.3f, text, info, 3.2f);
    // the load: three cores and the GPU in one chart; the legend names each line with its value
    const auto core = "Core"_i18n;
    const auto pct_text = [](float f) { char b[16]; std::snprintf(b, sizeof(b), " %.0f%%", f); return std::string{b}; };
    const NVGcolor c0 = nvgRGB(0x2D, 0xD4, 0xBF), c1 = nvgRGB(0x60, 0xA5, 0xFA), c2 = nvgRGB(0xA7, 0x8B, 0xFA), c3 = nvgRGB(0x4A, 0xDE, 0x80);
    Chart(vg, 60.f + cw + gap, cy, cw, ch, "CPU / GPU load"_i18n, "", {{&m_cpu[0], c0}, {&m_cpu[1], c1}, {&m_cpu[2], c2}, {&m_gpu, c3}},
          0.f, 105.f, text, info, -1.f,
          {{c0, core + " 1" + pct_text(Last(m_cpu[0]))}, {c1, core + " 2" + pct_text(Last(m_cpu[1]))},
           {c2, core + " 3" + pct_text(Last(m_cpu[2]))}, {c3, "GPU"_i18n + pct_text(Last(m_gpu))}});
    std::snprintf(v, sizeof(v), "%.1f \u00B0C", Last(m_temps));
    Chart(vg, 60.f + 2.f * (cw + gap), cy, cw, ch, "Temperature"_i18n, v, {{&m_temps, nvgRGB(0xF8, 0x71, 0x71)}}, 20.f, 70.f, text, info, static_cast<float>(HOT_C));
}

void DrainMenu::Draw(NVGcontext* vg, Theme* theme) {
    m_t += 0.02f;
    if (g_load) {
        for (int i = 0; i < CIRCLES; i++) {
            const float a = m_t + i * 0.37f;
            const float x = SCREEN_WIDTH / 2.f + std::cos(a * 1.3f) * 300.f;
            const float y = SCREEN_HEIGHT / 2.f + std::sin(a * 0.9f) * 200.f;
            const auto paint = nvgRadialGradient(vg, x, y, 10.f, 900.f, nvgRGBA(40 + (i * 5) % 200, 120, 200, 10), nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgCircle(vg, x, y, 900.f);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }
    }
    if (m_saver.OwnsScreen()) {
        m_saver.Draw(vg, theme, SaverReadout()); // black page; the circles above keep the GPU busy underneath
        return;
    }
    MenuBase::Draw(vg, theme); // after the load: the page background covers the circles, the GPU still draws them
    if (m_calib) {
        DrawCalib(vg, theme);
        return;
    }

    const auto text = theme->GetColour(ThemeEntryID_TEXT);
    const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto warn = nvgRGB(0xF5, 0x9E, 0x0B);
    const float pct = std::clamp(m_pct, 0, 100) / 100.f;
    // green at full, orange in the middle, red near empty; it breathes while the load runs
    const auto level = pct > 0.5f ? nvgLerpRGBA(nvgRGB(0xF5, 0x9E, 0x0B), nvgRGB(0x4C, 0xAF, 0x50), (pct - 0.5f) * 2.f)
                                  : nvgLerpRGBA(nvgRGB(0xE5, 0x39, 0x35), nvgRGB(0xF5, 0x9E, 0x0B), pct * 2.f);
    const float pulse = g_load ? 0.75f + 0.25f * std::sin(m_t * 4.f) : 1.f;

    // the battery
    const float bx = 120.f, by = 112.f, bw = 360.f, bh = 150.f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx, by, bw, bh, 18.f);
    nvgStrokeColor(vg, text);
    nvgStrokeWidth(vg, 6.f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx + bw, by + bh / 2.f - 30.f, 22.f, 60.f, 6.f);
    nvgFillColor(vg, text);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx + 12.f, by + 12.f, (bw - 24.f) * pct, bh - 24.f, 10.f);
    nvgFillColor(vg, nvgTransRGBAf(level, pulse));
    nvgFill(vg);
    ui::gfx::drawTextArgs(vg, bx + bw / 2.f, by + bh / 2.f, 90.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, text, "%d%%", m_pct);

    // the figures and what is going on
    const float fx = 560.f;
    const auto title = m_calib ? CalibTitle() : (m_cycle == 1 ? "First cycle"_i18n : "Second cycle"_i18n);
    ui::gfx::drawTextArgs(vg, fx, 125.f, 30.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", title.c_str());
    const auto good = nvgRGB(0x4C, 0xAF, 0x50);
    if (m_charger && m_step != wizard::DrainStep::CycleCounted) {
        // on the charger the page is about charging, whatever the cycle step is
        ui::gfx::drawTextArgs(vg, fx, 165.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            m_step == wizard::DrainStep::Charging ? good : text, "%s %d%% / 100%%", "Charging:"_i18n.c_str(), m_pct);
    } else if (m_step == wizard::DrainStep::CycleCounted) {
        ui::gfx::drawTextArgs(vg, fx, 165.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, good, "%s", "Both cycles done"_i18n.c_str());
    } else {
        ui::gfx::drawTextArgs(vg, fx, 165.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s %s", "Draining for"_i18n.c_str(), HoursMinutesSeconds(DrainSeconds()).c_str());
    }
    const bool draining = !m_charger && (m_step == wizard::DrainStep::Draining || m_step == wizard::DrainStep::NotEmptyYet);
    if (const auto zero = m_state.zero_at[m_cycle - 1]; draining && zero) {
        ui::gfx::drawTextArgs(vg, fx, 200.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, warn, "%s %s", "Running after 0% for"_i18n.c_str(), HoursMinutesSeconds(Since(zero)).c_str());
    } else if (draining && m_tte_s) {
        ui::gfx::drawTextArgs(vg, fx, 200.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s %s", "Switch-off in about"_i18n.c_str(), HoursMinutes(m_tte_s).c_str());
    }
    // the energy: what the battery holds now / when full, and what went out or in during this visit
    if (m_cap_full_mah > 0) {
        const float full_wh = m_cap_full_mah * NOMINAL_V / 1000.f;
        const float rem_wh = std::max(0, m_cap_rem_mah) * NOMINAL_V / 1000.f;
        char e[128];
        std::snprintf(e, sizeof(e), m_charger ? "%.1f / %.1f Wh · %d mAh · +%.2f Wh" : "%.1f / %.1f Wh · %d mAh · -%.2f Wh",
            rem_wh, full_wh, m_cap_full_mah, m_charger ? m_wh_in : m_wh_out);
        ui::gfx::drawTextArgs(vg, fx, 237.f, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s %s", "Energy:"_i18n.c_str(), e);
    }
    const bool attention = m_hot || m_docked || m_step == wizard::DrainStep::NotEmptyYet;
    calib_ui::DrawWrapped(vg, fx, 254.f, 17.f, 560.f, attention ? warn : info, m_status);

    // the live charts, 2 x 2
    const float cx = 120.f, cy = 332.f, cw = 510.f, ch = 138.f, gap = 20.f;
    char v[64];
    std::snprintf(v, sizeof(v), "%.0f%% · %.0f%% · %.0f%%", Last(m_cpu[0]), Last(m_cpu[1]), Last(m_cpu[2]));
    Chart(vg, cx, cy, cw, ch, "CPU"_i18n, v,
          {{&m_cpu[0], nvgRGB(0x2D, 0xD4, 0xBF)}, {&m_cpu[1], nvgRGB(0x60, 0xA5, 0xFA)}, {&m_cpu[2], nvgRGB(0xA7, 0x8B, 0xFA)}},
          0.f, 100.f, text, info);
    if (m_gpu_fd_ok) {
        std::snprintf(v, sizeof(v), "%.0f%%", Last(m_gpu));
        Chart(vg, cx + cw + gap, cy, cw, ch, "GPU"_i18n, v, {{&m_gpu, nvgRGB(0x4A, 0xDE, 0x80)}}, 0.f, 100.f, text, info);
    }
    float peak = 5.f;
    for (const auto w : m_watts) {
        peak = std::max(peak, w * 1.25f);
    }
    std::snprintf(v, sizeof(v), "%.1f W · %d mA", Last(m_watts), std::abs(m_ma));
    Chart(vg, cx, cy + ch + gap, cw, ch, "Power"_i18n, v, {{&m_watts, nvgRGB(0xF5, 0x9E, 0x0B)}}, 0.f, peak, text, info);
    std::snprintf(v, sizeof(v), "%.1f °C", Last(m_temps));
    Chart(vg, cx + cw + gap, cy + ch + gap, cw, ch, "Temperature"_i18n, v, {{&m_temps, nvgRGB(0xF8, 0x71, 0x71)}},
          20.f, 70.f, text, info, static_cast<float>(HOT_C));
}

auto DrainMenu::SaverReadout() const -> ui::SaverInfo {
    ui::SaverInfo info;
    if (m_calib) {
        // as in the guide: the clock, the stage, the battery with an approximate percent, and five columns
        info.battery_view = true;
        info.battery_percent = m_pct;
        info.status = CalibTitle();
        info.hint = "Right stick: brightness; left stick: move. Any button exits."_i18n;
        char v[32], i[32], t[32];
        std::snprintf(v, sizeof(v), "%.2f V", m_mv / 1000.0);
        std::snprintf(i, sizeof(i), "%d mA", std::abs(m_ma));
        std::snprintf(t, sizeof(t), "%.1f \u00B0C", m_temp);
        const auto load = m_calib->charge || m_charger || !g_load ? (m_calib->tail && !m_charger ? "Minimum"_i18n : "Off"_i18n) : "Maximum"_i18n;
        if (m_calib->charge) {
            info.columns = {{"Charge"_i18n, std::to_string(std::max(0, m_pct)) + " %"}, {"Charging current"_i18n, i}, {"Voltage"_i18n, v},
                            {"Temperature"_i18n, t}, {"Charging for"_i18n, HoursMinutes(DrainSeconds())}};
        } else {
            info.columns = {{"Voltage"_i18n, v}, {"Consumption"_i18n, i}, {"Load"_i18n, load}, {"Temperature"_i18n, t},
                            {"Elapsed"_i18n, HoursMinutes(DrainSeconds())}};
        }
        return info;
    }
    info.status = m_cycle == 1 ? "First cycle"_i18n : "Second cycle"_i18n;
    info.ratio = m_pct >= 0 ? m_pct / 100.0 : 0.0;
    char line[160];
    std::snprintf(line, sizeof(line), "%.1f W · %d mA · %.1f °C", Last(m_watts), std::abs(m_ma), m_temp);
    info.file = line;
    std::string stats;
    if (m_cap_full_mah > 0) {
        char e[64];
        std::snprintf(e, sizeof(e), "%.1f / %.1f Wh", std::max(0, m_cap_rem_mah) * NOMINAL_V / 1000.f, m_cap_full_mah * NOMINAL_V / 1000.f);
        stats = e;
    }
    if (m_charger) {
        stats += (stats.empty() ? "" : "   ·   ") + "Charging:"_i18n + " " + std::to_string(std::max(0, m_pct)) + "%";
    } else {
        stats += (stats.empty() ? "" : "   ·   ") + HoursMinutesSeconds(DrainSeconds());
        if (m_tte_s) {
            stats += "   ·   " + "Switch-off in about"_i18n + " " + HoursMinutes(m_tte_s);
        }
    }
    info.stats_text = stats;
    return info;
}

} // namespace sphaira::battery
