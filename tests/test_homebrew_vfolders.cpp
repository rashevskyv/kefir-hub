// Host test for sphaira/include/homebrew_vfolders.hpp

#include "homebrew_vfolders.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace sphaira::hb_folders;

    const std::vector<std::string> paths{"/switch/a/a.nro", "/switch/b/b.nro", "/switch/c.nro"};

    Store s;
    // nothing set: every app on the top level, no folders
    auto v = BuildView(s, paths, ROOT);
    assert((v.apps == std::vector<size_t>{0, 1, 2}) && v.folders.empty());

    const auto emus = CreateFolder(s, "Emulators");
    const auto tools = CreateFolder(s, "Tools");
    assert(emus == 1 && tools == 2);
    assert(CreateFolder(s, "Tools") == tools); // same name: same folder
    assert(CreateFolder(s, "") == ROOT && CreateFolder(s, "a\tb") == ROOT);

    // move (cut): a leaves the top level; copy: b stays on the top level and is in Tools too
    Move(s, paths[0], ROOT, emus);
    Add(s, paths[1], tools);
    Add(s, paths[0], tools); // one app, two folders
    v = BuildView(s, paths, ROOT);
    assert((v.apps == std::vector<size_t>{1, 2}));
    assert((v.folders == std::vector<uint32_t>{emus, tools}) && (v.counts == std::vector<size_t>{1, 2}));
    assert((v.previews[1] == std::vector<size_t>{0, 1})); // the folder picture: its apps, in list order
    assert((BuildView(s, paths, tools).apps == std::vector<size_t>{0, 1}));
    assert(BuildView(s, paths, tools).folders.empty()); // folders show on the top level only

    // out of every folder: back on the top level
    Remove(s, paths[0], emus);
    Remove(s, paths[0], tools);
    assert(IsIn(s, paths[0], ROOT));

    // an empty folder still shows, with 0 apps
    v = BuildView(s, paths, ROOT);
    assert(v.counts[0] == 0);

    assert(RenameFolder(s, emus, "Emu"));
    assert(!RenameFolder(s, emus, "Tools")); // name taken
    assert(FindFolder(s, emus)->name == "Emu");

    // the file round-trips; deleting a folder keeps the apps
    const auto back = Parse(Format(s));
    assert(back.folders.size() == 2 && back.members.size() == s.members.size());
    DeleteFolder(s, tools);
    assert(IsIn(s, paths[1], ROOT) && FoldersOf(s, paths[1]).size() == 1);

    // the picture shows at most PREVIEW_MAX apps, the count shows all
    {
        Store big;
        const auto f = CreateFolder(big, "Big");
        std::vector<std::string> many;
        for (int i = 0; i < 12; i++) {
            many.push_back("/switch/app" + std::to_string(i) + ".nro");
            Add(big, many.back(), f);
        }
        const auto bv = BuildView(big, many, ROOT);
        assert(bv.counts[0] == 12 && bv.previews[0].size() == PREVIEW_MAX);
    }

    // a broken file loses only its bad lines
    const auto p = Parse("F\t1\tGames\r\nF\tx\tBad\nA\t1\t/switch/g.nro\nA\t7\t/switch/h.nro\nA\t1\trelative.nro\njunk\n");
    assert(p.folders.size() == 1 && p.members.size() == 1 && p.members[0].path == "/switch/g.nro");

    std::puts("homebrew_vfolders: ok");
    return 0;
}
