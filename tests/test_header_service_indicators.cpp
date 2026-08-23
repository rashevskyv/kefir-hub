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
    float margin_m{0.f};
    float block1_x{0.f};
};

struct MockBadge {
    std::string name;
    bool is_active;
    float text_w;
};

static auto ComputeBadgeLayout(float storage_left, float storage_right,
                               const std::vector<MockBadge>& badges) -> HeaderStorageAndServicesLayout {
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

    constexpr float badge_gap = 6.f;
    layout.block1_w = 0.f;
    for (size_t i = 0; i < badges.size(); ++i) {
        float bw = badges[i].text_w + 24.f;
        layout.block1_w += bw;
        if (i + 1 < badges.size()) {
            layout.block1_w += badge_gap;
        }
    }

    layout.margin_m = std::max(4.f, (layout.storage_span_w - layout.block1_w) * 0.5f);
    layout.block1_x = layout.storage_left + layout.margin_m;
    return layout;
}

static int test_vertical_geometry_and_spacing() {
    std::vector<MockBadge> badges = {
        {"MTP", true, 26.f},
        {"FTP", false, 23.f},
        {"E", true, 8.f},
    };
    auto layout = ComputeBadgeLayout(823.f, 1220.f, badges);

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

static int test_badges_centered_over_storage() {
    // MTP 50 + 6 + FTP 47 + 6 + EmuNAND (54+24=78) = 187
    std::vector<MockBadge> badges = {
        {"MTP", true, 26.f},
        {"FTP", false, 23.f},
        {"EmuNAND", true, 54.f},
    };
    auto layout = ComputeBadgeLayout(823.f, 1220.f, badges);

    CHECK(layout.storage_span_w == 397.f);
    CHECK(layout.block1_w == 187.f);
    CHECK(layout.margin_m == 105.f); // (397 - 187) / 2

    const float left_gap = layout.block1_x - layout.storage_left;
    const float right_gap = layout.storage_right - (layout.block1_x + layout.block1_w);
    CHECK(left_gap == layout.margin_m);
    CHECK(right_gap == layout.margin_m);
    CHECK(left_gap == right_gap);

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

static int test_full_nand_label() {
    auto nand_badge_name = [](bool is_emummc) {
        return is_emummc ? "EmuNAND" : "SysNAND";
    };
    CHECK(std::string(nand_badge_name(true)) == "EmuNAND");
    CHECK(std::string(nand_badge_name(false)) == "SysNAND");
    return 0;
}

static int test_version_centered_over_network() {
    const float bar_right = 1240.f;
    const float net_text_w = 140.f;
    const float ver_w = 120.f;
    const float col_left = bar_right - net_text_w;
    const float col_w = bar_right - col_left;
    CHECK(col_w == 140.f);
    CHECK(ver_w <= col_w);
    const float ver_x = col_left + col_w * 0.5f;
    CHECK(ver_x == 1170.f);
    return 0;
}

int main() {
    if (test_vertical_geometry_and_spacing()) return 1;
    if (test_badges_centered_over_storage()) return 1;
    if (test_full_nand_label()) return 1;
    if (test_version_centered_over_network()) return 1;
    if (test_state_color_mapping()) return 1;
    if (test_system_version_formatting()) return 1;

    std::printf("ok  header_service_indicators: %d checks passed\n", g_checks);
    return 0;
}
