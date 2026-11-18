// Host test for sphaira/include/speed_test.hpp
//
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_speed_test.cpp -o /tmp/t && /tmp/t

#include "speed_test.hpp"

#include <cstdio>

static int g_checks = 0;

#define CHECK(expr)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(expr)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
            return 1;                                                   \
        }                                                               \
    } while (0)

int main() {
    using sphaira::speedtest::FormatRate;
    CHECK(FormatRate(6'200'000, 1000) == "6.2 MB/s (50 Mbit/s)");
    CHECK(FormatRate(25'000'000, 1000) == "25.0 MB/s (200 Mbit/s)");
    CHECK(FormatRate(12'500'000, 500) == "25.0 MB/s (200 Mbit/s)"); // half a second
    CHECK(FormatRate(0, 1000) == "-");
    CHECK(FormatRate(1000, 0) == "-");
    CHECK(FormatRate(-5, 100) == "-");
    std::printf("ok %d checks\n", g_checks);
    return 0;
}
