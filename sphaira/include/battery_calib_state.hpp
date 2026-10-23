#pragma once

// The state of the battery calibration wizard (docs/dev/BATTERY-CALIBRATION.md, "Cutoff pipeline" and "The wizard remembers").
// It lives in a marker file on the SD, so the Hub can be closed, the console used, and the wizard still knows where it is.
// No libnx: host-tested by tests/test_battery_calib_state.cpp.

#include "battery_cutoff.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace sphaira::battery::calib {

enum class Step : std::uint8_t {
    Drain = 1,    // drain to the dead battery (the Hub runs it: it needs the log)
    Zero = 2,     // the charger is in and the console started: the Hub sets the zero
    Cycle1 = 3,
    Cycle2 = 4,
    Report = 5,
    Transfer = 6,
    Finished = 7,
};

// the two phases of a cycle; step Drain is only ever in Phase::Drain
enum class Phase : std::uint8_t { Charge = 0, Drain = 1 };

struct Marker {
    Step step{Step::Drain};
    Phase phase{Phase::Drain};
    bool phase_started{};              // "Start the drain" / "Start the charge" was pressed
    std::int64_t phase_time{};         // when it was pressed
    int phase_percent{-1};             // the battery percent then
    int cutoffs{};                     // cut-offs seen by this calibration: 0 = maximum load, 1 or more = the tail mode
    int brightness_before{-1};         // what the user had (percent), -1 unknown
    bool auto_before{};                // auto-brightness the user had
    int last_set_brightness{-1};       // the last brightness the Hub set itself, -1 none
    int last_logged_remaining_mah{-1}; // X: the gauge's last logged remaining charge before the cut-off, -1 none
};

inline auto Begin(int brightness_now, bool auto_now, std::int64_t now) -> Marker {
    Marker m;
    m.brightness_before = brightness_now;
    m.auto_before = auto_now;
    m.phase_time = now;
    return m;
}

// ---- the load and the brightness -------------------------------------------------------------------------------------------

// Maximum before the first cut-off, minimal after every restart (the tail mode).
inline auto LoadFor(const Marker& m) -> cutoff::Load {
    return m.cutoffs == 0 ? cutoff::Load::Max : cutoff::Load::Min;
}

enum class Brightness {
    Max,     // the first run: the highest, auto-brightness off
    Min,     // the tail mode: the lowest, auto-brightness off
    Restore, // the user's own brightness and auto-brightness back
    Keep,    // the user changed it himself: leave it alone
};

inline constexpr int LOW_BATTERY_PERCENT = 15; // under this, with the charger just in, a self-drain is likely

// current: the brightness now (percent). draining: the Hub is draining right now.
inline auto DecideBrightness(const Marker& m, bool charger_in, bool draining, int current) -> Brightness {
    const bool user_changed = m.last_set_brightness >= 0 && current != m.last_set_brightness;
    if (charger_in || !draining) {
        // the charger is in: the drain is paused or over; give the user's values back, unless he set his own since
        return user_changed ? Brightness::Keep : Brightness::Restore;
    }
    if (m.cutoffs > 0) {
        return Brightness::Min; // squeezing the last charge: the Hub lowers it itself
    }
    return user_changed ? Brightness::Keep : Brightness::Max;
}

// ---- what the Hub finds when it starts -------------------------------------------------------------------------------------

struct Battery {
    int percent{};
    bool charger_in{};
};

enum class Found {
    Nothing,          // no calibration, or nothing to say
    CutoffSeen,       // the Hub drained, the console switched off and was switched on without the charger: go on in the tail mode
    DeadWithCharger,  // step 1: the console would not start without the charger: the zero comes next
    ChargeDone,       // 100 % with the charger in
    ChargeInterrupted,// the charger went out before 100 %
    ChargeInProgress, // still charging
    AskDrainFinished, // low battery and the charger just in during a drain phase: ask "did the console switch off from the drain?"
};

