#include "text_helper.hpp"
#include <cassert>
#include <iostream>
#include <string>

int main() {
    using namespace text_helper;

    // case-insensitive known text names
    assert(IsTextFile("test.txt"));
    assert(IsTextFile("config.INI"));
    assert(IsTextFile("settings.JSON"));
    assert(IsTextFile("script.Py"));
    assert(IsTextFile("README"));
    assert(IsTextFile("readme.txt"));
    assert(IsTextFile("CMakeLists.txt"));
    assert(IsTextFile("Makefile"));
    assert(IsTextFile(".gitignore"));
    assert(IsTextFile("/sdmc/switch/config.toml"));

    // rejection of representative binary names
    assert(!IsTextFile("app.nro"));
    assert(!IsTextFile("game.nsp"));
    assert(!IsTextFile("rom.xci"));
    assert(!IsTextFile("image.png"));
    assert(!IsTextFile("archive.zip"));
    assert(!IsTextFile("program.exe"));
    assert(!IsTextFile("data.bin"));

    // INI boolean toggle tests:
    // true -> false
    auto r1 = ToggleIniBoolean("setting = true");
    assert(r1.toggled && r1.new_line == "setting = false");

    // false -> true
    auto r2 = ToggleIniBoolean("setting = false");
    assert(r2.toggled && r2.new_line == "setting = true");

    // uppercase preservation
    auto r3 = ToggleIniBoolean("  setting  =   TRUE   ; inline comment");
    assert(r3.toggled && r3.new_line == "  setting  =   FALSE   ; inline comment");

    // titlecase preservation
    auto r4 = ToggleIniBoolean("flag = False # hash comment");
    assert(r4.toggled && r4.new_line == "flag = True # hash comment");

    // comment/section lines rejected
    auto r5 = ToggleIniBoolean("; setting = true");
    assert(!r5.toggled);

    auto r6 = ToggleIniBoolean("# setting = true");
    assert(!r6.toggled);

    auto r7 = ToggleIniBoolean("[section_name]");
    assert(!r7.toggled);

    // rejection of truthful/falsehood/quoted
    auto r8 = ToggleIniBoolean("setting = truthful");
    assert(!r8.toggled);

    auto r9 = ToggleIniBoolean("setting = falsehood");
    assert(!r9.toggled);

    auto r10 = ToggleIniBoolean("setting = \"true\"");
    assert(!r10.toggled);

    // Atmosphère typed flag toggles: u8!0x0 <-> u8!0x1
    auto r11 = ToggleIniBoolean("dmnt_cheats_enabled = u8!0x0");
    assert(r11.toggled && r11.new_line == "dmnt_cheats_enabled = u8!0x1");

    auto r12 = ToggleIniBoolean("dmnt_cheats_enabled = u8!0x1 ; comment");
    assert(r12.toggled && r12.new_line == "dmnt_cheats_enabled = u8!0x0 ; comment");

    auto r13 = ToggleIniBoolean("flag = U8!0X0");
    assert(r13.toggled && r13.new_line == "flag = U8!0X1");

    // Typed flag rejection cases:
    assert(!ToggleIniBoolean("flag = u8!0x2").toggled);
    assert(!ToggleIniBoolean("flag = u32!0x0").toggled);
    assert(!ToggleIniBoolean("flag = u8!0x01").toggled);
    assert(!ToggleIniBoolean("flag = \"u8!0x0\"").toggled);
    assert(!ToggleIniBoolean("; flag = u8!0x0").toggled);
    assert(!ToggleIniBoolean("flag = my_u8!0x0_value").toggled);

    std::cout << "ok  text_helper: all checks passed\n";
    return 0;
}
