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

int main() {
    if (test_empty_assets() != 0) return 1;
    if (test_exact_nro_match() != 0) return 1;
    if (test_fallback_nro_match() != 0) return 1;
    if (test_non_executable_ignored() != 0) return 1;
    if (test_running_from_hbmenu() != 0) return 1;

    std::printf("ok  auto_update_asset: %d checks passed\n", g_checks);
    return 0;
}

