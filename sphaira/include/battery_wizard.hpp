#pragma once

// Tools → Battery → + (calibration wizard): which step the user is at, worked out from a small state file on
// the SD (/config/kefir/battery/calibration.txt, shared by sysMMC and emuMMC) and what the console reads now.
// A discharge is measured with HOS's own cycle total (this MMC's copy + the chip's Cycles register delta):
// 1.00 cycle = 100 % of the battery discharged, so the Hub does not need to run while the console drains.
// Pure; host test tests/test_battery_wizard.cpp.

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::battery::wizard {

// a cycle counts once this much of the battery (x100: 90 = 0.90 cycles) was discharged since the last step
inline constexpr std::uint32_t CYCLE_DONE = 90;

enum Mmc { SYSMMC = 0, EMUMMC = 1 };

struct State {
    std::uint64_t reset_at{};       // unix time of the factory reset; 0 = not done
    int reset_mmc{-1};              // the MMC it ran in
    std::uint32_t total_at_reset{}; // HOS cycle total x100 right after the reset
    std::uint64_t full1_at{};       // first cycle: discharged CYCLE_DONE, then full on the charger
    std::uint64_t full2_at{};
    std::uint64_t match_at[2]{};    // Match the other MMC ran in sysMMC / emuMMC
    // per cycle (index 0 = first): when the Hub's drain began, when the console first showed 0 %, and the last
    // time the drain page saw the console on; alive - zero = how long it ran after showing 0 %
    std::uint64_t drain_at[2]{};
    std::uint64_t zero_at[2]{};
    std::uint64_t alive_at[2]{};
    std::uint64_t drain_s[2]{}; // seconds spent on battery in this cycle's drain (the charger hours do not count)
    // HOS cycle total x100 where each cycle is measured from (0 = not set: cycle 1 from the reset, cycle 2 from the
    // moment cycle 1 counted). "Redo this cycle" sets it to the total now, so a cycle that did not really happen can
    // be done again.
    std::uint32_t start[2]{};
    friend bool operator==(const State&, const State&) = default;
};

inline auto Format(const State& s) -> std::string {
    char buf[768];
    std::snprintf(buf, sizeof(buf),
        "reset_at=%llu\nreset_mmc=%d\ntotal_at_reset=%u\nfull1_at=%llu\nfull2_at=%llu\nmatch_sysmmc_at=%llu\nmatch_emummc_at=%llu\n"
        "drain1_at=%llu\nzero1_at=%llu\nalive1_at=%llu\ndrain2_at=%llu\nzero2_at=%llu\nalive2_at=%llu\n"
        "drain1_s=%llu\ndrain2_s=%llu\nstart1=%u\nstart2=%u\n",
        (unsigned long long)s.reset_at, s.reset_mmc, s.total_at_reset, (unsigned long long)s.full1_at,
        (unsigned long long)s.full2_at, (unsigned long long)s.match_at[SYSMMC], (unsigned long long)s.match_at[EMUMMC],
        (unsigned long long)s.drain_at[0], (unsigned long long)s.zero_at[0], (unsigned long long)s.alive_at[0],
        (unsigned long long)s.drain_at[1], (unsigned long long)s.zero_at[1], (unsigned long long)s.alive_at[1],
        (unsigned long long)s.drain_s[0], (unsigned long long)s.drain_s[1], s.start[0], s.start[1]);
    return buf;
}

inline auto Parse(std::string_view text) -> State {
    State s;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        const auto line = text.substr(0, nl);
        text = nl == text.npos ? std::string_view{} : text.substr(nl + 1);
        const auto eq = line.find('=');
        if (eq == line.npos) {
            continue;
        }
        const auto key = line.substr(0, eq);
        const auto v = std::strtoll(std::string{line.substr(eq + 1)}.c_str(), nullptr, 10);
        if (key == "reset_at") s.reset_at = v;
        else if (key == "reset_mmc") s.reset_mmc = static_cast<int>(v);
        else if (key == "total_at_reset") s.total_at_reset = static_cast<std::uint32_t>(v);
        else if (key == "full1_at") s.full1_at = v;
        else if (key == "full2_at") s.full2_at = v;
        else if (key == "match_sysmmc_at") s.match_at[SYSMMC] = v;
        else if (key == "match_emummc_at") s.match_at[EMUMMC] = v;
        else if (key == "drain1_at") s.drain_at[0] = v;
        else if (key == "zero1_at") s.zero_at[0] = v;
        else if (key == "alive1_at") s.alive_at[0] = v;
        else if (key == "drain2_at") s.drain_at[1] = v;
        else if (key == "zero2_at") s.zero_at[1] = v;
        else if (key == "alive2_at") s.alive_at[1] = v;
        else if (key == "drain1_s") s.drain_s[0] = v;
        else if (key == "drain2_s") s.drain_s[1] = v;
        else if (key == "start1") s.start[0] = static_cast<std::uint32_t>(v);
        else if (key == "start2") s.start[1] = static_cast<std::uint32_t>(v);
    }
    return s;
}

struct Live {
    std::uint32_t total{};   // HOS cycle total x100 now (copy + chip delta)
    bool charger{};
    std::uint32_t pct{};     // what the console shows
    int mmc{EMUMMC};
    bool other_known{};      // the page was opened in the other MMC
    std::uint64_t now{};
};

