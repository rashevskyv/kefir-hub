// Host test for sphaira/include/zip_extract_plan.hpp
//
//     g++ -std=c++20 -I sphaira/include tests/test_zip_extract_plan.cpp -o /tmp/t && /tmp/t

#include "zip_extract_plan.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using sphaira::zip_extract::SuggestExtractPath;
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

static auto Suggest(std::initializer_list<std::string_view> names, std::string_view stem) -> std::string {
    const std::vector<std::string_view> v{names};
    return SuggestExtractPath(v, stem);
}

static int test_zip_extract_plan() {
    CHECK(FileStem("MyApp.nro") == "MyApp");
    CHECK(FileStem("pack.ZIP") == "pack");
    CHECK(FileStem("https://x/a/b/Foo.Bar.zip?x=1") == "Foo.Bar");

    CHECK(SuggestNakedNroPath("MyApp.nro") == "/switch/MyApp/MyApp.nro");
    CHECK(SuggestNakedNroPath("https://cdn/HB.nro") == "/switch/HB/HB.nro");

    CHECK(Suggest({"MyApp.nro"}, "x") == "/switch/MyApp");
    CHECK(Suggest({"MyApp.nro", "readme.txt"}, "x") == "/switch/MyApp");

    CHECK(Suggest({"MyApp/MyApp.nro"}, "x") == "/switch");
    CHECK(Suggest({"MyApp/MyApp.nro", "MyApp/config.ini"}, "x") == "/switch");
    CHECK(Suggest({"switch/app.nro"}, "x") == "/");

    CHECK(Suggest({"atmosphere/kips/x", "bootloader/hekate_ipl.ini"}, "pack") == "/downloads/pack");
    CHECK(Suggest({"A/a.txt", "B/b.txt"}, "multi") == "/downloads/multi");
    CHECK(Suggest({"A/a.nro", "B/b.txt"}, "mixed") == "/downloads/mixed");

    CHECK(Suggest({"a.nro", "b.nro"}, "two") == "/downloads");
    CHECK(Suggest({"config.ini", "icon.png"}, "misc") == "/downloads");
    CHECK(Suggest({"App/app.nro", "readme.txt"}, "x") == "/switch");

    CHECK(Suggest({"App/a.nro", "App/b.nro"}, "two") == "/downloads");
    CHECK(Suggest({"naked.nro", "folder/file.txt"}, "mix") == "/downloads");

    return 0;
}

int main() {
    if (const int rc = test_zip_extract_plan()) {
        return rc;
    }
    std::printf("ok  zip_extract_plan: %d checks passed\n", g_checks);
    return 0;
}
