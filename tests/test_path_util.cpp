// Host test for sphaira/include/path_util.hpp -- the case-insensitive path
// helpers that had five separate copies across the tree.
//
//     g++ -std=c++20 -I sphaira/include tests/test_path_util.cpp -o /tmp/t && /tmp/t

#include "path_util.hpp"

#include <array>
#include <cstdio>
#include <string>

using namespace sphaira;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_equals_ic() {
    CHECK(path::EqualsIC("nsp", "nsp"));
    CHECK(path::EqualsIC("NSP", "nsp"));
    CHECK(path::EqualsIC("Nsp", "nSP"));
    CHECK(!path::EqualsIC("nsp", "nsz"));

    // length must be compared first -- a prefix is not a match
    CHECK(!path::EqualsIC("nsp", "ns"));
    CHECK(!path::EqualsIC("ns", "nsp"));

    CHECK(path::EqualsIC("", ""));
    CHECK(!path::EqualsIC("", "a"));

    // must only read the length given, not run to a NUL. If EqualsIC ever goes
    // back to strcasecmp this fails, which is the point.
    const std::string haystack = "nspXXXX";
    CHECK(path::EqualsIC(std::string_view{haystack.data(), 3}, "nsp"));
    CHECK(path::EqualsIC(std::string_view{haystack.data(), 3}, "NSP"));

    // digits and punctuation are unaffected by case folding
    CHECK(path::EqualsIC("v1.2", "V1.2"));
    return 0;
}

static int test_ends_with_ic() {
    CHECK(path::EndsWithIC("game.nsp", ".nsp"));
    CHECK(path::EndsWithIC("game.NSP", ".nsp"));
    CHECK(path::EndsWithIC("GAME.nsp", ".NSP"));
    CHECK(!path::EndsWithIC("game.nsz", ".nsp"));

    // suffix longer than the string
    CHECK(!path::EndsWithIC("nsp", "game.nsp"));

    // exact-length match is still a suffix
    CHECK(path::EndsWithIC(".nsp", ".nsp"));

    // empty suffix matches anything, like string_view::ends_with
    CHECK(path::EndsWithIC("game.nsp", ""));
    CHECK(path::EndsWithIC("", ""));

    // must not match in the middle
    CHECK(!path::EndsWithIC("a.tik.cert", ".tik"));
    CHECK(path::EndsWithIC("a.tik.cert", ".cert"));

    // the real yati use: ticket collections inside a container
    CHECK(path::EndsWithIC("0005000c.tik", ".tik"));
    CHECK(path::EndsWithIC("0005000C.TIK", ".tik"));
    return 0;
}

static int test_extension() {
    CHECK(path::Extension("game.nsp") == "nsp");
    CHECK(path::Extension("/a/b/game.nsp") == "nsp");
    CHECK(path::Extension("game.tar.gz") == "gz");

    // no extension at all
    CHECK(path::Extension("game") == "");
    CHECK(path::Extension("/a/b/game") == "");
    CHECK(path::Extension("") == "");

    // a dot in a directory name is not the file's extension
    CHECK(path::Extension("/a.b/game") == "");
    CHECK(path::Extension("/kefir-1.0/readme") == "");
    CHECK(path::Extension("/a.b/game.nsp") == "nsp");

    // trailing dot yields an empty extension, not a dot
    CHECK(path::Extension("game.") == "");

    // dotfile: the whole name after the dot
    CHECK(path::Extension(".gitignore") == "gitignore");
    return 0;
}

static int test_is_any_of_ic() {
    static constexpr std::array<std::string_view, 4> installable{
        "nsp", "nsz", "xci", "xcz",
    };

    CHECK(path::IsAnyOfIC("nsp", installable));
    CHECK(path::IsAnyOfIC("XCZ", installable));
    CHECK(path::IsAnyOfIC("Xci", installable));
    CHECK(!path::IsAnyOfIC("zip", installable));
    CHECK(!path::IsAnyOfIC("", installable));

    // a prefix of a list entry must not match
    CHECK(!path::IsAnyOfIC("ns", installable));

    // empty list matches nothing
    CHECK(!path::IsAnyOfIC("nsp", std::span<const std::string_view>{}));

    // composes with Extension, which is how callers actually use it
    CHECK(path::IsAnyOfIC(path::Extension("/games/Zelda.NSP"), installable));
    CHECK(!path::IsAnyOfIC(path::Extension("/games/notes.txt"), installable));
    return 0;
}

static int test_parse_title_id_name() {
    CHECK(path::ParseTitleIdName("0100000000001000") == 0x0100000000001000ULL);
    CHECK(path::ParseTitleIdName("420000000007e51a") == 0x420000000007E51AULL);
    CHECK(path::ParseTitleIdName("FFFFFFFFFFFFFFFF") == 0xFFFFFFFFFFFFFFFFULL);

    // an all-zero folder name is not a title, and 0 is the "no" answer anyway
    CHECK(path::ParseTitleIdName("0000000000000000") == 0);

    // wrong length, even when every digit is valid hex
    CHECK(path::ParseTitleIdName("010000000000100") == 0);
    CHECK(path::ParseTitleIdName("01000000000010000") == 0);
    CHECK(path::ParseTitleIdName("") == 0);

    // ordinary folder names of exactly 16 characters must not parse
    CHECK(path::ParseTitleIdName("contents/atmosp!") == 0);
    CHECK(path::ParseTitleIdName("0100000000001g00") == 0);
    CHECK(path::ParseTitleIdName(" 100000000001000") == 0);
    CHECK(path::ParseTitleIdName("+100000000001000") == 0);
    CHECK(path::ParseTitleIdName("0x00000000001000") == 0);

    // trailing junk after valid hex is rejected, not silently truncated
    CHECK(path::ParseTitleIdName("01000000000010 0") == 0);
    return 0;
}

int main() {
    if (test_equals_ic() || test_ends_with_ic() || test_extension() || test_is_any_of_ic() || test_parse_title_id_name()) {
        return 1;
    }
    std::printf("ok  path_util: %d checks passed\n", g_checks);
    return 0;
}
