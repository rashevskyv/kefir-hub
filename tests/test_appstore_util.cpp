#include "ui/menus/appstore_util.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace sphaira::ui::menu::appstore;

    // Test RetroArch detection
    assert(IsRetroArchPackageName("RetroNX", "Retroarch"));
    assert(IsRetroArchPackageName("retroarch", "RetroArch"));
    assert(IsRetroArchPackageName("custom_ra", "RetroArch Emulator"));
    assert(!IsRetroArchPackageName("ftpd", "FTP Server"));
    assert(!IsRetroArchPackageName("edizon", "EdiZon"));

    // Test Zip URL resolution
    const std::string base = "https://switch.cdn.fortheusers.org";
    assert(ResolveAppstoreZipUrl("RetroNX", "Retroarch", base) == RETROARCH_NIGHTLY_URL);
    assert(ResolveAppstoreZipUrl("ftpd", "FTP Server", base) == "https://switch.cdn.fortheusers.org/zips/ftpd.zip");

    std::cout << "test_appstore_util passed\n";
    return 0;
}
