// Installed games with a newer update in the list.
//   g++ -std=c++20 -I../sphaira/include test_game_updates.cpp -o t && ./t
#include "game_updates.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::game_updates;

int main() {
    const std::unordered_map<std::uint64_t, std::uint32_t> latest{
        {0x0100000000010000, 393216}, // v6
        {0x0100000000020000, 65536},  // v1
        {0x0100000000030000, 0},      // no updates
    };

    const auto report = FindOutdated({
        {0x0100000000020000, "Zelda", 0},          // behind: listed
        {0x0100000000010000, "Mario", 262144},     // behind: listed
        {0x0100000000030000, "Tetris", 0},         // equal: not listed
        {0x0100000000040000, "Homebrew game", 0},  // not in the list: counted only
    }, latest);

    assert(report.outdated.size() == 2);
    // sorted by name
    assert(report.outdated[0].name == "Mario");
    assert(report.outdated[0].installed == 262144 && report.outdated[0].latest == 393216);
    assert(report.outdated[1].name == "Zelda");
    assert(report.outdated[1].id == 0x0100000000020000);
    assert(report.unknown == 1);

    // a console ahead of the list is not outdated
    const auto ahead = FindOutdated({{0x0100000000020000, "Zelda", 131072}}, latest);
    assert(ahead.outdated.empty() && ahead.unknown == 0);

    // nothing installed
    const auto none = FindOutdated({}, latest);
    assert(none.outdated.empty() && none.unknown == 0);

    assert(UpdateLabel(0) == "v0");
    assert(UpdateLabel(393216) == "v6");

    std::puts("test_game_updates: ok");
    return 0;
}
