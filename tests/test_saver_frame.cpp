// FitSaverFrame: every screensaver view's rectangle stays on the panel, and the stick offset does not wind up at an edge.
#include "ui/saver_frame.hpp"

#include <cstdio>

using namespace sphaira::ui;

static int g_checks = 0;
#define CHECK(expr)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(expr)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
            return 1;                                                   \
        }                                                               \
    } while (0)

static bool OnPanel(SaverRect r) {
    return r.x >= SAVER_MARGIN && r.y >= SAVER_MARGIN && r.x + r.w <= 1280.f - SAVER_MARGIN && r.y + r.h <= 720.f - SAVER_MARGIN;
}

int main() {
    const SaverExtent transfer{420.f, 200.f, 200.f};
    const SaverExtent battery{330.f, 190.f, 145.f};

    // the drift (±170, ±90) plus the stick's furthest offset (±450, ±260) in every corner, for both views
    for (const auto e : {transfer, battery}) {
        for (const float sx : {-1.f, 1.f}) {
            for (const float sy : {-1.f, 1.f}) {
                float cx = 640.f + sx * (170.f + 450.f), cy = 360.f + sy * (90.f + 260.f);
                float ox = sx * 450.f, oy = sy * 260.f;
                CHECK(OnPanel(FitSaverFrame(e, 1280.f, 720.f, cx, cy, ox, oy)));
            }
        }
    }

    // pushed past the left edge: the block sits flush at the margin and the offset gives back the overshoot
    {
        float cx = 0.f, cy = 360.f, ox = -640.f, oy = 0.f;
        const auto r = FitSaverFrame(transfer, 1280.f, 720.f, cx, cy, ox, oy);
        CHECK(r.x == SAVER_MARGIN);
        CHECK(cx == SAVER_MARGIN + 420.f);
        CHECK(ox == -640.f + cx); // the raw centre was 640 + ox; now 640 + ox lands exactly on the clamped centre
    }

    // inside the panel nothing moves
    {
        float cx = 640.f, cy = 360.f, ox = 5.f, oy = -5.f;
        const auto r = FitSaverFrame(battery, 1280.f, 720.f, cx, cy, ox, oy);
        CHECK(cx == 640.f && cy == 360.f && ox == 5.f && oy == -5.f);
        CHECK(r.x == 310.f && r.y == 170.f && r.w == 660.f && r.h == 335.f);
    }

    // a block taller than the panel pins to the top margin rather than flipping
    {
        float cx = 640.f, cy = 360.f, ox = 0.f, oy = 0.f;
        const auto r = FitSaverFrame({100.f, 400.f, 400.f}, 1280.f, 720.f, cx, cy, ox, oy);
        CHECK(r.y == SAVER_MARGIN);
    }

    std::printf("test_saver_frame: %d checks passed\n", g_checks);
    return 0;
}
