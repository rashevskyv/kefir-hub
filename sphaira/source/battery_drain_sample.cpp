#include "battery_drain_menu.hpp"

namespace sphaira::battery {

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

auto DrainMenu::ZeroSeen() const -> bool {
    return m_read && (m_pct == 0 || m_gauge_pct == 0);
}

auto DrainMenu::Since(const std::uint64_t at) const -> u64 {
    const auto now = static_cast<u64>(std::time(nullptr));
    return at && now > at ? now - at : 0;
}

void DrainMenu::Push(std::deque<float>& d, float v) {
    d.push_back(v);
    if (d.size() > LIVE_N) {
        d.pop_front();
    }
}

void DrainMenu::SampleLive() {
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
        Push(m_volts, m_mv / 1000.f);
        m_ma = static_cast<int>(*ma);
        const float w = m_mv / 1000.f * std::abs(m_ma) / 1000.f;
        Push(m_watts, w);
        (m_ma < 0 ? m_wh_out : m_wh_in) += w * (LIVE_MS / 1000.f) / 3600.f; // Wh over one sample
    }
    if (const auto v = g.Read(MAX17050_Temp)) {
        Push(m_temps, static_cast<s16>(*v) / 256.f);
    }
}

} // namespace sphaira::battery
