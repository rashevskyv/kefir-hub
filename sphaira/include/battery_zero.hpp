#pragma once

// Battery calibration (owner, 2026-11-09): the Hub writes only the zero, then watches.
//   The zero: the console is drained until it switches itself off (a new boot; a sleep is never a cut-off); the last voltage
//   the drain log saw is the real empty battery. It goes into V_empty (0x3A), bits 15..7 = VE in 10 mV, the threshold the chip
//   takes as empty; the counters go to 1 % only while the cell is still empty (CountersAtOnePercent).
//   The full: the charger chip ends the charge (ChargeDone). Nothing is written: the reset already set the factory capacity,
//   and the chip learns its own over the two cycles. The Hub only reads FullCap for the report.
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

struct Counters {
    bool ok{};
    std::uint16_t mix{}, rep{}, soc{}; // 0x0F, 0x05, 0x06
};

} // namespace sphaira::battery::zero
