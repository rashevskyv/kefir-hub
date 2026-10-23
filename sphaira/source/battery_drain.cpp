// Tools → Battery → Calibrate, steps 3 and 4: drains the battery under load until the console switches off.
// Brightness to the maximum, no dimming or sleep, three CPU cores busy, the GPU drawing hundreds of gradient
// circles a frame. The load pauses while the battery is above HOT_C (until COOL_C), while the charger is in
// and while docked. An Ultrahand boot notice tells the user what to do after the switch-off.
#include "battery_page.hpp"
#include "battery_calib_log.hpp"
#include "fuel_gauge.hpp"
#include "battery_internal.hpp"
#include "account/account_restore.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "app.hpp"
#include "ui/screensaver.hpp"
#include "ui/screensaver_timeout.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <switch.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>

namespace sphaira::battery {
namespace {

constexpr double HOT_C = 60.0;  // user, 2026-10-15: no load above 60 °C
constexpr double COOL_C = 55.0;
constexpr int LOAD_CORES = 3;
// 59 is the lowest time-sliced priority band of Horizon: a busy thread there shares the core. At 0x2C (cooperative) it
// never yields and hangs the Hub; at 0x3F it got no time at all.
constexpr int LOAD_PRIO = 59;
// few draws, each filling most of the screen: the GPU fill rate is the load. 700 small circles a frame
// overflowed deko3d's per-frame command buffer (crash in dkCmdBufBindTextures on .141).
constexpr int CIRCLES = 80;
// below this shown charge the load goes off and only the bright screen drains the battery: on a worn battery the
// voltage under load collapses from about 30 % and the console cuts off before the chip sees a real empty
constexpr int GENTLE_PCT = 35;
constexpr float MIN_BRIGHTNESS = 0.02f; // the lowest the screen still shows something
constexpr float TAIL_BRIGHTNESS = 0.01f; // the calibration after a cut-off: as low as the panel goes
constexpr const char* CALIB_LOG = "/config/kefir/battery/calib-log.csv";
constexpr size_t CALIB_LOG_MAX = 4u * 1024 * 1024; // a longer log is kept as .old and a new one is started
constexpr float NOMINAL_V = 3.7f; // for Wh from mAh
constexpr long SAVER_DEFAULT_S = 120; // idle seconds before the screensaver when the install screen's timeout is off
constexpr double CHARGING_MA = 100.0; // an average current above this into the battery = on a charger
constexpr u64 LIVE_MS = 250;   // one point of the live charts
constexpr size_t LIVE_N = 120; // 30 s on a chart

// nvgpu NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD on /dev/nvhost-ctrl-gpu: the GPU load in 1/1000 (as Status Monitor reads it)
constexpr u32 NVGPU_GET_GPU_LOAD = 0x80044715;

std::atomic<bool> g_load{false};
std::atomic<bool> g_stop{false};
// ticks each load thread spent burning: busy ticks / elapsed ticks is the load the Hub puts on that core.
// (svcGetInfo IdleTickCount answers 0 here, which drew every core at 100 % even on the charger.)
std::atomic<u64> g_busy[LOAD_CORES]{};

void Burn(void* arg) {
    const auto core = reinterpret_cast<uintptr_t>(arg);
    // pinned: the idle-tick call answers only for the core the caller runs on, and a sleeping thread
    // that wakes on another core made it fail and left the chart at 100 %
    const Result mask_rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, static_cast<s32>(core), 1u << core);
    log_write("[Battery] burn thread %zu started, core mask rc 0x%X\n", static_cast<size_t>(core), mask_rc);
    bool first_chunk = true;
    volatile double x = 1.0001;
    while (!g_stop) {
        if (!g_load) {
            svcSleepThread(50'000'000ULL);
            continue;
        }
        const u64 t0 = armGetSystemTick();
        for (int i = 0; i < 200000; i++) {
            x = std::sqrt(x * 1.0000001 + 0.5);
        }
        const u64 spent = armGetSystemTick() - t0;
        g_busy[core] += spent;
        if (first_chunk) {
            first_chunk = false;
            log_write("[Battery] burn thread %zu first chunk took %llu ticks\n", static_cast<size_t>(core), (unsigned long long)spent);
        }
    }
}

constexpr const char* BRIGHTNESS_FILE = "/config/kefir/battery/brightness.txt";

struct SavedBrightness {
    float brightness{};
    bool auto_on{};
};

auto ReadSavedBrightness() -> std::optional<SavedBrightness> {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd{}.read_entire_file(BRIGHTNESS_FILE, data)) || data.empty()) {
        return std::nullopt;
    }
    const std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    float b{};
    int a{};
    if (std::sscanf(text.c_str(), "brightness=%f\nauto=%d", &b, &a) != 2 || b < 0.f || b > 1.f) {
        return std::nullopt;
    }
    return SavedBrightness{b, a != 0};
}

