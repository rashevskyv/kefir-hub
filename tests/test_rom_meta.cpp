// host test for the ROM facts (rom_meta.hpp): samples are copied from libretro-database/metadat. Run by tests/run.sh.
#include "rom_meta.hpp"
#include <cassert>
#include <cstdio>

using namespace sphaira::rom;

static const char* YEAR = R"DAT(clrmamepro (
	name "Nintendo - Super Nintendo Entertainment System"
	description "Nintendo - Super Nintendo Entertainment System"
)

game (
	comment "Aladdin (France)"
	releaseyear "1993"
	rom ( crc 63069939 )
)

game (
	comment "Aladdin (USA)"
	releaseyear "1993"
	rom ( crc 124D8E4D )
)

game (
	comment "Aladdin (USA) (Beta)"
	releaseyear "1993"
	rom ( crc 52FDFAB4 )
)
)DAT";

static const char* GENRE = R"DAT(clrmamepro (
	name "Nintendo - Super Nintendo Entertainment System"
)

game (
	comment "Aladdin (France)"
	genre "Platform"
	rom ( crc 63069939 )
)

game (
	comment "Aladdin (USA)"
	genre "Platform"
	rom ( crc 124D8E4D )
)

game (
	genre "Role-playing (RPG)"
	rom ( crc DBD0B8DC )
)

game (
	comment "3 Ninjas Kick Back (USA)"
	genre "Beat'em Up"
	rom ( crc F2EE11F9 )
)
)DAT";

static const char* PUBLISHER = "game (\r\n\tcomment \"Aladdin (USA)\"\r\n\tpublisher \"Capcom\"\r\n\trom ( crc 124D8E4D )\r\n)\r\n";
static const char* DEVELOPER = "game (\n\tcomment \"Aladdin (USA)\"\n\tdeveloper \"Capcom\"\n)\ngame (\n\tcomment \"3 Ninjas Kick Back (USA)\"\n\tdeveloper \"Malibu\"\n)\n";
// maxusers: the number is not quoted, the key is `users`
static const char* USERS = "game (\n\tcomment \"Aladdin (USA)\"\n\tusers 1\n)\ngame (\n\tcomment \"3 Ninjas Kick Back (USA)\"\n\tusers 2\n)\n";

int main() {
    // urls
    assert(MetaDatUrl("snes", MetaYear) == "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/releaseyear/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System.dat");
    assert(MetaDatUrl("master-system", MetaPlayers) == "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/maxusers/Sega%20-%20Master%20System%20-%20Mark%20III.dat");
    assert(MetaDatUrl("genesis", MetaGenre).find("/genre/Sega%20-%20Mega%20Drive%20-%20Genesis.dat") != std::string::npos);
    assert(MetaDatUrl("psx", MetaYear).empty());

    // one dat: the header block (name, no comment) is skipped, quoted and unquoted values
    const auto years = ParseDat(YEAR, "releaseyear");
    assert(years.size() == 3 && years[1].first == "Aladdin (USA)" && years[1].second == "1993");
    const auto users = ParseDat(USERS, "users");
    assert(users.size() == 2 && users[1].first == "3 Ninjas Kick Back (USA)" && users[1].second == "2");
    assert(ParseDat(YEAR, "genre").empty());
    assert(ParseDat(PUBLISHER, "publisher").at(0).second == "Capcom"); // CRLF

    // merged by name; a game that is in one dat only keeps the facts it has
    const auto recs = MergeDats(std::vector<std::string_view>{YEAR, GENRE, DEVELOPER, PUBLISHER, USERS});
    assert(recs.size() == 4);
    assert(recs[0].name == "3 Ninjas Kick Back (USA)" && recs[0].year.empty() && recs[0].genre == "Beat'em Up" && recs[0].players == "2");
    assert(recs[1].name == "Aladdin (France)" && recs[1].publisher.empty());
    const MetaRec* usa = nullptr;
    for (const auto& r : recs) if (r.name == "Aladdin (USA)") usa = &r;
    assert(usa && usa->year == "1993" && usa->genre == "Platform" && usa->publisher == "Capcom" && usa->developer == "Capcom" && usa->players == "1");
    assert(MergeDats(std::vector<std::string_view>{}).empty());
    assert(MergeDats(std::vector<std::string_view>{"", GENRE}).size() == 3);

    // the compact file round trips; a tab in a value cannot break a row; junk lines are skipped
    auto bad = recs;
    bad[0].genre = "Beat\tem\nUp";
    const auto text = FormatCompact(bad);
    assert(text.find("Aladdin (USA)\t1993\tCapcom\tCapcom\t1\tPlatform\n") != std::string::npos);
    const auto table = ParseCompact(text + "junk\n\t\t\t\t\t\n");
    assert(table.recs.size() == 4 && table.names.size() == 4);
    assert(table.recs[0].genre == "Beat em Up" && table.recs[2].name == "Aladdin (USA)" && table.recs[2].publisher == "Capcom");
    assert(ParseCompact("").recs.empty());

    // a loose old-style file name finds the No-Intro game (the USA release, not the beta or the French one)
    const auto* hit = FindMeta(table, "/roms/snes/Aladdin (U).smc");
    assert(hit && hit->name == "Aladdin (USA)");
    assert(FindMeta(table, "3 Ninjas Kick Back.sfc") && FindMeta(table, "3 Ninjas Kick Back.sfc")->players == "2");
    assert(!FindMeta(table, "Unknown Game.smc"));
    assert(!FindMeta(MetaTable{}, "Aladdin.smc"));

    // lines: only known facts; the developer only when it is not the publisher
    auto lines = MetaLines(*hit);
    assert(lines.size() == 4);
    assert(std::string{lines[0].first} == "Year" && lines[0].second == "1993");
    assert(std::string{lines[1].first} == "Publisher" && lines[1].second == "Capcom");
    assert(std::string{lines[2].first} == "Players" && lines[2].second == "1");
    assert(std::string{lines[3].first} == "Genre" && lines[3].second == "Platform");
    MetaRec odd{"X", "", "", "Malibu", "", ""};
    lines = MetaLines(odd);
    assert(lines.size() == 1 && std::string{lines[0].first} == "Developer");
    assert(MetaLines(MetaRec{}).empty());

    std::puts("rom_meta: ok");
    return 0;
}
