// Tools → Battery → Calibrate, steps 3 and 4: drains the battery under load until the console switches off.
// Brightness to the maximum, no dimming or sleep, three CPU cores busy, the GPU drawing hundreds of gradient
// circles a frame. The load pauses while the battery is above HOT_C (until COOL_C), while the charger is in
// and while docked. An Ultrahand boot notice tells the user what to do after the switch-off.
#include "battery_page.hpp"
#include "fuel_gauge.hpp"
#include "account/account_restore.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <switch.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <initializer_list>
#include <memory>
#include <string>

namespace sphaira::battery {
namespace {

constexpr double HOT_C = 60.0;  // user, 2026-10-15: no load above 60 °C
constexpr double COOL_C = 55.0;
constexpr int LOAD_CORES = 3;
// few draws, each filling most of the screen: the GPU fill rate is the load. 700 small circles a frame
// overflowed deko3d's per-frame command buffer (crash in dkCmdBufBindTextures on .141).
constexpr int CIRCLES = 80;
constexpr u64 LIVE_MS = 250;   // one point of the live charts
constexpr size_t LIVE_N = 120; // 30 s on a chart

// nvgpu NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD on /dev/nvhost-ctrl-gpu: the GPU load in 1/1000 (as Status Monitor reads it)
constexpr u32 NVGPU_GET_GPU_LOAD = 0x80044715;

std::atomic<bool> g_load{false};
std::atomic<bool> g_stop{false};
// idle ticks of each load core, read on that core (svcGetInfo IdleTickCount only answers for the caller's core)
std::atomic<u64> g_idle[LOAD_CORES]{};

void Burn(void* arg) {
    const auto core = reinterpret_cast<uintptr_t>(arg);
    volatile double x = 1.0001;
    while (!g_stop) {
        u64 idle{};
        if (R_SUCCEEDED(svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, core))) {
            g_idle[core] = idle;
        }
        if (!g_load) {
            svcSleepThread(50'000'000ULL);
            continue;
        }
        for (int i = 0; i < 200000; i++) {
            x = std::sqrt(x * 1.0000001 + 0.5);
        }
    }
}

struct DrainMenu final : ui::menu::MenuBase {
    explicit DrainMenu(int cycle) : MenuBase{"Battery drain"_i18n, ui::menu::MenuFlag_None}, m_cycle{cycle} {
        SetAction(Button::B, Action{"Stop"_i18n, [this]() {
            App::Push<ui::OptionBox>("Stop draining? The cycle then needs to be finished another way."_i18n,
                "Back"_i18n, "Stop"_i18n, 0, [this](auto op) {
                    if (op && *op == 1) {
                        account_restore::ClearReopenHubHint();
                        SetPop();
                    }
                });
        }});

        App::SetAutoSleepDisabled(true);
        appletSetMediaPlaybackState(true); // keeps the screen from dimming
        if ((m_lbl = R_SUCCEEDED(lblInitialize()))) {
            lblGetCurrentBrightnessSetting(&m_saved_brightness);
            lblIsAutoBrightnessControlEnabled(&m_saved_auto);
            if (m_saved_auto) {
                lblDisableAutoBrightnessControl();
            }
            lblSetCurrentBrightnessSetting(1.0f);
            lblIsDimmingEnabled(&m_saved_dimming);
            lblDisableDimming();
        }
        appletGetIdleTimeDetectionExtension(&m_saved_idle);
        appletSetIdleTimeDetectionExtension(AppletIdleTimeDetectionExtension_ExtendedUnsafe);
        account_restore::ArmReopenHubHint("Battery calibration: put the console on the charger until 100%, then open Kefir Hub → Tools → Battery → Calibrate."_i18n, false);

        g_stop = false;
        g_load = false;
        for (int i = 0; i < LOAD_CORES; i++) {
            if (R_SUCCEEDED(threadCreate(&m_threads[i], Burn, reinterpret_cast<void*>(static_cast<uintptr_t>(i)), nullptr, 0x4000, 0x3F, i))) {
                threadStart(&m_threads[i]);
                m_started[i] = true;
            }
        }
        if (R_SUCCEEDED(nvInitialize())) {
            m_nv = true;
            m_gpu_fd_ok = R_SUCCEEDED(nvOpen(&m_gpu_fd, "/dev/nvhost-ctrl-gpu"));
        }
        log_write("[Battery] drain started (cycle %d, gpu load %s)\n", m_cycle, m_gpu_fd_ok ? "readable" : "not readable");
    }

