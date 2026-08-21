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

    // path and section comparisons for search paths
    CHECK(path::EqualsIC("/switch", "/SWITCH"));
    CHECK(path::EqualsIC("/Switch", "/switch"));
    CHECK(path::EqualsIC("/Games/NRO", "/games/nro"));
    CHECK(path::EqualsIC("homebrew_paths", "HOMEBREW_PATHS"));
    return 0;
}

static int test_ends_with_ic() {
    CHECK(path::EndsWithIC("game.nsp", ".nsp"));
    CHECK(path::EndsWithIC("game.NSP", ".nsp"));
    CHECK(path::EndsWithIC("GAME.nsp", ".NSP"));
    CHECK(!path::EndsWithIC("game.nsz", ".nsp"));

    // nro extension case variants
    CHECK(path::EndsWithIC("app.nro", ".nro"));
    CHECK(path::EndsWithIC("app.NRO", ".nro"));
    CHECK(path::EndsWithIC("app.nRo", ".nro"));
    CHECK(path::EndsWithIC("APP.NRO", ".NRO"));
    CHECK(!path::EndsWithIC("app.nro.bak", ".nro"));

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

static int test_is_safe_archive_entry() {
    // Normal relative paths
    CHECK(path::IsSafeArchiveEntry("switch/app/app.nro"));
    CHECK(path::IsSafeArchiveEntry("atmosphere/contents/0100000000001000/flags/boot2.flag"));
    CHECK(path::IsSafeArchiveEntry("readme.txt"));
    CHECK(path::IsSafeArchiveEntry("a/b/c/d.bin"));

    // Directory entries
    CHECK(path::IsSafeArchiveEntry("switch/app/"));
    CHECK(path::IsSafeArchiveEntry("atmosphere/"));
    CHECK(path::IsSafeArchiveEntry("a/b/c/"));

    // Empty names rejected
    CHECK(!path::IsSafeArchiveEntry(""));

    // Absolute / leading slash rejected
    CHECK(!path::IsSafeArchiveEntry("/"));
    CHECK(!path::IsSafeArchiveEntry("/switch/app/app.nro"));
    CHECK(!path::IsSafeArchiveEntry("/readme.txt"));

    // Backslashes rejected
    CHECK(!path::IsSafeArchiveEntry("switch\\app\\app.nro"));
    CHECK(!path::IsSafeArchiveEntry("\\"));
    CHECK(!path::IsSafeArchiveEntry("switch/app\\nested"));

    // Control characters and DEL rejected
    CHECK(!path::IsSafeArchiveEntry("switch/\x01/app.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/\x1f/app.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/app\n.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/app\r.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/app\t.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/\x7f/app.nro"));

    // Colon / device-like paths rejected
    CHECK(!path::IsSafeArchiveEntry("sdmc:/switch/app.nro"));
    CHECK(!path::IsSafeArchiveEntry("c:/windows/system32"));
    CHECK(!path::IsSafeArchiveEntry("http://evil.com"));
    CHECK(!path::IsSafeArchiveEntry(":bad"));
    CHECK(!path::IsSafeArchiveEntry("bad:"));
    CHECK(!path::IsSafeArchiveEntry("a/b:c/d"));

    // Dot / DotDot path traversal components rejected
    CHECK(!path::IsSafeArchiveEntry("."));
    CHECK(!path::IsSafeArchiveEntry(".."));
    CHECK(!path::IsSafeArchiveEntry("./"));
    CHECK(!path::IsSafeArchiveEntry("../"));
    CHECK(!path::IsSafeArchiveEntry("./app.nro"));
    CHECK(!path::IsSafeArchiveEntry("../app.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/./app.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/../app.nro"));
    CHECK(!path::IsSafeArchiveEntry("switch/app/."));
    CHECK(!path::IsSafeArchiveEntry("switch/app/.."));
    CHECK(!path::IsSafeArchiveEntry("switch/app/./"));
    CHECK(!path::IsSafeArchiveEntry("switch/app/../"));
    CHECK(!path::IsSafeArchiveEntry("a/b/c/../../d"));

    // Ordinary names with dots accepted
    CHECK(path::IsSafeArchiveEntry(".config"));
    CHECK(path::IsSafeArchiveEntry("..data"));
    CHECK(path::IsSafeArchiveEntry("file.name"));
    CHECK(path::IsSafeArchiveEntry(".../foo"));
    CHECK(path::IsSafeArchiveEntry("switch/.config/app.nro"));
    CHECK(path::IsSafeArchiveEntry("switch/..data/app.nro"));
    CHECK(path::IsSafeArchiveEntry(".gitignore"));
    CHECK(path::IsSafeArchiveEntry("a...b"));

    // Non-structural characters handled by SanitizeZipEntryName accepted here
    CHECK(path::IsSafeArchiveEntry("Super*Mario"));
    CHECK(path::IsSafeArchiveEntry("games/Zelda? (v1.0)"));
    CHECK(path::IsSafeArchiveEntry("title<1>|test\"name"));

    return 0;
}

static int test_normalize_absolute_sd_path() {
    // Valid absolute paths
    CHECK(path::NormalizeAbsoluteSdPath("/") == "/");
    CHECK(path::NormalizeAbsoluteSdPath("///") == "/");
    CHECK(path::NormalizeAbsoluteSdPath("/switch") == "/switch");
    CHECK(path::NormalizeAbsoluteSdPath("/switch/") == "/switch");
    CHECK(path::NormalizeAbsoluteSdPath("/switch/apps") == "/switch/apps");
    CHECK(path::NormalizeAbsoluteSdPath("/switch/apps/") == "/switch/apps");
    CHECK(path::NormalizeAbsoluteSdPath("///switch///apps///") == "/switch/apps");
    CHECK(path::NormalizeAbsoluteSdPath("/Switch") == "/Switch");
    CHECK(path::NormalizeAbsoluteSdPath("/SWITCH/APPS/") == "/SWITCH/APPS");
    CHECK(path::NormalizeAbsoluteSdPath("/retroarch/cores") == "/retroarch/cores");
    CHECK(path::NormalizeAbsoluteSdPath("/Games/NRO") == "/Games/NRO");

    // Ordinary names with dots
    CHECK(path::NormalizeAbsoluteSdPath("/.config") == "/.config");
    CHECK(path::NormalizeAbsoluteSdPath("/..data") == "/..data");
    CHECK(path::NormalizeAbsoluteSdPath("/switch/.hidden/app.nro") == "/switch/.hidden/app.nro");
    CHECK(path::NormalizeAbsoluteSdPath("/switch/.../app") == "/switch/.../app");

    // Relative paths rejected
    CHECK(!path::NormalizeAbsoluteSdPath("").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("switch").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("switch/apps").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("app.nro").has_value());

    // Backslashes, colons, control chars rejected
    CHECK(!path::NormalizeAbsoluteSdPath("/switch\\apps").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("\\switch").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/sdmc:/switch").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("sdmc:/switch").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/c:/games").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/\x01").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/\x1f/app").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/\x7f").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch\n/app").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch\t").has_value());

    // Dot and double-dot traversal rejected
    CHECK(!path::NormalizeAbsoluteSdPath("/.").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/..").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/./").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/../").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/.").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/..").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/./apps").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/../apps").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/apps/.").has_value());
    CHECK(!path::NormalizeAbsoluteSdPath("/switch/apps/..").has_value());

    return 0;
}

