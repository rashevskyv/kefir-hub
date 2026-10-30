#pragma once

// What the battery calibration wizard remembers (owner, 2026-10-29): five steps. 1 the factory reset at 100 % (the proven
// logic of 0.14.111-163, battery_write.cpp RunReset). Then a cycle is a drain to the cut-off and then a charge to 100 %;
// in cycle 1 the cut-off voltage is written into the gauge as the zero (battery_zero.hpp) before the charge, so the
// factory model does not wipe it. A cycle counts by the drain log (battery_calib_log.hpp, CycleDoneByLog). The old wizard
// state (battery_wizard.hpp, calibration.txt) holds reset_at and mirrors full1_at/full2_at for the pages that read it.
// This marker file on the SD keeps the step, the zero, the user's brightness, and what the Hub needs to go on with a
// drain after a cut-off.
// No libnx: host-tested by tests/test_battery_calib_state.cpp.

#include "battery_cutoff.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace sphaira::battery::calib {

enum class Step : std::uint8_t {
    Reset = 1,    // the factory model at 100 %
    Cycle1 = 2,   // drain to the cut-off, the zero written, charge to 100 %
    Cycle2 = 3,   // drain to the cut-off, charge to 100 %
    Report = 4,
    Transfer = 5,
    Finished = 6,
};

inline constexpr int STEP_COUNT = 5; // what the user sees: reset, cycle 1, cycle 2, report, transfer

// 1-based position of a step in the list the user sees
inline auto StepIndex(Step s) -> int {
    return s == Step::Finished ? STEP_COUNT : static_cast<int>(s);
}

// where cycle 1 is, in words the pages use
enum class Phase {
    Drain,     // take the charger out, drain to the cut-off (cycle 1: then the zero is written)
    Charge,    // the console cut off (and the zero is written): plug the charger in, charge to 100 %
};

struct Marker {
    Step step{Step::Reset};
    std::int64_t started_at{};         // when this calibration began: log lines before it belong to an older run
    int zero_mv{};                     // the cut-off voltage of cycle 1's drain (the last log line before the gap), 0 = none yet
    std::int64_t zero_at{};            // when it was written into the gauge, 0 = not written
    std::int64_t full_at[2]{};         // cycle 1 and 2: when the charge after the cut-off saw 100 % (the cycle is done)
    bool drain_started{};              // the Hub's drain page was started in a cycle step and not left on purpose
    std::int64_t drain_time{};         // when it was started
    int cutoffs{};                     // cut-offs seen in this cycle: 0 = maximum load, 1 or more = the tail mode
    int brightness_before{-1};         // what the user had (percent), -1 unknown
    bool auto_before{};                // auto-brightness the user had
    int last_set_brightness{-1};       // the last brightness the Hub set itself, -1 none
    std::int64_t boot_time{};          // unix time the console booted when the drain started (0 = unknown): a Hub start in the
                                       // same boot is a restart of the Hub, in another boot the console itself switched off
};

inline auto Begin(int brightness_now, bool auto_now, std::int64_t now = 0) -> Marker {
    Marker m;
    m.brightness_before = brightness_now;
    m.auto_before = auto_now;
    m.started_at = now;
    return m;
}

inline auto IsCycle(Step s) -> bool { return s == Step::Cycle1 || s == Step::Cycle2; }
inline auto CycleOf(Step s) -> int { return s == Step::Cycle1 ? 1 : s == Step::Cycle2 ? 2 : 0; }

// the phase of the cycle on screen: the drain until the console cut off (cycle 1: and the zero is written), then the charge
inline auto PhaseOf(const Marker& m) -> Phase {
    if (m.step == Step::Cycle1) {
        return m.zero_at ? Phase::Charge : Phase::Drain;
    }
    return m.cutoffs > 0 ? Phase::Charge : Phase::Drain;
}

// when the charge after the cut-off saw 100 %: the cycle is done. The log's time of it, or now.
inline void MarkCycleDone(Marker& m, std::int64_t full_time) {
    const int c = CycleOf(m.step);
    if (c) {
        m.full_at[c - 1] = full_time;
    }
}

// ---- where the wizard is ---------------------------------------------------------------------------------------------------

// reset_done: the wizard's state has reset_at (the reset is battery_write.cpp's)
inline auto StepFor(const Marker& m, bool reset_done) -> Step {
    if (m.step == Step::Report || m.step == Step::Transfer || m.step == Step::Finished) {
        return m.step;
    }
    if (!reset_done) {
        return Step::Reset;
    }
    if (!m.full_at[0]) {
        return Step::Cycle1;
    }
    return m.full_at[1] ? Step::Report : Step::Cycle2;
}

