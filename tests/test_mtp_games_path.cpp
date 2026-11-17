// Host test for sphaira/include/mtp_games_path.hpp
//
// Build and run command:
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_mtp_games_path.cpp -o /tmp/test_mtp_games_path && /tmp/test_mtp_games_path

#include "mtp_games_path.hpp"

#include <cassert>
#include <cstdio>
#include <string>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_parse_games_path() {
    using namespace sphaira::mtp;

    // Root cases
    {
        auto res = ParseGamesPath("");
        CHECK(res.kind == PathKind::Root);
    }
    {
        auto res = ParseGamesPath("/");
        CHECK(res.kind == PathKind::Root);
    }
    {
        auto res = ParseGamesPath("//");
        CHECK(res.kind == PathKind::Root);
    }
    {
        auto res = ParseGamesPath("///");
        CHECK(res.kind == PathKind::Root);
    }

    // Merged cases
    {
        auto res = ParseGamesPath("/Merged");
        CHECK(res.kind == PathKind::MergedDir);
    }
    {
        auto res = ParseGamesPath("/merged/");
        CHECK(res.kind == PathKind::MergedDir);
    }
    {
        auto res = ParseGamesPath("//MERGED///");
        CHECK(res.kind == PathKind::MergedDir);
    }
    {
        auto res = ParseGamesPath("/Merged/Game A [0100000000010000][B+U65536+9DLC].nsp");
        CHECK(res.kind == PathKind::MergedFile);
        CHECK(res.filename == "Game A [0100000000010000][B+U65536+9DLC].nsp");
    }
    {
        auto res = ParseGamesPath("//merged/Game B [0100000000010000][B].nsp//");
        CHECK(res.kind == PathKind::MergedFile);
        CHECK(res.filename == "Game B [0100000000010000][B].nsp");
    }
    {
        auto res = ParseGamesPath("/Merged/dir/file.nsp");
        CHECK(res.kind == PathKind::Invalid);
    }

    // Separate cases
    {
        auto res = ParseGamesPath("/Separate");
        CHECK(res.kind == PathKind::SeparateDir);
    }
    {
        auto res = ParseGamesPath("/separate/");
        CHECK(res.kind == PathKind::SeparateDir);
    }
    {
        auto res = ParseGamesPath("/Separate/Game A [0100000000010000]");
        CHECK(res.kind == PathKind::SeparateGameDir);
        CHECK(res.game == "Game A [0100000000010000]");
    }
    {
        auto res = ParseGamesPath("//separate//Game A [0100000000010000]//");
        CHECK(res.kind == PathKind::SeparateGameDir);
        CHECK(res.game == "Game A [0100000000010000]");
    }
    {
        auto res = ParseGamesPath("/Separate/Game A [0100000000010000]/Game A [0100000000010000] [BASE].nsp");
        CHECK(res.kind == PathKind::SeparateFile);
        CHECK(res.game == "Game A [0100000000010000]");
        CHECK(res.filename == "Game A [0100000000010000] [BASE].nsp");
    }
    {
        auto res = ParseGamesPath("/Separate/Game A/sub/file.nsp");
        CHECK(res.kind == PathKind::Invalid);
    }

    // Readme / info files
    {
        auto res = ParseGamesPath("/Readme.txt");
        CHECK(res.kind == PathKind::InfoFile);
        CHECK(res.game.empty());
    }
    {
        auto res = ParseGamesPath("/Merged/Readme.txt");
        CHECK(res.kind == PathKind::InfoFile);
        CHECK(res.game == "merged");
    }
    {
        auto res = ParseGamesPath("/Separate/Readme.txt");
        CHECK(res.kind == PathKind::InfoFile);
        CHECK(res.game == "separate");
    }
    {
        auto res = ParseGamesPath("/Forwarders/Readme.txt");
        CHECK(res.kind == PathKind::InfoFile);
        CHECK(res.game == "forwarders");
    }

    // Locale aliases (Ukrainian folder names) still accept English.
    {
        GamesFolderNames uk{"Злиті", "Окремі", "Форвардери", "Прочитай.txt"};
        auto res = ParseGamesPath("/Злиті", GamesLayout::Both, uk);
        CHECK(res.kind == PathKind::MergedDir);
        res = ParseGamesPath("/Merged", GamesLayout::Both, uk);
        CHECK(res.kind == PathKind::MergedDir);
        res = ParseGamesPath("/Окремі", GamesLayout::Both, uk);
        CHECK(res.kind == PathKind::SeparateDir);
        res = ParseGamesPath("/Форвардери/x.nsp", GamesLayout::Both, uk);
        CHECK(res.kind == PathKind::ForwardersFile);
        CHECK(res.filename == "x.nsp");
        res = ParseGamesPath("/Прочитай.txt", GamesLayout::Both, uk);
        CHECK(res.kind == PathKind::InfoFile);
        res = ParseGamesPath("/Злиті/Прочитай.txt", GamesLayout::Both, uk);
        CHECK(res.kind == PathKind::InfoFile);
        CHECK(res.game == "merged");
    }

    // Unmerged is not a folder.
    {
        auto res = ParseGamesPath("/Unmerged");
        CHECK(res.kind == PathKind::Invalid);
    }

    // Forwarders cases (every layout)
    {
        auto res = ParseGamesPath("/Forwarders");
        CHECK(res.kind == PathKind::ForwardersDir);
    }
    {
        auto res = ParseGamesPath("/forwarders/");
        CHECK(res.kind == PathKind::ForwardersDir);
    }
    {
        auto res = ParseGamesPath("/Forwarders/App [0500000000123000][B].nsp");
        CHECK(res.kind == PathKind::ForwardersFile);
        CHECK(res.filename == "App [0500000000123000][B].nsp");
    }
    {
        auto res = ParseGamesPath("/Forwarders/a/b.nsp");
        CHECK(res.kind == PathKind::Invalid);
    }
    {
        auto res = ParseGamesPath("/Forwarders", GamesLayout::Compatible);
        CHECK(res.kind == PathKind::ForwardersDir);
    }
    {
        auto res = ParseGamesPath("/Forwarders/x.nsp", GamesLayout::Separate);
        CHECK(res.kind == PathKind::ForwardersFile);
        CHECK(res.filename == "x.nsp");
    }

    // Invalid roots
    {
        auto res = ParseGamesPath("/InvalidRoot");
        CHECK(res.kind == PathKind::Invalid);
    }
    {
        auto res = ParseGamesPath("/InvalidRoot/file.nsp");
        CHECK(res.kind == PathKind::Invalid);
    }

    // Compatible layout: merged NSP files sit at the drive root.
    {
        auto res = ParseGamesPath("/", GamesLayout::Compatible);
        CHECK(res.kind == PathKind::Root);
    }
    {
        auto res = ParseGamesPath("/Game A [0100000000010000][B+U65536+9DLC].nsp", GamesLayout::Compatible);
        CHECK(res.kind == PathKind::MergedFile);
        CHECK(res.filename == "Game A [0100000000010000][B+U65536+9DLC].nsp");
    }
    {
        auto res = ParseGamesPath("/Merged", GamesLayout::Compatible);
        CHECK(res.kind == PathKind::MergedFile);
        CHECK(res.filename == "Merged");
    }
    {
        auto res = ParseGamesPath("/a/b.nsp", GamesLayout::Compatible);
        CHECK(res.kind == PathKind::Invalid);
    }

    // Separate layout: a folder per game at the drive root.
    {
        auto res = ParseGamesPath("/", GamesLayout::Separate);
        CHECK(res.kind == PathKind::Root);
    }
    {
        auto res = ParseGamesPath("/Game A [0100000000010000]", GamesLayout::Separate);
        CHECK(res.kind == PathKind::SeparateGameDir);
        CHECK(res.game == "Game A [0100000000010000]");
    }
    {
        auto res = ParseGamesPath("/Game A [0100000000010000]/Game A [BASE].nsp", GamesLayout::Separate);
        CHECK(res.kind == PathKind::SeparateFile);
        CHECK(res.game == "Game A [0100000000010000]");
        CHECK(res.filename == "Game A [BASE].nsp");
    }
    {
        auto res = ParseGamesPath("/a/b/c.nsp", GamesLayout::Separate);
        CHECK(res.kind == PathKind::Invalid);
    }

    // per-game mods folder: <game>/Mods[/path], both layouts with game folders.
    {
        auto res = ParseGamesPath("/Separate/Game A/Mods");
        CHECK(res.kind == PathKind::ModsPath);
        CHECK(res.game == "Game A");
        CHECK(res.filename.empty());
    }
    {
        auto res = ParseGamesPath("/separate/Game A/mods/romfs/Data/x.bin");
        CHECK(res.kind == PathKind::ModsPath);
        CHECK(res.filename == "romfs/Data/x.bin");
    }
    {
        auto res = ParseGamesPath("/Game A/Mods/exefs", GamesLayout::Separate);
        CHECK(res.kind == PathKind::ModsPath);
        CHECK(res.game == "Game A");
        CHECK(res.filename == "exefs");
    }
    {
        GamesFolderNames names;
        names.mods = "Моди";
        auto res = ParseGamesPath("/Separate/Game A/Моди/cheats", GamesLayout::Both, names);
        CHECK(res.kind == PathKind::ModsPath && res.filename == "cheats");
        CHECK(ParseGamesPath("/Separate/Game A/Mods", GamesLayout::Both, names).kind == PathKind::ModsPath);
    }
    {
        // an nsp keeps its meaning next to a mods folder.
        CHECK(ParseGamesPath("/Separate/Game A/Mods.nsp").kind == PathKind::SeparateFile);
        // a mods path is joined onto a real SD folder: no way out of it.
        CHECK(ParseGamesPath("/Separate/Game A/Mods/../../x").kind == PathKind::Invalid);
        CHECK(ParseGamesPath("/Separate/Game A/Mods/romfs/./a").kind == PathKind::Invalid);
    }

    // install folders: at the root in every layout, English and locale names.
    for (const auto layout : {GamesLayout::Compatible, GamesLayout::Separate, GamesLayout::Both}) {
        auto dir = ParseGamesPath("/Install to SD", layout);
        CHECK(dir.kind == PathKind::InstallDir && dir.install_sd);
        auto file = ParseGamesPath("/install to nand/Game.nsz", layout);
        CHECK(file.kind == PathKind::InstallFile && !file.install_sd && file.filename == "Game.nsz");
        // a dropped folder: the path inside the install folder.
        auto nested = ParseGamesPath("/Install to SD/Merged/Mods/x [0100000000010000]/romfs", layout);
        CHECK(nested.kind == PathKind::InstallFile && nested.install_sd);
        CHECK(nested.filename == "Merged/Mods/x [0100000000010000]/romfs");
        CHECK(ClassifyInstallDrop(ParseGamesPath("/Install to SD/a/b.nsp", layout).filename, "").kind == DropKind::Install);
        CHECK(ClassifyInstallDrop(ParseGamesPath("/Install to SD/a/readme.txt", layout).filename, "").kind == DropKind::Skip);
        CHECK(ClassifyInstallDrop(ParseGamesPath("/Install to SD/readme.txt", layout).filename, "").kind == DropKind::Reject);
        CHECK(ParseGamesPath("/Install to SD/a/../b.nsp", layout).kind == PathKind::Invalid);
    }
    {
        GamesFolderNames names;
        names.install_sd = "Встановити на SD";
        auto res = ParseGamesPath("/Встановити на SD/x.xci", GamesLayout::Both, names);
        CHECK(res.kind == PathKind::InstallFile && res.install_sd && res.filename == "x.xci");
        CHECK(ParseGamesPath("/Install to SD/x.xci", GamesLayout::Both, names).kind == PathKind::InstallFile);
    }
    // the installer reads the target back from the queued path, whatever the locale.
    CHECK(InstallTargetFromPath(InstallQueuePath(true, "x.nsp")) == 1);
    CHECK(InstallTargetFromPath(InstallQueuePath(false, "x.nsp")) == 0);
    CHECK(InstallTargetFromPath("x.nsp") == -1);
    CHECK(InstallTargetFromPath("/install/x.nsp") == -1);

    CHECK(IsForwarderTitleId(0x0500000000123000ULL));
    CHECK(IsForwarderTitleId(0x03DB1280BD84000ULL));
    CHECK(IsForwarderTitleId(0x03DB12780BD84000ULL));
    CHECK(!IsForwarderTitleId(0x0100000000010000ULL));
    CHECK(!IsForwarderTitleId(0x0700000000000000ULL));

    return 0;
}

