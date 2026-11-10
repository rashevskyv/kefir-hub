#pragma once

// Battery drain: how much load the Hub puts on the console (owner, 2026-11-10). Full load down to a real 10 %, then less and
// less: moderate at a real 1 %, nothing at the cut-off, so the console reaches its real empty at a small current.
// "Real" is the cell's own voltage, not the gauge: at the real 10 % the gauge showed 2 % in one drain and 38 % in the next
// (2026-11-09). No libnx: host-tested by tests/test_battery_drain_load.cpp.

#include <iterator>

namespace sphaira::battery::load {

// The cell's inner resistance: 81-85 mV per 680 mA when the load went off (2026-11-09). A voltage under load reads lower by
// current x resistance; with it added back the value does not jump when the load changes.
// ponytail: one console's cell, warm. A colder or older cell reads a few 0.1 % emptier, so the load only goes down earlier.
inline constexpr int CELL_MOHM = 125;

// the voltage the cell would show with no current; `ma` below 0 = out of the battery
inline auto RestMv(int mv, int ma) -> int {
    return ma < 0 ? mv - ma * CELL_MOHM / 1000 : mv;
}

// Rest voltage -> real charge in 0.1 %, by the mAh counted to the cut-off at 3.095 V (2026-11-09, 4500 mAh = 100 %)
struct Point {
    int mv, pct10;
};
inline constexpr Point CURVE[] = {{3186, 0}, {3285, 10}, {3374, 22}, {3427, 33}, {3461, 44}, {3486, 56}, {3520, 78}, {3549, 100}};

// 0 (the cut-off) .. 100 (10 % or more)
inline auto RealPct10(int rest_mv) -> int {
    if (rest_mv <= CURVE[0].mv) {
        return 0;
    }
    for (auto p = std::begin(CURVE) + 1; p != std::end(CURVE); p++) {
        if (rest_mv < p->mv) {
            return (p - 1)->pct10 + (p->pct10 - (p - 1)->pct10) * (rest_mv - (p - 1)->mv) / (p->mv - (p - 1)->mv);
        }
    }
    return 100;
}

// The load in per cent of the full one: 100 at 10 % and above, MODERATE at 1 %, 0 at the cut-off
inline constexpr int MODERATE = 30;

inline auto Level(int pct10) -> int {
    if (pct10 >= 100) {
        return 100;
    }
    if (pct10 >= 10) {
        return MODERATE + (100 - MODERATE) * (pct10 - 10) / 90;
    }
    return pct10 > 0 ? MODERATE * pct10 / 10 : 0;
}

} // namespace sphaira::battery::load
