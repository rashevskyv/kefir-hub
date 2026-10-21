// Host test for sphaira/include/storage_model.hpp

#include "storage_model.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::storage;

int main() {
    constexpr std::int64_t GB = 1'000'000'000;
    // a 256 GB card with 198 GB free: 58 GB used, games 30, homebrew 2, system 1, an empty emuMMC part dropped
    const auto s = Build(256 * GB, 198 * GB, {{Id::Games, 30 * GB}, {Id::EmuData, 0}, {Id::Homebrew, 2 * GB}, {Id::System, 1 * GB}});
    assert(s.size() == 5);                                   // games, homebrew, system, other, free
    assert(s[0].id == Id::Games && s[1].id == Id::Homebrew && s[2].id == Id::System);
    assert(s[3].id == Id::Other && s[3].bytes == 25 * GB);   // 58 - 33
    assert(s[4].id == Id::Free && s[4].bytes == 198 * GB);
    assert(Total(s) == 256 * GB);                            // the ring is the whole card

    // slices as per mille: they add up to the whole (rounding loses at most one per slice)
    int sum = 0;
    for (const auto& x : s) {
        sum += PerMille(x, Total(s));
    }
    assert(sum <= 1000 && sum >= 1000 - static_cast<int>(s.size()));

    // parts bigger than the used space: no negative Other, the total says it
    const auto over = Build(100 * GB, 60 * GB, {{Id::Games, 50 * GB}});
    assert(over[1].id == Id::Other && over[1].bytes == 0 && Total(over) == 110 * GB);

    // the emuMMC and its games are one family: the partition outside the file system, its files and its games
    const auto e = Build(256 * GB, 100 * GB, {{Id::Games, 40 * GB}, {Id::EmuGames, 20 * GB}, {Id::EmuData, 5 * GB}, {Id::EmuPartition, 29 * GB}});
    assert(EmummcTotal(e) == 54 * GB);
    assert(BelongsToEmummc(Id::EmuGames) && !BelongsToEmummc(Id::Games) && !BelongsToEmummc(Id::Other));
    assert(Total(e) == 256 * GB);

    // the emuMMC's parts sit together in the ring, after the system's games: the container reads as one block
    const auto o = Build(256 * GB, 100 * GB, {{Id::Homebrew, 3 * GB}, {Id::EmuGames, 20 * GB}, {Id::Games, 40 * GB}, {Id::EmuPartition, 29 * GB}});
    assert(o[0].id == Id::Games && o[1].id == Id::EmuPartition && o[2].id == Id::EmuGames && o[3].id == Id::Homebrew);

    // an unreadable card (0 total, 0 free) gives an empty ring, not a crash
    const auto none = Build(0, 0, {});
    assert(Total(none) == 0 && PerMille(none[0], Total(none)) == 0);
    // negative parts are ignored
    assert(Build(10 * GB, 5 * GB, {{Id::Games, -1}}).size() == 2);

    std::puts("test_storage_model: ok");
    return 0;
}
