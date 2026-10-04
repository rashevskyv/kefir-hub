// Host test for sphaira/include/system_cleanup_plan.hpp

#include "system_cleanup_plan.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace sphaira::cleanup;

    // games yes, sysmodules / system titles / updates no.
    assert(IsGameContentsFolder("0100AC300919A000"));
    assert(IsGameContentsFolder("01007ef00011e000"));
    assert(!IsGameContentsFolder("010000000000BD00")); // MissionControl
    assert(!IsGameContentsFolder("0100000000000352")); // emuiibo
    assert(!IsGameContentsFolder("4200000000000010")); // ldn_mitm
    assert(!IsGameContentsFolder("00FF0000636C6BFF")); // sys-clk
    assert(!IsGameContentsFolder("0100000000001000")); // qlaunch
    assert(!IsGameContentsFolder("0100AC300919A800")); // an update id
    assert(!IsGameContentsFolder("exefs_patches"));
    assert(!IsGameContentsFolder(""));

    // only the older updates of each game.
    std::vector<PatchRef> patches{
        {0x0100000000010000, 65536, 0},
        {0x0100000000010000, 196608, 1},
        {0x0100000000020000, 65536, 2},
        {0x0100000000010000, 131072, 3},
    };
    const auto old = OldUpdates(patches);
    assert(old.size() == 2);
    assert(old[0] == 0 && old[1] == 3);
    assert(OldUpdates({}).empty());

    std::printf("ok  system_cleanup_plan: all checks passed\n");
    return 0;
}
