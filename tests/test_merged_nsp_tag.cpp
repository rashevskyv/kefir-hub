// Host test for sphaira/include/merged_nsp_tag.hpp
//
// Build and run command:
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_merged_nsp_tag.cpp -o /tmp/test_merged_nsp_tag && /tmp/test_merged_nsp_tag

#include "merged_nsp_tag.hpp"

#include <cassert>
#include <cstdio>
#include <string>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_merged_nsp_tag() {
    using namespace sphaira::title;

    // [B]
    CHECK(FormatMergedNspTag(true, false, 0, 0) == "[B]");

    // [B+U65536]
    CHECK(FormatMergedNspTag(true, true, 65536, 0) == "[B+U65536]");

    // [B+9DLC]
    CHECK(FormatMergedNspTag(true, false, 0, 9) == "[B+9DLC]");

    // [B+U65536+9DLC]
    CHECK(FormatMergedNspTag(true, true, 65536, 9) == "[B+U65536+9DLC]");

    // Absent parts: no base
    CHECK(FormatMergedNspTag(false, true, 131072, 0) == "[U131072]");
    CHECK(FormatMergedNspTag(false, false, 0, 3) == "[3DLC]");
    CHECK(FormatMergedNspTag(false, true, 65536, 5) == "[U65536+5DLC]");

    // Empty tag when nothing present
    CHECK(FormatMergedNspTag(false, false, 0, 0) == "");

    return 0;
}

static int test_merged_nsp_filename() {
    using namespace sphaira::title;

    const std::string name = "Super Mario Odyssey";
    const uint64_t app_id = 0x0100000000010000ULL;

    CHECK(FormatMergedNspFilename(name, app_id, true, false, 0, 0)
          == "Super Mario Odyssey [0100000000010000][B].nsp");

    CHECK(FormatMergedNspFilename(name, app_id, true, true, 65536, 0)
          == "Super Mario Odyssey [0100000000010000][B+U65536].nsp");

    CHECK(FormatMergedNspFilename(name, app_id, true, false, 0, 9)
          == "Super Mario Odyssey [0100000000010000][B+9DLC].nsp");

    CHECK(FormatMergedNspFilename(name, app_id, true, true, 65536, 9)
          == "Super Mario Odyssey [0100000000010000][B+U65536+9DLC].nsp");

    return 0;
}

int main() {
    if (test_merged_nsp_tag() || test_merged_nsp_filename()) {
        return 1;
    }
    std::printf("ok  merged_nsp_tag: %d checks passed\n", g_checks);
    return 0;
}
