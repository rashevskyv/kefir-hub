// Unit test for MTP, FTP, USB 3.0 and EmuNAND/SysNAND header status badges, 2-block layout, 3-way symmetric spacing, and state color mapping
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
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

struct ColorRGBA {
    uint8_t r, g, b, a;
    bool operator==(const ColorRGBA& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

static constexpr ColorRGBA COLOR_ACTIVE{76, 210, 120, 255};
static constexpr ColorRGBA COLOR_INACTIVE_GRAY{128, 128, 128, 255};

static auto GetServiceIndicatorColor(bool is_running, ColorRGBA theme_inactive) -> ColorRGBA {
    return is_running ? COLOR_ACTIVE : theme_inactive;
}

struct HeaderStorageAndServicesLayout {
    float y_ip{48.f};
    float start_y{70.f};
    float storage_gap{20.f};
    float storage_mid{0.f};
    float badge_y{17.f};
    float badge_h{16.f};
    float storage_font{15.5f};
    float y_nand{0.f};
    float y_sd{0.f};

    float storage_left{823.f};
    float storage_right{1220.f};
    float storage_span_w{0.f};

    float block1_w{0.f};
    float block2_w{0.f};
    float margin_m{0.f};
    float block1_x{0.f};
    float block2_x{0.f};
};

struct MockBadge {
    std::string name;
    bool is_active;
    float text_w;
};

static auto ComputeTwoBlockLayout(float storage_left, float storage_right,
                                  const std::vector<MockBadge>& badges,
                                  float version_text_w) -> HeaderStorageAndServicesLayout {
    HeaderStorageAndServicesLayout layout;
    layout.y_ip = 48.f;
    layout.start_y = 70.f;
    layout.storage_gap = 20.f;
    layout.storage_mid = (layout.y_ip + layout.start_y) * 0.5f;
    layout.y_nand = layout.storage_mid - layout.storage_gap * 0.5f;
    layout.y_sd = layout.storage_mid + layout.storage_gap * 0.5f;

    layout.storage_left = storage_left;
    layout.storage_right = storage_right;
    layout.storage_span_w = std::max(0.f, storage_right - storage_left);

    // Measure Block 1 (Badges)
    constexpr float badge_gap = 6.f;
    layout.block1_w = 0.f;
    for (size_t i = 0; i < badges.size(); ++i) {
        float bw = badges[i].text_w + 24.f;
        layout.block1_w += bw;
        if (i + 1 < badges.size()) {
            layout.block1_w += badge_gap;
        }
    }

    // Measure Block 2 (Version)
    layout.block2_w = version_text_w;

    // 3-way equal margin calculation
    const float content_total_w = layout.block1_w + layout.block2_w;
    if (layout.block2_w > 0.f) {
        layout.margin_m = std::max(4.f, (layout.storage_span_w - content_total_w) / 3.f);
    } else {
        layout.margin_m = std::max(4.f, (layout.storage_span_w - layout.block1_w) * 0.5f);
    }

    layout.block1_x = layout.storage_left + layout.margin_m;
    layout.block2_x = layout.block1_x + layout.block1_w + layout.margin_m;

    return layout;
}

static int test_vertical_geometry_and_spacing() {
    std::vector<MockBadge> badges = {
        {"MTP", true, 26.f},
        {"FTP", false, 23.f},
        {"E", true, 8.f},
    };
    auto layout = ComputeTwoBlockLayout(823.f, 1220.f, badges, 160.f);

    // 1. Storage mid-point calculation: (48 + 70) / 2 = 59.f
    CHECK(layout.storage_mid == 59.f);

    // 2. Storage bars:
    // Row 2 (NAND bar & text): 59 - 10 = 49.f (bar rect top = 39.f)
    // Row 3 (SD bar & text):   59 + 10 = 69.f (bar rect top = 59.f)
    CHECK(layout.y_nand == 49.f);
    CHECK(layout.y_sd == 69.f);

    // 3. Badges at y = 17.f (h = 16.f -> bottom at 33.f, 6px above NAND bar rect top at 39.f)
    CHECK(layout.badge_y == 17.f);
    CHECK(layout.badge_y + layout.badge_h == 33.f);
    CHECK(layout.y_nand - 10.f - (layout.badge_y + layout.badge_h) >= 6.f);

    // 4. Text ascender separation check: top of NAND text (y_nand - storage_font) vs bottom of badge/version line
    const float nand_text_top = layout.y_nand - layout.storage_font; // 49 - 15.5 = 33.5
    const float badge_bottom = layout.badge_y + layout.badge_h;      // 17 + 16 = 33.0
    CHECK(nand_text_top >= badge_bottom); // Clean separation!

    // 5. Bounded within header band [0, 86]
    CHECK(layout.badge_y > 0.f);
    CHECK(layout.y_sd < 86.f);

    return 0;
}

static int test_3way_symmetric_spacing() {
    // Badges: MTP (50px) + 6 + FTP (47px) + 6 + E (32px) = 141px
    std::vector<MockBadge> badges = {
        {"MTP", true, 26.f},
        {"FTP", false, 23.f},
        {"E", true, 8.f},
    };
    float version_w = 160.f;
    float storage_left = 823.f;
    float storage_right = 1220.f;

    auto layout = ComputeTwoBlockLayout(storage_left, storage_right, badges, version_w);

    // Span width = 1220 - 823 = 397.f
    CHECK(layout.storage_span_w == 397.f);
    CHECK(layout.block1_w == 141.f);
    CHECK(layout.block2_w == 160.f);

    // Total content = 141 + 160 = 301.f
    // Space remaining = 397 - 301 = 96.f
    // M = 96 / 3 = 32.f
    CHECK(layout.margin_m == 32.f);

    // 1. Distance from storage_left to Block 1 left edge = M
    float left_gap = layout.block1_x - layout.storage_left;
    CHECK(left_gap == layout.margin_m);

    // 2. Distance between Block 1 right edge and Block 2 left edge = M
    float mid_gap = layout.block2_x - (layout.block1_x + layout.block1_w);
    CHECK(mid_gap == layout.margin_m);

    // 3. Distance from Block 2 right edge to storage_right = M
    float right_gap = layout.storage_right - (layout.block2_x + layout.block2_w);
    CHECK(right_gap == layout.margin_m);

    // Verify all three intervals are exactly equal!
    CHECK(left_gap == mid_gap);
    CHECK(mid_gap == right_gap);

    return 0;
}

static int test_state_color_mapping() {
    ColorRGBA theme_gray = COLOR_INACTIVE_GRAY;

    // Inactive -> Gray
    CHECK(GetServiceIndicatorColor(false, theme_gray) == COLOR_INACTIVE_GRAY);
    // Active -> Green
    CHECK(GetServiceIndicatorColor(true, theme_gray) == COLOR_ACTIVE);

    // EmuNAND mode: is_emummc = true -> Green / active
    bool is_emummc = true;
    CHECK(GetServiceIndicatorColor(is_emummc, theme_gray) == COLOR_ACTIVE);

    // SysNAND mode: is_emummc = false -> Gray / inactive
    is_emummc = false;
    CHECK(GetServiceIndicatorColor(is_emummc, theme_gray) == COLOR_INACTIVE_GRAY);

    return 0;
}

static auto FormatSystemVersionString(const std::string& fw, const std::string& ams, const std::string& kefir) -> std::string {
    std::string sys_str;
    if (!fw.empty() && fw != "Unknown" && !ams.empty() && ams != "Unknown") {
        sys_str = fw + "|AMS " + ams;
    } else if (!fw.empty() && fw != "Unknown") {
        sys_str = fw;
    } else if (!ams.empty() && ams != "Unknown") {
        sys_str = "AMS " + ams;
    }

    if (!kefir.empty()) {
        if (!sys_str.empty()) {
            return kefir + " · " + sys_str;
        }
        return kefir;
    }
    return sys_str;
}

static int test_system_version_formatting() {
    // 1. Full Kefir + Firmware + AMS
    std::string s1 = FormatSystemVersionString("19.0.1", "1.8.0", "Kefir 802");
    CHECK(s1 == "Kefir 802 · 19.0.1|AMS 1.8.0");

    // 2. No Kefir, Firmware + AMS
    std::string s2 = FormatSystemVersionString("18.1.0", "1.7.1", "");
    CHECK(s2 == "18.1.0|AMS 1.7.1");

    // 3. OFW without AMS
    std::string s3 = FormatSystemVersionString("19.0.1", "", "");
    CHECK(s3 == "19.0.1");

    // 4. Only Kefir
    std::string s4 = FormatSystemVersionString("", "", "Kefir 802");
    CHECK(s4 == "Kefir 802");

    return 0;
}

static int test_adaptive_nand_label() {
    constexpr float badge_gap = 6.f;
    float storage_span_w = 397.f;
    float version_w = 160.f;

    auto compute_block1_w = [&](bool has_usb3, float nand_text_w) -> float {
        float w = (26.f + 24.f) + badge_gap + (23.f + 24.f) + badge_gap; // MTP + FTP
        if (has_usb3) {
            w += (48.f + 24.f) + badge_gap; // USB 3.0
        }
        w += (nand_text_w + 24.f);
        return w;
    };

    // Case 1: Without USB 3.0 -> "EmuNAND" (text_w = 54px, badge_w = 78px)
    {
        float block1_full_w = compute_block1_w(false, 54.f); // 50 + 6 + 47 + 6 + 78 = 187px
        float margin_full = (storage_span_w - (block1_full_w + version_w)) / 3.f; // (397 - 347) / 3 = 16.67px
        CHECK(margin_full >= 10.f);

        std::string chosen_label = (margin_full >= 10.f) ? "EmuNAND" : "E";
        CHECK(chosen_label == "EmuNAND");
    }

    // Case 2: With USB 3.0 -> Falls back to compact "E" (text_w = 8px, badge_w = 32px)
    {
        float block1_full_w = compute_block1_w(true, 54.f); // 50 + 6 + 47 + 6 + 72 + 6 + 78 = 265px
        float margin_full = (storage_span_w - (block1_full_w + version_w)) / 3.f; // (397 - 425) / 3 = -9.33px
        CHECK(margin_full < 10.f);

        std::string chosen_label = (margin_full >= 10.f) ? "EmuNAND" : "E";
        CHECK(chosen_label == "E");

        float block1_short_w = compute_block1_w(true, 8.f); // 219px
        float margin_short = (storage_span_w - (block1_short_w + version_w)) / 3.f; // (397 - 379) / 3 = 6.0px
        CHECK(margin_short >= 4.f);
    }

    return 0;
}

int main() {
    if (test_vertical_geometry_and_spacing()) return 1;
    if (test_3way_symmetric_spacing()) return 1;
    if (test_adaptive_nand_label()) return 1;
    if (test_state_color_mapping()) return 1;
    if (test_system_version_formatting()) return 1;

    std::printf("ok  header_service_indicators: %d checks passed\n", g_checks);
    return 0;
}
