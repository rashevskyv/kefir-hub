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

    return 0;
}

int main() {
    if (test_parse_games_path()) {
        return 1;
    }
    std::printf("ok  mtp_games_path: %d checks passed\n", g_checks);
    return 0;
}
