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
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace sphaira::battery {
namespace {

constexpr double HOT_C = 60.0;  // user, 2026-10-15: no load above 60 °C
constexpr double COOL_C = 55.0;
constexpr int LOAD_CORES = 3;
// few draws, each filling most of the screen: the GPU fill rate is the load. 700 small circles a frame
// overflowed deko3d's per-frame command buffer (crash in dkCmdBufBindTextures on .141).
constexpr int CIRCLES = 40;

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
        }
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
            lblExit();
        }
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

    void Draw(NVGcontext* vg, Theme* theme) override {
        if (g_load) {
            // the GPU part of the load: a frame full of gradient circles
            m_t += 0.02f;
            for (int i = 0; i < CIRCLES; i++) {
                const float a = m_t + i * 0.37f;
                const float x = SCREEN_WIDTH / 2.f + std::cos(a * 1.3f) * 300.f;
                const float y = SCREEN_HEIGHT / 2.f + std::sin(a * 0.9f) * 200.f;
                const auto paint = nvgRadialGradient(vg, x, y, 10.f, 900.f, nvgRGBA(40 + (i * 5) % 200, 120, 200, 12), nvgRGBA(0, 0, 0, 0));
                nvgBeginPath(vg);
                nvgCircle(vg, x, y, 900.f);
                nvgFillPaint(vg, paint);
                nvgFill(vg);
            }
        }
        MenuBase::Draw(vg, theme);
        const float cx = SCREEN_WIDTH / 2.f;
        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const auto title = (m_cycle == 1 ? "First cycle"_i18n : "Second cycle"_i18n);
        ui::gfx::drawTextArgs(vg, cx, 170.f, 30.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, info, "%s", title.c_str());
        ui::gfx::drawTextArgs(vg, cx, 280.f, 120.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, text, "%d%%", m_pct);
        ui::gfx::drawTextArgs(vg, cx, 380.f, 26.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, info, "%d mV · %d mA · %.1f °C", m_mv, m_ma, m_temp);
        ui::gfx::drawTextArgs(vg, cx, 450.f, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, m_hot || m_charger || m_docked ? nvgRGB(0xF5, 0x9E, 0x0B) : text, "%s", m_status.c_str());
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
