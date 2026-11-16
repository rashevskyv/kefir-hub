#include "auto_update.hpp"

#include <cassert>
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

static int test_empty_assets() {
    std::vector<auto_update::ReleaseAsset> assets;
    CHECK(auto_update::SelectBestAsset(assets, "/switch/kefir-hub.nro") == -1);
    CHECK(auto_update::SelectBestAsset(assets, "") == -1);
    return 0;
}

static int test_exact_nro_match() {
    std::vector<auto_update::ReleaseAsset> assets = {
        { .name = "sphaira.nro", .browser_download_url = "https://example.com/sphaira.nro", .content_type = "", .size = 0 },
        { .name = "kefir-hub.nro", .browser_download_url = "https://example.com/kefir-hub.nro", .content_type = "", .size = 0 },
        { .name = "kefir-hub.zip", .browser_download_url = "https://example.com/kefir-hub.zip", .content_type = "", .size = 0 }
    };

    // If running kefir-hub.nro, kefir-hub.nro is selected (index 1)
    CHECK(auto_update::SelectBestAsset(assets, "/switch/kefir-hub.nro") == 1);

    // If running sphaira.nro, sphaira.nro is selected (index 0)
    CHECK(auto_update::SelectBestAsset(assets, "/switch/sphaira/sphaira.nro") == 0);
    return 0;
}

static int test_fallback_nro_match() {
    // Only sphaira.nro available, running as kefir-hub.nro
    std::vector<auto_update::ReleaseAsset> assets1 = {
        { .name = "source_code.tar.gz", .browser_download_url = "https://example.com/src.tar.gz", .content_type = "", .size = 0 },
        { .name = "sphaira.nro", .browser_download_url = "https://example.com/sphaira.nro", .content_type = "", .size = 0 }
    };
    CHECK(auto_update::SelectBestAsset(assets1, "/switch/kefir-hub.nro") == 1);

    // Only zip available
    std::vector<auto_update::ReleaseAsset> assets2 = {
        { .name = "README.md", .browser_download_url = "https://example.com/README.md", .content_type = "", .size = 0 },
        { .name = "kefir-hub.zip", .browser_download_url = "https://example.com/kefir-hub.zip", .content_type = "", .size = 0 }
    };
    CHECK(auto_update::SelectBestAsset(assets2, "/switch/kefir-hub.nro") == 1);
    return 0;
}

static int test_non_executable_ignored() {
    std::vector<auto_update::ReleaseAsset> assets = {
        { .name = "changelog.txt", .browser_download_url = "https://example.com/changelog.txt", .content_type = "", .size = 0 },
        { .name = "config.json", .browser_download_url = "https://example.com/config.json", .content_type = "", .size = 0 }
    };
    CHECK(auto_update::SelectBestAsset(assets, "/switch/kefir-hub.nro") == -1);
    return 0;
}

static int test_running_from_hbmenu() {
    std::vector<auto_update::ReleaseAsset> assets = {
        { .name = "other.zip", .browser_download_url = "https://example.com/other.zip", .content_type = "", .size = 0 },
        { .name = "kefir-hub.nro", .browser_download_url = "https://example.com/kefir-hub.nro", .content_type = "", .size = 0 }
    };
    CHECK(auto_update::SelectBestAsset(assets, "/hbmenu.nro") == 1);
    return 0;
}

// started as /hbmenu.nro the update replaces /hbmenu.nro, never a copy under /switch
static int test_install_destination() {
    CHECK(auto_update::InstallDestinationFor("/hbmenu.nro") == "/hbmenu.nro");
    CHECK(auto_update::InstallDestinationFor("/switch/kefir-hub/kefir-hub.nro") == "/switch/kefir-hub/kefir-hub.nro");
    CHECK(auto_update::InstallDestinationFor("") == "/hbmenu.nro");
    return 0;
}

// What's new: only the releases between the installed and the offered version, newest first, own titles dropped
static int test_release_notes_between() {
    const std::vector<auto_update::ReleaseNote> releases = {
        { .tag = "0.14.391", .body = "# Kefir Hub 0.14.391\r\n\r\nUpdate from 0.14.378.\r\n\r\n### Saves\r\n- old" },
        { .tag = "0.14.394", .body = "# Kefir Hub 0.14.394\n\nUpdate from 0.14.391.\n\n### USB and MTP\n- saves writable" },
        { .tag = "0.14.395", .body = "### Later\n- not offered yet" },
        { .tag = "0.14.392", .body = "### Games\n- delete one DLC" },
        { .tag = "0.14.393", .body = "" },
    };
    const auto text = auto_update::ReleaseNotesBetween(releases, "0.14.391", "0.14.394");
    CHECK(text.find("0.14.391") == std::string::npos);
    CHECK(text.find("0.14.395") == std::string::npos);
    CHECK(text.find("not offered") == std::string::npos);
    CHECK(text.find("Update from") == std::string::npos);
    CHECK(text.find("# Kefir Hub 0.14.394\n") == 0);
    const auto p394 = text.find("saves writable");
    const auto p393 = text.find("# Kefir Hub 0.14.393\n");
    const auto p392 = text.find("delete one DLC");
    CHECK(p394 != std::string::npos && p393 != std::string::npos && p392 != std::string::npos);
    CHECK(p394 < p393 && p393 < p392);
    // the body's own title is replaced, not repeated
    CHECK(text.find("# Kefir Hub 0.14.394", 1) == std::string::npos);
    CHECK(text.find('\r') == std::string::npos);
    CHECK(auto_update::ReleaseNotesBetween(releases, "0.14.394", "0.14.394").empty());
    return 0;
}

int main() {
    if (test_release_notes_between() != 0) return 1;
    if (test_empty_assets() != 0) return 1;
    if (test_exact_nro_match() != 0) return 1;
    if (test_fallback_nro_match() != 0) return 1;
    if (test_non_executable_ignored() != 0) return 1;
    if (test_running_from_hbmenu() != 0) return 1;
    if (test_install_destination() != 0) return 1;

    std::printf("ok  auto_update_asset: %d checks passed\n", g_checks);
    return 0;
}

