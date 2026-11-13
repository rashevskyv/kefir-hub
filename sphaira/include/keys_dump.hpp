#pragma once

// After an install fails because the console keys are missing, wrong or out of date, the Hub offers
// one question: "Reboot and dump the keys?". Kefir's hekate dumps the keys by itself on the next
// autoboot when /config/autokeys.bin (its record of the last dump) is gone: it starts
// /bootloader/payloads/Lockpick_RCM.bin, Lockpick saves /switch/prod.keys and returns to hekate,
// and hekate boots the console as usual.

#include <cstdint>

namespace sphaira::keys_dump {

enum class Offer {
    None,       // no keys error since the last question
    Wait,       // a transfer or an error box is still on screen: ask later
    Ask,        // ask "Reboot and dump the keys?"
    NoPayload,  // Lockpick_RCM.bin is not on the microSD: say so, do not reboot
    Disabled,   // /config/autokeys.disable is on the microSD: hekate skips the dump, say so
};

// pure: what to do on this frame.
constexpr Offer Decide(bool keys_error_pending, bool busy, bool payload_present, bool autodump_disabled) {
    if (!keys_error_pending) {
        return Offer::None;
    }
    if (busy) {
        return Offer::Wait;
    }
    if (!payload_present) {
        return Offer::NoPayload;
    }
    if (autodump_disabled) {
        return Offer::Disabled;
    }
    return Offer::Ask;
}

// any thread: remembers a keys error from an install result (rc is a libnx Result).
void NoteInstallResult(std::uint32_t rc);
// main thread, once per frame: shows the question when the install has stopped.
void Poll();

} // namespace sphaira::keys_dump