// Merged/<Mods>: one folder per game with mods, its subfolders are the game's SD folder.
static int test_merged_mods() {
    using namespace sphaira::mtp;

    CHECK(ParseGamesPath("/Merged/Mods").kind == PathKind::ModsRootDir);
    CHECK(ParseGamesPath("/Mods", GamesLayout::Compatible).kind == PathKind::ModsRootDir);
    {
        auto res = ParseGamesPath("/Merged/Mods/Game A [0100000000010000]/romfs/x.bin");
        CHECK(res.kind == PathKind::ModsPath);
        CHECK(res.game == "Game A [0100000000010000]");
        CHECK(res.filename == "romfs/x.bin");
    }
    {
        auto res = ParseGamesPath("/Mods/Game A/cheats", GamesLayout::Compatible);
        CHECK(res.kind == PathKind::ModsPath && res.game == "Game A" && res.filename == "cheats");
    }
    {
        GamesFolderNames names;
        names.merged = "Злиті";
        names.mods = "Моди";
        auto game = ParseGamesPath("/Злиті/Моди/Гра", GamesLayout::Both, names);
        CHECK(game.kind == PathKind::ModsPath && game.game == "Гра" && game.filename.empty());
        CHECK(ParseGamesPath("/Злиті/Mods", GamesLayout::Both, names).kind == PathKind::ModsRootDir);
        // the merged games and the readme keep their meaning.
        CHECK(ParseGamesPath("/Злиті/Гра [0100000000010000].nsp", GamesLayout::Both, names).kind == PathKind::MergedFile);
        CHECK(ParseGamesPath("/Злиті/Readme.txt", GamesLayout::Both, names).kind == PathKind::InfoFile);
    }
    CHECK(ParseGamesPath("/Merged/Mods/Game A/../../x").kind == PathKind::Invalid);
    return 0;
}

