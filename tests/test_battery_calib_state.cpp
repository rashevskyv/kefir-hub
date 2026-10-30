// Host test for sphaira/include/battery_calib_state.hpp

#include "battery_calib_state.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::calib;
using sphaira::battery::cutoff::Load;

int main() {
    // a calibration begins with the reset; the user's brightness is kept for later
    auto m = Begin(60, true, 500);
    assert(m.step == Step::Reset && !m.drain_started && m.brightness_before == 60 && m.auto_before && m.started_at == 500);
    assert(StepIndex(Step::Reset) == 1 && StepIndex(Step::Cycle1) == 2 && StepIndex(Step::Transfer) == 5 && StepIndex(Step::Finished) == 5);
    assert(CycleOf(Step::Cycle1) == 1 && CycleOf(Step::Cycle2) == 2 && CycleOf(Step::Report) == 0);
    assert(StepFor(m, false) == Step::Reset && StepFor(m, true) == Step::Cycle1);
    assert(!Sync(m, false) && Sync(m, true) && m.step == Step::Cycle1);
    assert(PhaseOf(m) == Phase::Drain);

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
    // the boot looks the same, but the log holds a cut-off of this drain and the charger is in: the console did switch off
    {
        auto c = m;
        assert(Check(c, {true, 5003, true}) == Found::CutoffCharging && !c.drain_started && c.cutoffs == 1);
        assert(Check(m, {false, 5003, true}) == Found::HubRestarted && m.cutoffs == 0); // charger out: the boot time decides
    }

    // the console cut off and was switched on WITHOUT the charger: the Hub goes on in the tail mode, by itself
    assert(Check(m, {false, 9000}) == Found::CutoffSeen && m.cutoffs == 1 && m.boot_time == 9000);
    assert(LoadFor(m) == Load::Min);
    assert(DecideBrightness(m, false, true, 100) == Brightness::Min);
    assert(Check(m, {false, 9005}) == Found::HubRestarted && m.cutoffs == 1);   // a Hub restart in the new boot
    assert(Check(m, {false, 12000}) == Found::CutoffSeen && m.cutoffs == 2);
    // cycle 1 stays in the drain phase until the zero is written, whatever the cut-offs
    assert(PhaseOf(m) == Phase::Drain);

    // the console would not start without the charger: it started WITH it: the drain is over, the brightness comes back
    m.last_set_brightness = 1;
    assert(DecideBrightness(m, true, false, 1) == Brightness::Restore);  // still what the Hub set: give the user's back
    assert(DecideBrightness(m, true, false, 55) == Brightness::Keep);    // he set his own meanwhile
    assert(Check(m, {true, 15000}) == Found::CutoffCharging && !m.drain_started && m.boot_time == 15000 && m.cutoffs == 3);
    assert(Check(m, {false, 20000}) == Found::Nothing);                  // no drain running: nothing to go on with

    // the zero is written: cycle 1 is in its charge phase; the step stays until the charge saw 100 %
    m.zero_mv = 3193;
    m.zero_at = 15100;
    assert(PhaseOf(m) == Phase::Charge);
    assert(!Sync(m, true) && m.step == Step::Cycle1);
    MarkCycleDone(m, 20000);
    assert(m.full_at[0] == 20000 && Sync(m, true) && m.step == Step::Cycle2 && m.cutoffs == 0 && !m.drain_started);
    assert(PhaseOf(m) == Phase::Drain);

    // cycle 2: a cut-off puts it in the charge phase; 100 % ends it, then the report
    StartDrain(m, 21000, 21000);
    assert(Check(m, {true, 30000}) == Found::CutoffCharging && PhaseOf(m) == Phase::Charge);
    MarkCycleDone(m, 40000);
    assert(Sync(m, true) && m.step == Step::Report);

    // the report and the transfer are the marker's own; a reset done again cannot take them back
    assert(StepFor(m, false) == Step::Report);
    ReportSeen(m);
    assert(m.step == Step::Transfer);
    TransferDone(m);
    assert(m.step == Step::Finished && Check(m, {true, 1}) == Found::Nothing);
    assert(StepFor(m, false) == Step::Finished);

    // the file: round trip and a broken one
    Marker a = Begin(45, false, 42);
    StartDrain(a, 888, 4242);
    a.cutoffs = 3;
    a.last_set_brightness = 1;
    a.step = Step::Cycle2;
    a.zero_mv = 3193;
    a.zero_at = 777;
    a.full_at[0] = 999;
    const auto back = Parse(Format(a));
    assert(back.step == Step::Cycle2 && back.drain_started && back.drain_time == 888 && back.started_at == 42);
    assert(back.cutoffs == 3 && back.brightness_before == 45 && !back.auto_before);
    assert(back.last_set_brightness == 1 && back.boot_time == 4242);
    {
        Marker c;
        c.cut_mv = 3195; // what the page saw before a sleep: survives a lost drain log
        assert(Parse(Format(c)).cut_mv == 3195 && Parse("cut_mv=-5\n").cut_mv == 0);
    }
    assert(back.zero_mv == 3193 && back.zero_at == 777 && back.full_at[0] == 999 && back.full_at[1] == 0);
    const auto bad = Parse("step=99\r\ncutoffs=-4\r\nnonsense\r\nphase=x\r\nstarted=1\r\n");
    assert(bad.step == Step::Reset && bad.cutoffs == 0 && bad.drain_started);
    assert(Parse("step=7\n").step == Step::Reset);  // a file from the short-lived six-step layout: start over

    std::puts("test_battery_calib_state: ok");
    return 0;
}