// discharged since the reset, x100
inline auto Discharged(const State& s, const Live& l) -> std::uint32_t {
    return l.total > s.total_at_reset ? l.total - s.total_at_reset : 0;
}

// where cycle 1 or 2 is measured from (see State::start)
inline auto CycleStart(const State& s, int cycle) -> std::uint32_t {
    if (cycle == 1) {
        return s.start[0] ? s.start[0] : s.total_at_reset;
    }
    return s.start[1] ? s.start[1] : s.total_at_reset + CYCLE_DONE;
}

// x100 discharged in this cycle so far
inline auto Got(const State& s, const Live& l, int cycle) -> std::uint32_t {
    const auto from = CycleStart(s, cycle);
    return l.total > from ? l.total - from : 0;
}

// "Redo this cycle": measure it again from now; a later cycle and the match step are redone with it.
inline void Redo(State& s, int cycle, std::uint32_t total_now) {
    const int c = cycle - 1;
    if (c < 0 || c > 1) {
        return;
    }
    for (int i = c; i < 2; i++) {
        s.drain_at[i] = s.zero_at[i] = s.alive_at[i] = s.drain_s[i] = 0;
        s.start[i] = 0;
    }
    s.start[c] = total_now;
    if (c == 0) {
        s.full1_at = 0;
    }
    s.full2_at = 0;
    s.match_at[SYSMMC] = s.match_at[EMUMMC] = 0;
}

// records a finished cycle: enough discharged since the last mark, now full on the charger. true = state changed.
inline auto Advance(State& s, const Live& l) -> bool {
    // psm shows 100 only when the battery is full: a full battery counts with the charger out too (the user pulls
    // the cable and opens the page afterwards, so the "on the charger" moment is easy to miss)
    if (!s.reset_at || l.pct < 100) {
        return false;
    }
    if (!s.full1_at && Got(s, l, 1) >= CYCLE_DONE) {
        s.full1_at = l.now;
        if (!s.start[1]) {
            s.start[1] = l.total; // cycle 2 is measured from here
        }
        return true;
    }
    if (s.full1_at && !s.full2_at && Got(s, l, 2) >= CYCLE_DONE) {
        s.full2_at = l.now;
        return true;
    }
    return false;
}

// The drain page, about once a second for cycle 1 or 2: the drain start survives Hub restarts, the first 0 %
// is kept, alive moves on. true = something other than alive changed (worth saving at once).
inline auto NoteDrain(State& s, int cycle, std::uint64_t now, int shown_pct) -> bool {
    if (cycle < 1 || cycle > 2) {
        return false;
    }
    const int c = cycle - 1;
    bool changed = false;
    if (!s.drain_at[c]) {
        s.drain_at[c] = now;
        changed = true;
    }
    if (shown_pct == 0 && !s.zero_at[c]) {
        s.zero_at[c] = now;
        changed = true;
    }
    s.alive_at[c] = now;
    return changed;
}

// seconds the console ran after it first showed 0 % in this cycle (up to the last time the page saw it); 0 = unknown
inline auto AfterZero(const State& s, int cycle) -> std::uint64_t {
    const int c = cycle - 1;
    if (c < 0 || c > 1 || !s.zero_at[c] || s.alive_at[c] < s.zero_at[c]) {
        return 0;
    }
    return s.alive_at[c] - s.zero_at[c];
}

// Where the drain page is in cycle 1 or 2 (call Advance first, so a full battery has been counted already).
enum class DrainStep {
    Draining,     // on battery
    NotEmptyYet,  // the charger is in before this cycle's discharge is done
    Charging,     // this cycle's discharge is done, charging up to 100 %
    CycleCounted, // this cycle is counted
};

inline auto DrainStepOf(const State& s, const Live& l, int cycle) -> DrainStep {
    if ((cycle == 1 && s.full1_at) || (cycle == 2 && s.full2_at)) {
        return DrainStep::CycleCounted;
    }
    if (!l.charger) {
        return DrainStep::Draining;
    }
    return Got(s, l, cycle) >= CYCLE_DONE ? DrainStep::Charging : DrainStep::NotEmptyYet;
}

enum class Step { CheckOther, Reset, Cycle1, Cycle2, MatchOther, Finished };
enum class Mark { Done, Next, Later };

struct View {
    Step step;
    Mark mark;
    std::uint32_t discharged{}; // for the cycle steps: x100 in that cycle so far
};

inline auto Steps(const State& s, const Live& l) -> std::vector<View> {
    const int other = s.reset_mmc == SYSMMC ? EMUMMC : SYSMMC;
    const bool matched = s.full2_at && s.match_at[other] > s.full2_at;
    const bool done[] = {l.other_known || s.reset_at, s.reset_at != 0, s.full1_at != 0, s.full2_at != 0, matched, matched};
    std::vector<View> out;
    bool next_given = false;
    for (int i = 0; i < 6; i++) {
        Mark m = Mark::Later;
        if (done[i]) {
            m = Mark::Done;
        } else if (!next_given) {
            m = Mark::Next;
            next_given = true;
        }
        out.push_back({static_cast<Step>(i), m, i == 2 ? Got(s, l, 1) : i == 3 ? Got(s, l, 2) : 0u});
    }
    return out;
}

} // namespace sphaira::battery::wizard
