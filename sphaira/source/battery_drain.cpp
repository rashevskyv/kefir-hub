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
#include <memory>
#include <string>
#include <vector>

namespace sphaira::battery {
namespace {

constexpr double HOT_C = 60.0;  // user, 2026-10-15: no load above 60 °C
constexpr double COOL_C = 55.0;
constexpr int LOAD_CORES = 3;
// few draws, each filling most of the screen: the GPU fill rate is the load. 700 small circles a frame
// overflowed deko3d's per-frame command buffer (crash in dkCmdBufBindTextures on .141).
constexpr int CIRCLES = 80;
constexpr u64 SAMPLE_S = 20;   // one point of the graph
constexpr size_t SAMPLES = 180; // one hour on the graph

std::atomic<bool> g_load{false};
std::atomic<bool> g_stop{false};

void Burn(void*) {
    volatile double x = 1.0001;
    while (!g_stop) {
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
            if (R_SUCCEEDED(threadCreate(&m_threads[i], Burn, nullptr, nullptr, 0x4000, 0x3F, i))) {
                threadStart(&m_threads[i]);
                m_started[i] = true;
            }
        }
        log_write("[Battery] drain started (cycle %d)\n", m_cycle);
    }

    ~DrainMenu() override {
        g_stop = true;
        for (int i = 0; i < LOAD_CORES; i++) {
            if (m_started[i]) {
                threadWaitForExit(&m_threads[i]);
                threadClose(&m_threads[i]);
            }
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
        if (m_poll.GetSeconds() < 1 && m_read) {
            return;
        }
        m_poll.Update();
        m_read = true;
        FuelGauge g;
        if (const auto v = g.Read(MAX17050_RepSOC)) m_pct = *v >> 8;
        if (const auto v = g.Read(MAX17050_VCell)) m_mv = static_cast<int>((*v >> 3) * 0.625);
        if (const auto v = g.Current(MAX17050_AvgCurrent)) m_ma = static_cast<int>(*v);
        if (const auto v = g.Read(MAX17050_Temp)) m_temp = static_cast<s16>(*v) / 256.0;
        if (const auto v = g.Read(MAX17050_TTE)) m_tte_s = *v == 0xFFFF ? 0 : static_cast<u64>(*v * 5.625);
        if (m_hist.empty() || m_sample.GetSeconds() >= SAMPLE_S) {
            m_sample.Update();
            m_hist.push_back(m_pct);
            if (m_hist.size() > SAMPLES) {
                m_hist.erase(m_hist.begin());
            }
        }
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
            : "Draining. The console will switch itself off; then charge it to 100% and open Kefir Hub."_i18n;
    }

    static auto HoursMinutes(u64 sec) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lu:%02lu", sec / 3600, (sec / 60) % 60);
        return buf;
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme); // first: the load circles go over the page background, where they are seen
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

        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const auto warn = nvgRGB(0xF5, 0x9E, 0x0B);
        const float pct = std::clamp(m_pct, 0, 100) / 100.f;
        // green at full, orange in the middle, red near empty; it breathes while the load runs
        const auto level = pct > 0.5f ? nvgLerpRGBA(nvgRGB(0xF5, 0x9E, 0x0B), nvgRGB(0x4C, 0xAF, 0x50), (pct - 0.5f) * 2.f)
                                      : nvgLerpRGBA(nvgRGB(0xE5, 0x39, 0x35), nvgRGB(0xF5, 0x9E, 0x0B), pct * 2.f);
        const float pulse = g_load ? 0.75f + 0.25f * std::sin(m_t * 4.f) : 1.f;

        // the battery
        const float bx = 120.f, by = 150.f, bw = 360.f, bh = 170.f;
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

        // the figures
        const float fx = 560.f;
        const auto title = (m_cycle == 1 ? "First cycle"_i18n : "Second cycle"_i18n);
        ui::gfx::drawTextArgs(vg, fx, 160.f, 30.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s", title.c_str());
        ui::gfx::drawTextArgs(vg, fx, 205.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s %s", "Draining for"_i18n.c_str(), HoursMinutes(m_since.GetSeconds()).c_str());
        if (m_tte_s) {
            ui::gfx::drawTextArgs(vg, fx, 245.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s %s", "Switch-off in about"_i18n.c_str(), HoursMinutes(m_tte_s).c_str());
        }
        ui::gfx::drawTextArgs(vg, fx, 285.f, 24.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%d mV · %d mA · %.1f °C", m_mv, m_ma, m_temp);

        // the graph: the charge since the start, one point every SAMPLE_S seconds, an hour wide
        const float gx = 120.f, gy = 380.f, gw = 1040.f, gh = 180.f;
        nvgBeginPath(vg);
        nvgRect(vg, gx, gy, gw, gh);
        nvgStrokeColor(vg, nvgTransRGBAf(info, 0.4f));
        nvgStrokeWidth(vg, 1.f);
        nvgStroke(vg);
        for (int l = 1; l < 4; l++) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, gx, gy + gh * l / 4.f);
            nvgLineTo(vg, gx + gw, gy + gh * l / 4.f);
            nvgStroke(vg);
        }
        if (m_hist.size() > 1) {
            nvgBeginPath(vg);
            for (size_t i = 0; i < m_hist.size(); i++) {
                const float x = gx + gw * i / static_cast<float>(SAMPLES - 1);
                const float y = gy + gh * (1.f - std::clamp(m_hist[i], 0, 100) / 100.f);
                i ? nvgLineTo(vg, x, y) : nvgMoveTo(vg, x, y);
            }
            nvgStrokeColor(vg, level);
            nvgStrokeWidth(vg, 4.f);
            nvgStroke(vg);
        }
        ui::gfx::drawTextArgs(vg, gx - 10.f, gy, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info, "100%%");
        ui::gfx::drawTextArgs(vg, gx - 10.f, gy + gh, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info, "0%%");

        ui::gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, 600.f, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            m_hot || m_charger || m_docked ? warn : text, "%s", m_status.c_str());
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
    TimeStamp m_sample{};
    TimeStamp m_since{};
    u64 m_tte_s{};
    std::vector<int> m_hist;
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
