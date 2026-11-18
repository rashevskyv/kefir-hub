#pragma once

// The timeline of the Kefir Updater guide (ui::UpdaterGuide): A on the old Kefir Updater tile shows where the
// updater is now. R is framed and pressed, the page slides to Tools, the Updater tile pulses, then it opens.
// Frames at 60 fps. Pure, so tests/test_updater_guide.cpp runs it on the host.

namespace sphaira::ui::updater_guide {

inline constexpr int PRESS_AT = 35;      // R looks pressed
inline constexpr int SWITCH_AT = 50;     // the page flips to Tools (MainMenu::Draw eases the slide, ~30 frames)
inline constexpr int PULSE_AT = 85;      // the slide has settled: the Updater tile pulses
inline constexpr int PULSE_FRAMES = 32;  // one ring
inline constexpr int PULSES = 3;
inline constexpr int TAP_AT = PULSE_AT + PULSE_FRAMES * PULSES; // the tile looks pressed
inline constexpr int END_AT = TAP_AT + 16;                      // the Updater opens

enum class Phase { PressR, Slide, Pulse, Done };

constexpr auto PhaseAt(int frame) -> Phase {
    if (frame < SWITCH_AT) {
        return Phase::PressR;
    }
    if (frame < PULSE_AT) {
        return Phase::Slide;
    }
    if (frame < END_AT) {
        return Phase::Pulse;
    }
    return Phase::Done;
}

// 0..1..0: how far a button is pushed in around `at` (6 frames down, 10 frames up).
constexpr auto PressDepth(int frame, int at) -> float {
    if (frame <= at - 6 || frame >= at + 10) {
        return 0.f;
    }
    if (frame <= at) {
        return float(frame - (at - 6)) / 6.f;
    }
    return float(at + 10 - frame) / 10.f;
}

// 0..1 inside the current ring of the pulse phase (the ring grows and fades); 0 outside the rings.
constexpr auto PulseT(int frame) -> float {
    if (frame < PULSE_AT || frame >= TAP_AT) {
        return 0.f;
    }
    return float((frame - PULSE_AT) % PULSE_FRAMES) / float(PULSE_FRAMES);
}

} // namespace sphaira::ui::updater_guide
