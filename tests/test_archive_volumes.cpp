// Host test for sphaira/include/archive_volumes.hpp

#include "archive_volumes.hpp"

#include <cassert>
#include <cstdio>

using V = std::vector<std::string>;
using sphaira::archive::Volumes;

int main() {
    // rar5: any part lists every part, in numeric order (part10 after part9).
    const V parts{"Game.part2.rar", "Game.part10.rar", "Game.part1.rar", "Game.part3.rar", "Other.part1.rar", "readme.txt"};
    const V all_game{"Game.part1.rar", "Game.part2.rar", "Game.part3.rar", "Game.part10.rar"};
    assert(Volumes("Game.part1.rar", parts) == all_game);
    assert(Volumes("Game.part3.rar", parts) == all_game);
    assert(Volumes("Other.part1.rar", parts) == V{"Other.part1.rar"});

    // rar4: name.rar then name.r00, name.r01.
    const V old{"a.r01", "a.rar", "a.r00", "b.rar"};
    const V all_a{"a.rar", "a.r00", "a.r01"};
    assert(Volumes("a.rar", old) == all_a);
    assert(Volumes("a.r01", old) == all_a);
    assert(Volumes("b.rar", old) == V{"b.rar"});

    // 7z split.
    const V sevenz{"x.7z.002", "x.7z.001", "x.7z.003"};
    const V all_x{"x.7z.001", "x.7z.002", "x.7z.003"};
    assert(Volumes("x.7z.002", sevenz) == all_x);

    // not split: just itself, case does not matter.
    assert(Volumes("solo.rar", V{"solo.rar", "other.zip"}) == V{"solo.rar"});
    assert(Volumes("solo.7z", V{"solo.7z"}) == V{"solo.7z"});
    assert(Volumes("Solo.PART1.RAR", V{"Solo.PART1.RAR"}) == V{"Solo.PART1.RAR"});
    assert(Volumes("Game.PART2.RAR", V{"Game.PART1.RAR", "Game.PART2.RAR"}) == (V{"Game.PART1.RAR", "Game.PART2.RAR"}));

    // the libarchive text of a solid rar4 archive (seen in a user's log).
    assert(sphaira::archive::IsSolidRar("RAR solid archive support unavailable."));
    assert(!sphaira::archive::IsSolidRar("Truncated RAR file data"));
    assert(!sphaira::archive::IsSolidRar(nullptr));

    std::printf("ok  archive_volumes: all checks passed\n");
    return 0;
}
