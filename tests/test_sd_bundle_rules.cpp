// Host test for sphaira/include/sd_bundle_rules.hpp

#include "sd_bundle_rules.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace sphaira::sd_bundle;

    assert(IsBundleName("Pikmin 4 - Russian Translation.kefir-sd.tar"));
    assert(!IsBundleName(".kefir-sd.tar"));
    assert(!IsBundleName("Game [0100B7C00933A000][v0].nsp"));

    // the backend's rules (release_bundle.archive_rules): folder prefixes and exact renames.
    const auto rules = ParseRules("sdcard/switch/\tswitch/\r\nfont.pak_duckbill\tatmosphere/contents/X/romfs/p.pak\n\tbad\nnotab\n");
    assert(rules.size() == 2);
    assert(rules[0].src == "sdcard/switch/" && rules[0].dst == "switch/");

    assert(MapEntry("sdcard/switch/port/port.nro", rules) == "switch/port/port.nro");
    assert(MapEntry("./sdcard\\switch\\port\\a b.bin", rules) == "switch/port/a b.bin");
    assert(MapEntry("font.pak_duckbill", rules) == "atmosphere/contents/X/romfs/p.pak");
    assert(!MapEntry("readme.txt", rules));                 // not part of the option
    assert(!MapEntry("sdcard/switch/", rules));              // the folder itself
    assert(!MapEntry("sdcard/switch/../../evil", rules));    // never leaves the microSD
    assert(!MapEntry("sdcard/switchy/x", rules));            // a prefix is a whole folder

    assert(CleanEntry("./atmosphere/contents/a") == "atmosphere/contents/a");
    assert(CleanEntry("/abs") == "abs");                     // like the other archive paths

    assert(IsWritable("switch/port/port.nro"));
    assert(IsWritable("atmosphere/contents/0100B7C00933A000/romfs/a.bin"));
    assert(!IsWritable("Nintendo/Contents/registered/x"));
    assert(!IsWritable("emuMMC/RAW1/x"));
    assert(!IsWritable(".kefir/rules"));
    assert(!IsWritable("../x"));

    assert(IsSaveData("switch/simpsons/save/slot1.bin"));
    assert(IsSaveData("switch/dkc1/Saves/a"));
    assert(!IsSaveData("switch/port/save"));                 // a file named "save" is data
    assert(!IsSaveData("switch/port/savegame_icons/a.png"));

    std::printf("ok  sd_bundle_rules: all checks passed\n");
    return 0;
}
