#pragma once

// Pure decisions of the "PC Install (USB)" waiting screen (dbi_draw.cpp).
// Host test: tests/test_usb3_indicator.cpp.

namespace sphaira::ui::menu::dbi {

// The status line in the USB badge, as its i18n key (the English text).
// waiting_detached: the screen waits for a PC and usb:ds reports Detached.
// The keys live in en.json; they are looked up with i18n::get(), so keep them in sync there.
constexpr auto UsbStatusTextKey(bool waiting_detached, bool is_super_speed, bool is_usb3_forced) -> const char* {
    if (waiting_detached) {
        return (is_usb3_forced || is_super_speed)
            ? "USB 3.0 Enabled · Waiting for PC connection"
            : "USB 2.0 · Waiting for PC connection";
    }
    if (is_super_speed) {
        return "USB 3.0 SuperSpeed (5 Gbps)";
    }
    if (is_usb3_forced) {
        return "USB 3.0 Enabled · Link: USB 2.0 High Speed (480 Mbps)";
    }
    return "USB 2.0 High Speed (480 Mbps)";
}

// Top of the applet-mode warning card: 35 px below the wrapped main text, never above y = 470.
constexpr auto WaitingWarningY(float main_text_bottom) -> float {
    const float below_text = main_text_bottom + 35.f;
    return below_text > 470.f ? below_text : 470.f;
}

} // namespace sphaira::ui::menu::dbi
