// Host test for List::Draw traversal order (sphaira/include/ui/list_draw_order.hpp)
// Verifies that focused items are drawn last to avoid focus outline overlap,
// while preserving visibility, scissor clipping, geometry, and single-execution guarantees.
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_list_draw_order.cpp -o /tmp/t && /tmp/t

#include <cstdint>
#include <cstdio>
#include <vector>
#include "ui/list_draw_order.hpp"

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

namespace {

struct Vec4 {
    float x{0.f};
    float y{0.f};
    float w{0.f};
    float h{0.f};
};

struct DrawRecord {
    int64_t index;
    Vec4 rect;
};

static constexpr int64_t NO_FOCUS = -1;

int TestHomeAdjacentFocus() {
    // 2 adjacent items: index 0 and 1. Focus on 0.
    // Non-focused index 1 must execute first; focused index 0 must execute last.
    std::vector<DrawRecord> calls;
    Vec4 initial{100.f, 200.f, 50.f, 50.f};
    float step_x = 60.f; // 50w + 10pad
    float min_x = 0.f;
    float max_x = 1280.f;
    int64_t count = 2;
    int64_t focus = 0;

    sphaira::ui::draw_order::TraverseHome(
        initial, step_x, min_x, max_x, count, focus,
        [&](const Vec4& v, int64_t i) {
            calls.push_back({i, v});
        }
    );

    CHECK(calls.size() == 2);
    CHECK(calls[0].index == 1);
    CHECK(calls[0].rect.x == 160.f);
    CHECK(calls[1].index == 0);
    CHECK(calls[1].rect.x == 100.f);
    return 0;
}

int TestHomeMiddleAndEndFocus() {
    // 5 visible items: 0, 1, 2, 3, 4.
    Vec4 initial{0.f, 100.f, 100.f, 100.f};
    float step_x = 120.f;
    float min_x = 0.f;
    float max_x = 1280.f;
    int64_t count = 5;

    // Middle focus: 2
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseHome(
            initial, step_x, min_x, max_x, count, 2,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.size() == 5);
        CHECK(calls[0].index == 0);
        CHECK(calls[1].index == 1);
        CHECK(calls[2].index == 3);
        CHECK(calls[3].index == 4);
        CHECK(calls[4].index == 2);
        CHECK(calls[4].rect.x == 240.f);
    }

    // End focus: 4
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseHome(
            initial, step_x, min_x, max_x, count, 4,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.size() == 5);
        CHECK(calls[0].index == 0);
        CHECK(calls[1].index == 1);
        CHECK(calls[2].index == 2);
        CHECK(calls[3].index == 3);
        CHECK(calls[4].index == 4);
        CHECK(calls[4].rect.x == 480.f);
    }
    return 0;
}

int TestHomeEmptyAndInvalidFocus() {
    Vec4 initial{100.f, 100.f, 50.f, 50.f};
    float step_x = 60.f;
    float min_x = 0.f;
    float max_x = 1280.f;

    // Empty list (count = 0)
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseHome(
            initial, step_x, min_x, max_x, 0, 0,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.empty());
    }

    // NO_FOCUS (-1)
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseHome(
            initial, step_x, min_x, max_x, 3, NO_FOCUS,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.size() == 3);
        CHECK(calls[0].index == 0);
        CHECK(calls[1].index == 1);
        CHECK(calls[2].index == 2);
    }

    // Out-of-bounds focus index (e.g. 99)
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseHome(
            initial, step_x, min_x, max_x, 3, 99,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.size() == 3);
        CHECK(calls[0].index == 0);
        CHECK(calls[1].index == 1);
        CHECK(calls[2].index == 2);
    }
    return 0;
}

int TestHomeScrolledOffFocus() {
    // Items 0 and 1 are scrolled off to the left (v.x + v.w < min_x).
    // Focus is on index 0 (which is invisible).
    // Index 0 must NOT be drawn.
    Vec4 initial{-200.f, 100.f, 50.f, 50.f}; // item 0: x = -200, w = 50 -> right = -150 < min_x (0)
    float step_x = 100.f;                     // item 1: x = -100, right = -50 < 0
                                              // item 2: x = 0, right = 50 >= 0 (visible)
                                              // item 3: x = 100 (visible)
    float min_x = 0.f;
    float max_x = 1280.f;
    int64_t count = 4;
    int64_t focus = 0; // scrolled off

    std::vector<DrawRecord> calls;
    sphaira::ui::draw_order::TraverseHome(
        initial, step_x, min_x, max_x, count, focus,
        [&](const Vec4& v, int64_t i) {
            calls.push_back({i, v});
        }
    );

    // Only items 2 and 3 should be drawn. Item 0 is off-screen.
    CHECK(calls.size() == 2);
    CHECK(calls[0].index == 2);
    CHECK(calls[1].index == 3);
    return 0;
}

int TestGridAdjacentFocus() {
    // 2x2 grid (row_count = 2), items 0, 1, 2, 3. Focus on 0.
    Vec4 initial{100.f, 100.f, 50.f, 50.f};
    int64_t row_count = 2;
    float step_x = 60.f;
    float step_y = 60.f;
    float min_x = 0.f;
    float max_x = 1280.f;
    float min_y = 0.f;
    float max_y = 720.f;
    int64_t count = 4;
    int64_t focus = 0;

    std::vector<DrawRecord> calls;
    sphaira::ui::draw_order::TraverseGrid(
        initial, row_count, step_x, step_y, min_x, max_x, min_y, max_y, count, focus,
        [&](const Vec4& v, int64_t i) {
            calls.push_back({i, v});
        }
    );

    CHECK(calls.size() == 4);
    CHECK(calls[0].index == 1);
    CHECK(calls[1].index == 2);
    CHECK(calls[2].index == 3);
    CHECK(calls[3].index == 0); // focus drawn last
    CHECK(calls[3].rect.x == 100.f);
    CHECK(calls[3].rect.y == 100.f);
    return 0;
}

