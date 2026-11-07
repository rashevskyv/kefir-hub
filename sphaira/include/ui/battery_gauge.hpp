#pragma once

#include "ui/nvg_util.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

// The big battery of the calibration (the drain page and the screensaver). The level has a wavy edge that ripples up
// while the battery charges and down while it drains; `dir` is +1, -1 or 0 (idle, no ripple, no arrow).
namespace sphaira::ui::gfx {

inline void drawBatteryLevel(NVGcontext* vg, float x, float y, float w, float h, float r, float pct, NVGcolor col, int dir) {
    if (pct <= 0.f) {
        return;
    }
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float edge = x + w * std::clamp(pct, 0.f, 1.f);
    // the waves die out at the empty and the full end, so a full battery is a clean block
    const float amp = dir == 0 ? 0.f : std::min(h * 0.05f, w * 0.5f * std::min(pct, 1.f - pct));
    const float phase = static_cast<float>(std::fmod(t * 2.5, 6.2831853)) * (dir > 0 ? 1.f : -1.f); // the ripple runs up the edge while charging
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + r, y);
    for (float yy = 0.f; yy <= h; yy += 3.f) {
        nvgLineTo(vg, std::clamp(edge + amp * std::sin(yy / h * 12.566f + phase), x + r, x + w), y + yy);
    }
    nvgLineTo(vg, x + r, y + h);
    nvgArcTo(vg, x, y + h, x, y + h - r, r);
    nvgLineTo(vg, x, y + r);
    nvgArcTo(vg, x, y, x + r, y, r);
    nvgClosePath(vg);
    nvgFillColor(vg, col);
    nvgFill(vg);
}

// an arrow of the given height: up while charging, down while draining
inline void drawBatteryArrow(NVGcontext* vg, float cx, float cy, float s, int dir, NVGcolor col) {
    if (dir == 0) {
        return;
    }
    const float d = dir > 0 ? -1.f : 1.f; // y grows downward: "up" is a negative y
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx, cy + d * s * 0.5f);
    nvgLineTo(vg, cx + s * 0.5f, cy + d * s * 0.05f);
    nvgLineTo(vg, cx + s * 0.18f, cy + d * s * 0.05f);
    nvgLineTo(vg, cx + s * 0.18f, cy - d * s * 0.5f);
    nvgLineTo(vg, cx - s * 0.18f, cy - d * s * 0.5f);
    nvgLineTo(vg, cx - s * 0.18f, cy + d * s * 0.05f);
    nvgLineTo(vg, cx - s * 0.5f, cy + d * s * 0.05f);
    nvgClosePath(vg);
    nvgFillColor(vg, col);
    nvgFill(vg);
}

// the drain's screensaver background: the whole w x h lit in two hues 120° apart, a full turn in two minutes, the gradient's
// direction turning slower still (OLED: a lit pixel draws the power, the moving colours keep it from burning in)
inline void drawDrainFill(NVGcontext* vg, float w, float h, float t) {
    const float hue = std::fmod(t / 120.f, 1.f), a = t * 0.01f;
    const float dx = std::cos(a) * w / 2.f, dy = std::sin(a) * h / 2.f;
    nvgBeginPath(vg);
    nvgRect(vg, 0.f, 0.f, w, h);
    nvgFillPaint(vg, nvgLinearGradient(vg, w / 2.f - dx, h / 2.f - dy, w / 2.f + dx, h / 2.f + dy, nvgHSL(hue, 0.8f, 0.7f),
                                       nvgHSL(std::fmod(hue + 0.33f, 1.f), 0.8f, 0.7f)));
    nvgFill(vg);
}

} // namespace sphaira::ui::gfx
