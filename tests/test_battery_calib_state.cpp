// Host test for sphaira/include/battery_calib_state.hpp

#include "battery_calib_state.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::calib;
using sphaira::battery::cutoff::Load;

int main() {
    // a calibration begins in step 1, draining; the user's brightness is kept for later
    auto m = Begin(60, true, 1000);
    assert(m.step == Step::Drain && m.phase == Phase::Drain && !m.phase_started);
    assert(m.brightness_before == 60 && m.auto_before);

    // before the first cut-off: maximum load and brightness; the Hub sets the brightness itself
    StartPhase(m, 80, 1010, 5000);
    m.last_set_brightness = 100;
    assert(LoadFor(m) == Load::Max);
    assert(DecideBrightness(m, false, true, 100) == Brightness::Max);
    assert(DecideBrightness(m, false, true, 40) == Brightness::Keep);    // the user turned it down himself: leave it

    // the console cut off and was switched on WITHOUT the charger: the Hub goes on in the tail mode, by itself
    // the Hub starts again in the SAME boot (it was left or updated): the drain goes on as before, no cut-off is counted
    assert(Check(m, {80, false, 5003}) == Found::HubRestarted && m.cutoffs == 0 && LoadFor(m) == Load::Max);
    assert(Check(m, {80, true, 5003}) == Found::Nothing && m.step == Step::Drain); // the charger was plugged in a running drain
    // a boot time that is not known never counts as a cut-off
    assert(Check(m, {80, false, 0}) == Found::HubRestarted);
    assert(Check(m, {35, false, 9000}) == Found::CutoffSeen && m.cutoffs == 1 && m.boot_time == 9000);
    assert(LoadFor(m) == Load::Min);
    assert(DecideBrightness(m, false, true, 100) == Brightness::Min);
    assert(DecideBrightness(m, false, true, 3) == Brightness::Min);      // the tail lowers it even if he had lowered it
    assert(Check(m, {20, false, 9005}) == Found::HubRestarted && m.cutoffs == 1);   // a Hub restart in the new boot
    assert(Check(m, {20, false, 12000}) == Found::CutoffSeen && m.cutoffs == 2);

    // the console would not start without the charger: it started WITH it -> the zero comes next, brightness comes back at once
    m.last_set_brightness = 1;
    assert(DecideBrightness(m, true, false, 1) == Brightness::Restore);  // still what the Hub set: give the user's back
    assert(DecideBrightness(m, true, false, 55) == Brightness::Keep);    // he set his own meanwhile
    assert(Check(m, {3, true, 15000}) == Found::DeadWithCharger && m.step == Step::Zero);
    assert(Check(m, {3, true, 15000}) == Found::Nothing);                       // the zero step waits for the Hub itself

    // the zero is written: cycle 1, charge phase first, the cut-off counter starts again
    ZeroDone(m);
    assert(m.step == Step::Cycle1 && m.phase == Phase::Charge && !m.phase_started && m.cutoffs == 0);
    assert(Check(m, {50, true, 15000}) == Found::Nothing);               // the charge was not started yet

    // the charge: start, the user plays, comes back
    StartPhase(m, 3, 2000);
    assert(Check(m, {60, true, 15000}) == Found::ChargeInProgress && m.phase_started);
    assert(Check(m, {100, true, 15000}) == Found::ChargeDone && m.phase == Phase::Drain && !m.phase_started);

    // a charge that lost its charger is not counted and must be started again
    ZeroDone(m);
    StartPhase(m, 3, 3000);
    assert(Check(m, {70, false, 15000}) == Found::ChargeInterrupted && !m.phase_started && m.phase == Phase::Charge);

    // the drain phase of a cycle: self-drain; the user switches on with the charger in and a low battery -> the Hub asks
    m = Begin(60, true, 4000);
    m.step = Step::Cycle1;
    m.phase = Phase::Drain;
    StartPhase(m, 100, 4000, 7000);
    assert(Check(m, {9, true, 7001}) == Found::Nothing);                   // the same boot: only the Hub restarted
    assert(Check(m, {9, true, 20000}) == Found::AskDrainFinished);       // the console started again, low, with the charger
    assert(Check(m, {70, true, 20000}) == Found::Nothing);                      // a high battery with the charger: no question
    DrainConfirmed(m);
    assert(m.step == Step::Cycle2 && m.phase == Phase::Charge && m.cutoffs == 0);
    m.phase = Phase::Drain;
    DrainConfirmed(m);
    assert(m.step == Step::Report);
    ReportSeen(m);
    assert(m.step == Step::Transfer);
    TransferDone(m);
    assert(m.step == Step::Finished && Check(m, {50, true}) == Found::Nothing);

    // the file: round trip and a broken one
    Marker a = Begin(45, false, 777);
    StartPhase(a, 12, 888, 4242);
    a.cutoffs = 3;
    a.last_set_brightness = 1;
    a.last_logged_remaining_mah = 4321;
    a.step = Step::Cycle2;
    a.phase = Phase::Charge;
    const auto back = Parse(Format(a));
    assert(back.step == Step::Cycle2 && back.phase == Phase::Charge && back.phase_started && back.phase_time == 888);
    assert(back.phase_percent == 12 && back.cutoffs == 3 && back.brightness_before == 45 && !back.auto_before);
    assert(back.last_set_brightness == 1 && back.last_logged_remaining_mah == 4321 && back.boot_time == 4242);
    const auto bad = Parse("step=99\r\ncutoffs=-4\r\nnonsense\r\nphase=x\r\nstarted=1\r\n");
    assert(bad.step == Step::Drain && bad.cutoffs == 0 && bad.phase_started);

    std::puts("test_battery_calib_state: ok");
    return 0;
}
