#pragma once

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

constexpr double HOT_C = 60.0;  // user, 2026-10-15: no load above 60 °C
constexpr double COOL_C = 55.0;
constexpr int LOAD_CORES = 3;
// 59 is the lowest time-sliced priority band of Horizon: a busy thread there shares the core. At 0x2C (cooperative) it
// never yields and hangs the Hub; at 0x3F it got no time at all.
constexpr int LOAD_PRIO = 59;
// few draws, each filling most of the screen: the GPU fill rate is the load. 700 small circles a frame
// overflowed deko3d's per-frame command buffer (crash in dkCmdBufBindTextures on .141).
constexpr int CIRCLES = 80;
// The load is the goal (owner 2026-10-31: the GPU must read 100 %), the frame rate is not: the count starts here and
// grows while the GPU reads below GPU_BUSY, up to CIRCLES; the frames may get slow, but not slower than SLOW_FRAME_MS.
constexpr int CIRCLES_START = 8;
constexpr int CIRCLES_MIN = 2;
constexpr u32 GPU_BUSY = 950;       // nvgpu load, 1000 = 100 %
constexpr double SLOW_FRAME_MS = 70.0;
// below this shown charge the load goes off and only the bright screen drains the battery: on a worn battery the
// voltage under load collapses from about 30 % and the console cuts off before the chip sees a real empty
constexpr int GENTLE_PCT = 35;
constexpr float MIN_BRIGHTNESS = 0.02f; // the lowest the screen still shows something
constexpr float TAIL_BRIGHTNESS = 0.01f; // the calibration after a cut-off: as low as the panel goes
// the calibration's drain: at this voltage under load (SOFT_TICKS polls in a row) the load goes off for good and the user's own
// screen comes back, so the console cuts off at a light load and the zero is a voltage near the battery's own, not one sagged by 1 A
constexpr int SOFT_MV = 3300;
constexpr int SOFT_TICKS = 5;
constexpr const char* CALIB_LOG = "/config/kefir/battery/calib-log.csv";
constexpr size_t CALIB_LOG_MAX = 4u * 1024 * 1024; // a longer log is kept as .old and a new one is started
constexpr float NOMINAL_V = 3.7f; // for Wh from mAh
constexpr u64 LIVE_MS = 250;   // one point of the live charts
constexpr size_t LIVE_N = 240; // points on a chart; the history squashes to stay inside

// nvgpu NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD on /dev/nvhost-ctrl-gpu: the GPU load in 1/1000 (as Status Monitor reads it)
constexpr u32 NVGPU_GET_GPU_LOAD = 0x80044715;

extern std::atomic<bool> g_load;
extern std::atomic<bool> g_stop;
extern std::atomic<u64> g_busy[LOAD_CORES];

struct DrainMenu final : ui::menu::MenuBase {
    explicit DrainMenu(int cycle, std::optional<CalibRun> calib = std::nullopt);

    ~DrainMenu() override;

    auto GetShortTitle() const -> const char* override { return m_calib && m_calib->charge ? "Battery charge" : "Battery drain"; }
    // the screensaver is a black page of its own: no header or footer over it
    auto WantsChrome() const -> bool override { return !m_saver.OwnsScreen(); }

    void Update(Controller* controller, TouchInfo* touch) override;

    // which phase of the cycle this is; a full battery on the charger counts the cycle here, like the wizard does,
    // and the page moves on to the second cycle
    void UpdateStep();

    // ---- the calibration (docs/dev/BATTERY-CALIBRATION.md, "Brightness rule" and "Cutoff pipeline") ----

    // the screen follows the state: the user's own brightness and auto-brightness while the charger is in (the drain is
    // paused), the minimum after a cut-off, the maximum before it. A charge leaves the screen alone.
    void ApplyCalibScreen();

    // once a second: the time on battery (or on the charger), a line in the log, and the end of a charge phase
    void CalibTick();

    // the console slept at a low battery (the lock screen) and was woken while it still drains: a cut-off for the drain.
    // A flag line in the log, the marker counts it, and the run goes to the tail (no load, minimum brightness)
    void OnSleptAwake(s64 now);

    auto CalibTitle() const -> std::string;

    auto CalibStatus() const -> std::string;

    auto DrainSeconds() const -> u64;

    void OpenCalibLog();

