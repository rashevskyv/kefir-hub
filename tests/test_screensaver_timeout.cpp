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
    assert(!tracker.Update(true, false, 0, idle_input, 100.0));
    assert(!tracker.Update(true, false, -1, idle_input, 100.0));

    // 2. Non-installing state does not count or trigger auto-start
    tracker.Reset(0.0);
    assert(!tracker.Update(false, false, 30, idle_input, 40.0));

    // Entering installing state resets last activity time
    tracker.OnStateChange(true, 50.0);
    assert(!tracker.Update(true, false, 30, idle_input, 70.0)); // 20s idle < 30s
    assert(tracker.Update(true, false, 30, idle_input, 80.0));  // 30s idle >= 30s at deadline!

    // 3. Before vs at deadline
    tracker.Reset(100.0);
    assert(!tracker.Update(true, false, 60, idle_input, 159.9)); // before deadline
    assert(tracker.Update(true, false, 60, idle_input, 160.0));  // at deadline

    // 4. Activity resets deadline
    tracker.Reset(200.0);
    assert(!tracker.Update(true, false, 30, idle_input, 220.0)); // 20s idle
    assert(!tracker.Update(true, false, 30, active_input, 225.0)); // Activity at 225s
    assert(!tracker.Update(true, false, 30, idle_input, 250.0)); // 25s idle after activity
    assert(tracker.Update(true, false, 30, idle_input, 255.0));  // 30s idle after activity

    // Stick deadzone test
    tracker.Reset(300.0);
    TimeoutInput sub_deadzone{};
    sub_deadzone.stick_l_x = 3999;
    assert(!tracker.Update(true, false, 30, sub_deadzone, 320.0)); // 20s idle (sub-deadzone ignored)
    assert(tracker.Update(true, false, 30, idle_input, 330.0));   // 30s idle triggers

    tracker.Reset(400.0);
    TimeoutInput super_deadzone{};
    super_deadzone.stick_r_y = -4001;
    assert(!tracker.Update(true, false, 30, super_deadzone, 420.0)); // resets activity
    assert(!tracker.Update(true, false, 30, idle_input, 445.0));     // 25s idle
    assert(tracker.Update(true, false, 30, idle_input, 450.0));      // 30s idle triggers

    // 5. Already-active state resets activity time and returns false
    tracker.Reset(500.0);
    assert(!tracker.Update(true, true, 30, idle_input, 540.0)); // already active
    assert(!tracker.Update(true, false, 30, idle_input, 560.0)); // 20s idle < 30s after stopping
    assert(tracker.Update(true, false, 30, idle_input, 570.0));  // 30s idle triggers

    std::cout << "ok  screensaver_timeout: all checks passed\n";
    return 0;
}
