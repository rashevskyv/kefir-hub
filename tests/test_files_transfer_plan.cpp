// Host test for sphaira/include/files_transfer_plan.hpp

#include "files_transfer_plan.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace sphaira::files_transfer;

    // a path splits into where it was and what it is called.
    assert((SplitPath("/switch/app") == std::pair<std::string, std::string>{"/switch", "app"}));
    assert((SplitPath("/switch/app/") == std::pair<std::string, std::string>{"/switch", "app"}));
    assert((SplitPath("/boot.ini") == std::pair<std::string, std::string>{"/", "boot.ini"}));

    // what one console writes the other reads back.
    std::vector<Item> items{
        {true, PayloadName(0, true), "/switch", "My App", 1234},
        {false, PayloadName(1, false), "/", "boot.ini", 29},
    };
    const auto back = ParseManifest(FormatManifest(items));
    assert(back.size() == 2);
    assert(back[0].dir && back[0].payload == "0.zip" && back[0].parent == "/switch" && back[0].name == "My App" && back[0].size == 1234);
    assert(!back[1].dir && back[1].payload == "1.bin" && back[1].parent == "/" && back[1].name == "boot.ini");

    // a manifest from another console is not trusted: nothing leaves the chosen folder or the package folder.
    assert(ParseManifest("d\t0.zip\t/switch\t..\t1\n").empty());
    assert(ParseManifest("d\t0.zip\t/switch\ta/b\t1\n").empty());
    assert(ParseManifest("d\t0.zip\t/switch/../etc\ta\t1\n").empty());
    assert(ParseManifest("d\t0.zip\tswitch\ta\t1\n").empty());
    assert(ParseManifest("f\t../x.bin\t/switch\ta\t1\n").empty());
    assert(ParseManifest("f\t0.zip\t/switch\ta\t1\n").empty()); // a file is a .bin
    assert(ParseManifest("f\t0.bin\t/switch\ta\n").empty());    // a field is missing
    assert(ParseManifest("f\t0.bin\t/switch\ta\t1\textra\n").empty());
    assert(ParseManifest("f\t0.bin\t/switch\ta\tx\n").empty()); // size is a number
    assert(ParseManifest("f\t0.bin\t/switch\ta\t1\r\n").size() == 1); // a Windows line end is fine
    assert(ParseManifest("junk\nf\t0.bin\t/switch\ta\t1\n").size() == 1); // one bad line does not stop the rest

    // where an item goes: back to its place, or into the folder the user picked.
    assert(TargetPath(back[0], "", true) == "/switch/My App");
    assert(TargetPath(back[0], "", false) == "/switch/My App.zip");
    assert(TargetPath(back[0], "/downloads", true) == "/downloads/My App");
    assert(TargetPath(back[1], "", true) == "/boot.ini");
    assert(TargetPath(back[1], "/downloads/", false) == "/downloads/boot.ini");

    std::puts("files_transfer_plan: ok");
    return 0;
}
