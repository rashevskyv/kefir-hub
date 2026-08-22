// Unit test for USB 3.0 indicator resolution and waiting screen layout anti-overlap geometry
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

enum MockUsbState {
    MockUsbState_Detached,
    MockUsbState_Attached,
    MockUsbState_Configured,
};

enum MockUsbSpeed {
    MockUsbSpeed_None,
    MockUsbSpeed_Full,
    MockUsbSpeed_High,
    MockUsbSpeed_Super,
};

static auto ResolveUsbStatusText(bool is_waiting_for_usb, MockUsbState state, MockUsbSpeed speed, bool is_usb3_forced) -> std::string {
    const bool is_super_speed = (speed == MockUsbSpeed_Super);
    const bool is_usb3 = is_usb3_forced || is_super_speed;

    if (is_waiting_for_usb && state == MockUsbState_Detached) {
        return is_usb3
            ? "USB 3.0 Enabled · Waiting for PC connection"
            : "USB 2.0 · Waiting for PC connection";
    } else if (is_super_speed) {
        return "USB 3.0 SuperSpeed (5 Gbps)";
    } else if (is_usb3_forced) {
        return "USB 3.0 Enabled · Link: USB 2.0 High Speed (480 Mbps)";
    } else {
        return "USB 2.0 High Speed (480 Mbps)";
    }
}

struct WaitingScreenGeometry {
    float badge_y{180.f};
    float badge_h{36.f};
    float main_text_y{250.f};
    float main_text_h{140.f}; // 5 lines of wrapped text
    float warn_y{0.f};
    float warn_h{55.f};
    float footer_y{646.f};
};

static auto ComputeWaitingScreenLayout(float main_text_height) -> WaitingScreenGeometry {
    WaitingScreenGeometry geom;
    geom.main_text_h = main_text_height;
    const float main_text_bottom = geom.main_text_y + geom.main_text_h;
    geom.warn_y = std::max(main_text_bottom + 35.f, 470.f);
    return geom;
}

int main() {
    // 1. Detached with USB 3.0 force enabled in config
    {
        const auto text = ResolveUsbStatusText(true, MockUsbState_Detached, MockUsbSpeed_None, true);
        CHECK(text == "USB 3.0 Enabled · Waiting for PC connection");
    }

    // 2. Detached with USB 3.0 disabled in config
    {
        const auto text = ResolveUsbStatusText(true, MockUsbState_Detached, MockUsbSpeed_None, false);
        CHECK(text == "USB 2.0 · Waiting for PC connection");
    }

    // 3. Connected at SuperSpeed (USB 3.0)
    {
        const auto text = ResolveUsbStatusText(false, MockUsbState_Configured, MockUsbSpeed_Super, true);
        CHECK(text == "USB 3.0 SuperSpeed (5 Gbps)");
    }

    // 4. Connected at HighSpeed with USB 3.0 enabled (e.g. USB 2.0 port/cable)
    {
        const auto text = ResolveUsbStatusText(false, MockUsbState_Configured, MockUsbSpeed_High, true);
        CHECK(text == "USB 3.0 Enabled · Link: USB 2.0 High Speed (480 Mbps)");
    }

    // 5. Connected at HighSpeed with USB 3.0 disabled
    {
        const auto text = ResolveUsbStatusText(false, MockUsbState_Configured, MockUsbSpeed_High, false);
        CHECK(text == "USB 2.0 High Speed (480 Mbps)");
    }

    // 6. Anti-overlap geometry checks with 5 lines of text (height = 140px)
    {
        const auto geom = ComputeWaitingScreenLayout(140.f);
        const float badge_bottom = geom.badge_y + geom.badge_h; // 180 + 36 = 216
        CHECK(geom.main_text_y >= badge_bottom + 20.f); // Main text starts at 250, > 216

        const float main_bottom = geom.main_text_y + geom.main_text_h; // 250 + 140 = 390
        CHECK(geom.warn_y >= main_bottom + 35.f); // Warning starts at >= 470, well above 425
        CHECK(geom.warn_y == 470.f);

        const float warn_bottom = geom.warn_y + geom.warn_h; // 470 + 55 = 525
        CHECK(warn_bottom < geom.footer_y - 20.f); // 525 < 626 (no overlap with footer)
    }

    // 7. Anti-overlap geometry checks with extreme wrapped text (height = 220px)
    {
        const auto geom = ComputeWaitingScreenLayout(220.f);
        const float main_bottom = geom.main_text_y + geom.main_text_h; // 250 + 220 = 470
        CHECK(geom.warn_y >= main_bottom + 35.f); // Warning pushed to 505
        CHECK(geom.warn_y == 505.f);

        const float warn_bottom = geom.warn_y + geom.warn_h; // 505 + 55 = 560
        CHECK(warn_bottom < geom.footer_y); // 560 < 646 (still fits safely)
    }

    std::printf("ok  usb3_indicator: %d checks passed\n", g_checks);
    return 0;
}
