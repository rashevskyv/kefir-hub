// Host test for sphaira/include/battery_drain_load.hpp

#include "battery_drain_load.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::load;

int main() {
    // the load does not move the rest voltage: 3546 mV at 1307 mA and 3628 mV at 650 mA are the same cell
    assert(RestMv(3546, -1307) == 3709 && RestMv(3628, -650) == 3709);
    assert(RestMv(4100, 2000) == 4100); // a charge is not a load

    // the drain of 2026-11-09: 450 mAh left at 3469 mV / 645 mA, 45 mAh left at 3200 mV / 687 mA, off at 3098 mV / 706 mA
    assert(RealPct10(RestMv(3469, -645)) == 100);
    assert(RealPct10(RestMv(3200, -687)) == 10);
    assert(RealPct10(RestMv(3098, -706)) == 0 && RealPct10(0) == 0);
    assert(RealPct10(RestMv(3405, -652)) >= 50 && RealPct10(RestMv(3405, -652)) <= 60); // 250 mAh left = 5.6 %

    // the old rule took the load off at 3.5 V under full load: 22 % real (2026-11-09). Now it is still the full load there,
    // and at 3.3 V under full load (about 4 %) about half of it
    assert(Level(RealPct10(RestMv(3500, -1300))) == 100);
    assert(Level(RealPct10(RestMv(3300, -1300))) > MODERATE && Level(RealPct10(RestMv(3300, -1300))) < 70);

    // full at 10 %, moderate at 1 %, nothing at the cut-off, never up as the cell goes down
    assert(Level(100) == 100 && Level(250) == 100 && Level(10) == MODERATE && Level(0) == 0 && Level(-5) == 0);
    assert(Level(55) > MODERATE && Level(55) < 100 && Level(5) == MODERATE / 2);
    for (int mv = 3100, last = 0; mv <= 3700; mv++) {
        const int level = Level(RealPct10(mv));
        assert(level >= last && level <= 100);
        last = level;
    }

    std::puts("test_battery_drain_load: ok");
    return 0;
}
