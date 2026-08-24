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
using sphaira::zip_extract::BuildZipTree;
using sphaira::zip_extract::NewFolderDest;
using sphaira::zip_extract::EntryMatchesSelection;
using sphaira::zip_extract::SelectedFilePrefixes;

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

    CHECK(NewFolderDest("/downloads", "Atmosphere.zip") == "/downloads/Atmosphere");
    CHECK(NewFolderDest("/", "pack.ZIP") == "/pack");
    CHECK(NewFolderDest("/downloads/", "a.zip") == "/downloads/a");

    {
        const std::vector<std::string_view> names{
            "atmosphere/kips/x", "atmosphere/package3", "switch/daybreak/daybreak.nro", "hbmenu.nro"};
        const auto t = BuildZipTree(names);
        CHECK(t.size() == 8);
        CHECK(t[0].label == "atmosphere/" && t[0].is_dir && t[0].depth == 0);
        CHECK(t[1].label == "kips/" && t[1].is_dir && t[1].depth == 1);
        CHECK(t[2].label == "x" && !t[2].is_dir && t[2].prefix == "atmosphere/kips/x");
        CHECK(t[3].label == "package3" && !t[3].is_dir);
        CHECK(t[4].label == "switch/" && t[4].is_dir);
        CHECK(t[7].label == "hbmenu.nro" && !t[7].is_dir && t[7].depth == 0);

        std::vector<char> checked(t.size(), 1);
        auto files = SelectedFilePrefixes(t, checked);
        CHECK(files.size() == 4);
        CHECK(EntryMatchesSelection("atmosphere/package3", files));
        CHECK(EntryMatchesSelection("hbmenu.nro", files));

        checked[2] = 0;
        files = SelectedFilePrefixes(t, checked);
        CHECK(!EntryMatchesSelection("atmosphere/kips/x", files));
        CHECK(EntryMatchesSelection("atmosphere/package3", files));
    }

    {
        const std::vector<std::string_view> names{"Awoo-Installer.nro"};
        const auto t = BuildZipTree(names);
        CHECK(t.size() == 1);
        CHECK(t[0].label == "Awoo-Installer.nro" && !t[0].is_dir);
    }

    return 0;
}

int main() {
    if (const int rc = test_zip_extract_plan()) {
        return rc;
    }
    std::printf("ok  zip_extract_plan: %d checks passed\n", g_checks);
    return 0;
}
