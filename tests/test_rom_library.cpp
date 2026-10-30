// host test for the ROM library's pure part (rom_library.hpp). Run by tests/run.sh.
#include "rom_library.hpp"
#include <cassert>
#include <cstdio>

using namespace sphaira::rom;

int main() {
    // collections file: round trip, tolerant parse
    std::vector<Collection> list{{"snes", "my-snes-set", "My SNES set"}, {"nes", "shooter_nes", "shooter_nes"}};
    const auto text = Format(list);
    assert(text == "snes\tmy-snes-set\tMy SNES set\nnes\tshooter_nes\tshooter_nes\n");
    const auto back = Parse(text + "junk line\r\n\tno-console\npsx\tonly-id\r\n");
    assert(back.size() == 3);
    assert(back[0].console == "snes" && back[0].item == "my-snes-set" && back[0].label == "My SNES set");
    assert(back[2].console == "psx" && back[2].item == "only-id" && back[2].label == "only-id");

    // what the user may paste
    assert(ItemFromInput("shooter_nes") == "shooter_nes");
    assert(ItemFromInput(" https://archive.org/details/shooter_nes/ ") == "shooter_nes");
    assert(ItemFromInput("https://archive.org/download/shooter_nes/Shooterv1.11.nes") == "shooter_nes");
    assert(ItemFromInput("archive.org/metadata/abc") == "abc");

    // which files of an item are content
    assert(IsContent("original", "Unknown", 24592));
    assert(IsContent("original", "ZIP", 1));
    assert(!IsContent("original", "Metadata", 980));
    assert(!IsContent("metadata", "Archive BitTorrent", 1992));
    assert(!IsContent("derivative", "ZIP", 5));
    assert(!IsContent("original", "Unknown", 0));

    // urls and destinations
    assert(MetadataUrl("shooter_nes") == "https://archive.org/metadata/shooter_nes");
    assert(DownloadUrl("abc", "Nintendo - DS.zip") == "https://archive.org/download/abc/Nintendo%20-%20DS.zip");
    assert(DownloadUrl("abc", "sub/dir/game (USA).nes") == "https://archive.org/download/abc/sub/dir/game%20%28USA%29.nes");
    assert(DestPath("nes", "sub/dir/game (USA).nes") == "/roms/nes/game (USA).nes");
    assert(DestPath("psx", "disc.chd") == "/roms/psx/disc.chd");

    // the console table
    assert(std::string{ConsoleName("snes")} == "Nintendo - SNES");
    assert(std::string{ConsoleName("nope")}.empty());
    for (const auto& c : Consoles()) {
        for (const char* p = c.folder; *p; p++) {
            assert((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-');
        }
    }

    std::puts("test_rom_library: ok");
    return 0;
}