    ~DrainMenu() override {
        g_stop = true;
        for (int i = 0; i < LOAD_CORES; i++) {
            if (m_started[i]) {
                threadWaitForExit(&m_threads[i]);
                threadClose(&m_threads[i]);
            }
        }
        if (m_gpu_fd_ok) {
            nvClose(m_gpu_fd);
        }
        if (m_nv) {
            nvExit();
        }
        if (m_lbl) {
            lblSetCurrentBrightnessSetting(m_saved_brightness);
            if (m_saved_auto) {
                lblEnableAutoBrightnessControl();
            }
            if (m_saved_dimming) {
                lblEnableDimming();
            }
            lblExit();
        }
        appletSetIdleTimeDetectionExtension(m_saved_idle);
        appletSetMediaPlaybackState(false);
        App::SetAutoSleepDisabled(false);
        log_write("[Battery] drain stopped\n");
    }

    auto GetShortTitle() const -> const char* override { return "Battery drain"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_live.GetMs() >= LIVE_MS) {
            m_live.Update();
            SampleLive();
        }
        if (m_poll.GetSeconds() < 1 && m_read) {
            return;
        }
        m_poll.Update();
        m_read = true;
        FuelGauge g;
        if (const auto v = g.Read(MAX17050_RepSOC)) m_pct = *v >> 8;
        if (const auto v = g.Read(MAX17050_Temp)) m_temp = static_cast<s16>(*v) / 256.0;
        if (const auto v = g.Read(MAX17050_TTE)) m_tte_s = *v == 0xFFFF ? 0 : static_cast<u64>(*v * 5.625);
        if (m_lbl && m_reapply.GetSeconds() >= 10) {
            // the system can switch dimming back on; the screen must stay at full brightness
            m_reapply.Update();
            lblDisableDimming();
            lblSetCurrentBrightnessSetting(1.0f);
        }
        PsmChargerType charger{};
        psmGetChargerType(&charger);
        m_charger = charger != PsmChargerType_Unconnected;
        m_docked = appletGetOperationMode() == AppletOperationMode_Console;
        if (m_temp >= HOT_C) {
            m_hot = true;
        } else if (m_temp <= COOL_C) {
            m_hot = false;
        }
        const bool load = !m_charger && !m_docked && !m_hot;
        if (load != g_load) {
            log_write("[Battery] drain load %s (%d%%, %.1f C, charger %d, docked %d)\n", load ? "on" : "off", m_pct, m_temp, m_charger, m_docked);
        }
        g_load = load;
        m_status = m_charger ? "Paused: unplug the charger"_i18n
            : m_docked ? "Paused: take the console out of the dock"_i18n
            : m_hot ? "Paused: the battery is hot, waiting until it cools to 55 °C"_i18n
            : "Kefir Hub loads the CPU and GPU to drain the battery. The console switches itself off; then charge it to 100%."_i18n;
    }

    static void Push(std::deque<float>& d, float v) {
        d.push_back(v);
        if (d.size() > LIVE_N) {
            d.pop_front();
        }
    }

    // CPU load from each load core's idle ticks, GPU load from nvgpu, power and temperature from the gauge
    void SampleLive() {
        const u64 tick = armGetSystemTick();
        for (int i = 0; i < LOAD_CORES; i++) {
            const u64 idle = g_idle[i];
            if (m_tick && tick > m_tick) {
                Push(m_cpu[i], 100.f - std::min(100.f, (idle - m_idle[i]) * 100.f / (tick - m_tick)));
            }
            m_idle[i] = idle;
        }
        m_tick = tick;
        u32 gpu{};
        if (m_gpu_fd_ok && R_SUCCEEDED(nvIoctl(m_gpu_fd, NVGPU_GET_GPU_LOAD, &gpu)) && gpu <= 1000) {
            Push(m_gpu, gpu / 10.f);
        }
        FuelGauge g;
        const auto mv = g.Read(MAX17050_VCell);
        const auto ma = g.Current(MAX17050_Current);
        if (mv && ma) {
            m_mv = static_cast<int>((*mv >> 3) * 0.625);
            m_ma = static_cast<int>(*ma);
            Push(m_watts, m_mv / 1000.f * std::abs(m_ma) / 1000.f);
        }
        if (const auto v = g.Read(MAX17050_Temp)) {
            Push(m_temps, static_cast<s16>(*v) / 256.f);
        }
    }

