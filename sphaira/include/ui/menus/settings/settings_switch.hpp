#pragma once

// Pure logic of the tri-state switch and the status lamp (drawn in settings_internal.cpp).

namespace sphaira::ui::menu::settings {

enum class Tri : int {
    Waiting = -1, // grey, knob in the middle
    Off = 0,      // red, knob on the left
    On = 1,       // green, knob on the right
};

// A bool setting always has a value, so it is never Waiting by itself. It is Waiting only when the
// owner of the row passes `pending` = true: no choice is stored yet, or a decision is still open.
constexpr auto TriFromBool(bool on, bool pending) -> Tri {
    return pending ? Tri::Waiting : (on ? Tri::On : Tri::Off);
}

// knob position 0 (left) .. 1 (right)
constexpr auto KnobTarget(Tri s) -> float {
    return s == Tri::On ? 1.f : (s == Tri::Off ? 0.f : 0.5f);
}

// one frame of the slide; reaches the target and never overshoots
constexpr auto KnobStep(float cur, float target, float step = 0.12f) -> float {
    if (cur < target) { return cur + step > target ? target : cur + step; }
    if (cur > target) { return cur - step < target ? target : cur - step; }
    return cur;
}

} // namespace sphaira::ui::menu::settings
