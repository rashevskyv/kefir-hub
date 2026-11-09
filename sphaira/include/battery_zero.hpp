#pragma once

// Battery calibration, cycle 1 (owner, 2026-11-05): two facts, nothing counted.
//   The zero: the console is drained until it switches itself off; the last voltage the drain log saw is the real empty battery.
//   It goes into V_empty (0x3A), bits 15..7 = VE in 10 mV, the threshold the chip takes as empty. The charge counters stay as
//   the chip has them: a QH difference written there taught the chip 7066 mAh against a 4667 mAh design (owner's console).
//   The full: the charger chip ends the charge (bq24193 CHRG_STAT = done). FullCap and FullCapNom get the factory capacity of
//   this console's battery (never more), the counters 100 %. Cycle 2 writes nothing: the chip learns from these two points.
// No libnx: host-tested by tests/test_battery_zero.cpp.

#include <cstdint>
#include <optional>

namespace sphaira::battery::zero {

inline constexpr int MIN_MV = 2800; // a cut-off voltage outside this range is a log error, not a zero; with the load off below 3.5 V the console runs
                                    // down to about 3.0 V and the first cut-off of 2026-11-08 fell under 3.00 V
inline constexpr int MAX_MV = 3400; // the real cut-offs: 3.19-3.21 V; a page left by HOME at 3.45 V must not become the zero
inline constexpr std::uint16_t VE_DEFAULT = 0xA05F; // 3.20 V / 3.80 V, the console's own value (BDF's X sequence verifies it)

inline auto InRange(int mv) -> bool {
    return mv >= MIN_MV && mv <= MAX_MV;
}

// 0x3A with VE replaced by mv (10 mV steps, bits 15..7); VR (bits 6..0, the recovery voltage) stays what it was
inline auto VEmptyWith(std::uint16_t old, int mv) -> std::uint16_t {
    const auto ve = static_cast<std::uint16_t>((mv / 10) & 0x1FF);
    return static_cast<std::uint16_t>((ve << 7) | (old & 0x7F));
}

inline auto VEmptyMv(std::uint16_t reg) -> int {
    return (reg >> 7) * 10;
}

// The counters at 1 % go in only while the cell is still empty: a chip far from 0 % at a low cell (91 % at 3.18 V, 2026-11-06)
// is wrong. A cell above this voltage was charged since the cut-off (2026-11-09: 100 % at 4.19 V after a night asleep on USB):
// its % is right, and 1 % would be the wrong value. A chip already near empty keeps its own % too.
inline constexpr int COUNTERS_MAX_MV = 3700;
inline constexpr unsigned NEAR_EMPTY_PCT = 10;

inline auto CountersAtOnePercent(int mv_now, unsigned chip_pct) -> bool {
    return mv_now > 0 && mv_now <= COUNTERS_MAX_MV && chip_pct > NEAR_EMPTY_PCT;
}

// The full battery is the charger's word, not a voltage guess: bq24193 REG08 bits 5-4 (CHRG_STAT) = 11, charge termination done.
// A cell above 4.1 V too, so a misread register (0xFF) is not taken for it.
inline constexpr int DONE_MIN_MV = 4100;

// HOS does not leave CHRG_STAT at 11 (2026-11-06: 4.2 V, 0 mA, the gauge at 100 % for 11 hours, never "done"): a high cell that
// takes no current with the charger in is full too
inline constexpr int TAPER_MIN_MV = 4150;
inline constexpr int TAPER_MA = 50;

inline auto ChargeDone(std::uint8_t reg08, int mv, int avg_ma) -> bool {
    if (reg08 == 0xFF) {
        return false;
    }
    return (((reg08 >> 4) & 0x3) == 0x3 && mv >= DONE_MIN_MV) || (mv >= TAPER_MIN_MV && avg_ma < TAPER_MA && avg_ma > -TAPER_MA);
}

struct CountersIn {
    std::uint16_t full_cap{};     // 0x10
    std::uint16_t full_cap_nom{}; // 0x23
    std::uint16_t qh_at_start{};  // 0x4D at the cut-off: the last value the drain page saw on battery (else when the Hub started)
    std::uint16_t qh_now{};       // 0x4D now: the difference is what the charger put in since
    bool full{};                  // the counters say 100 %
};
struct Counters {
    bool ok{};
    std::uint16_t mix{}, rep{}, soc{}; // 0x0F, 0x05, 0x06
};

// the charge since the cut-off, as the three counters; two's complement QH so a discharge since then reads as 0
inline auto PlanCounters(const CountersIn& in) -> Counters {
    if (!in.full_cap || !in.full_cap_nom) {
        return {};
    }
    if (in.full) {
        return {true, in.full_cap_nom, in.full_cap, 0x6400};
    }
    const auto delta = static_cast<std::int16_t>(in.qh_now - in.qh_at_start);
    const std::uint32_t charged = delta > 0 ? static_cast<std::uint32_t>(delta) : 0u;
    Counters c;
    c.mix = static_cast<std::uint16_t>(charged < in.full_cap_nom ? charged : in.full_cap_nom);
    c.rep = static_cast<std::uint16_t>(static_cast<std::uint32_t>(c.mix) * in.full_cap / in.full_cap_nom);
    c.soc = static_cast<std::uint16_t>(static_cast<std::uint32_t>(c.rep) * 0x6400 / in.full_cap); // 1/256 %
    c.ok = true;
    return c;
}

} // namespace sphaira::battery::zero
