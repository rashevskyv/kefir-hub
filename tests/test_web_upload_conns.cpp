// Host test for sphaira/include/web_upload_conns.hpp
//
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_web_upload_conns.cpp -o /tmp/t && /tmp/t

#include "web_upload_conns.hpp"

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
    using namespace sphaira::web;
    CHECK(UploadConnsFixed(-1) == 0);
    CHECK(UploadConnsFixed(0) == 0);
    CHECK(UploadConnsFixed(1) == 0);
    CHECK(UploadConnsFixed(2) == 2);
    CHECK(UploadConnsFixed(3) == 3);
    CHECK(UploadConnsFixed(4) == 4);
    CHECK(UploadConnsFixed(5) == 4);
    CHECK(UploadConnsFixed(6) == 6);
    CHECK(UploadConnsFixed(7) == 6);
    CHECK(UploadConnsFixed(99) == 6);

    CHECK(!UploadConnsAuto(0));
    CHECK(!UploadConnsAuto(6));
    CHECK(UploadConnsAuto(7));
    CHECK(!UploadConnsAuto(8));

    CHECK(UploadConnsMax(0, false) == 0);
    CHECK(UploadConnsMax(0, true) == 0);
    CHECK(UploadConnsMax(3, false) == 3);
    CHECK(UploadConnsMax(3, true) == 3);
    CHECK(UploadConnsMax(4, true) == 4);
    CHECK(UploadConnsMax(6, false) == 6);
    CHECK(UploadConnsMax(6, true) == 5);
    CHECK(UploadConnsMax(7, false) == 6);
    CHECK(UploadConnsMax(7, true) == 5);
    std::printf("ok %d checks\n", g_checks);
    return 0;
}
