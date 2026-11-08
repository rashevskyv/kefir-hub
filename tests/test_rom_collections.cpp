// host test for rom_collections.hpp (RetroArch playlist edit, names). Run by tests/run.sh.
#include "rom_collections.hpp"
#include <cassert>
#include <cstdio>

using namespace sphaira::rom::coll;

static size_t Count(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) n++;
    return n;
}

int main() {
    PlaylistGame g{"/roms/snes/Aladdin (U).smc", "Aladdin (U)", "/retroarch/cores/snes9x_libretro_libnx.nro", "Snes9x",
                   "Nintendo - Super Nintendo Entertainment System.lpl"};

    // names
    assert(Stem("/roms/snes/Aladdin (U).smc") == "Aladdin (U)");
    assert(Stem("Mr. Do.nes") == "Mr. Do");
    assert(Stem("noext") == "noext");
    assert(ThumbFile("Q*bert: \"Qubes\"/2") == "Q_bert_ _Qubes__2");
    assert(RootOf("/RetroArch/cores/a.nro") == "retroarch" && RootOf("tico/cores/x.nro") == "tico" && RootOf("/switch/a.nro") == "switch");
    assert(CoreStem("/retroarch/cores/snes9x_libretro_libnx.nro") == "snes9x");
    assert(CoreStem("/tico/cores/tico-gambatte.nro") == "tico-gambatte");
    assert(InfoDisplayName("display_name = \"Snes9x\"\ncorename = \"Snes9x\"\n") == "Snes9x");
    assert(InfoDisplayName("corename = \"x\"\n").empty());
    assert(DbNameFromRepo("Sega_-_Mega_Drive_-_Genesis") == "Sega - Mega Drive - Genesis");
    assert(JsonString("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"");

    // no playlist yet: a fresh one that reads back as an object with one item
    for (const char* blank : {"", "  \n"}) {
        const auto fresh = AddToPlaylist(blank, g);
        assert(fresh);
        assert(fresh->find("\"version\": \"1.5\"") != std::string::npos);
        assert(Count(*fresh, "\"path\"") == 1);
        assert(fresh->find("\"path\": \"/roms/snes/Aladdin (U).smc\"") != std::string::npos);
        assert(fresh->find("\"crc32\": \"DETECT\"") != std::string::npos);
        assert(fresh->find("\"db_name\": \"Nintendo - Super Nintendo Entertainment System.lpl\"") != std::string::npos);
        // adding the same game again changes nothing
        assert(AddToPlaylist(*fresh, g) == fresh);
    }

    // a playlist RetroArch wrote: unknown top-level and item fields survive byte for byte
    const std::string ra =
        "{\n  \"version\": \"1.5\",\n  \"default_core_path\": \"\",\n  \"default_core_name\": \"\",\n  \"label_display_mode\": 0,\n"
        "  \"scan_content_dir\": \"/roms/snes\",\n  \"items\": [\n"
        "    {\n      \"path\": \"/roms/snes/Zelda \\\"x\\\".sfc\",\n      \"label\": \"Zelda\",\n      \"core_path\": \"DETECT\",\n"
        "      \"core_name\": \"DETECT\",\n      \"crc32\": \"ABCD1234|crc\",\n      \"db_name\": \"Nintendo - Super Nintendo Entertainment System.lpl\",\n"
        "      \"runtime_hours\": 3,\n      \"future_field\": {\"a\": [1, 2, \"]\"]}\n    }\n  ]\n}\n";
    const auto added = AddToPlaylist(ra, g);
    assert(added);
    // everything before the end of the old entry is untouched
    const auto old_end = ra.find("    }\n  ]");
    assert(added->compare(0, old_end + 5, ra, 0, old_end + 5) == 0);
    assert(Count(*added, "\"path\"") == 2);
    assert(added->find("\"scan_content_dir\": \"/roms/snes\"") != std::string::npos);
    assert(added->find("\"runtime_hours\": 3") != std::string::npos);
    assert(added->find("\"future_field\": {\"a\": [1, 2, \"]\"]}") != std::string::npos);
    assert(added->find("\"core_path\": \"/retroarch/cores/snes9x_libretro_libnx.nro\"") != std::string::npos);
    // the second add of the same game: no duplicate, nothing else moves
    assert(AddToPlaylist(*added, g) == added);

    // the same path with a new core replaces that entry, in place, and keeps its play time and found CRC
    const std::string with_crc =
        "{\n  \"version\": \"1.5\",\n  \"items\": [\n    {\n      \"path\": \"/roms/snes/Aladdin (U).smc\",\n      \"label\": \"old\",\n"
        "      \"core_path\": \"DETECT\",\n      \"core_name\": \"DETECT\",\n      \"crc32\": \"0A0B0C0D|crc\",\n      \"db_name\": \"x.lpl\",\n"
        "      \"runtime_hours\": 7\n    },\n    {\n      \"path\": \"/roms/snes/B.sfc\",\n      \"label\": \"B\"\n    }\n  ]\n}\n";
    const auto replaced = AddToPlaylist(with_crc, g);
    assert(replaced);
    assert(Count(*replaced, "\"path\"") == 2);
    assert(replaced->find("\"label\": \"old\"") == std::string::npos);
    assert(replaced->find("\"label\": \"Aladdin (U)\"") != std::string::npos);
    assert(replaced->find("\"crc32\": \"0A0B0C0D|crc\"") != std::string::npos);
    assert(replaced->find("\"runtime_hours\": 7") != std::string::npos);
    assert(replaced->find("\"path\": \"/roms/snes/B.sfc\",\n      \"label\": \"B\"\n    }") != std::string::npos);
    assert(replaced->find("\"path\": \"/roms/snes/Aladdin (U).smc\"") < replaced->find("\"path\": \"/roms/snes/B.sfc\""));

    // an empty list, a minified file
    const auto empty_items = AddToPlaylist("{\"version\": \"1.5\", \"items\": []}", g);
    assert(empty_items && Count(*empty_items, "\"path\"") == 1 && empty_items->find("\"items\": [\n") != std::string::npos);
    assert(AddToPlaylist(*empty_items, g) == empty_items);
    const auto mini = AddToPlaylist("{\"items\":[{\"path\":\"/roms/a.nes\",\"label\":\"a\"}]}", g);
    assert(mini && Count(*mini, "\"path\"") == 2 && mini->find("{\"path\":\"/roms/a.nes\",\"label\":\"a\"},\n") != std::string::npos);

    // a path with a unicode escape in the file is the same path
    const std::string esc = "{\"items\":[{\"path\":\"/roms/snes/Caf\\u00e9.sfc\",\"label\":\"x\"}]}";
    PlaylistGame cafe{"/roms/snes/Caf\xC3\xA9.sfc", "Caf\xC3\xA9", "/retroarch/cores/a.nro", "A", "d.lpl"};
    const auto once = AddToPlaylist(esc, cafe);
    assert(once && Count(*once, "\"path\"") == 1);

    // text this code cannot read is refused, never overwritten
    assert(!AddToPlaylist("not json", g));
    assert(!AddToPlaylist("{\"version\": \"1.5\"}", g));          // no items
    assert(!AddToPlaylist("{\"items\": [ {\"path\": ", g));       // cut off
    assert(!AddToPlaylist("{\"items\": {\"a\": 1}}", g));         // items is not a list

    // only "/roms/<console>" counts as a ROM folder
    assert(RomConsoleOf("/roms/snes") == "snes");
    assert(RomConsoleOf("/roms/snes/") == "snes");
    assert(RomConsoleOf("/roms/snes/sub").empty());
    assert(RomConsoleOf("/roms").empty() && RomConsoleOf("/roms/").empty() && RomConsoleOf("/").empty());
    assert(RomConsoleOf("/romsx/snes").empty() && RomConsoleOf("").empty());

    std::puts("rom_collections: ok");
    return 0;
}
