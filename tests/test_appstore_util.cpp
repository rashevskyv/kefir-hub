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
    // a direct url wins over the store layout and over the RetroArch special case
    assert(ResolveAppstoreZipUrl("sm64", "Super Mario 64", base, "https://x.y/sm64.zip") == "https://x.y/sm64.zip");
    assert(ResolveAppstoreZipUrl("RetroNX", "Retroarch", base, "https://x.y/ra.zip") == "https://x.y/ra.zip");

    // Source list: defaults first, then the user file (comments, blanks, CRLF, trailing slash, duplicates)
    auto sources = DefaultSources();
    assert(sources.size() == 2 && sources[0] == SOURCE_FORTHEUSERS && sources[1] == SOURCE_KEFIR_RECOMPILES);
    AppendSourceList("# my stores\r\n\r\n  https://a.b/store/  # trailing slash\r\nftp://nope\nhttps://a.b/store\nhttp://c.d\n", sources);
    assert(sources.size() == 4);
    assert(sources[2] == "https://a.b/store");
    assert(sources[3] == "http://c.d");
    AppendSourceList(std::string(SOURCE_FORTHEUSERS) + "/", sources); // a default repeated is ignored
    assert(sources.size() == 4);
    AppendSourceList("", sources);
    assert(sources.size() == 4);

    std::cout << "test_appstore_util passed\n";
    return 0;
}
