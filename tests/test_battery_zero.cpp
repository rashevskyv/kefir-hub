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
    assert(InRange(3193) && InRange(3000) && InRange(3500));
    assert(!InRange(2999) && !InRange(3501) && !InRange(0));

    // switched on without the charger: the counters say 0
    CountersIn in{0x2310, 0x2304, 0x1000, 0x1000};
    auto c = PlanCounters(in);
    assert(c.ok && c.mix == 0 && c.rep == 0 && c.soc == 0);

    // the console drained a little more since the start (QH went down): still 0, never negative
    in.qh_now = 0x0FF0;
    c = PlanCounters(in);
    assert(c.ok && c.mix == 0 && c.soc == 0);

    // switched on with the charger: 100 mAh (200 LSB at 0.5 mAh) came in -> about 2 % of 4488 mAh
    in.qh_now = 0x1000 + 200;
    c = PlanCounters(in);
    assert(c.ok && c.mix == 200 && c.rep == 200 * 0x2310 / 0x2304);
    assert(c.soc >= (2u << 8) && c.soc < (3u << 8));

    // QH wrapped around (it is a 16-bit counter): the difference still counts
    in.qh_at_start = 0xFFF0;
    in.qh_now = 0x0010;
    assert(PlanCounters(in).mix == 0x20);

    // more charge than a full battery cannot be: capped
    in.qh_at_start = 0;
    in.qh_now = 0x7FFF;
    assert(PlanCounters(in).mix == 0x2304);

    assert(!PlanCounters({0, 0x2304, 0, 0}).ok);

    std::puts("test_battery_zero: ok");
    return 0;
}
