#pragma once

// The pure rules of the battery cut-off calibration (docs/dev/BATTERY-CALIBRATION.md, "Cutoff pipeline"). No libnx: host-tested
// by tests/test_battery_cutoff.cpp.
//   - ZeroAfterCutoff : what to write as the gauge's remaining charge after a drain to the cut-off
//   - LoadPlan        : max load first, minimal load for the last 0.2 V, latched
//   - SystemId        : which system (sysMMC or one of several emuMMCs) the Hub runs in
//   - Ledger          : where a calibration was made and where it was written

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::battery::cutoff {

// ---- the zero --------------------------------------------------------------------------------------------------------------

struct Zero {
    std::int32_t value_mah{}; // what to write as the remaining charge
    bool fallback{};          // true: plain 0 (the gauge was reset, the log is lost, or the result would be negative)
};

// counter_now: the gauge's remaining charge read after the switch-on (it kept counting the minutes of charge).
// last_logged: the last remaining charge in the log before the cut-off (the gauge's error: the real value was 0).
inline auto ZeroAfterCutoff(std::optional<std::int32_t> counter_now, std::optional<std::int32_t> last_logged, bool gauge_reset) -> Zero {
    if (gauge_reset || !counter_now || !last_logged) {
        return {0, true};
    }
    const auto value = *counter_now - *last_logged;
    // a gauge that reported too LITTLE before the cut-off (X below 0 does not happen, but N < X does after a reset it missed)
    return value < 0 ? Zero{0, true} : Zero{value, false};
}

// ---- the load plan ---------------------------------------------------------------------------------------------------------

enum class Load { Max, Min };

inline constexpr int TAIL_START_MV = 3400;  // 0.2 V above the expected cut-off of 3200 mV (terminal voltage under load)
inline constexpr int TAIL_MIN_SAG_MV = 50;  // below this sag the low-load tail changes nothing

struct LoadPlan {
    bool latched{}; // once low, always low: the voltage jumps up when the load drops, a plain threshold would flip back
};

// terminal_mv: the voltage now. tail_enabled: false skips the tail (see TailWorthIt).
inline auto NextLoad(LoadPlan& plan, int terminal_mv, bool tail_enabled) -> Load {
    if (tail_enabled && (plan.latched || (terminal_mv > 0 && terminal_mv <= TAIL_START_MV))) {
        plan.latched = true;
        return Load::Min;
    }
    return Load::Max;
}

// sag_mv: how far the voltage fell at the load change in the log of a first run; unknown (no log yet) keeps the tail.
inline auto TailWorthIt(std::optional<int> sag_mv) -> bool {
    return !sag_mv || *sag_mv >= TAIL_MIN_SAG_MV;
}

// ---- which system ----------------------------------------------------------------------------------------------------------

// "sys" for sysMMC; "emu-p-<hex sector>" for a partition emuMMC; "emu-f-<folder>" for a file-based one.
inline auto SystemId(bool emummc, bool partition, std::uint64_t sector, std::string_view path) -> std::string {
    if (!emummc) {
        return "sys";
    }
    if (partition) {
        static constexpr char HEX[] = "0123456789abcdef";
        std::string hex;
        do {
            hex.insert(hex.begin(), HEX[sector & 0xF]);
            sector >>= 4;
        } while (sector);
        return "emu-p-" + hex;
    }
    return "emu-f-" + std::string{path};
}

// "sys" -> "sysMMC"; "emu-p-2f5000" -> "emuMMC (sector 0x2f5000)"; "emu-f-emuMMC/SD01" -> "emuMMC emuMMC/SD01"
inline auto SystemName(std::string_view id) -> std::string {
    if (id == "sys") {
        return "sysMMC";
    }
    if (id.starts_with("emu-p-")) {
        return "emuMMC (sector 0x" + std::string{id.substr(6)} + ")";
    }
    if (id.starts_with("emu-f-")) {
        return "emuMMC " + std::string{id.substr(6)};
    }
    return std::string{id};
}

// ---- the ledger ------------------------------------------------------------------------------------------------------------

// File format, one record a line, tab separated:
//   cal <seq> <time> <origin id> <battery model> <good 0|1>      the newest calibration
//   w   <system id> <seq> <time>                                 the sequence number last written in a system
struct Written {
    std::string system;
    std::uint64_t seq{};
    std::int64_t time{};
};

struct Ledger {
    std::uint64_t seq{}; // 0: no calibration yet
    std::int64_t time{};
    std::string origin{};
    std::string model{};
    bool good{};
    std::vector<Written> written{};
};

inline auto Format(const Ledger& l) -> std::string {
    std::string out;
    if (l.seq) {
        out += "cal\t" + std::to_string(l.seq) + "\t" + std::to_string(l.time) + "\t" + l.origin + "\t" + l.model + "\t" + (l.good ? "1" : "0") + "\n";
    }
    for (const auto& w : l.written) {
        out += "w\t" + w.system + "\t" + std::to_string(w.seq) + "\t" + std::to_string(w.time) + "\n";
    }
    return out;
}

namespace detail {
inline auto Fields(std::string_view line) -> std::vector<std::string> {
    std::vector<std::string> out;
    while (true) {
        const auto tab = line.find('\t');
        out.emplace_back(line.substr(0, tab));
        if (tab == std::string_view::npos) {
            break;
        }
        line.remove_prefix(tab + 1);
    }
    return out;
}
inline auto Num(const std::string& s) -> std::int64_t {
    char* end = nullptr;
    const auto v = std::strtoll(s.c_str(), &end, 10);
    return end == s.c_str() ? 0 : v; // no exceptions on the console build
}
} // namespace detail

// a hand-edited or broken file loads what it can; a bad line is skipped
inline auto Parse(std::string_view text) -> Ledger {
    Ledger l;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto f = detail::Fields(line);
        if (f[0] == "cal" && f.size() >= 6) {
            l.seq = static_cast<std::uint64_t>(detail::Num(f[1]));
            l.time = detail::Num(f[2]);
            l.origin = f[3];
            l.model = f[4];
            l.good = f[5] == "1";
        } else if (f[0] == "w" && f.size() >= 4 && !f[1].empty()) {
            l.written.push_back({f[1], static_cast<std::uint64_t>(detail::Num(f[2])), detail::Num(f[3])});
        }
    }
    return l;
}