int TestGridMiddleAndEndFocus() {
    // 3x3 grid (row_count = 3), items 0..8.
    Vec4 initial{50.f, 50.f, 40.f, 40.f};
    int64_t row_count = 3;
    float step_x = 50.f;
    float step_y = 50.f;
    float min_x = 0.f;
    float max_x = 1280.f;
    float min_y = 0.f;
    float max_y = 720.f;
    int64_t count = 9;

    // Middle focus: 4
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseGrid(
            initial, row_count, step_x, step_y, min_x, max_x, min_y, max_y, count, 4,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.size() == 9);
        CHECK(calls[0].index == 0);
        CHECK(calls[1].index == 1);
        CHECK(calls[2].index == 2);
        CHECK(calls[3].index == 3);
        CHECK(calls[4].index == 5);
        CHECK(calls[5].index == 6);
        CHECK(calls[6].index == 7);
        CHECK(calls[7].index == 8);
        CHECK(calls[8].index == 4); // focus drawn last
        CHECK(calls[8].rect.x == 100.f);
        CHECK(calls[8].rect.y == 100.f);
    }

    // End focus: 8
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseGrid(
            initial, row_count, step_x, step_y, min_x, max_x, min_y, max_y, count, 8,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.size() == 9);
        for (int64_t i = 0; i < 8; i++) {
            CHECK(calls[static_cast<size_t>(i)].index == i);
        }
        CHECK(calls[8].index == 8);
        CHECK(calls[8].rect.x == 150.f);
        CHECK(calls[8].rect.y == 150.f);
    }
    return 0;
}

int TestGridScrolledOffFocus() {
    // 2x4 grid (row_count = 2), items 0..7.
    // Row 0 (items 0, 1) is scrolled above min_y (v.y + v.h < min_y).
    // Focus is on item 0. Item 0 must NOT be drawn.
    Vec4 initial{50.f, -100.f, 40.f, 40.f}; // row 0: y = -100, y + h = -60 < min_y (0)
    int64_t row_count = 2;
    float step_x = 50.f;
    float step_y = 60.f;
    float min_x = 0.f;
    float max_x = 1280.f;
    float min_y = 10.f;
    float max_y = 720.f;
    int64_t count = 8;
    int64_t focus = 0; // scrolled off above min_y

    std::vector<DrawRecord> calls;
    sphaira::ui::draw_order::TraverseGrid(
        initial, row_count, step_x, step_y, min_x, max_x, min_y, max_y, count, focus,
        [&](const Vec4& v, int64_t i) {
            calls.push_back({i, v});
        }
    );

    // Row 0 items (0 and 1) have y + h = -60 < 10, so they are skipped.
    // Focused item 0 must NOT be drawn.
    for (const auto& call : calls) {
        CHECK(call.index != 0);
        CHECK(call.index != 1);
    }
    CHECK(!calls.empty());
    return 0;
}

int TestGridLeftClippedFocus() {
    Vec4 initial{-10.f, 50.f, 40.f, 40.f};
    std::vector<DrawRecord> calls;
    sphaira::ui::draw_order::TraverseGrid(
        initial, 2, 50.f, 50.f, 0.f, 1280.f, 0.f, 720.f, 2, 0,
        [&](const Vec4& v, int64_t i) {
            calls.push_back({i, v});
        }
    );

    CHECK(calls.size() == 1);
    CHECK(calls[0].index == 1);
    CHECK(calls[0].rect.x == 40.f);
    return 0;
}

int TestGridEmptyAndNoFocus() {
    Vec4 initial{50.f, 50.f, 40.f, 40.f};
    int64_t row_count = 2;
    float step_x = 50.f;
    float step_y = 50.f;
    float min_x = 0.f;
    float max_x = 1280.f;
    float min_y = 0.f;
    float max_y = 720.f;

    // Empty list
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseGrid(
            initial, row_count, step_x, step_y, min_x, max_x, min_y, max_y, 0, 0,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.empty());
    }

    // NO_FOCUS (-1)
    {
        std::vector<DrawRecord> calls;
        sphaira::ui::draw_order::TraverseGrid(
            initial, row_count, step_x, step_y, min_x, max_x, min_y, max_y, 4, NO_FOCUS,
            [&](const Vec4& v, int64_t i) {
                calls.push_back({i, v});
            }
        );
        CHECK(calls.size() == 4);
        for (int64_t i = 0; i < 4; i++) {
            CHECK(calls[static_cast<size_t>(i)].index == i);
        }
    }
    return 0;
}

} // namespace

int main() {
    if (TestHomeAdjacentFocus()) return 1;
    if (TestHomeMiddleAndEndFocus()) return 1;
    if (TestHomeEmptyAndInvalidFocus()) return 1;
    if (TestHomeScrolledOffFocus()) return 1;
    if (TestGridAdjacentFocus()) return 1;
    if (TestGridMiddleAndEndFocus()) return 1;
    if (TestGridScrolledOffFocus()) return 1;
    if (TestGridLeftClippedFocus()) return 1;
    if (TestGridEmptyAndNoFocus()) return 1;

    std::printf("OK  test_list_draw_order (%d checks passed)\n", g_checks);
    return 0;
}
