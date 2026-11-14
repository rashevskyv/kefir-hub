// Auto backup state of changed saves (ui/menus/save/save_auto_backup_state.hpp): parse, format, change check.
#include "ui/menus/save/save_auto_backup_state.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::ui::menu::save::autobackup;

int main() {
    States s;
    s[0x8000000000000001ULL] = State{0x10, "/dumps/Zelda [0100]/0100_A_2026-11-14.zip"};
    s[0x8000000000000002ULL] = State{0x20, ""}; // an empty save: no copy, not written
    const auto text = Format(s);
    assert(text == "8000000000000001 0000000000000010 /dumps/Zelda [0100]/0100_A_2026-11-14.zip\n");

    // a path with spaces survives; CRLF, broken and zero-id lines are dropped
    const auto back = Parse(text + "bad line\r\n0 1 /x\r\n8000000000000003 zz /y\n8000000000000004 5 /z\r\n");
    assert(back.size() == 2);
    assert(back.at(0x8000000000000001ULL).commit == 0x10);
    assert(back.at(0x8000000000000001ULL).path == "/dumps/Zelda [0100]/0100_A_2026-11-14.zip");
    assert(back.at(0x8000000000000004ULL).path == "/z");

    // a copy is made for a new save, a changed save, and a save whose copy is gone
    assert(NeedsCopy(back, 0x99, 1, true));
    assert(NeedsCopy(back, 0x8000000000000001ULL, 0x11, true));
    assert(NeedsCopy(back, 0x8000000000000001ULL, 0x10, false));
    assert(!NeedsCopy(back, 0x8000000000000001ULL, 0x10, true));

    // exclusions: a name with spaces survives; broken lines are dropped; ALL_TYPES covers every type
    std::vector<Exclusion> ex{{0x0100000000010000ULL, 1, "Super Game 2"}, {0x0100000000020000ULL, ALL_TYPES, ""}};
    const auto ex_text = FormatExclusions(ex);
    assert(ex_text == "0100000000010000 1 Super Game 2\n0100000000020000 -1 \n");
    const auto ex_back = ParseExclusions(ex_text + "zz 1 x\n0100000000030000 x y\r\n");
    assert(ex_back.size() == 2);
    assert(ex_back[0].name == "Super Game 2" && ex_back[0].type == 1);
    assert(ex_back[1].type == ALL_TYPES && ex_back[1].name.empty());
    assert(IsExcluded(ex_back, 0x0100000000010000ULL, 1));
    assert(!IsExcluded(ex_back, 0x0100000000010000ULL, 3));
    assert(IsExcluded(ex_back, 0x0100000000020000ULL, 3));
    assert(!IsExcluded(ex_back, 0x0100000000040000ULL, 1));

    // size limit: index 0 is no limit
    assert(!TooBig(1ULL << 40, 0));
    assert(TooBig((16ULL << 20) + 1, 1));
    assert(!TooBig(16ULL << 20, 1));
    assert(!TooBig(1ULL << 40, 99));

    std::puts("test_save_auto_backup_state: ok");
    return 0;
}
