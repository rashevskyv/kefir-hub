// Host test for sphaira/include/web_part_range.hpp
//
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_web_part_range.cpp -o /tmp/t && /tmp/t

#include "web_part_range.hpp"

#include <cstdint>
#include <cstdio>

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
    using sphaira::WebPartRangeOk;
    using sphaira::WEB_PART_MAX;

    CHECK(WebPartRangeOk(0, 1024, 1024));
    CHECK(WebPartRangeOk(1024, 1, 1025));
    CHECK(!WebPartRangeOk(1024, 1, 1024));       // starts at the end
    CHECK(!WebPartRangeOk(0, 1025, 1024));       // runs past the end
    CHECK(!WebPartRangeOk(-1, 10, 100));
    CHECK(!WebPartRangeOk(0, 0, 100));           // empty part
    CHECK(!WebPartRangeOk(0, -5, 100));
    CHECK(WebPartRangeOk(0, WEB_PART_MAX, 10 * WEB_PART_MAX));
    CHECK(!WebPartRangeOk(0, WEB_PART_MAX + 1, 10 * WEB_PART_MAX));
    CHECK(!WebPartRangeOk(INT64_MAX, 10, INT64_MAX)); // offset + len would overflow
    CHECK(WebPartRangeOk(5LL << 30, 1 << 20, 6LL << 30)); // offsets past 4 GiB

    std::printf("ok %d checks\n", g_checks);
    return 0;
}
