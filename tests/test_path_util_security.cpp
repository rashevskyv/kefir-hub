// Host test for sphaira/include/path_util.hpp path security, sanitization,
// and containment checks.
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_path_util_security.cpp -o /tmp/t && /tmp/t

#include "path_util.hpp"

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

static int test_normalize_save_archive_entry() {
    // Normal relative paths are preserved
    CHECK(path::NormalizeSaveArchiveEntry("folder/file") == "folder/file");
    CHECK(path::NormalizeSaveArchiveEntry("folder/") == "folder/");
    CHECK(path::NormalizeSaveArchiveEntry("a/b/c/d.bin") == "a/b/c/d.bin");
    CHECK(path::NormalizeSaveArchiveEntry(".nx_save_meta.bin") == ".nx_save_meta.bin");
    CHECK(path::NormalizeSaveArchiveEntry(".dbi_save_info.ini") == ".dbi_save_info.ini");

    // DBI-compatible single leading slash is stripped to relative path
    CHECK(path::NormalizeSaveArchiveEntry("/folder/file") == "folder/file");
    CHECK(path::NormalizeSaveArchiveEntry("/folder/") == "folder/");
    CHECK(path::NormalizeSaveArchiveEntry("/file") == "file");
    CHECK(path::NormalizeSaveArchiveEntry("/.nx_save_meta.bin") == ".nx_save_meta.bin");
    CHECK(path::NormalizeSaveArchiveEntry("/.dbi_save_info.ini") == ".dbi_save_info.ini");

    // Invalid / unsafe entries rejected
    CHECK(!path::NormalizeSaveArchiveEntry("").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("//folder/file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("///file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/../evil").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("../evil").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/folder/../../evil").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("folder/../../evil").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/.").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/..").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/./file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("folder\\file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/folder\\file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("c:/file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/c:/file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("sdmc:/file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/sdmc:/file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("/\x01/file").has_value());
    CHECK(!path::NormalizeSaveArchiveEntry("file\nname").has_value());

    return 0;
}

static int test_is_safe_destination_path() {
    // Valid destination paths within base
    CHECK(path::IsSafeDestinationPath("/folder/file", "/"));
    CHECK(path::IsSafeDestinationPath("/folder/", "/"));
    CHECK(path::IsSafeDestinationPath("/file", "/"));
    CHECK(path::IsSafeDestinationPath("/switch/app.nro", "/switch"));
    CHECK(path::IsSafeDestinationPath("/switch/folder/app.nro", "/switch"));

    // Traversal and escape attempts rejected
    CHECK(!path::IsSafeDestinationPath("", "/"));
    CHECK(!path::IsSafeDestinationPath("/../evil", "/"));
    CHECK(!path::IsSafeDestinationPath("/folder/../../evil", "/"));
    CHECK(!path::IsSafeDestinationPath("/folder/./file", "/"));
    CHECK(!path::IsSafeDestinationPath("//folder/file", "/"));
    CHECK(!path::IsSafeDestinationPath("/folder//file", "/"));
    CHECK(!path::IsSafeDestinationPath("sdmc:/folder/file", "/"));
    CHECK(!path::IsSafeDestinationPath("/folder\\file", "/"));
    CHECK(!path::IsSafeDestinationPath("/other/path", "/switch"));
    CHECK(!path::IsSafeDestinationPath("/switch2/app.nro", "/switch"));

    return 0;
}

static int test_is_safe_extraction_destination() {
    // Non-save default mode (save_dbi_compat = false) retains existing behavior,
    // explicitly allowing UMS device-prefixed destinations such as "ums0:/backups/..."
    CHECK(path::IsSafeExtractionDestination("ums0:/backups/file.txt", "ums0:/backups", false));
    CHECK(path::IsSafeExtractionDestination("ums0:/backups/folder/", "ums0:/backups", false));
    CHECK(path::IsSafeExtractionDestination("sdmc:/switch/app.nro", "sdmc:/switch", false));
    CHECK(path::IsSafeExtractionDestination("/switch/app.nro", "/switch", false));

    // Save mode (save_dbi_compat = true) enforces strict containment within base_path
    CHECK(path::IsSafeExtractionDestination("/folder/file", "/", true));
    CHECK(path::IsSafeExtractionDestination("/folder/", "/", true));
    CHECK(path::IsSafeExtractionDestination("/file", "/", true));

    // Save mode rejects escapes, device switches, traversal, and invalid chars
    CHECK(!path::IsSafeExtractionDestination("ums0:/backups/file.txt", "/", true));
    CHECK(!path::IsSafeExtractionDestination("sdmc:/switch/app.nro", "/", true));
    CHECK(!path::IsSafeExtractionDestination("/../evil", "/", true));
    CHECK(!path::IsSafeExtractionDestination("/folder/../../evil", "/", true));
    CHECK(!path::IsSafeExtractionDestination("/folder/./file", "/", true));
    CHECK(!path::IsSafeExtractionDestination("//folder/file", "/", true));
    CHECK(!path::IsSafeExtractionDestination("/folder//file", "/", true));
    CHECK(!path::IsSafeExtractionDestination("/folder\\file", "/", true));
    CHECK(!path::IsSafeExtractionDestination("", "/", true));

    return 0;
}

int main() {
    if (test_is_safe_archive_entry() ||
        test_normalize_absolute_sd_path() ||
        test_is_safe_filename() ||
        test_normalize_save_archive_entry() ||
        test_is_safe_destination_path() ||
        test_is_safe_extraction_destination()) {
        return 1;
    }
    std::printf("ok  path_util_security: %d checks passed\n", g_checks);
    return 0;
}
