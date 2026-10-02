// Host test for sphaira/include/path_util.hpp -- the case-insensitive path
// helpers that had five separate copies across the tree.
//
//     g++ -std=c++20 -I sphaira/include tests/test_path_util.cpp -o /tmp/t && /tmp/t

#include "path_util.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

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

static int test_extract_basename() {
    CHECK(path::ExtractBasename("app.nro") == "app.nro");
    CHECK(path::ExtractBasename("/switch/app.nro") == "app.nro");
    CHECK(path::ExtractBasename("https://github.com/owner/repo/releases/download/v1/app.nro") == "app.nro");
    CHECK(path::ExtractBasename("https://example.com/download/app.nro?token=123#hash") == "app.nro");
    CHECK(path::ExtractBasename("") == "");

    return 0;
}

static int test_parse_github_repo_url() {
    // Valid standard URLs
    auto r1 = path::ParseGitHubRepoUrl("https://github.com/owner/repo");
    CHECK(r1.has_value() && r1->owner == "owner" && r1->repo == "repo");

    auto r2 = path::ParseGitHubRepoUrl("http://github.com/Owner-1/Repo_2");
    CHECK(r2.has_value() && r2->owner == "Owner-1" && r2->repo == "Repo_2");

    auto r3 = path::ParseGitHubRepoUrl("https://www.github.com/my.name/cool-project");
    CHECK(r3.has_value() && r3->owner == "my.name" && r3->repo == "cool-project");

    // Trailing slash
    auto r4 = path::ParseGitHubRepoUrl("https://github.com/owner/repo/");
    CHECK(r4.has_value() && r4->owner == "owner" && r4->repo == "repo");

    // .git suffix removal
    auto r5 = path::ParseGitHubRepoUrl("https://github.com/owner/repo.git");
    CHECK(r5.has_value() && r5->owner == "owner" && r5->repo == "repo");

    auto r6 = path::ParseGitHubRepoUrl("https://github.com/owner/repo.GIT/");
    CHECK(r6.has_value() && r6->owner == "owner" && r6->repo == "repo");

    // Invalid URLs
    CHECK(!path::ParseGitHubRepoUrl("").has_value());
    CHECK(!path::ParseGitHubRepoUrl("ftp://github.com/owner/repo").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://other.com/owner/repo").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/owner").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/owner/").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/owner/repo/extra").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/owner/repo?token=123").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/owner/repo#readme").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://user:pass@github.com/owner/repo").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com:8080/owner/repo").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/../repo").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/owner/..").has_value());
    CHECK(!path::ParseGitHubRepoUrl("https://github.com/own er/repo").has_value());

    return 0;
}

static int test_is_valid_direct_asset_url() {
    // Valid direct URLs
    CHECK(path::IsValidDirectAssetUrl("https://example.com/file.zip"));
    CHECK(path::IsValidDirectAssetUrl("http://cdn.site.org:8080/downloads/app.nro"));
    CHECK(path::IsValidDirectAssetUrl("https://host.com/path/to/asset?token=abc%20123"));

    // Invalid direct URLs
    CHECK(!path::IsValidDirectAssetUrl(""));
    CHECK(!path::IsValidDirectAssetUrl("ftp://example.com/file.zip"));
    CHECK(!path::IsValidDirectAssetUrl("file:///sdmc/app.nro"));
    CHECK(!path::IsValidDirectAssetUrl("https:///file.zip")); // missing host
    CHECK(!path::IsValidDirectAssetUrl("https://"));
    CHECK(!path::IsValidDirectAssetUrl("https://user:pass@example.com/file.zip"));
    CHECK(!path::IsValidDirectAssetUrl("https://example.com/file.zip#frag"));
    CHECK(!path::IsValidDirectAssetUrl("https://example.com/file name.zip"));

    return 0;
}

static int test_is_valid_direct_zip_url() {
    CHECK(path::IsValidDirectZipUrl("https://example.com/file.zip"));
    CHECK(path::IsValidDirectZipUrl("https://example.com/archive.ZIP"));
    CHECK(path::IsValidDirectZipUrl("http://site.org/downloads/bundle.zip?token=123"));
    CHECK(!path::IsValidDirectZipUrl("https://example.com/file.nro"));
    CHECK(!path::IsValidDirectZipUrl("https://example.com/file.nro?fake=file.zip"));
    CHECK(!path::IsValidDirectZipUrl("ftp://example.com/file.zip"));
    CHECK(!path::IsValidDirectZipUrl(""));

    return 0;
}

static int test_is_valid_direct_nro_url() {
    CHECK(path::IsValidDirectNroUrl("https://example.com/app.nro"));
    CHECK(path::IsValidDirectNroUrl("https://example.com/game.NRO"));
    CHECK(path::IsValidDirectNroUrl("http://site.org/downloads/payload.nro?auth=xyz"));
    CHECK(!path::IsValidDirectNroUrl("https://example.com/archive.zip"));
    CHECK(!path::IsValidDirectNroUrl("https://example.com/archive.zip?fake=file.nro"));
    CHECK(!path::IsValidDirectNroUrl("ftp://example.com/app.nro"));
    CHECK(!path::IsValidDirectNroUrl(""));

    return 0;
}

static int test_is_valid_direct_download_url() {
    CHECK(path::IsValidDirectDownloadUrl("https://example.com/app.nro"));
    CHECK(path::IsValidDirectDownloadUrl("https://example.com/file.zip"));
    CHECK(path::IsValidDirectDownloadUrl("http://site.org/test.ZIP?q=1"));
    CHECK(path::IsValidDirectDownloadUrl("http://site.org/test.NRO?q=1"));
    CHECK(!path::IsValidDirectDownloadUrl("https://example.com/image.png"));
    CHECK(!path::IsValidDirectDownloadUrl("https://example.com/test.bin"));
    CHECK(!path::IsValidDirectDownloadUrl(""));

    return 0;
}

static int test_collapse_repeated_http_schemes() {
    CHECK(path::CollapseRepeatedHttpSchemes("https://github.com/a/b.zip") == "https://github.com/a/b.zip");
    CHECK(path::CollapseRepeatedHttpSchemes("https://https://github.com/a/b.zip") == "https://github.com/a/b.zip");
    CHECK(path::CollapseRepeatedHttpSchemes("https://HTTPS://github.com/a/b.nro") == "HTTPS://github.com/a/b.nro");
    CHECK(path::CollapseRepeatedHttpSchemes("http://https://cdn.example/x.zip") == "https://cdn.example/x.zip");
    CHECK(path::CollapseRepeatedHttpSchemes("  https://https://https://e.com/a.nro  ") == "https://e.com/a.nro");
    CHECK(path::CollapseRepeatedHttpSchemes("https://") == "https://");
    CHECK(path::CollapseRepeatedHttpSchemes("") == "");

    return 0;
}

// DBI's dbi.config: "SavesFolder=sdmc:/switch/DBI/saves/" (plan H7).
static int test_sd_folder_from_config_value() {
    // .dbi_save_info.ini from the user's card (DBI) and as Kefir Hub writes it (no trailing newline)
    CHECK(path::IniAccountName("TitleId=0100C3C015738000\r\nTitleName=Fall Guys\r\nBackupDate=2026-09-04 13:35:00\r\nAccount=Shark\r\nSpace=User\r\n") == "Shark");
    CHECK(path::IniAccountName("TitleId=0100C3C015738000\nTitleName=\nAccount=nin10do\nSpace=User") == "nin10do");
    CHECK(path::IniAccountName("account = Big Boss \nSpace=User") == "Big Boss");
    CHECK(path::IniAccountName("Account=") == "");
    CHECK(path::IniAccountName("TitleId=0100C3C015738000\nSpace=Device") == "");
    CHECK(path::IniAccountName("") == "");
    CHECK(path::IniAccountName("AccountId=7\nAccount=Kid") == "Kid");
    CHECK(path::SdFolderFromConfigValue("sdmc:/switch/DBI/saves/") == std::string{"/switch/DBI/saves"});
    CHECK(path::SdFolderFromConfigValue("SDMC:/DBISaves") == std::string{"/DBISaves"});
    CHECK(path::SdFolderFromConfigValue("/backups//dbi/") == std::string{"/backups/dbi"});

    // missing key, the SD root, relative paths and other devices fall back to the default.
    CHECK(!path::SdFolderFromConfigValue(""));
    CHECK(!path::SdFolderFromConfigValue("sdmc:/"));
    CHECK(!path::SdFolderFromConfigValue("sdmc:"));
    CHECK(!path::SdFolderFromConfigValue("switch/DBI/saves"));
    CHECK(!path::SdFolderFromConfigValue("ums0:/saves"));
    CHECK(!path::SdFolderFromConfigValue("sdmc:/switch/../saves"));
    return 0;
}

static int test_is_subpath_of() {
    // Exact match and trailing slashes
    CHECK(path::IsSubpathOf("/switch", "/switch"));
    CHECK(path::IsSubpathOf("/switch/", "/switch"));
    CHECK(path::IsSubpathOf("/switch", "/switch/"));
    CHECK(path::IsSubpathOf("/switch/", "/switch/"));
    CHECK(path::IsSubpathOf("/SWITCH/APP", "/switch"));
    CHECK(path::IsSubpathOf("/switch/app", "/SWITCH"));

    // Subpaths
    CHECK(path::IsSubpathOf("/switch/app.nro", "/switch"));
    CHECK(path::IsSubpathOf("/switch/folder/app.nro", "/switch"));
    CHECK(path::IsSubpathOf("/switch/a/b/c/d", "/switch"));
    CHECK(path::IsSubpathOf("/custom/nros/game.nro", "/custom/nros"));

    // Boundary prefix traps (must NOT match)
    CHECK(!path::IsSubpathOf("/switch2", "/switch"));
    CHECK(!path::IsSubpathOf("/switch2/app.nro", "/switch"));
    CHECK(!path::IsSubpathOf("/switchboard", "/switch"));
    CHECK(!path::IsSubpathOf("/switchboard/app.nro", "/switch"));
    CHECK(!path::IsSubpathOf("/switc", "/switch"));
    CHECK(!path::IsSubpathOf("/custom/nros2/game.nro", "/custom/nros"));

    // Root directory matching
    CHECK(path::IsSubpathOf("/switch", "/"));
    CHECK(path::IsSubpathOf("/anything/file", "/"));
    CHECK(path::IsSubpathOf("/", "/"));
    CHECK(!path::IsSubpathOf("relative/path", "/"));

    // Empty checks
    CHECK(!path::IsSubpathOf("", "/switch"));
    CHECK(!path::IsSubpathOf("/switch", ""));

    return 0;
}

static int test_is_nro_path() {
    CHECK(path::IsNroPath("/switch/app.nro"));
    CHECK(path::IsNroPath("/switch/APP.NRO"));
    CHECK(path::IsNroPath("/switch/folder/GAME.Nro"));
    CHECK(path::IsNroPath("app.nro"));
    CHECK(!path::IsNroPath("/switch/app.nro.bak"));
    CHECK(!path::IsNroPath("/switch/app.zip"));
    CHECK(!path::IsNroPath("/switch/nro"));
    CHECK(!path::IsNroPath("/switch/"));
    CHECK(!path::IsNroPath(""));

    return 0;
}

static int test_path_affects_homebrew() {
    const std::vector<std::string> custom_roots = {"/retroarch/cores", "/games/homebrew"};

    // Default /switch root: NRO files
    CHECK(path::PathAffectsHomebrew("/switch/app.nro"));
    CHECK(path::PathAffectsHomebrew("/switch/APP.NRO"));
    CHECK(path::PathAffectsHomebrew("/switch/folder/app.nro"));
    CHECK(path::PathAffectsHomebrew("/SWITCH/sub/game.NRO"));

    // Default /switch root: Non-NRO files do NOT affect catalog unless directory
    CHECK(!path::PathAffectsHomebrew("/switch/readme.txt"));
    CHECK(!path::PathAffectsHomebrew("/switch/image.png"));
    CHECK(!path::PathAffectsHomebrew("/switch/folder/config.ini"));

    // Default /switch root: Directory mutations DO affect catalog
    CHECK(path::PathAffectsHomebrew("/switch", {}, true));
    CHECK(path::PathAffectsHomebrew("/switch/folder", {}, true));
    CHECK(path::PathAffectsHomebrew("/switch/folder/subfolder", {}, true));

    // Prefix traps must NOT affect homebrew
    CHECK(!path::PathAffectsHomebrew("/switch2/app.nro"));
    CHECK(!path::PathAffectsHomebrew("/switchboard/app.nro"));
    CHECK(!path::PathAffectsHomebrew("/switch2/folder", {}, true));

    // Custom roots: NRO files
    CHECK(path::PathAffectsHomebrew("/retroarch/cores/fceumm_libretro.nro", custom_roots));
    CHECK(path::PathAffectsHomebrew("/GAMES/homebrew/doom.NRO", custom_roots));
    CHECK(!path::PathAffectsHomebrew("/retroarch/cores/info.txt", custom_roots));

    // Custom roots: Directory mutations
    CHECK(path::PathAffectsHomebrew("/retroarch/cores", custom_roots, true));
    CHECK(path::PathAffectsHomebrew("/games/homebrew/doom", custom_roots, true));

    // Completely unrelated paths
    CHECK(!path::PathAffectsHomebrew("/atmosphere/contents/0100000000001000/exefs.nsp"));
    CHECK(!path::PathAffectsHomebrew("/Nintendo/Album/2026/08/21/photo.jpg"));
    CHECK(!path::PathAffectsHomebrew("/atmosphere/kips", custom_roots, true));
    CHECK(!path::PathAffectsHomebrew(""));

    return 0;
}
int main() {
    if (test_equals_ic() ||
        test_starts_with_ic() ||
        test_ends_with_ic() ||
        test_extension() ||
        test_is_any_of_ic() ||
        test_parse_title_id_name() ||
        test_is_zip_asset() ||
        test_extract_basename() ||
        test_parse_github_repo_url() ||
        test_is_valid_direct_asset_url() ||
        test_is_valid_direct_zip_url() ||
        test_is_valid_direct_nro_url() ||
        test_is_valid_direct_download_url() ||
        test_collapse_repeated_http_schemes() ||
        test_sd_folder_from_config_value() ||
        test_is_subpath_of() ||
        test_is_nro_path() ||
        test_path_affects_homebrew()) {
        return 1;
    }
    std::printf("ok  path_util: %d checks passed\n", g_checks);
    return 0;
}