// true = the step moved; a new step starts without a running drain and in the maximum load again
inline auto Sync(Marker& m, bool reset_done) -> bool {
    const auto next = StepFor(m, reset_done);
    if (next == m.step) {
        return false;
    }
    m.step = next;
    m.drain_started = false;
    m.cutoffs = 0;
    return true;
}

inline void ReportSeen(Marker& m) { if (m.step == Step::Report) m.step = Step::Transfer; }
inline void TransferDone(Marker& m) { if (m.step == Step::Transfer) m.step = Step::Finished; }

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
    bool charger_in{};
    std::int64_t boot_time{}; // unix time this boot began (now minus the uptime), 0 = unknown
    bool cut_in_log{};        // the drain log has a cut-off of this drain (a low last line, then a gap): the console did switch off
};

inline constexpr std::int64_t BOOT_SAME_S = 120; // two boot times this close are one boot (the clock and the uptime differ a little)

// the console started again since the drain was started
inline auto Rebooted(const Marker& m, const Battery& b) -> bool {
    if (!m.boot_time || !b.boot_time) {
        return false; // unknown: do not take a Hub restart for a cut-off
    }
    const auto d = b.boot_time - m.boot_time;
    return d > BOOT_SAME_S || d < -BOOT_SAME_S;
}

enum class Found {
    Nothing,      // no drain was running, or the console is charging: the cycle counts by the cycle total
    HubRestarted, // a drain was running and only the Hub started again (the console did not restart): go on as before
    CutoffCharging, // the Hub drained, the console switched off and was switched on WITH the charger: the charge phase
    CutoffSeen,   // the Hub drained, the console switched off and was switched on without the charger: go on in the tail mode
};

inline auto Check(Marker& m, const Battery& b) -> Found {
    if (!IsCycle(m.step) || !m.drain_started) {
        return Found::Nothing;
    }
    // the same boot: the drain page was left, not cut off. Unless the log says the console did switch off and it is on the
    // charger now: then the boot-time guess was wrong (the user saw the red battery), and the drain is over
    if (!Rebooted(m, b) && !(b.charger_in && b.cut_in_log)) {
        return b.charger_in ? Found::Nothing : Found::HubRestarted;
    }
    m.boot_time = b.boot_time;
    if (b.charger_in) {
        m.drain_started = false; // the console was off and is charging: the drain is over
        m.cutoffs++;             // and it did cut off: the charge phase follows, whatever the cycle total says
        return Found::CutoffCharging;
    }
    m.cutoffs++; // the Hub was draining, the console cut off and was switched on without the charger
    return Found::CutoffSeen;
}

inline void StartDrain(Marker& m, std::int64_t now, std::int64_t boot_time = 0) {
    m.drain_started = true;
    m.drain_time = now;
    m.boot_time = boot_time;
}

// ---- the file --------------------------------------------------------------------------------------------------------------

// key=value lines
inline auto Format(const Marker& m) -> std::string {
    std::string o;
    const auto kv = [&](const char* k, long long v) { o += std::string{k} + "=" + std::to_string(v) + '\n'; };
    kv("step", static_cast<int>(m.step));
    kv("started_at", m.started_at);
    kv("zero_mv", m.zero_mv);
    kv("zero_at", m.zero_at);
    kv("full1_at", m.full_at[0]);
    kv("full2_at", m.full_at[1]);
    kv("started", m.drain_started ? 1 : 0);
    kv("time", m.drain_time);
    kv("cutoffs", m.cutoffs);
    kv("brightness_before", m.brightness_before);
    kv("auto_before", m.auto_before ? 1 : 0);
    kv("last_set", m.last_set_brightness);
    kv("boot", m.boot_time);
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
        const std::string number{line.substr(eq + 1)};
        char* end = nullptr;
        const long long v = std::strtoll(number.c_str(), &end, 10);
        if (end == number.c_str()) {
            continue; // not a number: no exceptions on the console build
        }
        if (key == "step" && v >= 1 && v <= 6) m.step = static_cast<Step>(v);
        else if (key == "started_at") m.started_at = v;
        else if (key == "zero_mv") m.zero_mv = static_cast<int>(v);
        else if (key == "zero_at") m.zero_at = v;
        else if (key == "full1_at") m.full_at[0] = v;
        else if (key == "full2_at") m.full_at[1] = v;
        else if (key == "started") m.drain_started = v != 0;
        else if (key == "time") m.drain_time = v;
        else if (key == "cutoffs") m.cutoffs = static_cast<int>(v < 0 ? 0 : v);
        else if (key == "brightness_before") m.brightness_before = static_cast<int>(v);
        else if (key == "auto_before") m.auto_before = v != 0;
        else if (key == "last_set") m.last_set_brightness = static_cast<int>(v);
        else if (key == "boot") m.boot_time = v;
    }
    return m;
}

} // namespace sphaira::battery::calib
