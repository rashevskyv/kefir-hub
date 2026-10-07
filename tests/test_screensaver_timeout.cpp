#include "ui/screensaver_timeout.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace sphaira::ui;

    InactivityTracker tracker{};
    TimeoutInput idle_input{};
    TimeoutInput active_input{};
    active_input.kdown = 1;

    // 1. Timeout 0 (Off) never auto-starts
    tracker.Reset(0.0);
    assert(!tracker.Update(BlankPhase_Installing, false, 0, idle_input, 100.0));
    assert(!tracker.Update(BlankPhase_Installing, false, -1, idle_input, 100.0));

    // 2. Non-installing state does not count or trigger auto-start
    tracker.Reset(0.0);
    assert(!tracker.Update(BlankPhase_None, false, 30, idle_input, 40.0));

    // Entering installing state resets last activity time
    tracker.OnStateChange(BlankPhase_Installing, 50.0);
    assert(!tracker.Update(BlankPhase_Installing, false, 30, idle_input, 70.0)); // 20s idle < 30s
    assert(tracker.Update(BlankPhase_Installing, false, 30, idle_input, 80.0));  // 30s idle >= 30s at deadline!

    // 3. Before vs at deadline
    tracker.Reset(100.0);
    assert(!tracker.Update(BlankPhase_Installing, false, 60, idle_input, 159.9)); // before deadline
    assert(tracker.Update(BlankPhase_Installing, false, 60, idle_input, 160.0));  // at deadline

    // 4. Activity resets deadline
    tracker.Reset(200.0);
    assert(!tracker.Update(BlankPhase_Installing, false, 30, idle_input, 220.0)); // 20s idle
    assert(!tracker.Update(BlankPhase_Installing, false, 30, active_input, 225.0)); // Activity at 225s
    assert(!tracker.Update(BlankPhase_Installing, false, 30, idle_input, 250.0)); // 25s idle after activity
    assert(tracker.Update(BlankPhase_Installing, false, 30, idle_input, 255.0));  // 30s idle after activity

    // Stick deadzone test
    tracker.Reset(300.0);
    TimeoutInput sub_deadzone{};
    sub_deadzone.stick_l_x = 3999;
    assert(!tracker.Update(BlankPhase_Installing, false, 30, sub_deadzone, 320.0)); // 20s idle (sub-deadzone ignored)
    assert(tracker.Update(BlankPhase_Installing, false, 30, idle_input, 330.0));   // 30s idle triggers

    tracker.Reset(400.0);
    TimeoutInput super_deadzone{};
    super_deadzone.stick_r_y = -4001;
    assert(!tracker.Update(BlankPhase_Installing, false, 30, super_deadzone, 420.0)); // resets activity
    assert(!tracker.Update(BlankPhase_Installing, false, 30, idle_input, 445.0));     // 25s idle
    assert(tracker.Update(BlankPhase_Installing, false, 30, idle_input, 450.0));      // 30s idle triggers

    // 5. Already-active state resets activity time and returns false
    tracker.Reset(500.0);
    assert(!tracker.Update(BlankPhase_Installing, true, 30, idle_input, 540.0)); // already active
    assert(!tracker.Update(BlankPhase_Installing, false, 30, idle_input, 560.0)); // 20s idle < 30s after stopping
    assert(tracker.Update(BlankPhase_Installing, false, 30, idle_input, 570.0));  // 30s idle triggers

    // 6. Queue finished: a fresh clock starts at the transition, however long the install sat idle
    tracker.Reset(0.0);
    assert(!tracker.Update(BlankPhase_Installing, false, 0, idle_input, 600.0)); // timeout Off during install
    assert(!tracker.Update(BlankPhase_Finished, false, kFinishedBlankTimeoutSec, idle_input, 600.0)); // transition
    assert(!tracker.Update(BlankPhase_Finished, false, kFinishedBlankTimeoutSec, idle_input, 659.9));
    assert(tracker.Update(BlankPhase_Finished, false, kFinishedBlankTimeoutSec, idle_input, 660.0));

    // 7. Input restarts the finished clock; a new install replaces it with the install rule
    tracker.Reset(700.0);
    assert(!tracker.Update(BlankPhase_Finished, false, kFinishedBlankTimeoutSec, active_input, 730.0));
    assert(!tracker.Update(BlankPhase_Finished, false, kFinishedBlankTimeoutSec, idle_input, 789.9));
    assert(!tracker.Update(BlankPhase_Installing, false, 0, idle_input, 790.0)); // new install, timeout Off
    assert(!tracker.Update(BlankPhase_Installing, false, 0, idle_input, 900.0));
    assert(!tracker.Update(BlankPhase_Finished, false, kFinishedBlankTimeoutSec, idle_input, 900.0)); // finished again
    assert(tracker.Update(BlankPhase_Finished, false, kFinishedBlankTimeoutSec, idle_input, 960.0));

    std::cout << "ok  screensaver_timeout: all checks passed\n";
    return 0;
}