    void CloseCalibLog();

    // the drain start, the first 0 % and the last time seen on go to the wizard's state file, so a Hub restart
    // keeps them; alive is written every 30 s
    void NoteState(bool force);

    // HOS can switch off while it still shows 1 % (on .141 the gauge read 0 % with psm at 1 %): either one at 0 counts
    auto ZeroSeen() const -> bool;

    auto Since(const std::uint64_t at) const -> u64;

    // The power, voltage and temperature charts keep the whole drain (the CPU / GPU load only the last minute): a point is taken every m_stride samples; when a chart is full, every two
    // neighbours merge into their mean and the stride doubles, so the left end stays on screen as the right end grows.
    void Push(std::deque<float>& d, float v);
    void Squash();

    // CPU load from each load core's idle ticks, GPU load from nvgpu, power and temperature from the gauge
    void SampleLive();

    struct Line {
        const std::deque<float>* d;
        NVGcolor c;
        bool live{}; // the last LIVE_N samples at a fixed step from the right edge: new points push the line left, nothing squeezes
    };

    // A chart of the whole drain: the first point at the left edge, the newest at the right edge.
    // legend: a coloured dot and a text each, in two columns under the title (the plot starts below it)
    void Chart(NVGcontext* vg, float x, float y, float w, float h, const std::string& title, const std::string& value,
               std::initializer_list<Line> lines, float lo, float hi, NVGcolor text, NVGcolor info, float mark = -1.f,
               const std::vector<std::pair<NVGcolor, std::string>>& legend = {}) const;

    static auto Last(const std::deque<float>& d) -> float;

    static auto HoursMinutes(u64 sec) -> std::string;

    static auto HoursMinutesSeconds(u64 sec) -> std::string;

    // the calibration's page, as in the guide: the battery (the percent inside, approximate), the voltage beside it, what is
    // going on, the load and brightness chips, the consumption and the time, and a row of three charts
    void DrawCalib(NVGcontext* vg, Theme* theme);

    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    // what the screensaver paints: the cycle, the battery bar, power and temperature, the energy, the time on battery
    auto SaverReadout() const -> ui::SaverInfo;
    // the calibration's drain under load on the battery: the screen is part of the load (the saver at full brightness)
    bool DrainLoad() const { return m_calib && !m_calib->charge && !m_charger && g_load; }
    // +1 while the battery charges, -1 while it drains (the live current), 0 when it hardly moves
    int BatteryDir() const { return m_ma > 30 ? 1 : m_ma < -30 ? -1 : 0; }
    // the percent on the screen: 99 while the battery still takes current, so nobody pulls the charger at a 100 % that comes early
    int ShownPct() const { return m_pct >= 100 && m_ma > 100 ? 99 : m_pct; }

    int m_cycle;
    std::optional<CalibRun> m_calib;
    int m_calib_mode{-1};   // 1 the user's screen (charger in), 2 the tail, 3 full, 4 the user's screen (below SOFT_MV); -1 = put it back at the next poll
    bool m_soft{};          // the drain got to SOFT_MV: no load until the cut-off
    int m_soft_n{};         // polls in a row at or below SOFT_MV
    u64 m_run_s{};          // seconds of this run
    s64 m_calib_last{};
    int m_tick_mv{};        // the voltage and the charger at the last tick: what was true before a sleep gap
    bool m_tick_charger{};
    int m_qh{};             // the gauge's QH counter + 1 at the last live sample, 0 = not read
    int m_tick_qh{};        // the same at the last tick on battery: the baseline of the charge that comes in after the cut-off
    TimeStamp m_qh_saved{}; // when it was last written to the marker
    bool m_measured{};      // the capacity of this cycle is measured, or refused
    bool m_charged_asleep{}; // the voltage rose over a sleep gap: the charger charged the battery meanwhile, the drain is over
    TimeStamp m_measure_ts{};
    void MeasureOnFull();
    bool m_stop_ask{};      // the "stop?" question is open: the load is paused
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
    TimeStamp m_frame_ts{};
    int m_circles{CIRCLES_START};
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
    std::deque<float> m_volts;
    std::deque<float> m_temps;
    u64 m_busy[LOAD_CORES]{};
    u64 m_tick{};
    u64 m_sample_n{};
    u64 m_stride{1};
};

} // namespace sphaira::battery