    struct Line {
        const std::deque<float>* d;
        NVGcolor c;
    };

    // A live chart that scrolls smoothly: the newest point enters on the right, LIVE_MS apart.
    void Chart(NVGcontext* vg, float x, float y, float w, float h, const std::string& title, const std::string& value,
               std::initializer_list<Line> lines, float lo, float hi, NVGcolor text, NVGcolor info, float mark = -1.f) const {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, 12.f);
        nvgFillColor(vg, nvgTransRGBAf(info, 0.08f));
        nvgFill(vg);
        ui::gfx::drawTextArgs(vg, x + 16.f, y + 22.f, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", title.c_str());
        ui::gfx::drawTextArgs(vg, x + w - 16.f, y + 22.f, 24.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, text, "%s", value.c_str());

        const float px = x + 12.f, py = y + 44.f, pw = w - 24.f, ph = h - 56.f;
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

    static auto Last(const std::deque<float>& d) -> float {
        return d.empty() ? 0.f : d.back();
    }

    static auto HoursMinutes(u64 sec) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lu:%02lu", sec / 3600, (sec / 60) % 60);
        return buf;
    }

    static auto HoursMinutesSeconds(u64 sec) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", sec / 3600, (sec / 60) % 60, sec % 60);
        return buf;
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
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
        MenuBase::Draw(vg, theme); // after the load: the page background covers the circles, the GPU still draws them

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
        const auto title = (m_cycle == 1 ? "First cycle"_i18n : "Second cycle"_i18n);
        ui::gfx::drawTextArgs(vg, fx, 125.f, 30.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", title.c_str());
        ui::gfx::drawTextArgs(vg, fx, 165.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s %s", "Draining for"_i18n.c_str(), HoursMinutesSeconds(m_since.GetSeconds()).c_str());
        if (m_tte_s) {
            ui::gfx::drawTextArgs(vg, fx, 200.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s %s", "Switch-off in about"_i18n.c_str(), HoursMinutes(m_tte_s).c_str());
        }
        ui::gfx::drawTextBox(vg, fx, 222.f, 20.f, 600.f, m_hot || m_charger || m_docked ? warn : info, m_status.c_str());

        // the live charts, 2 x 2
        const float cx = 120.f, cy = 290.f, cw = 510.f, ch = 152.f, gap = 20.f;
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

private:
    int m_cycle;
    Thread m_threads[LOAD_CORES]{};
    bool m_started[LOAD_CORES]{};
    bool m_lbl{};
    float m_saved_brightness{};
    bool m_saved_auto{};
    TimeStamp m_poll{};
    bool m_read{};
    int m_pct{};
    int m_mv{};
    int m_ma{};
    double m_temp{};
    bool m_charger{};
    bool m_docked{};
    bool m_hot{};
    float m_t{};
    std::string m_status;
    bool m_saved_dimming{};
    AppletIdleTimeDetectionExtension m_saved_idle{};
    TimeStamp m_reapply{};
    TimeStamp m_since{};
    u64 m_tte_s{};
    bool m_nv{};
    bool m_gpu_fd_ok{};
    u32 m_gpu_fd{};
    TimeStamp m_live{};
    std::deque<float> m_cpu[LOAD_CORES];
    std::deque<float> m_gpu;
    std::deque<float> m_watts;
    std::deque<float> m_temps;
    u64 m_idle[LOAD_CORES]{};
    u64 m_tick{};
};

} // namespace

void OpenDrain(int cycle) {
    App::Push<ui::OptionBox>(
        "Drain the battery now? Unplug the charger and keep the console in your hands. Kefir Hub sets the screen to full brightness and loads the CPU and GPU until the console switches itself off; the load pauses above 60 °C. Afterwards charge it to 100% and open Kefir Hub."_i18n,
        "Back"_i18n, "Start"_i18n, 0, [cycle](auto op) {
            if (op && *op == 1) {
                App::Push<DrainMenu>(cycle);
            }
        });
}

} // namespace sphaira::battery
