// Host test for sphaira/include/battery_calib_state.hpp

#include "battery_calib_state.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::calib;
using sphaira::battery::cutoff::Load;

int main() {
    // a calibration begins with the reset; the user's brightness is kept for later
    auto m = Begin(60, true);
    assert(m.step == Step::Reset && !m.drain_started && m.brightness_before == 60 && m.auto_before);

    // the steps follow the wizard's state: reset, cycle 1, cycle 2, report
    assert(StepFor(m, false, false, false) == Step::Reset);
    assert(StepFor(m, true, false, false) == Step::Cycle1);
    assert(StepFor(m, true, true, false) == Step::Cycle2);
    assert(StepFor(m, true, true, true) == Step::Report);
    assert(StepFor(m, false, true, true) == Step::Reset);          // a reset done again takes the cycles back
    assert(Sync(m, true, false, false) && m.step == Step::Cycle1);
    assert(!Sync(m, true, false, false));                          // nothing moved
    m.cutoffs = 2;
    m.drain_started = true;
    assert(Sync(m, true, true, false) && m.step == Step::Cycle2 && m.cutoffs == 0 && !m.drain_started); // a new cycle starts at full load

    // before the first cut-off: maximum load and brightness; the Hub sets the brightness itself
    StartDrain(m, 1010, 5000);
    m.last_set_brightness = 100;
    assert(LoadFor(m) == Load::Max);
    assert(DecideBrightness(m, false, true, 100) == Brightness::Max);
    assert(DecideBrightness(m, false, true, 40) == Brightness::Keep);    // the user turned it down himself: leave it

    // the Hub starts again in the SAME boot (it was left or updated): the drain goes on as before, no cut-off is counted
    assert(Check(m, {false, 5003}) == Found::HubRestarted && m.cutoffs == 0 && LoadFor(m) == Load::Max);
    assert(Check(m, {true, 5003}) == Found::Nothing && m.drain_started);   // the charger was plugged into a running drain
    assert(Check(m, {false, 0}) == Found::HubRestarted);                   // a boot time that is not known never counts as a cut-off

    // the console cut off and was switched on WITHOUT the charger: the Hub goes on in the tail mode, by itself
    assert(Check(m, {false, 9000}) == Found::CutoffSeen && m.cutoffs == 1 && m.boot_time == 9000);
    assert(LoadFor(m) == Load::Min);
    assert(DecideBrightness(m, false, true, 100) == Brightness::Min);
    assert(DecideBrightness(m, false, true, 3) == Brightness::Min);      // the tail lowers it even if he had lowered it
    assert(Check(m, {false, 9005}) == Found::HubRestarted && m.cutoffs == 1);   // a Hub restart in the new boot
    assert(Check(m, {false, 12000}) == Found::CutoffSeen && m.cutoffs == 2);

    // the console would not start without the charger: it started WITH it: the drain is over, the brightness comes back
    m.last_set_brightness = 1;
    assert(DecideBrightness(m, true, false, 1) == Brightness::Restore);  // still what the Hub set: give the user's back
    assert(DecideBrightness(m, true, false, 55) == Brightness::Keep);    // he set his own meanwhile
    assert(Check(m, {true, 15000}) == Found::Nothing && !m.drain_started && m.boot_time == 15000);
    assert(Check(m, {false, 20000}) == Found::Nothing);                  // no drain running: nothing to go on with

    // the report and the transfer are the marker's own; the cycles cannot take them back
    m.step = Step::Report;
    assert(StepFor(m, false, false, false) == Step::Report);
    ReportSeen(m);
    assert(m.step == Step::Transfer);
    TransferDone(m);
    assert(m.step == Step::Finished && Check(m, {true, 1}) == Found::Nothing);
    assert(StepFor(m, false, false, false) == Step::Finished);

    // the file: round trip and a broken one
    Marker a = Begin(45, false);
    StartDrain(a, 888, 4242);
    a.cutoffs = 3;
    a.last_set_brightness = 1;
    a.step = Step::Cycle2;
    const auto back = Parse(Format(a));
    assert(back.step == Step::Cycle2 && back.drain_started && back.drain_time == 888);
    assert(back.cutoffs == 3 && back.brightness_before == 45 && !back.auto_before);
    assert(back.last_set_brightness == 1 && back.boot_time == 4242);
    const auto bad = Parse("step=99\r\ncutoffs=-4\r\nnonsense\r\nphase=x\r\nstarted=1\r\n");
    assert(bad.step == Step::Reset && bad.cutoffs == 0 && bad.drain_started);

    std::puts("test_battery_calib_state: ok");
    return 0;
}
