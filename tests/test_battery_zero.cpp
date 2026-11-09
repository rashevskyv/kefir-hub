// Host test for sphaira/include/battery_zero.hpp

#include "battery_zero.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::zero;

int main() {
    // the console's own 0x3A: VE 3.20 V, VR 3.80 V
    assert(VEmptyMv(VE_DEFAULT) == 3200);
    assert((VE_DEFAULT & 0x7F) == 0x5F);

    // the cut-off voltage replaces VE, VR stays
    const auto v = VEmptyWith(VE_DEFAULT, 3193);
    assert(VEmptyMv(v) == 3190 && (v & 0x7F) == 0x5F);
    assert(VEmptyWith(0x0000, 3500) == (350u << 7));

    // a log value outside the battery's real range is not a zero
    assert(InRange(3193) && InRange(3000) && InRange(2990) && InRange(2800) && InRange(3400));
    assert(!InRange(2799) && !InRange(3401) && !InRange(3450) && !InRange(0));
    assert(CountersAtOnePercent(3180, 91) && CountersAtOnePercent(3700, 11)); // 2026-11-06: a wrong 91 % at the empty cell
    assert(!CountersAtOnePercent(4190, 100) && !CountersAtOnePercent(3701, 50)); // 2026-11-09: charged asleep, the % is right
    assert(!CountersAtOnePercent(3200, 10) && !CountersAtOnePercent(0, 91));   // near empty already; no voltage read

    // the full battery is the charger's word: bq24193 REG08 CHRG_STAT (bits 5-4) = 11, and a cell near the top
    assert(ChargeDone(0x30, 4180, 300) && ChargeDone(0xB4, 4100, 300));   // done; the other bits do not matter
    assert(!ChargeDone(0x20, 4200, 300) && !ChargeDone(0x10, 4200, 300) && !ChargeDone(0x00, 4200, 300)); // fast, pre-charge, not charging
    assert(!ChargeDone(0x30, 4099, 0));                                   // "done" on a low cell is not a full battery
    assert(!ChargeDone(0xFF, 4200, 0));                                   // a misread register
    assert(ChargeDone(0x00, 4172, 0) && ChargeDone(0x20, 4173, -2));      // 2026-11-06: HOS ended the charge, CHRG_STAT not 11
    assert(!ChargeDone(0x20, 4201, 147) && !ChargeDone(0x00, 4120, 0));   // still tapering; a cell not high enough

    std::puts("test_battery_zero: ok");
    return 0;
}