// a calibration was made in `origin`: a new sequence number; the origin holds it already
inline void RecordCalibration(Ledger& l, const std::string& origin, const std::string& model, bool good, std::int64_t time) {
    l.seq++;
    l.time = time;
    l.origin = origin;
    l.model = model;
    l.good = good;
    l.written.clear(); // the older numbers belong to the older calibration
    l.written.push_back({origin, l.seq, time});
}

inline auto WrittenSeq(const Ledger& l, const std::string& system) -> std::uint64_t {
    for (const auto& w : l.written) {
        if (w.system == system) {
            return w.seq;
        }
    }
    return 0;
}

inline void RecordWritten(Ledger& l, const std::string& system, std::int64_t time) {
    for (auto& w : l.written) {
        if (w.system == system) {
            w.seq = l.seq;
            w.time = time;
            return;
        }
    }
    l.written.push_back({system, l.seq, time});
}

enum class Decision {
    Apply,          // write the calibration into this system, tell the user
    NoCalibration,  // nothing was calibrated yet
    UpToDate,       // this system holds the newest one (also the origin itself)
    NotGood,        // the calibration did not pass: never spread it
    DifferentModel, // another battery model (CAL0): not for this battery
};

inline auto Decide(const Ledger& l, const std::string& system, const std::string& model_here) -> Decision {
    if (!l.seq) {
        return Decision::NoCalibration;
    }
    if (WrittenSeq(l, system) >= l.seq) {
        return Decision::UpToDate;
    }
    if (!l.good) {
        return Decision::NotGood;
    }
    if (l.model != model_here) {
        return Decision::DifferentModel;
    }
    return Decision::Apply;
}

} // namespace sphaira::battery::cutoff
