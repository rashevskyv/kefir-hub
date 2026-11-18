// Host test for sphaira/include/ui/updater_guide_timeline.hpp
//
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_updater_guide.cpp -o /tmp/t && /tmp/t

#include "ui/updater_guide_timeline.hpp"

#include <cstdio>

using namespace sphaira::ui::updater_guide;

static int g_checks = 0;
#define CHECK(expr)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(expr)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
            return 1;                                                   \
        }                                                               \
    } while (0)

int main() {
    // phases come in order and the guide ends
    CHECK(PhaseAt(0) == Phase::PressR);
    CHECK(PhaseAt(PRESS_AT) == Phase::PressR);
    CHECK(PhaseAt(SWITCH_AT) == Phase::Slide);
    CHECK(PhaseAt(PULSE_AT - 1) == Phase::Slide);
    CHECK(PhaseAt(PULSE_AT) == Phase::Pulse);
    CHECK(PhaseAt(END_AT - 1) == Phase::Pulse);
    CHECK(PhaseAt(END_AT) == Phase::Done);
    CHECK(PRESS_AT < SWITCH_AT && SWITCH_AT < PULSE_AT && PULSE_AT < TAP_AT && TAP_AT < END_AT);
    // the press is released before the page flips, and the tap before the Updater opens
    CHECK(PressDepth(SWITCH_AT, PRESS_AT) == 0.f);
    CHECK(PressDepth(END_AT, TAP_AT) == 0.f);
    CHECK(END_AT - TAP_AT >= 10);

    // press: 0 -> 1 at the press frame -> 0
    CHECK(PressDepth(PRESS_AT - 6, PRESS_AT) == 0.f);
    CHECK(PressDepth(PRESS_AT - 3, PRESS_AT) == 0.5f);
    CHECK(PressDepth(PRESS_AT, PRESS_AT) == 1.f);
    CHECK(PressDepth(PRESS_AT + 5, PRESS_AT) == 0.5f);
    CHECK(PressDepth(PRESS_AT + 10, PRESS_AT) == 0.f);

    // rings: start at 0, grow under 1, restart each PULSE_FRAMES, none after the last
    CHECK(PulseT(PULSE_AT - 1) == 0.f);
    CHECK(PulseT(PULSE_AT) == 0.f);
    CHECK(PulseT(PULSE_AT + PULSE_FRAMES / 2) == 0.5f);
    CHECK(PulseT(PULSE_AT + PULSE_FRAMES - 1) < 1.f);
    CHECK(PulseT(PULSE_AT + PULSE_FRAMES) == 0.f);
    CHECK(PulseT(TAP_AT) == 0.f);

    std::printf("test_updater_guide: %d checks ok\n", g_checks);
    return 0;
}
