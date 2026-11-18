#pragma once

#include <cstdint>

// Order of the footer hint row. Pure (no libnx) so tests/test_footer_order.cpp runs it;
// widget.cpp static_asserts these bits against the Button enum.
namespace sphaira::ui::footer {

using u64 = std::uint64_t;

inline constexpr u64 A = 1ULL << 0, B = 1ULL << 1, X = 1ULL << 2, Y = 1ULL << 3;
inline constexpr u64 L3 = 1ULL << 4, R3 = 1ULL << 5, L = 1ULL << 6, R = 1ULL << 7;
inline constexpr u64 ZL = 1ULL << 8, ZR = 1ULL << 9, PLUS = 1ULL << 10, MINUS = 1ULL << 11;
inline constexpr u64 DPAD_LEFT = 1ULL << 12, DPAD_UP = 1ULL << 13, DPAD_RIGHT = 1ULL << 14, DPAD_DOWN = 1ULL << 15;
inline constexpr u64 ANY_LEFT = DPAD_LEFT | 1ULL << 16 | 1ULL << 20;
inline constexpr u64 ANY_UP = DPAD_UP | 1ULL << 17 | 1ULL << 21;
inline constexpr u64 ANY_RIGHT = DPAD_RIGHT | 1ULL << 18 | 1ULL << 22;
inline constexpr u64 ANY_DOWN = DPAD_DOWN | 1ULL << 19 | 1ULL << 23;

// The row from right to left. On screen it reads:
// arrows  -  +  ZL ZR  L R  L3 R3  ZR+Y  Y  X  B  A
// A (confirm) is the rightmost, B (cancel) is next to it, a left button is always left of its right twin.
inline constexpr u64 ORDER[] = {
    A, B, X, Y, ZR | Y,
    R3, L3, R, L, ZR, ZL,
    PLUS, MINUS,
    ANY_RIGHT, ANY_LEFT, DPAD_RIGHT, DPAD_LEFT, ANY_DOWN, ANY_UP, DPAD_DOWN, DPAD_UP,
};

// slot in the row, 0 = rightmost. A button that is not listed goes to the far left.
constexpr int Rank(u64 button) {
    int i = 0;
    for (const u64 e : ORDER) {
        if (e == button) {
            return i;
        }
        i++;
    }
    return i;
}

// drawn before (to the right of) the other one.
constexpr bool Before(u64 lhs, u64 rhs) {
    const int l = Rank(lhs), r = Rank(rhs);
    return l != r ? l < r : lhs < rhs;
}

// two neighbours of the row, the right one first, that a line break must not separate.
constexpr bool IsPair(u64 right, u64 left) {
    return (right == R && left == L) || (right == ZR && left == ZL) || (right == R3 && left == L3) ||
           (right == ANY_RIGHT && left == ANY_LEFT) || (right == DPAD_RIGHT && left == DPAD_LEFT);
}

} // namespace sphaira::ui::footer
