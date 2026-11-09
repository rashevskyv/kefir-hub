#pragma once

// The one frame every screensaver view is drawn in: a rectangle around a drifting centre that never leaves the panel.
// libnx-free, so tests/test_saver_frame.cpp runs it on the host.

#include <algorithm>

namespace sphaira::ui {

// how far the view reaches from its centre: half_w to each side, `up` above, `down` below.
struct SaverExtent {
    float half_w{};
    float up{};
    float down{};
};

struct SaverRect {
    float x{}, y{}, w{}, h{};
};

inline constexpr float SAVER_MARGIN = 10.f;

// Moves (cx, cy) so the whole rectangle stays on a screen_w x screen_h panel with SAVER_MARGIN on every side.
// What the edge takes off is taken off the stick offset too, so pushing into an edge winds nothing up.
inline auto FitSaverFrame(SaverExtent e, float screen_w, float screen_h, float& cx, float& cy, float& off_x, float& off_y) -> SaverRect {
    const float fx = std::clamp(cx, SAVER_MARGIN + e.half_w, std::max(SAVER_MARGIN + e.half_w, screen_w - SAVER_MARGIN - e.half_w));
    const float fy = std::clamp(cy, SAVER_MARGIN + e.up, std::max(SAVER_MARGIN + e.up, screen_h - SAVER_MARGIN - e.down));
    off_x += fx - cx;
    off_y += fy - cy;
    cx = fx;
    cy = fy;
    return {cx - e.half_w, cy - e.up, e.half_w * 2.f, e.up + e.down};
}

} // namespace sphaira::ui