void WriteSavedBrightness(float brightness, bool auto_on) {
    char buf[64];
    const auto n = std::snprintf(buf, sizeof(buf), "brightness=%.4f\nauto=%d\n", brightness, auto_on ? 1 : 0);
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively("/config/kefir/battery");
    sd.write_entire_file(BRIGHTNESS_FILE, std::vector<u8>(buf, buf + n));
}

void DeleteSavedBrightness() {
    fs::FsNativeSd{}.DeleteFile(BRIGHTNESS_FILE);
}

struct DrainMenu final : ui::menu::MenuBase {
    explicit DrainMenu(int cycle, std::optional<CalibRun> calib = std::nullopt)
        : MenuBase{"Battery drain"_i18n, ui::menu::MenuFlag_None}, m_cycle{cycle}, m_calib{std::move(calib)} {
        SetAction(Button::B, Action{"Stop"_i18n, [this]() {
            App::Push<ui::OptionBox>(m_calib
                ? (m_calib->charge ? "Leave the charge page? The calibration remembers where it is."_i18n
                                   : "Stop the drain? The calibration remembers where it is."_i18n)
                : "Stop draining? The cycle then needs to be finished another way."_i18n,
                "Back"_i18n, "Stop"_i18n, 0, [this](auto op) {
                    if (op && *op == 1) {
                        account_restore::ClearReopenHubHint();
                        SetAutoDrainSuppressed(true); // the user stopped it: the wizard must not restart it now
                        SetPop();
                    }
                });
        }});

        App::SetAutoSleepDisabled(true);
        appletSetMediaPlaybackState(true); // keeps the screen from dimming
        if ((m_lbl = R_SUCCEEDED(lblInitialize()))) {
            lblGetCurrentBrightnessSetting(&m_saved_brightness);
            lblIsAutoBrightnessControlEnabled(&m_saved_auto);
            // a file from an earlier drain (the Hub was left, or died, before it could restore) holds the real values;
            // what the screen shows now is the drain's own full brightness
            if (const auto kept = ReadSavedBrightness()) {
                m_saved_brightness = kept->brightness;
                m_saved_auto = kept->auto_on;
            } else {
                WriteSavedBrightness(m_saved_brightness, m_saved_auto);
            }
            // automatic brightness off and the brightness at the maximum on every model: the drain goes faster.
            // The calibration's charge leaves the user's brightness alone (the screen only stays on); its drain after a
            // cut-off goes to the minimum (the tail).
            if (!(m_calib && m_calib->charge)) {
                if (m_saved_auto) {
                    lblDisableAutoBrightnessControl();
                }
                lblSetCurrentBrightnessSetting(m_calib && m_calib->tail ? TAIL_BRIGHTNESS : 1.0f);
            }
            m_dimmed = m_calib && m_calib->tail;
            lblIsDimmingEnabled(&m_saved_dimming);
            lblDisableDimming();
        }
        appletGetIdleTimeDetectionExtension(&m_saved_idle);
        appletSetIdleTimeDetectionExtension(AppletIdleTimeDetectionExtension_ExtendedUnsafe);
        m_state = detail::LoadState();
        PsmChargerType charger_now{};
        psmGetChargerType(&charger_now);
        m_charger = charger_now != PsmChargerType_Unconnected;
        if (m_calib) {
            OpenCalibLog();
            account_restore::ArmReopenHubHint("Battery calibration: when the console switches off, switch it on again and open Kefir Hub → Tools → Battery calibration."_i18n, false);
        } else {
            NoteState(true);
            account_restore::ArmReopenHubHint("Battery calibration: put the console on the charger until 100%, then open Kefir Hub → Tools → Battery → Calibrate."_i18n, false);
        }

        g_stop = false;
        g_load = false;
        for (int i = 0; i < LOAD_CORES; i++) {
            const Result create_rc = threadCreate(&m_threads[i], Burn, reinterpret_cast<void*>(static_cast<uintptr_t>(i)), nullptr, 0x4000, LOAD_PRIO, i);
            if (R_SUCCEEDED(create_rc)) {
                const Result start_rc = threadStart(&m_threads[i]);
                log_write("[Battery] burn thread %d created, start rc 0x%X\n", i, start_rc);
                m_started[i] = true;
            } else {
                log_write("[Battery] burn thread %d NOT created: 0x%X\n", i, create_rc);
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
            DeleteSavedBrightness();
        }
        appletSetIdleTimeDetectionExtension(m_saved_idle);
        appletSetMediaPlaybackState(false);
        App::SetAutoSleepDisabled(false);
        CloseCalibLog();
        log_write("[Battery] drain stopped\n");
    }

    auto GetShortTitle() const -> const char* override { return "Battery drain"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        // The panel shows the same still charts for hours at full brightness: after the idle time (the one set for the install
        // screen, two minutes when that is off) the screensaver takes over with the numbers that matter. A press wakes it and
        // does nothing else; the measuring below goes on behind it.
        const double now_sec = m_idle_clock.GetSecondsD();
        if (m_saver.IsActive()) {
            m_saver.Update(controller, touch);
            if (m_saver.WantsWake(controller, touch)) {
                m_saver.Stop();
                App::SetAutoSleepDisabled(true); // the saver's Stop lets the console sleep again: a sleeping console stops the drain
                m_idle.Reset(now_sec);
                m_reapply = {};
                m_reapply_due = true; // the brightness the saver restored is put back to the drain's own
                return;
            }
        } else {
            MenuBase::Update(controller, touch);
            ui::TimeoutInput in{};
            if (controller) {
                in.kdown = controller->m_kdown;
                in.kheld = controller->m_kheld;
                in.stick_l_x = controller->m_stick_l.x;
                in.stick_l_y = controller->m_stick_l.y;
                in.stick_r_x = controller->m_stick_r.x;
                in.stick_r_y = controller->m_stick_r.y;
            }
            if (touch) {
                in.is_touching = touch->is_touching;
                in.is_clicked = touch->is_clicked;
            }
            const long timeout = App::GetBlankTimeout() > 0 ? App::GetBlankTimeout() : SAVER_DEFAULT_S;
            if (m_idle.Update(ui::BlankPhase_Installing, false, timeout, in, now_sec)) {
                m_saver.Start();
                m_idle.Reset(now_sec);
            }
        }
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
        u32 shown{};
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&shown))) m_pct = static_cast<int>(shown); // what the console shows
        if (const auto v = g.Read(MAX17050_RepSOC)) m_gauge_pct = *v >> 8;
        // 0.5 mAh per step (10 mOhm sense resistor, as the Battery page reads it)
        if (const auto v = g.Read(MAX17050_RepCap)) m_cap_rem_mah = static_cast<int>(*v) / 2;
        if (const auto v = g.Read(MAX17050_FullCap)) m_cap_full_mah = static_cast<int>(*v) / 2;
        if (!m_calib) {
            NoteState(false);
        }
        // one line a minute: if the console cuts off early (HOS shuts down on voltage while the % is still high),
        // the log shows what the gauge and the voltage were doing
        if (m_trace.GetSeconds() >= 60) {
            m_trace.Update();
            log_write("[Battery] trace psm %d%% gauge %d%% %d mV %d mA %.1f C step %d\n", m_pct, m_gauge_pct, m_mv, m_ma, m_temp, static_cast<int>(m_step));
        }
        if (const auto v = g.Read(MAX17050_Temp)) m_temp = static_cast<s16>(*v) / 256.0;
        if (const auto v = g.Read(MAX17050_TTE)) m_tte_s = *v == 0xFFFF ? 0 : static_cast<u64>(*v * 5.625);
        if (m_lbl && !m_saver.IsActive() && (m_reapply.GetSeconds() >= 10 || m_reapply_due)) {
            m_reapply_due = false;
            // the system can switch dimming back on; the screen must stay at full brightness
            m_reapply.Update();
            lblDisableDimming();
            if (m_calib) {
                m_calib_mode = -1; // the calibration puts its own value back (ApplyCalibScreen)
            } else {
                lblSetCurrentBrightnessSetting(m_dimmed ? MIN_BRIGHTNESS : 1.0f);
            }
        }
        PsmChargerType charger{};
        psmGetChargerType(&charger);
        // a charging current also means a charger (psm can lag behind a cable plugged in at 0 %)
        const auto avg = g.Current(MAX17050_AvgCurrent);
        m_charger = charger != PsmChargerType_Unconnected || (avg && *avg > CHARGING_MA);
        if (!m_calib) {
            UpdateStep();
        }
        m_docked = appletGetOperationMode() == AppletOperationMode_Console;
        if (m_temp >= HOT_C) {
            m_hot = true;
        } else if (m_temp <= COOL_C) {
            m_hot = false;
        }
        // the calibration: full load until the first cut-off (no 35 % gentle mode), none in a charge or in the tail after a cut-off
        const bool load = m_calib ? (!m_charger && !m_docked && !m_hot && !m_calib->charge && !m_calib->tail)
                                  : (!m_charger && !m_docked && !m_hot && !(m_pct >= 0 && m_pct <= GENTLE_PCT));
        if (load != g_load) {
            log_write("[Battery] drain load %s (%d%%, %.1f C, charger %d, docked %d)\n", load ? "on" : "off", m_pct, m_temp, m_charger, m_docked);
        }
        g_load = load;
        // below GENTLE_PCT the screen goes to the minimum too: the lowest current that still drains the battery, so
        // the console reaches a real empty at rest-like load instead of cutting off on a voltage collapse
        if (m_calib) {
            ApplyCalibScreen();
            CalibTick();
        } else if (m_lbl) {
            const bool gentle = m_pct >= 0 && m_pct <= GENTLE_PCT && !m_charger;
            if (gentle != m_dimmed) {
                m_dimmed = gentle;
                if (!m_saver.IsActive()) { // the saver owns the brightness while it is on; the reapply puts this back after it
                    lblSetCurrentBrightnessSetting(gentle ? MIN_BRIGHTNESS : 1.0f);
                }
            }
        }
        using wizard::DrainStep;
        if (m_calib) {
            m_status = CalibStatus();
        } else if (m_step == DrainStep::CycleCounted) {
            m_status = "Both cycles are done. Press B and finish step 5 in the wizard: match in the other MMC."_i18n;
        } else if (m_step == DrainStep::Charging) {
            m_status = "The battery ran down: this cycle's discharge is done. Keep the charger in until 100%; the cycle then counts by itself."_i18n
                + " " + "Keep the screen on while it charges. After a red battery, leave it on the charger for about 3 minutes before you switch it on."_i18n;
        } else if (m_step == DrainStep::NotEmptyYet) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " %u%% / %u%%", m_cycle_got, wizard::CYCLE_DONE);
            m_status = "Charging, the drain is paused. Discharged in this cycle:"_i18n + buf + ". "
                + "Charge to 100% and unplug the charger: the drain goes on. Or unplug it now to drain further first."_i18n;
        } else {
            m_status = m_docked ? "Paused: take the console out of the dock"_i18n
                : m_hot ? "Paused: the battery is hot, waiting until it cools to 55 °C"_i18n
                : m_pct >= 0 && m_pct <= GENTLE_PCT ? "Below 35%: the load is off and the screen is dimmed, so the console runs down gently to a real empty. It switches itself off; then charge it to 100%."_i18n
            : "Kefir Hub loads the CPU and GPU to drain the battery. The console switches itself off; then charge it to 100%."_i18n;
        }
    }

    // which phase of the cycle this is; a full battery on the charger counts the cycle here, like the wizard does,
    // and the page moves on to the second cycle
    void UpdateStep() {
        using wizard::DrainStep;
        if (m_step_poll.GetSeconds() < 5 && m_step_read) {
            return;
        }
        m_step_poll.Update();
        m_step_read = true;
        const auto total = detail::CycleTotalNow();
        if (!total) {
            return;
        }
        const wizard::Live live{*total, m_charger, static_cast<std::uint32_t>(std::max(m_pct, 0)), 0, false,
                                static_cast<u64>(std::time(nullptr))};
        if (wizard::Advance(m_state, live)) {
            detail::SaveState(m_state);
            account_restore::ClearReopenHubHint();
            log_write("[Battery] cycle %d counted on the drain page (total %u)\n", m_cycle, *total);
        }
        m_cycle_got = wizard::Got(m_state, live, m_cycle);
        m_step = wizard::DrainStepOf(m_state, live, m_cycle);
        if (m_step == DrainStep::CycleCounted && m_cycle == 1) {
            m_cycle = 2;
            m_step = wizard::DrainStepOf(m_state, live, m_cycle);
        }
    }

    // ---- the calibration (docs/dev/BATTERY-CALIBRATION.md, "Brightness rule" and "Cutoff pipeline") ----

    // the screen follows the state: the user's own brightness and auto-brightness while the charger is in (the drain is
    // paused), the minimum after a cut-off, the maximum before it. A charge leaves the screen alone.
    void ApplyCalibScreen() {
        if (!m_lbl || !m_calib || m_calib->charge || m_saver.IsActive()) {
            return;
        }
        const int mode = m_charger ? 1 : m_calib->tail ? 2 : 3;
        if (mode == m_calib_mode) {
            return;
        }
        m_calib_mode = mode;
        if (mode == 1) {
            lblSetCurrentBrightnessSetting(m_saved_brightness);
            if (m_saved_auto) {
                lblEnableAutoBrightnessControl();
            }
        } else {
            if (m_saved_auto) {
                lblDisableAutoBrightnessControl();
            }
            lblSetCurrentBrightnessSetting(mode == 2 ? TAIL_BRIGHTNESS : 1.0f);
        }
    }

    // once a second: the time on battery (or on the charger), a line in the log, and the end of a charge phase
    void CalibTick() {
        const auto now = static_cast<s64>(std::time(nullptr));
        if (m_calib_last && now > m_calib_last && now - m_calib_last <= 5) {
            m_run_s += static_cast<u64>(now - m_calib_last);
        }
        m_calib_last = now;
        if (m_log) {
            calib::Sample sm;
            sm.time = now;
            sm.kind = m_calib->charge ? 'C' : 'D';
            sm.psm_pct = m_pct;
            sm.gauge_pct = m_gauge_pct;
            sm.rem_mah = m_cap_rem_mah;
            sm.full_mah = m_cap_full_mah;
            sm.mv = m_mv;
            sm.ma = m_ma;
            sm.temp_c = m_temp;
            sm.load = g_load;
            sm.brightness = m_calib->charge ? -1 : m_calib_mode == 1 ? static_cast<int>(m_saved_brightness * 100.f + 0.5f) : m_calib_mode == 2 ? 1 : 100;
            sm.charger = m_charger;
            const auto line = calib::FormatLine(sm);
            std::fputs(line.c_str(), m_log);
            std::fflush(m_log);
            if (m_log_commit.GetSeconds() >= 10) {
                m_log_commit.Update();
                fsdevCommitDevice("sdmc"); // the last seconds before a cut-off must reach the card
            }
        }
        if (m_calib->charge) {
            if (m_charger && m_pct >= 100) {
                m_full_seen = true;
            }
            // full, and the charger came out: this phase is done, the Hub goes on to the drain by itself
            if (m_full_seen && !m_charger && m_calib->on_charge_done) {
                const auto done = std::move(m_calib->on_charge_done);
                m_calib->on_charge_done = nullptr;
                SetPop();
                done();
            }
        }
    }

    auto CalibTitle() const -> std::string {
        return m_calib->charge ? "Charge to 100 %"_i18n : m_calib->tail ? "Drain after the cut-off"_i18n : "Drain to the cut-off"_i18n;
    }

    auto CalibStatus() const -> std::string {
        if (m_calib->charge) {
            if (!m_charger) {
                return "The charger is out: the charge is paused. Plug it in to go on."_i18n;
            }
            if (m_pct >= 100) {
                return "Charged to 100 %. Now we drain: take the charger out."_i18n;
            }
            return "Charging. The screen stays on, so the console surely charges to the end. You can leave the console as it is or use it; when it reaches 100 %, come back to Kefir Hub and open Battery calibration."_i18n;
        }
        if (m_charger) {
            return "The charger is in. The drain is paused. Take the charger out to go on."_i18n;
        }
        if (m_docked) {
            return "Paused: take the console out of the dock"_i18n;
        }
        if (m_hot) {
            return "Paused: the battery is hot, waiting until it cools to 55 °C"_i18n;
        }
        return (m_calib->tail
            ? "The console has switched off already: now it drains slowly to squeeze out the last charge. Auto-brightness is off and the brightness is at the minimum."_i18n
            : "Full load: the console drains as fast as it can. When it switches off, switch it on again and open Kefir Hub."_i18n)
            + " " + "Everything is written to a log."_i18n;
    }

    auto DrainSeconds() const -> u64 {
        return m_calib ? m_run_s : m_state.drain_s[m_cycle - 1];
    }

    void OpenCalibLog() {
        fs::FsNativeSd sd;
        sd.CreateDirectoryRecursively("/config/kefir/battery");
        const std::string path = std::string{"sdmc:"} + CALIB_LOG;
        long size = -1;
        if (auto* probe = std::fopen(path.c_str(), "rb")) {
            std::fseek(probe, 0, SEEK_END);
            size = std::ftell(probe);
            std::fclose(probe);
        }
        if (size > static_cast<long>(CALIB_LOG_MAX)) {
            const auto old = path + ".old";
            std::remove(old.c_str());
            std::rename(path.c_str(), old.c_str());
            size = -1;
        }
        m_log = std::fopen(path.c_str(), "ab");
        if (m_log && size <= 0) {
            std::fputs(calib::LOG_HEADER, m_log);
        }
    }

    void CloseCalibLog() {
        if (m_log) {
            std::fclose(m_log);
            m_log = nullptr;
            fsdevCommitDevice("sdmc");
        }
    }

    // the drain start, the first 0 % and the last time seen on go to the wizard's state file, so a Hub restart
    // keeps them; alive is written every 30 s
    void NoteState(bool force) {
        const auto now = static_cast<u64>(std::time(nullptr));
        // a drain that starts from a full battery is a new drain: the time of an earlier, interrupted one (the
        // battery was charged up again meanwhile) must not be shown as this one's
        const bool fresh = !m_charger && (m_prev_charger || !m_noted_once) && m_read && m_pct >= 99 && !m_state.zero_at[m_cycle - 1];
        if (fresh && m_state.drain_s[m_cycle - 1]) {
            m_state.drain_s[m_cycle - 1] = 0;
            m_state.drain_at[m_cycle - 1] = 0;
            m_last_note = 0;
        }
        m_prev_charger = m_charger;
        m_noted_once = m_noted_once || m_read;
        const bool changed = !m_charger && wizard::NoteDrain(m_state, m_cycle, now, ZeroSeen() ? 0 : m_pct);
        // time on battery only: the seconds since the last note, when no charger was in (a gap longer than a
        // minute is a restart or a switch-off, not drain time)
        if (!m_charger && m_last_note && now > m_last_note && now - m_last_note <= 60) {
            m_state.drain_s[m_cycle - 1] += now - m_last_note;
        }
        m_last_note = now;
        if (changed || force || m_saved.GetSeconds() >= 30) {
            m_saved.Update();
            detail::SaveState(m_state);
        }
    }

    // HOS can switch off while it still shows 1 % (on .141 the gauge read 0 % with psm at 1 %): either one at 0 counts
    auto ZeroSeen() const -> bool {
        return m_read && (m_pct == 0 || m_gauge_pct == 0);
    }

    auto Since(const std::uint64_t at) const -> u64 {
        const auto now = static_cast<u64>(std::time(nullptr));
        return at && now > at ? now - at : 0;
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
            const u64 busy = g_busy[i];
            if (m_tick && tick > m_tick) {
                Push(m_cpu[i], std::min(100.f, (busy - m_busy[i]) * 100.f / (tick - m_tick)));
            }
            m_busy[i] = busy;
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
            const float w = m_mv / 1000.f * std::abs(m_ma) / 1000.f;
            Push(m_watts, w);
            (m_ma < 0 ? m_wh_out : m_wh_in) += w * (LIVE_MS / 1000.f) / 3600.f; // Wh over one sample
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
        if (m_saver.OwnsScreen()) {
            m_saver.Draw(vg, theme, SaverReadout()); // black page; the circles above keep the GPU busy underneath
            return;
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
        ui::gfx::drawTextBox(vg, fx, 254.f, 17.f, 560.f, attention ? warn : info, m_status.c_str());

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

private:
    // what the screensaver paints: the cycle, the battery bar, power and temperature, the energy, the time on battery
    auto SaverReadout() const -> ui::SaverInfo {
        ui::SaverInfo info;
        info.status = m_calib ? CalibTitle() : (m_cycle == 1 ? "First cycle"_i18n : "Second cycle"_i18n);
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

    int m_cycle;
    std::optional<CalibRun> m_calib;
    int m_calib_mode{-1};   // 1 the user's screen (charger in), 2 the tail, 3 full; -1 = put it back at the next poll
    u64 m_run_s{};          // seconds of this run
    s64 m_calib_last{};
    bool m_full_seen{};     // a charge phase saw 100 % on the charger
    FILE* m_log{};
    TimeStamp m_log_commit{};
    ui::Screensaver m_saver{};
    ui::InactivityTracker m_idle{};
    TimeStamp m_idle_clock{};
    bool m_reapply_due{};
    Thread m_threads[LOAD_CORES]{};
    bool m_started[LOAD_CORES]{};
    bool m_lbl{};
    float m_saved_brightness{};
    bool m_saved_auto{};
    TimeStamp m_poll{};
    bool m_read{};
    int m_pct{-1}; // psm, what the console shows; -1 until read
    int m_gauge_pct{-1}; // the fuel gauge's own RepSOC
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
    wizard::State m_state;
    TimeStamp m_trace{};
    u64 m_last_note{};
    bool m_prev_charger{}; // the charger state at the last note
    bool m_noted_once{};   // NoteState has seen a reading
    int m_cap_rem_mah{-1};
    int m_cap_full_mah{-1};
    float m_wh_in{};
    float m_wh_out{};
    u32 m_cycle_got{};
    bool m_dimmed{};
    wizard::DrainStep m_step{wizard::DrainStep::Draining};
    TimeStamp m_step_poll{};
    bool m_step_read{};
    TimeStamp m_saved{};
    u64 m_tte_s{};
    bool m_nv{};
    bool m_gpu_fd_ok{};
    u32 m_gpu_fd{};
    TimeStamp m_live{};
    std::deque<float> m_cpu[LOAD_CORES];
    std::deque<float> m_gpu;
    std::deque<float> m_watts;
    std::deque<float> m_temps;
    u64 m_busy[LOAD_CORES]{};
    u64 m_tick{};
};

} // namespace

namespace {
std::atomic<bool> g_auto_suppressed{false};
} // namespace

auto AutoDrainSuppressed() -> bool { return g_auto_suppressed; }
void SetAutoDrainSuppressed(bool v) { g_auto_suppressed = v; }

// at the Hub's start: a drain that ended without restoring the screen (crash, switch-off, the Hub killed) leaves its
// file behind; put the brightness and the automatic brightness back as they were
namespace {
std::atomic<bool> g_drain_interrupted{false};
} // namespace

auto DrainWasInterrupted() -> bool { return g_drain_interrupted; }

void RestoreDrainBrightness() {
    const auto kept = ReadSavedBrightness();
    if (!kept) {
        return;
    }
    g_drain_interrupted = true; // the drain did not end by the user: the console switched off or the Hub died
    if (R_SUCCEEDED(lblInitialize())) {
        lblSetCurrentBrightnessSetting(kept->brightness);
        if (kept->auto_on) {
            lblEnableAutoBrightnessControl();
        }
        lblExit();
        log_write("[Battery] screen restored after an unfinished drain (brightness %.2f, auto %d)\n", kept->brightness, kept->auto_on ? 1 : 0);
    }
    DeleteSavedBrightness();
}

void StartDrain(int cycle) {
    App::Push<DrainMenu>(cycle);
}

void StartCalibRun(CalibRun run) {
    App::Push<DrainMenu>(1, std::move(run));
}

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
