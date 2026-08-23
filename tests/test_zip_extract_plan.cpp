// Host test for sphaira/include/zip_extract_plan.hpp
//
//     g++ -std=c++20 -I sphaira/include tests/test_zip_extract_plan.cpp -o /tmp/t && /tmp/t

#include "zip_extract_plan.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using sphaira::zip_extract::FindSingleNro;
using sphaira::zip_extract::FormatZipRoots;
using sphaira::zip_extract::NroInstallDest;
using sphaira::zip_extract::SuggestNakedNroPath;
using sphaira::zip_extract::FileStem;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static auto Nro(std::initializer_list<std::string_view> names) -> std::optional<std::string> {
    const std::vector<std::string_view> v{names};
    return FindSingleNro(v);
}

static auto Roots(std::initializer_list<std::string_view> names) -> std::string {
    const std::vector<std::string_view> v{names};
    return FormatZipRoots(v);
}

static int test_zip_extract_plan() {
    CHECK(FileStem("MyApp.nro") == "MyApp");
    CHECK(FileStem("pack.ZIP") == "pack");

    CHECK(SuggestNakedNroPath("MyApp.nro") == "/switch/MyApp/MyApp.nro");
    CHECK(NroInstallDest("deep/nested/HB.nro") == "/switch/HB/HB.nro");
    CHECK(NroInstallDest("switch/appstore/appstore.nro") == "/switch/appstore/appstore.nro");

    CHECK(Nro({"MyApp.nro"}) == "MyApp.nro");
    CHECK(Nro({"MyApp/MyApp.nro", "MyApp/config.ini"}) == "MyApp/MyApp.nro");
    CHECK(Nro({"a/b/c/foo.nro"}) == "a/b/c/foo.nro");
    CHECK(Nro({"atmosphere/x", "hbmenu.nro"}) == "hbmenu.nro");
    CHECK(!Nro({"a.nro", "b.nro"}));
    CHECK(!Nro({"config.ini"}));

    CHECK(Roots({"atmosphere/kips/x", "bootloader/hekate_ipl.ini", "hbmenu.nro"})
        == "atmosphere/, bootloader/, hbmenu.nro");
    CHECK(Roots({"switch/appstore/appstore.nro"}) == "switch/");

    return 0;
}

int main() {
    if (const int rc = test_zip_extract_plan()) {
        return rc;
    }
    std::printf("ok  zip_extract_plan: %d checks passed\n", g_checks);
    return 0;
}
