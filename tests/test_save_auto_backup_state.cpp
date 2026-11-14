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

    std::puts("test_save_auto_backup_state: ok");
    return 0;
}
