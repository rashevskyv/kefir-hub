#pragma once

// What the battery calibration wizard remembers besides the old wizard's state (battery_wizard.hpp, calibration.txt): the
// reset and the two cycles are counted there, by HOS's cycle total, as in 0.14.111-163. This marker file on the SD keeps
// the rest: the report and the transfer step, the user's brightness, and what the Hub needs to go on with a drain after
// a cut-off (docs/dev/BATTERY-CALIBRATION.md, "The wizard remembers").
// No libnx: host-tested by tests/test_battery_calib_state.cpp.

#include "battery_cutoff.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace sphaira::battery::calib {

enum class Step : std::uint8_t {
    Reset = 1,    // the factory model at 100 % (the old step 2)
    Cycle1 = 2,   // drain, then 100 % on the charger; counted by the cycle total
    Cycle2 = 3,
    Report = 4,
    Transfer = 5,
    Finished = 6,
};

struct Marker {
    Step step{Step::Reset};
    bool drain_started{};              // the Hub's drain page was started in a cycle step and not left on purpose
    std::int64_t drain_time{};         // when it was started
    int cutoffs{};                     // cut-offs seen in this cycle: 0 = maximum load, 1 or more = the tail mode
    int brightness_before{-1};         // what the user had (percent), -1 unknown
    bool auto_before{};                // auto-brightness the user had
    int last_set_brightness{-1};       // the last brightness the Hub set itself, -1 none
    std::int64_t boot_time{};          // unix time the console booted when the drain started (0 = unknown): a Hub start in the
                                       // same boot is a restart of the Hub, in another boot the console itself switched off
};

inline auto Begin(int brightness_now, bool auto_now) -> Marker {
    Marker m;
    m.brightness_before = brightness_now;
    m.auto_before = auto_now;
    return m;
}

inline auto IsCycle(Step s) -> bool { return s == Step::Cycle1 || s == Step::Cycle2; }

// ---- where the wizard is ---------------------------------------------------------------------------------------------------

// The reset and the cycles come from the wizard's state (the marker only mirrors them); the report and the transfer are the marker's own.
inline auto StepFor(const Marker& m, bool reset_done, bool cycle1_done, bool cycle2_done) -> Step {
    if (m.step == Step::Report || m.step == Step::Transfer || m.step == Step::Finished) {
        return m.step;
    }
    if (!reset_done) {
        return Step::Reset;
    }
    if (!cycle1_done) {
        return Step::Cycle1;
    }
    return cycle2_done ? Step::Report : Step::Cycle2;
}

// true = the step moved; a new step starts without a running drain and in the maximum load again
inline auto Sync(Marker& m, bool reset_done, bool cycle1_done, bool cycle2_done) -> bool {
    const auto next = StepFor(m, reset_done, cycle1_done, cycle2_done);
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
    if (!Rebooted(m, b)) {
        return b.charger_in ? Found::Nothing : Found::HubRestarted; // the same boot: the drain page was left, not cut off
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
