// Unit test for title font scaling constraints and 2-row footer layout calculations
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static auto CalculateTitleFont(float title_w, float avail_w) -> std::pair<float, bool> {
    constexpr float BASE_TITLE_FONT = 28.f;
    constexpr float MIN_TITLE_FONT = BASE_TITLE_FONT * 0.60f; // 16.8f

    if (title_w <= avail_w || avail_w <= 0.f) {
        return {BASE_TITLE_FONT, false};
    }

    const float scale = avail_w / title_w;
    if (scale >= 0.60f) {
        return {BASE_TITLE_FONT * scale, false};
    }

    return {MIN_TITLE_FONT, true};
}

static int test_title_font_scaling() {
    // 1. Fits within available width -> full 28px font, no scrolling
    auto [f1, s1] = CalculateTitleFont(500.f, 950.f);
    CHECK(f1 == 28.f);
    CHECK(!s1);

    // 2. Exactly equal -> full 28px font, no scrolling
    auto [f2, s2] = CalculateTitleFont(950.f, 950.f);
    CHECK(f2 == 28.f);
    CHECK(!s2);

    // 3. Slightly larger (needs 20% reduction, scale 0.80) -> scaled font ~22.4px, no scrolling
    auto [f3, s3] = CalculateTitleFont(1000.f, 800.f);
    CHECK(std::abs(f3 - 22.4f) < 0.01f);
    CHECK(!s3);

    // 4. Exceeds by 40% (scale 0.60) -> minimum font 16.8px, no scrolling
    auto [f4, s4] = CalculateTitleFont(1000.f, 600.f);
    CHECK(std::abs(f4 - 16.8f) < 0.01f);
    CHECK(!s4);

    // 5. Exceeds beyond 40% (scale 0.50) -> minimum font 16.8px, scrolling enabled
    auto [f5, s5] = CalculateTitleFont(1200.f, 600.f);
    CHECK(std::abs(f5 - 16.8f) < 0.01f);
    CHECK(s5);

    // 6. Huge filename (2000px, available 800px) -> minimum font 16.8px, scrolling enabled
    auto [f6, s6] = CalculateTitleFont(2000.f, 800.f);
    CHECK(std::abs(f6 - 16.8f) < 0.01f);
    CHECK(s6);

    return 0;
}

static int test_footer_2row_split_and_justified() {
    // 8 buttons in image viewer: START, A, B, X, Y, L2, ZR, LEFT
    std::vector<float> btn_widths = {70.f, 115.f, 60.f, 70.f, 125.f, 115.f, 95.f, 185.f};

    float total_content = 0.f;
    for (float w : btn_widths) total_content += w;
    CHECK(total_content == 835.f);

    constexpr float avail = 1190.f; // 1220 - 30

    // Find best split k that minimizes |W_bottom - W_top|
    size_t best_k = 0;
    float best_diff = 1e9f;
    float best_max_w = 1e9f;

    float current_bottom_w = 0.f;
    for (size_t k = 1; k < btn_widths.size(); k++) {
        current_bottom_w += btn_widths[k - 1];
        const float current_top_w = total_content - current_bottom_w;

        const float diff = std::abs(current_bottom_w - current_top_w);
        const float max_w = std::max(current_bottom_w, current_top_w);

        if (diff < best_diff || (std::abs(diff - best_diff) < 1.f && max_w < best_max_w)) {
            best_diff = diff;
            best_max_w = max_w;
            best_k = k;
        }
    }

    CHECK(best_k == 5);
    // Row 2 (bottom, buttons 0..4): START(70) + A(115) + B(60) + X(70) + Y(125) = 440
    // Row 1 (top, buttons 5..7): L2(115) + ZR(95) + LEFT(185) = 395
    // Difference is only 45 pixels!
    CHECK(best_diff == 45.f);

    // Test Justified Spacing on Row 2 (bottom: 5 buttons, 4 gaps):
    const float content_bottom = 440.f;
    const float gap_bottom = (avail - content_bottom) / 4.f; // (1190 - 440) / 4 = 187.5
    CHECK(gap_bottom == 187.5f);

    // Simulate right-to-left layout:
    float x = 1220.f;
    for (size_t i = 0; i < best_k; i++) {
        x -= btn_widths[i];
        if (i + 1 < best_k) {
            x -= gap_bottom;
        }
    }
    // Leftmost button edge must land EXACTLY at 30.f
    CHECK(std::abs(x - 30.f) < 0.001f);

    // Test Justified Spacing on Row 1 (top: 3 buttons, 2 gaps):
    const float content_top = 395.f;
    const float gap_top = (avail - content_top) / 2.f; // (1190 - 395) / 2 = 397.5
    CHECK(gap_top == 397.5f);

    x = 1220.f;
    for (size_t i = best_k; i < btn_widths.size(); i++) {
        x -= btn_widths[i];
        if (i + 1 < btn_widths.size()) {
            x -= gap_top;
        }
    }
    // Leftmost button edge must land EXACTLY at 30.f
    CHECK(std::abs(x - 30.f) < 0.001f);

    return 0;
}

int main() {
    if (test_title_font_scaling()) return 1;
    if (test_footer_2row_split_and_justified()) return 1;

    std::printf("ok  title_scaling_and_2row_footer: %d checks passed\n", g_checks);
    return 0;
}