inline auto Check(Marker& m, const Battery& b) -> Found {
    if (m.step == Step::Finished || m.step == Step::Zero || m.step == Step::Report || m.step == Step::Transfer || !m.phase_started) {
        return Found::Nothing;
    }
    if (m.step == Step::Drain) {
        if (b.charger_in) {
            m.step = Step::Zero; // the user plugged the charger in and the console started: the zero comes now
            m.phase_started = false;
            return Found::DeadWithCharger;
        }
        m.cutoffs++;
        return Found::CutoffSeen;
    }
    if (m.phase == Phase::Charge) {
        if (b.charger_in && b.percent >= 100) {
            m.phase = Phase::Drain;
            m.phase_started = false;
            return Found::ChargeDone;
        }
        if (!b.charger_in) {
            m.phase_started = false; // the charge must be started again
            return Found::ChargeInterrupted;
        }
        return Found::ChargeInProgress;
    }
    // a drain phase of a cycle
    if (!b.charger_in) {
        m.cutoffs++; // the Hub was draining, the console cut off and was switched on without the charger
        return Found::CutoffSeen;
    }
    return b.percent <= LOW_BATTERY_PERCENT ? Found::AskDrainFinished : Found::Nothing;
}

// ---- moving on -------------------------------------------------------------------------------------------------------------

inline void StartPhase(Marker& m, int percent, std::int64_t now) {
    m.phase_started = true;
    m.phase_time = now;
    m.phase_percent = percent;
}

// the zero is written
inline void ZeroDone(Marker& m) {
    m.step = Step::Cycle1;
    m.phase = Phase::Charge;
    m.phase_started = false;
    m.cutoffs = 0;
    m.last_logged_remaining_mah = -1;
}

// the drain phase of a cycle is over (the Hub saw the cut-off and the console is dead with the charger in, or the user said yes)
inline void DrainConfirmed(Marker& m) {
    m.phase_started = false;
    m.cutoffs = 0;
    if (m.step == Step::Cycle1) {
        m.step = Step::Cycle2;
        m.phase = Phase::Charge;
    } else if (m.step == Step::Cycle2) {
        m.step = Step::Report;
    }
}

inline void ReportSeen(Marker& m) { if (m.step == Step::Report) m.step = Step::Transfer; }
inline void TransferDone(Marker& m) { if (m.step == Step::Transfer) m.step = Step::Finished; }

// ---- the file --------------------------------------------------------------------------------------------------------------

// key=value lines
inline auto Format(const Marker& m) -> std::string {
    std::string o;
    const auto kv = [&](const char* k, long long v) { o += std::string{k} + "=" + std::to_string(v) + '\n'; };
    kv("step", static_cast<int>(m.step));
    kv("phase", static_cast<int>(m.phase));
    kv("started", m.phase_started ? 1 : 0);
    kv("time", m.phase_time);
    kv("percent", m.phase_percent);
    kv("cutoffs", m.cutoffs);
    kv("brightness_before", m.brightness_before);
    kv("auto_before", m.auto_before ? 1 : 0);
    kv("last_set", m.last_set_brightness);
    kv("x_mah", m.last_logged_remaining_mah);
    return o;
}

inline auto Parse(std::string_view text) -> Marker {
    Marker m;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string key{line.substr(0, eq)};
        long long v = 0;
        try { v = std::stoll(std::string{line.substr(eq + 1)}); } catch (...) { continue; }
        if (key == "step" && v >= 1 && v <= 7) m.step = static_cast<Step>(v);
        else if (key == "phase" && (v == 0 || v == 1)) m.phase = static_cast<Phase>(v);
        else if (key == "started") m.phase_started = v != 0;
        else if (key == "time") m.phase_time = v;
        else if (key == "percent") m.phase_percent = static_cast<int>(v);
        else if (key == "cutoffs") m.cutoffs = static_cast<int>(v < 0 ? 0 : v);
        else if (key == "brightness_before") m.brightness_before = static_cast<int>(v);
        else if (key == "auto_before") m.auto_before = v != 0;
        else if (key == "last_set") m.last_set_brightness = static_cast<int>(v);
        else if (key == "x_mah") m.last_logged_remaining_mah = static_cast<int>(v);
    }
    return m;
}

} // namespace sphaira::battery::calib
