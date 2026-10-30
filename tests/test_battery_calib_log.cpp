// Host test for sphaira/include/battery_calib_log.hpp

#include "battery_calib_log.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace sphaira::battery::calib;

int main() {
    Sample a;
    a.time = 1000; a.kind = 'D'; a.step = 3; a.psm_pct = 12; a.gauge_pct = 15; a.rem_mah = 480; a.full_mah = 3200;
    a.mv = 3410; a.ma = -950; a.temp_c = 31.5; a.load = true; a.brightness = 100; a.charger = false;
    const auto line = FormatLine(a);
    const auto back = ParseLine(line);
    assert(back && back->time == 1000 && back->kind == 'D' && back->step == 3 && back->psm_pct == 12 && back->gauge_pct == 15);
    assert(back->rem_mah == 480 && back->full_mah == 3200 && back->mv == 3410 && back->ma == -950);
    assert(back->load && back->brightness == 100 && !back->charger && back->temp_c > 31.4 && back->temp_c < 31.6);

    assert(!ParseLine(LOG_HEADER));                 // the header is not a sample
    assert(!ParseLine("1000,D,3,12,15,480"));          // a half-written line
    assert(!ParseLine("1000,X,3,12,15,480,3200,3410,-950,31.5,1,100,0")); // an unknown kind
    assert(!ParseLine("1000,S,1,3,2,-1,-1,3380,0,0.0,0,0,0"));            // the sleep flag is not a sample

    std::string log = LOG_HEADER;
    a.rem_mah = 480; a.time = 1000; log += FormatLine(a);
    a.rem_mah = 120; a.time = 1001; log += FormatLine(a);
    Sample c = a;
    c.kind = 'C'; c.rem_mah = 900; c.time = 5000; c.charger = true;
    log += FormatLine(c);
    log += "5001,D,1,3,4,7";                           // cut off in the middle of a write
    assert(LastSample(log) && LastSample(log)->time == 5000);

    // cut-offs: a drain line with a low voltage before a gap, a charge line, or the end of the log
    std::string run = LOG_HEADER;
    Sample d;
    d.kind = 'D'; d.step = 1; d.mv = 3900; d.gauge_pct = 80; d.rem_mah = 2500;
    for (int t = 0; t < 5; t++) { d.time = 100 + t; run += FormatLine(d); }          // a drain page that was left at 3.9 V: no cut-off
    d.time = 2000; d.mv = 3300; d.gauge_pct = 35; run += FormatLine(d);                 // the first run goes on...
    d.time = 2001; d.mv = 3220; d.gauge_pct = 31; run += FormatLine(d);                 // ...and the console goes off after this line
    d.time = 2100; d.mv = 3400; d.gauge_pct = 2; run += FormatLine(d);                  // tail mode, a minute later
    d.time = 2101; d.mv = 3205; d.gauge_pct = 1; run += FormatLine(d);                  // the second cut-off
    Sample ch = d;
    ch.kind = 'C'; ch.step = 1; ch.time = 9000; ch.mv = 3700; run += FormatLine(ch);   // the charge afterwards
    const auto cuts = Cutoffs(run);
    assert(cuts.size() == 2);
    assert(cuts[0].mv == 3220 && cuts[0].gauge_pct == 31 && cuts[0].step == 1);
    assert(cuts[1].mv == 3205 && cuts[1].gauge_pct == 1 && cuts[1].time == 2101);
    assert(Cutoffs(LOG_HEADER).empty());

    // a cycle is a drain to the cut-off and then a charge to 100 %; the log alone tells when it is done
    // the zero of the cycle: its last cut-off (the tail mode's one, the lowest)
    assert(ZeroCutoff(run, 1) && ZeroCutoff(run, 1)->mv == 3205 && ZeroCutoff(run, 1)->time == 2101);
    assert(!ZeroCutoff(run, 1, 3000));                     // a cycle that began later has no cut-off yet
    assert(!CycleDoneByLog(run, 1));                       // charged to 3.7 V only, never 100 %
    ch.time = 9500; ch.psm_pct = 99; run += FormatLine(ch);
    assert(!CycleDoneByLog(run, 1));
    ch.time = 9600; ch.psm_pct = 100; run += FormatLine(ch);
    const auto done1 = CycleDoneByLog(run, 1);
    assert(done1 && done1->cutoff_mv == 3205 && done1->cutoff_time == 2101 && done1->full_time == 9600);
    assert(!CycleDoneByLog(run, 1, 3000));                 // the cycle began after that cut-off: not done
    assert(!CycleDoneByLog(run, 2));                       // cycle 2 has no cut-off yet
    // a 100 % line of cycle 2 BEFORE its cut-off (the charge of cycle 1 logged under step 2) does not count
    Sample c2 = ch; c2.step = 2; c2.time = 9700; run += FormatLine(c2);
    assert(!CycleDoneByLog(run, 2));
    d.step = 2; d.time = 12000; d.mv = 3210; run += FormatLine(d);
    c2.time = 20000; run += FormatLine(c2);
    assert(CycleDoneByLog(run, 2) && CycleDoneByLog(run, 2)->full_time == 20000);

    std::puts("test_battery_calib_log: ok");
    return 0;
}
