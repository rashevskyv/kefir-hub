// Host test for sphaira/include/homebrew_tree.hpp

#include "homebrew_tree.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace sphaira::homebrew_tree;

    const std::vector<std::string> paths{
        "/switch/tool.nro",                 // 0 loose in the root
        "/switch/app/app.nro",              // 1 app folder
        "/switch/app/data/x.nro",           // 2 nro inside an app folder: hidden
        "/switch/games/emu/emu.nro",        // 3 group "games" -> app folder "emu"
        "/switch/games/emu2/emu2.nro",      // 4
        "/switch/games/sub/deep/deep.nro",  // 5 group in a group
        "/roms/tools/other.nro",            // 6 extra search folder
    };

    const auto root = BuildView(paths, "/switch");
    assert((root.apps == std::vector<size_t>{0, 1, 6}));
    assert(root.folders.size() == 1);
    assert(root.folders[0].path == "/switch/games" && root.folders[0].name == "games" && root.folders[0].apps == 3);

    const auto games = BuildView(paths, "/switch/games");
    assert((games.apps == std::vector<size_t>{3, 4}));
    assert(games.folders.size() == 1 && games.folders[0].path == "/switch/games/sub" && games.folders[0].apps == 1);

    const auto sub = BuildView(paths, "/switch/games/sub");
    assert((sub.apps == std::vector<size_t>{5}) && sub.folders.empty());

    // a name that only starts like the folder is not inside it.
    assert(BuildView({"/switch/gamesX/a/a.nro"}, "/switch/games").apps.empty());

    assert(Parent("/switch/games/sub") == "/switch/games");
    assert(Parent("/switch/games") == "/switch");
    assert(Parent("/switch") == "/switch");

    std::puts("homebrew_tree: ok");
    return 0;
}
