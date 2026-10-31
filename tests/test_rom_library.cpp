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
        assert(*c.terms);
    }

    // the archive.org search
    assert(SearchQuery("gc", "") == "mediatype:software AND (title:(gamecube) OR subject:(gamecube))");
    assert(SearchQuery("gc", "mario") == "mediatype:software AND (title:(gamecube) OR subject:(gamecube)) AND (mario)");
    assert(SearchQuery("nope", "zelda") == "mediatype:software AND (zelda)");
    const auto url = SearchUrl(SearchQuery("gc", ""));
    assert(url.starts_with("https://archive.org/advancedsearch.php?q=mediatype%3Asoftware%20AND%20%28title%3A%28gamecube%29"));
    assert(url.find("&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=downloads&fl%5B%5D=item_size&rows=50&sort%5B%5D=downloads+desc&output=json") != std::string::npos);

    // whole-set archives: which ones are opened, and the page archive.org gives for them
    assert(IsArchiveName("Set.ZIP") && IsArchiveName("a.7z") && IsArchiveName("# - C.rar"));
    assert(!IsArchiveName("game.nes") && !IsArchiveName(".zip") && !IsArchiveName("disc.chd"));
    assert(LookInside("set.zip", 316ull * 1024 * 1024));
    assert(!LookInside("one game.7z", 903704)); // a small archive is one game: downloaded as it is
    assert(!LookInside("big.chd", 900ull * 1024 * 1024));
    assert(ArchiveListUrl("abc", "Nintendo - DS.zip") == "https://archive.org/download/abc/Nintendo%20-%20DS.zip/");

    const std::string page =
        "<html><table><tr><td><a href=\"/\">parent</a><td><td><td></tr>\n"
        "<tr><td><a href=\"//archive.org/download/N64-Roms-Col/%23%20-%20C.rar/%23%20-%20C%2F1080F.V64\"># - C/1080F.V64</a><td><td>1998-03-25 17:27<td id=\"size\">16777216</tr>\n"
        "<tr><td><a href=\"//archive.org/download/x/a.7z/Tom%20%26%20Jerry~1.32x\">Tom &amp; Jerry~1.32x</a><td><td>1996-12-25 04:32:00<td id=\"size\">2097152</tr>\n"
        "<tr><td><a href=\"//archive.org/download/x/a.7z/folder/\">folder/</a><td><td>1996<td id=\"size\"></tr>\n"
        "<tr><td><a href=\"//archive.org/download/x/a.7z/empty\">empty</a><td><td>1996<td id=\"size\">0</tr>\n"
        "</table></html>";
    const auto rows = ParseArchiveListing(page);
    assert(rows.size() == 2);
    assert(rows[0].name == "# - C/1080F.V64" && rows[0].size == 16777216);
    assert(rows[0].url == "https://archive.org/download/N64-Roms-Col/%23%20-%20C.rar/%23%20-%20C%2F1080F.V64");
    assert(rows[1].name == "Tom & Jerry~1.32x" && rows[1].size == 2097152);
    assert(DestPath("n64", rows[0].name) == "/roms/n64/1080F.V64");
    assert(ParseArchiveListing("").empty() && ParseArchiveListing("<tr><td><a href=\"//a\">broken").empty());
    assert(HtmlUnescape("a &lt;b&gt; &quot;c&quot; &#39;d&#39; &unknown; &") == "a <b> \"c\" 'd' &unknown; &");

    assert(MatchesWords("Super Mario 64 (USA).z64", ""));
    assert(MatchesWords("Super Mario 64 (USA).z64", "mario USA"));
    assert(MatchesWords("Super Mario 64 (USA).z64", "  mario  "));
    assert(!MatchesWords("Super Mario 64 (USA).z64", "mario europe"));

    std::puts("test_rom_library: ok");
    return 0;
}
