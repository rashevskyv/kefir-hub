#pragma once

// At start-up, and after an install or dump fails because the console keys are missing, wrong or out of
// date, the Hub offers one question: "Dump the keys now?". Kefir's hekate dumps the keys by itself on the next
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

// The start-up check of /switch/prod.keys.
enum class KeysState {
    Unknown,  // no Atmosphere or a service failed (emulator, applet quirk): say nothing
    Ok,
    Missing,  // no /switch/prod.keys
    Outdated, // keys of the current firmware or the eTicket key are not in the file
    Wrong,    // keys from another console, or a key the console's own key engine does not confirm
};

// pure: the facts the check collected -> the state.
constexpr KeysState Classify(bool services_ok, bool file_found, bool other_console, bool current_keys_found,
                             bool current_key_matches, bool eticket_kek_found) {
    if (!services_ok) {
        return KeysState::Unknown;
    }
    if (!file_found) {
        return KeysState::Missing;
    }
    if (other_console || !current_key_matches) {
        return KeysState::Wrong;
    }
    if (!current_keys_found || !eticket_kek_found) {
        return KeysState::Outdated;
    }
    return KeysState::Ok;
}

constexpr bool IsBad(KeysState s) {
    return s == KeysState::Missing || s == KeysState::Outdated || s == KeysState::Wrong;
}

// What the start-up shows. dump_requested = the Hub rebooted for a key dump before this start.
enum class StartupNotice { None, Ask, Dumped, StillBad };

// pure.
constexpr StartupNotice DecideStartup(bool dump_requested, KeysState s) {
    if (dump_requested) {
        if (s == KeysState::Ok) {
            return StartupNotice::Dumped;
        }
        // the dump did not help: never offer the same reboot again in this start (no reboot loop).
        return IsBad(s) ? StartupNotice::StillBad : StartupNotice::None;
    }
    return IsBad(s) ? StartupNotice::Ask : StartupNotice::None;
}

// main thread, at start-up before MTP and FTP start: checks the keys once.
void CheckAtStartup();
// any thread: the start-up check found the keys missing, outdated or wrong. Does not change in a session.
bool KeysBad();

// any thread: remembers a keys error from an install or dump result (rc is a libnx Result).
void NoteKeysResult(std::uint32_t rc);
// main thread, once per frame: shows the question when the install has stopped.
void Poll();

} // namespace sphaira::keys_dump