// a folder dropped on the Install drive: games install, mods go to the SD card, the rest is skipped.
static int test_install_drop() {
    using namespace sphaira::mtp;

    CHECK(TitleIdFromName("Zelda [0100F2C0115B6000]") == 0x0100F2C0115B6000ULL);
    CHECK(TitleIdFromName("Zelda [0100f2c0115b6000]") == 0x0100F2C0115B6000ULL);
    CHECK(TitleIdFromName("[01006A800016E000]") == 0x01006A800016E000ULL);
    // the last match wins: a name may carry a bracket of its own.
    CHECK(TitleIdFromName("Game [Deluxe] [0100000000010000]") == 0x0100000000010000ULL);
    CHECK(!TitleIdFromName("Zelda"));
    CHECK(!TitleIdFromName("Zelda [0100F2C0115B600]"));
    CHECK(!TitleIdFromName("Zelda [0100F2C0115B600G]"));
    CHECK(!TitleIdFromName("Zelda [0100F2C0115B6000"));

    const std::string mods = "Моди";
    // the whole Merged folder copied back from the PC.
    CHECK(ClassifyInstallDrop("/Злиті/Zelda [0100F2C0115B6000][v0].nsp", mods).kind == DropKind::Install);
    CHECK(ClassifyInstallDrop("/Злиті/Sub/deeper/Game.NSZ", mods).kind == DropKind::Install);
    CHECK(ClassifyInstallDrop("/Злиті/Прочитай.txt", mods).kind == DropKind::Skip);
    {
        auto d = ClassifyInstallDrop("/Злиті/Моди/Zelda [0100F2C0115B6000]/romfs/Text/ua.msbt", mods);
        CHECK(d.kind == DropKind::Mod);
        CHECK(d.sd_path == "/atmosphere/contents/0100F2C0115B6000/romfs/Text/ua.msbt");
    }
    {
        auto d = ClassifyInstallDrop("/Злиті/Mods/Zelda [0100f2c0115b6000]/cheats/ABCDEF0123456789.txt", mods);
        CHECK(d.kind == DropKind::Mod);
        CHECK(d.sd_path == "/atmosphere/contents/0100F2C0115B6000/cheats/ABCDEF0123456789.txt");
    }
    // the Separate layout puts the mods folder inside the game folder.
    {
        auto d = ClassifyInstallDrop("/Окремі/Zelda [0100F2C0115B6000]/Моди/exefs/main.npdm", mods);
        CHECK(d.kind == DropKind::Mod);
        CHECK(d.sd_path == "/atmosphere/contents/0100F2C0115B6000/exefs/main.npdm");
        CHECK(ClassifyInstallDrop("/Окремі/Zelda [0100F2C0115B6000]/Zelda [BASE].nsp", mods).kind == DropKind::Install);
    }
    // any file in a mods folder is a mod, even an nsp.
    CHECK(ClassifyInstallDrop("/Моди/Game [0100000000010000]/exefs.nsp", mods).kind == DropKind::Mod);
    // a mods folder without a title id in the game folder name: nothing to map it to.
    CHECK(ClassifyInstallDrop("/Злиті/Моди/Zelda/romfs/a.bin", mods).kind == DropKind::Skip);
    CHECK(ClassifyInstallDrop("/Злиті/Моди/Zelda [0100F2C0115B6000]/../../x", mods).kind == DropKind::Skip);
    // a single file at the drive root keeps the old rule.
    CHECK(ClassifyInstallDrop("/Game.xci", mods).kind == DropKind::Install);
    CHECK(ClassifyInstallDrop("/notes.txt", mods).kind == DropKind::Reject);

    // folders: the game's mods folder itself maps to its SD folder, the others are virtual.
    CHECK(ModsDropSdPath("/Злиті/Моди/Zelda [0100F2C0115B6000]", mods) == std::string("/atmosphere/contents/0100F2C0115B6000"));
    CHECK(ModsDropSdPath("/Окремі/Zelda [0100F2C0115B6000]/Mods", mods) == std::string("/atmosphere/contents/0100F2C0115B6000"));
    CHECK(!ModsDropSdPath("/Злиті/Моди", mods));
    CHECK(!ModsDropSdPath("/Злиті", mods));
    return 0;
}

int main() {
    if (test_parse_games_path() || test_merged_mods() || test_install_drop()) {
        return 1;
    }
    std::printf("ok  mtp_games_path: %d checks passed\n", g_checks);
    return 0;
}
