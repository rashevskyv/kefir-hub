#include "battery_drain_menu.hpp"
#include "battery_calib_internal.hpp"
#include "battery_zero.hpp"
#include "battery_charge.hpp"

namespace sphaira::battery {

// a cut-off cell rests back up by 0.2-0.3 V; more than this over a sleep gap is the charger's work
constexpr int CHARGED_ASLEEP_MV = 400;

void DrainMenu::OnSleptAwake(s64 now) {
    const auto slept = now - m_calib_last;
    // A sleep is never a cut-off (owner, 2026-11-09): the zero is the moment the console really switched off, which the Hub
    // finds in the log at its next start (a new boot). HOS sleeps at 3.20 V (the gauge said 30 % once) and takes the Hub's cut of
    // the charger input back in the sleep (REG00 04), so with the charger in it charges there (2026-11-10: 3.19 V -> 4.04 V in
    // 28 minutes, twice in one boot); the real cut-off came lower (3.01 V). The S line tells Cutoffs() so.
    if (m_log) {
        std::fprintf(m_log, "%lld,S,%d,%d,%d,-1,-1,%d,0,0.0,0,0,%d\n", static_cast<long long>(now), m_calib->step, m_pct, m_gauge_pct, m_tick_mv, m_charger ? 1 : 0);
        std::fflush(m_log);
        fsdevCommitDevice("sdmc");
    }
    m_slept_mv = m_tick_mv;
    m_slept_charged = m_mv > m_tick_mv + CHARGED_ASLEEP_MV;
    log_write("[Calib] the console slept %lld s after %d mV on battery (now %d mV, charger %d, charged %d): a sleep, the drain goes on\n",
              static_cast<long long>(slept), m_tick_mv, m_mv, m_charger ? 1 : 0, m_slept_charged ? 1 : 0);
    App::Notify(m_slept_charged ? "The console charged in the sleep: the drain starts over"_i18n : "The console slept: the drain goes on slowly"_i18n);
    if (m_slept_charged) { // the drain starts over: a dialog says why (not when it goes on: the page must keep its log to the cut-off)
        App::Push<ui::OptionBox>(SleptNote(), "OK"_i18n);
    }
}

// The toast above is gone in seconds and the user was unlocking the console then (owner, 2026-11-10: "did it see a cut-off or
// not?"). The page says what happened until it closes.
auto DrainMenu::SleptNote() const -> std::string {
    char v[16];
    std::snprintf(v, sizeof(v), "%.2f", m_slept_mv / 1000.0);
    return "The console went to sleep at"_i18n + " " + v + " V. " + "A low battery does that: it is not a switch-off, so the zero was not set."_i18n + " "
        + (m_slept_charged ? "It charged in the sleep, so the drain starts over."_i18n : "The drain goes on until the console switches off by itself."_i18n)
        + (charge::HizActive() ? " " + "Take the charger out: with it in, the console charges in every such sleep and never switches off."_i18n : std::string{});
}

void DrainMenu::OpenCalibLog() {
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

void DrainMenu::CloseCalibLog() {
    if (m_log) {
        std::fclose(m_log);
        m_log = nullptr;
        fsdevCommitDevice("sdmc");
    }
}

void DrainMenu::NoteState(bool force) {
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

// The full battery is handled by calib_flow::MeasureCapacityIfFull(), whatever page is open (the Hub also tries it at its start).
// The charge page asks every 5 s while the voltage is near the top, so the result shows at once.
void DrainMenu::MeasureOnFull() {
    if (m_measured || !m_calib->charge || !m_charger || !m_read || m_mv < zero::DONE_MIN_MV || m_measure_ts.GetSeconds() < 5) {
        return;
    }
    m_measure_ts.Update();
    const auto got = calib_flow::MeasureCapacityIfFull();
    m_waits_full = got == calib_flow::Measure::Waits;
    m_measured = got == calib_flow::Measure::Done && m_pct >= 100; // 100 % is in the log by now: the cycle count needs that line
}

auto DrainMenu::ZeroSeen() const -> bool {
    return m_read && (m_pct == 0 || m_gauge_pct == 0);
}

auto DrainMenu::Since(const std::uint64_t at) const -> u64 {
    const auto now = static_cast<u64>(std::time(nullptr));
    return at && now > at ? now - at : 0;
}

void DrainMenu::Push(std::deque<float>& d, float v) {
    if (m_sample_n % m_stride == 0) {
        d.push_back(v);
    }
}

void DrainMenu::Squash() {
    for (auto* d : {&m_watts, &m_volts, &m_temps}) {
        std::deque<float> half;
        for (size_t k = 0; k + 1 < d->size(); k += 2) {
            half.push_back(((*d)[k] + (*d)[k + 1]) / 2.f);
        }
        *d = std::move(half);
    }
    m_stride *= 2;
}

void DrainMenu::SampleLive() {
    m_sample_n++;
    // the load is live, not a history: the last LIVE_N samples (one minute) scroll to the left
    const auto Roll = [](std::deque<float>& d, float v) {
        d.push_back(v);
        if (d.size() > LIVE_N) {
            d.pop_front();
        }
    };
    const u64 tick = armGetSystemTick();
    for (int i = 0; i < LOAD_CORES; i++) {
        const u64 busy = g_busy[i];
        if (m_tick && tick > m_tick) {
            Roll(m_cpu[i], std::min(100.f, (busy - m_busy[i]) * 100.f / (tick - m_tick)));
        }
        m_busy[i] = busy;
    }
    m_tick = tick;
    u32 gpu{};
    if (m_gpu_fd_ok && R_SUCCEEDED(nvIoctl(m_gpu_fd, NVGPU_GET_GPU_LOAD, &gpu)) && gpu <= 1000) {
        Roll(m_gpu, gpu / 10.f);
        if (g_load > 0 && gpu < GPU_BUSY) {
            m_circles = std::min(CIRCLES, m_circles + 2);
        }
        m_circles = std::min(m_circles, std::max(CIRCLES_MIN, CIRCLES * g_load / 100)); // a part load draws a part of the circles
    }
    FuelGauge g;
    const auto mv = g.Read(MAX17050_VCell);
    const auto ma = g.Current(MAX17050_Current);
    if (mv && ma) {
        m_mv = static_cast<int>((*mv >> 3) * 0.625);
        Push(m_volts, m_mv / 1000.f);
        m_ma = static_cast<int>(*ma);
        // what HOS's own power logic makes of the voltage (3 normal, 2 no boost, 1 sleep, 0 shut down). The console slept at
        // 3.20 V in five drains and passed it awake in one (2026-11-09): the log tells which state did it
        PsmBatteryVoltageState state{};
        if (R_SUCCEEDED(psmGetBatteryVoltageState(&state)) && static_cast<int>(state) != m_psm_volt) {
            m_psm_volt = state;
            log_write("[Battery] psm voltage state %d at %d mV, %d mA\n", m_psm_volt, m_mv, m_ma);
            if (m_log) { // a V line: the Hub's log is cut at every start, the drain log keeps what HOS thought before a sleep or a cut-off
                std::fprintf(m_log, "%lld,V,%d,%d,%d\n", static_cast<long long>(std::time(nullptr)), m_psm_volt, m_mv, m_ma);
            }
        }
        const float w = m_mv / 1000.f * std::abs(m_ma) / 1000.f;
        Push(m_watts, w);
        (m_ma < 0 ? m_wh_out : m_wh_in) += w * (LIVE_MS / 1000.f) / 3600.f; // Wh over one sample
    }
    if (const auto qh = g.Read(MAX17050_QH)) {
        m_qh = *qh + 1;
    }
    if (const auto v = g.Read(MAX17050_Temp)) {
        Push(m_temps, static_cast<s16>(*v) / 256.f);
    }
    if (m_volts.size() > LIVE_N) {
        Squash();
    }
}

} // namespace sphaira::battery
