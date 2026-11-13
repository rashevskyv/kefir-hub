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

    std::puts("test_keys_dump: ok");
    return 0;
}