static int test_starts_with_ic() {
    CHECK(path::StartsWithIC("Homebrew menu.nsp", "Homebrew menu"));
    CHECK(path::StartsWithIC("homebrew menu [010000000000100D].nsp", "Homebrew MENU"));
    CHECK(path::StartsWithIC("HOMEBREW MENU.NSP", "homebrew menu"));
    CHECK(!path::StartsWithIC("Other menu.nsp", "Homebrew menu"));
    CHECK(!path::StartsWithIC("Homebrew", "Homebrew menu"));
    CHECK(path::StartsWithIC("Homebrew menu", "Homebrew menu"));
    CHECK(path::StartsWithIC("Homebrew menu", ""));
    CHECK(path::StartsWithIC("", ""));
    return 0;
}

static int test_is_zip_asset() {
    // Content-type checks
    CHECK(path::IsZipAsset("application/zip", "app.bin", ""));
    CHECK(path::IsZipAsset("application/x-zip-compressed", "app.bin", ""));
    CHECK(path::IsZipAsset("APPLICATION/ZIP", "app.bin", ""));
    CHECK(!path::IsZipAsset("application/octet-stream", "app.nro", ""));

    // Filename extension checks
    CHECK(path::IsZipAsset("", "app.zip", ""));
    CHECK(path::IsZipAsset("", "APP.ZIP", ""));
    CHECK(path::IsZipAsset("", "release_v1.0.Zip", ""));
    CHECK(!path::IsZipAsset("", "app.nro", ""));
    CHECK(!path::IsZipAsset("", "app.zip.bak", ""));

    // URL path checks with query parameters
    CHECK(path::IsZipAsset("", "", "https://github.com/foo/bar/releases/download/v1/app.zip"));
    CHECK(path::IsZipAsset("", "", "https://github.com/foo/bar/releases/download/v1/app.ZIP?token=123#frag"));
    CHECK(!path::IsZipAsset("", "", "https://github.com/foo/bar/releases/download/v1/app.nro?file=foo.zip"));
    CHECK(!path::IsZipAsset("", "", "https://github.com/foo/bar/releases/download/v1/app.nro"));

    return 0;
}

static int test_is_safe_filename() {
    CHECK(path::IsSafeFilename("app.nro"));
    CHECK(path::IsSafeFilename("Sphaira.zip"));
    CHECK(path::IsSafeFilename("file-1.2.3_final.bin"));
    CHECK(path::IsSafeFilename(".hidden"));

    // Unsafe / traversal names
    CHECK(!path::IsSafeFilename(""));
    CHECK(!path::IsSafeFilename("."));
    CHECK(!path::IsSafeFilename(".."));
    CHECK(!path::IsSafeFilename("a/b"));
    CHECK(!path::IsSafeFilename("a\\b"));
    CHECK(!path::IsSafeFilename("c:file"));
    CHECK(!path::IsSafeFilename("app\n.nro"));
    CHECK(!path::IsSafeFilename("app\x01.nro"));
    CHECK(!path::IsSafeFilename("app\x7f.nro"));

    return 0;
}

static int test_extract_basename() {
    CHECK(path::ExtractBasename("app.nro") == "app.nro");
    CHECK(path::ExtractBasename("/switch/app.nro") == "app.nro");
    CHECK(path::ExtractBasename("https://github.com/owner/repo/releases/download/v1/app.nro") == "app.nro");
    CHECK(path::ExtractBasename("https://example.com/download/app.nro?token=123#hash") == "app.nro");
    CHECK(path::ExtractBasename("") == "");

    return 0;
}

int main() {
    if (test_equals_ic() || test_starts_with_ic() || test_ends_with_ic() || test_extension() || test_is_any_of_ic() || test_parse_title_id_name() || test_is_safe_archive_entry() || test_normalize_absolute_sd_path() || test_is_zip_asset() || test_is_safe_filename() || test_extract_basename()) {
        return 1;
    }
    std::printf("ok  path_util: %d checks passed\n", g_checks);
    return 0;
}

