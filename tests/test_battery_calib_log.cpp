// Host test for sphaira/include/battery_calib_log.hpp

#include "battery_calib_log.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace sphaira::battery::calib;

int main() {
    Sample a;
    a.time = 1000; a.kind = 'D'; a.psm_pct = 12; a.gauge_pct = 15; a.rem_mah = 480; a.full_mah = 3200;
    a.mv = 3410; a.ma = -950; a.temp_c = 31.5; a.load = true; a.brightness = 100; a.charger = false;
    const auto line = FormatLine(a);
    const auto back = ParseLine(line);
    assert(back && back->time == 1000 && back->kind == 'D' && back->psm_pct == 12 && back->gauge_pct == 15);
    assert(back->rem_mah == 480 && back->full_mah == 3200 && back->mv == 3410 && back->ma == -950);
    assert(back->load && back->brightness == 100 && !back->charger && back->temp_c > 31.4 && back->temp_c < 31.6);

    assert(!ParseLine(LOG_HEADER));                 // the header is not a sample
    assert(!ParseLine("1000,D,12,15,480"));          // a half-written line
    assert(!ParseLine("1000,X,12,15,480,3200,3410,-950,31.5,1,100,0")); // an unknown kind

    // X is the last drain sample's remaining charge; the tail may be charge lines or a broken line
    std::string log = LOG_HEADER;
    a.rem_mah = 480; a.time = 1000; log += FormatLine(a);
    a.rem_mah = 120; a.time = 1001; log += FormatLine(a);
    assert(LastDrainRemainingMah(log) == 120);
    Sample c = a;
    c.kind = 'C'; c.rem_mah = 900; c.time = 5000; c.charger = true;
    log += FormatLine(c);
    assert(LastDrainRemainingMah(log) == 120);       // a charge line after it does not count
    log += "5001,D,3,4,7";                           // cut off in the middle of a write
    assert(LastDrainRemainingMah(log) == 120);
    assert(LastDrainRemainingMah("") == -1);
    assert(LastDrainRemainingMah(LOG_HEADER) == -1);
    assert(LastDrainRemainingMah(FormatLine(c)) == -1);
    assert(LastSample(log) && LastSample(log)->time == 5000);

    std::puts("test_battery_calib_log: ok");
    return 0;
}
