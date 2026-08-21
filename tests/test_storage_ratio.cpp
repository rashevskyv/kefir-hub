// Host test for sphaira/include/storage_ratio.hpp
//
// Build and run command:
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_storage_ratio.cpp -o /tmp/test_storage_ratio && /tmp/test_storage_ratio

#include "storage_ratio.hpp"

#include <cassert>
#include <cmath>
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

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) < (eps))

static int test_storage_ratio() {
    using namespace sphaira::ui;

    // total == 0 (query failed or not inserted)
    CHECK_NEAR(CalculateStorageUsedRatio(0, 0), 0.0, 1e-6);
    CHECK_NEAR(CalculateStorageUsedRatio(0, 1000), 0.0, 1e-6);
    CHECK_NEAR(CalculateStorageFreeGb(0, 0), 0.0, 1e-6);
    CHECK_NEAR(CalculateStorageFreeGb(0, 1000), 0.0, 1e-6);

    // total < 0 (error code or invalid)
    CHECK_NEAR(CalculateStorageUsedRatio(-100, 50), 0.0, 1e-6);
    CHECK_NEAR(CalculateStorageFreeGb(-100, 50), 0.0, 1e-6);

    // free < 0 (underflow / invalid)
    CHECK_NEAR(CalculateStorageUsedRatio(100, -10), 1.0, 1e-6);
    CHECK_NEAR(CalculateStorageFreeGb(100, -10), 0.0, 1e-6);

    // free > total (overflow / corrupted metadata)
    CHECK_NEAR(CalculateStorageUsedRatio(100, 150), 0.0, 1e-6);
    CHECK_NEAR(CalculateStorageFreeGb(100, 150), 100.0 / 0x40000000, 1e-6);

    // normal 50% used
    CHECK_NEAR(CalculateStorageUsedRatio(100, 50), 0.5, 1e-6);

    // normal 75% used (25 free out of 100)
    CHECK_NEAR(CalculateStorageUsedRatio(100, 25), 0.75, 1e-6);

    // normal 100% used (0 free out of 100)
    CHECK_NEAR(CalculateStorageUsedRatio(100, 0), 1.0, 1e-6);

    // normal 0% used (100 free out of 100)
    CHECK_NEAR(CalculateStorageUsedRatio(100, 100), 0.0, 1e-6);

    return 0;
}

int main() {
    if (test_storage_ratio() != 0) return 1;

    std::printf("ok  storage_ratio: %d checks passed\n", g_checks);
    return 0;
}
