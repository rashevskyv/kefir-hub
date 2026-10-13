// Closest Build ID when the game's own one has no cheats in the database.
//   g++ -std=c++20 -I../sphaira/include test_cheat_closest_bid.cpp -o t && ./t
#include "ui/menus/cheats/cheat_closest_bid.hpp"

#include <cassert>
#include <cstdio>

using sphaira::ui::menu::hats::detail::BuildIdCheats;
using sphaira::ui::menu::hats::detail::PickClosestBuildId;

int main() {
    // nothing, or only empty sets: no answer
    assert(PickClosestBuildId({}).empty());
    assert(PickClosestBuildId({{"AAAA", 0}}).empty());

    // the most complete set wins
    assert(PickClosestBuildId({{"AAAA", 5}, {"BBBB", 197}, {"CCCC", 40}}) == "BBBB");

    // equal sets: the one listed last (newest version) wins
    assert(PickClosestBuildId({{"AAAA", 10}, {"BBBB", 10}}) == "BBBB");

    // a single set is taken as it is
    assert(PickClosestBuildId({{"AAAA", 1}}) == "AAAA");

    std::puts("test_cheat_closest_bid: ok");
    return 0;
}
