// Footer hint row order: left buttons left of right ones, A rightmost, B next to it.
#include "../sphaira/include/ui/footer_order.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace sphaira::ui::footer;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

// the row as it reads on screen, left to right.
static auto Screen(std::vector<u64> v) -> std::vector<u64> {
    std::sort(v.begin(), v.end(), Before);
    std::reverse(v.begin(), v.end());
    return v;
}

int main() {
    // the fan curve set, given in map order.
    const auto fan = Screen({A, B, X, Y, L, R, ZL, ZR, PLUS});
    CHECK((fan == std::vector<u64>{PLUS, ZL, ZR, L, R, Y, X, B, A}));

    // sticks, the R2+Y footer-only hint and arrows.
    const auto all = Screen({ANY_UP, R3, A, ANY_RIGHT, ZR | Y, L3, ANY_LEFT, MINUS, Y, B});
    CHECK((all == std::vector<u64>{ANY_UP, ANY_LEFT, ANY_RIGHT, MINUS, L3, R3, ZR | Y, Y, B, A}));

    // an unlisted button goes to the far left and keeps a stable place.
    const u64 odd = 1ULL << 24;
    CHECK(Screen({A, odd, MINUS}).front() == odd);

    CHECK(IsPair(R, L));
    CHECK(IsPair(ZR, ZL));
    CHECK(IsPair(R3, L3));
    CHECK(IsPair(ANY_RIGHT, ANY_LEFT));
    CHECK(!IsPair(L, R));
    CHECK(!IsPair(B, A));

    std::printf("ok %d checks\n", g_checks);
    return 0;
}
