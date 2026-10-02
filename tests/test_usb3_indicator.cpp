// Host test for the USB waiting screen decisions in
// sphaira/include/ui/menus/dbi/usb_status_text.hpp (used by dbi_draw.cpp):
// which status line the badge shows and where the applet warning card goes.
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_usb3_indicator.cpp -o /tmp/t && /tmp/t

#include "ui/menus/dbi/usb_status_text.hpp"

#include <cstdio>
#include <string_view>

using namespace sphaira::ui::menu::dbi;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static auto Text(bool waiting_detached, bool is_super_speed, bool is_usb3_forced) -> std::string_view {
    return UsbStatusTextKey(waiting_detached, is_super_speed, is_usb3_forced);
}

int main() {
    // 1. Detached with USB 3.0 force enabled in config
    CHECK(Text(true, false, true) == "USB 3.0 Enabled · Waiting for PC connection");

    // 2. Detached with USB 3.0 disabled in config
    CHECK(Text(true, false, false) == "USB 2.0 · Waiting for PC connection");

    // 3. Connected at SuperSpeed (USB 3.0), with or without the force flag
    CHECK(Text(false, true, true) == "USB 3.0 SuperSpeed (5 Gbps)");
    CHECK(Text(false, true, false) == "USB 3.0 SuperSpeed (5 Gbps)");

    // 4. Connected at HighSpeed with USB 3.0 enabled (e.g. USB 2.0 port/cable)
    CHECK(Text(false, false, true) == "USB 3.0 Enabled · Link: USB 2.0 High Speed (480 Mbps)");

    // 5. Connected at HighSpeed with USB 3.0 disabled
    CHECK(Text(false, false, false) == "USB 2.0 High Speed (480 Mbps)");

    // Waiting screen geometry from dbi_draw.cpp: badge at 180 (h 36), main text from 250,
    // warning card of about 55 px, footer at 646.
    constexpr float badge_bottom = 180.f + 36.f;
    constexpr float main_text_y = 250.f;
    constexpr float warn_h = 55.f;
    constexpr float footer_y = 646.f;
    CHECK(main_text_y >= badge_bottom + 20.f);

    // 6. Five lines of wrapped text (140 px): the card keeps its minimum position.
    {
        const float main_bottom = main_text_y + 140.f; // 390
        const float warn_y = WaitingWarningY(main_bottom);
        CHECK(warn_y == 470.f);
        CHECK(warn_y >= main_bottom + 35.f);
        CHECK(warn_y + warn_h < footer_y - 20.f);
    }

    // 7. Extreme wrapped text (220 px): the card is pushed down and still clears the footer.
    {
        const float main_bottom = main_text_y + 220.f; // 470
        const float warn_y = WaitingWarningY(main_bottom);
        CHECK(warn_y == 505.f);
        CHECK(warn_y + warn_h < footer_y);
    }

    std::printf("ok  usb3_indicator: %d checks passed\n", g_checks);
    return 0;
}
