// parental::MapZipEntry against the real NSParentalControl 2.4.0 release zip listing.
#include "parental_controls.hpp"

#include <cstdio>

using sphaira::parental::MapZipEntry;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main() {
    // the version folder on top is dropped
    CHECK(MapZipEntry("2.4.0/atmosphere/contents/4200000000003103/exefs.nsp") == "/atmosphere/contents/4200000000003103/exefs.nsp");
    CHECK(MapZipEntry("2.4.0/atmosphere/contents/4200000000003103/toolbox.json") == "/atmosphere/contents/4200000000003103/toolbox.json");
    CHECK(MapZipEntry("2.4.0/atmosphere/contents/4200000000003103/flags/") == "/atmosphere/contents/4200000000003103/flags/");
    CHECK(MapZipEntry("2.4.0/atmosphere/contents/4200000000003103/flags/boot2.flag") == "/atmosphere/contents/4200000000003103/flags/boot2.flag");
    CHECK(MapZipEntry("2.4.0/switch/.overlays/parental_control.ovl") == "/switch/.overlays/parental_control.ovl");
    // a zip without the version folder works too
    CHECK(MapZipEntry("switch/.overlays/parental_control.ovl") == "/switch/.overlays/parental_control.ovl");

    // junk and folders above the payload are skipped
    CHECK(!MapZipEntry("2.4.0/"));
    CHECK(!MapZipEntry("2.4.0/atmosphere/"));
    CHECK(!MapZipEntry("2.4.0/atmosphere/contents/"));
    CHECK(!MapZipEntry("2.4.0/atmosphere/contents/4200000000003103/.DS_Store"));
    CHECK(!MapZipEntry("__MACOSX/2.4.0/atmosphere/contents/4200000000003103/._.DS_Store"));
    // nothing outside the sysmodule folder and the overlay folder
    CHECK(!MapZipEntry("2.4.0/atmosphere/contents/0100000000001000/romfs/x"));
    CHECK(!MapZipEntry("2.4.0/switch/kefir-hub.nro"));
    CHECK(!MapZipEntry("2.4.0/switch/.overlays/../../hbmenu.nro"));
    CHECK(!MapZipEntry("readme.txt"));

    std::printf("test_parental_zip_map: %d checks ok\n", g_checks);
    return 0;
}
