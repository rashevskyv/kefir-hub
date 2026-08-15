// Host test for sphaira/include/version_compare.hpp -- the dotted-version logic
// that firmware_menu.cpp and kefir_firmware.cpp used to each carry their own
// copy of. Nothing here needs a Switch, so it builds and runs anywhere:
//
//     g++ -std=c++20 -I sphaira/include tests/test_version_compare.cpp -o /tmp/t && /tmp/t
//
// or just: tests/run.sh

#include "version_compare.hpp"

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

static int test_parse() {
    CHECK((version::Parse("20.1.5") == std::vector<int>{20, 1, 5}));
    CHECK((version::Parse("1.0.0") == std::vector<int>{1, 0, 0}));
    CHECK((version::Parse("20") == std::vector<int>{20}));
    CHECK((version::Parse("") == std::vector<int>{}));

    // empty segments are skipped, not treated as zero
    CHECK((version::Parse("1..2") == std::vector<int>{1, 2}));
    CHECK((version::Parse(".1.2") == std::vector<int>{1, 2}));

    // parsing stops at the first non-numeric segment
    CHECK((version::Parse("1.2.beta") == std::vector<int>{1, 2}));
    CHECK((version::Parse("beta") == std::vector<int>{}));

    // more than three components is preserved -- this is what makes it NOT
    // interchangeable with App::GetVersionFromString
    CHECK((version::Parse("1.2.3.4") == std::vector<int>{1, 2, 3, 4}));

    // trailing junk inside an otherwise numeric segment: strtol takes the number
    CHECK((version::Parse("20.1rc.5") == std::vector<int>{20, 1, 5}));
    return 0;
}

static int test_is_lower() {
    // the case the firmware menu actually asks: is the target older than what
    // is installed (i.e. would installing it be a downgrade)?
    CHECK(version::IsLower("19.0.1", "20.0.0"));
    CHECK(!version::IsLower("20.0.0", "19.0.1"));
    CHECK(!version::IsLower("20.0.0", "20.0.0"));

    // ordering must be per-component, not lexicographic: "9" > "10" as strings
    CHECK(version::IsLower("9.0.0", "10.0.0"));
    CHECK(!version::IsLower("10.0.0", "9.0.0"));

    // and not decimal either: 20.10 is newer than 20.9
    CHECK(version::IsLower("20.9.0", "20.10.0"));

    // missing components count as zero
    CHECK(!version::IsLower("20", "20.0.0"));
    CHECK(!version::IsLower("20.0.0", "20"));
    CHECK(version::IsLower("20", "20.0.1"));
    CHECK(version::IsLower("20.0", "20.1"));

    // differences in later components still decide it
    CHECK(version::IsLower("20.1.4", "20.1.5"));
    CHECK(!version::IsLower("20.1.5", "20.1.4"));

    // unparseable compares as empty, i.e. all zeroes
    CHECK(version::IsLower("beta", "1.0.0"));
    CHECK(!version::IsLower("1.0.0", "beta"));
    CHECK(!version::IsLower("", ""));
    return 0;
}

static int test_format_packed() {
    // layout: major << 26 | minor << 20 | micro << 16
    const auto pack = [](std::uint32_t maj, std::uint32_t min, std::uint32_t mic) {
        return (maj << 26) | (min << 20) | (mic << 16);
    };
    CHECK(version::FormatPacked(pack(20, 1, 5)) == "20.1.5");
    CHECK(version::FormatPacked(pack(1, 0, 0)) == "1.0.0");
    CHECK(version::FormatPacked(pack(4, 0, 0)) == "4.0.0");
    CHECK(version::FormatPacked(4 << 26) == "4.0.0");
    CHECK(version::FormatPacked(0) == "0.0.0");

    // each field is masked to its own width, so the low 16 bits are ignored
    CHECK(version::FormatPacked(pack(20, 1, 5) | 0xFFFF) == "20.1.5");

    // widest values each field can hold
    CHECK(version::FormatPacked(pack(31, 31, 15)) == "31.31.15");

    // a real one: 20.1.5 as amssu reports it
    CHECK(version::FormatPacked(0x50100000) == "20.1.0");

    // round trip through Parse
    CHECK((version::Parse(version::FormatPacked(pack(18, 1, 0)))
           == std::vector<int>{18, 1, 0}));
    return 0;
}

int main() {
    if (test_parse() || test_is_lower() || test_format_packed()) {
        return 1;
    }
    std::printf("ok  version_compare: %d checks passed\n", g_checks);
    return 0;
}
