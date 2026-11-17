#include "keys_dump.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::keys_dump;

int main() {
    // no keys error: never ask, whatever else is true.
    assert(Decide(false, false, true, false) == Offer::None);
    assert(Decide(false, true, false, true) == Offer::None);

    // a transfer or an error box is still up: wait, even when the payload is missing.
    assert(Decide(true, true, true, false) == Offer::Wait);
    assert(Decide(true, true, false, true) == Offer::Wait);

    // stopped: no Lockpick on the card -> say so, never reboot.
    assert(Decide(true, false, false, false) == Offer::NoPayload);
    assert(Decide(true, false, false, true) == Offer::NoPayload);

    // stopped, payload present, the user turned automatic dumps off -> say so.
    assert(Decide(true, false, true, true) == Offer::Disabled);

    // stopped, payload present, dumps allowed -> ask.
    assert(Decide(true, false, true, false) == Offer::Ask);

    // start-up check: (services_ok, file, other console, current keys, key engine agrees, eticket kek)
    // a failed service (emulator) never makes a verdict, whatever the file says.
    assert(Classify(false, false, true, false, false, false) == KeysState::Unknown);
    assert(Classify(true, false, false, true, true, true) == KeysState::Missing);
    assert(Classify(true, true, true, true, true, true) == KeysState::Wrong);
    assert(Classify(true, true, false, true, false, true) == KeysState::Wrong);
    assert(Classify(true, true, false, false, true, true) == KeysState::Outdated);
    assert(Classify(true, true, false, true, true, false) == KeysState::Outdated);
    assert(Classify(true, true, false, true, true, true) == KeysState::Ok);
    assert(!IsBad(KeysState::Ok) && !IsBad(KeysState::Unknown));
    assert(IsBad(KeysState::Missing) && IsBad(KeysState::Outdated) && IsBad(KeysState::Wrong));

    // start-up notice: ask only when bad; after a dump say the result, never ask again (no reboot loop).
    assert(DecideStartup(false, KeysState::Ok) == StartupNotice::None);
    assert(DecideStartup(false, KeysState::Unknown) == StartupNotice::None);
    assert(DecideStartup(false, KeysState::Outdated) == StartupNotice::Ask);
    assert(DecideStartup(true, KeysState::Ok) == StartupNotice::Dumped);
    assert(DecideStartup(true, KeysState::Wrong) == StartupNotice::StillBad);
    assert(DecideStartup(true, KeysState::Unknown) == StartupNotice::None);

    std::puts("test_keys_dump: ok");
    return 0;
}
