// Unit test for header network status layout, SSID/IP formatting, and anti-overlap scrolling constraints
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

enum NifmConnectionType {
    ConnectionType_WiFi = 0,
    ConnectionType_Ethernet = 1,
};

static auto FormatNetworkString(uint32_t ip, NifmConnectionType type, const std::string& ssid) -> std::string {
    if (ip == 0) {
        return "No Internet";
    }

    std::string network;
    if (type == ConnectionType_Ethernet) {
        network = "LAN";
    } else if (!ssid.empty()) {
        network = ssid;
    }

    if (!network.empty()) {
        network += " · ";
    }

    char ip_buf[32];
    std::snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u",
        ip & 0xFF, (ip >> 8) & 0xFF,
        (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    network += ip_buf;
    return network;
}

struct HeaderStatusLayout {
    float bar_right{1240.f};
    float start_x{1240.f};
    float storage_right{0.f};
    float value_x{0.f};
    float nand_right{0.f};
    float net_left{0.f};
    float net_w{0.f};
    bool should_scroll{false};
};

static auto ComputeHeaderLayout(bool is_12h, bool is_charging, bool is_applet, float nand_text_w, float text_width) -> HeaderStatusLayout {
    (void)is_charging;
    HeaderStatusLayout layout;
    float start_x = layout.bar_right;

    // Battery width
    start_x -= 94.f;

    // Clock width
    if (is_12h) {
        start_x -= 132.f;
    } else {
        start_x -= 90.f;
    }

    // Applet mode indicator [A]
    if (is_applet) {
        start_x -= 25.f;
    }

    layout.start_x = start_x;
    layout.storage_right = std::min(start_x - 10.f, layout.bar_right);

    // Approximate value_col_w ~ 130px
    const float value_col_w = 130.f;
    layout.value_x = layout.storage_right - value_col_w;
    layout.nand_right = layout.value_x + nand_text_w;
    layout.net_left = layout.nand_right + 12.f;
    layout.net_w = std::max(0.f, layout.bar_right - layout.net_left);
    layout.should_scroll = (layout.net_w > 0.f && text_width > layout.net_w);

    return layout;
}

static int test_network_string_formatting() {
    // 1. IP = 0 -> "No Internet"
    CHECK(FormatNetworkString(0, ConnectionType_WiFi, "MyWifi") == "No Internet");
    CHECK(FormatNetworkString(0, ConnectionType_Ethernet, "") == "No Internet");

    // 2. Ethernet -> "LAN · 192.168.1.5"
    uint32_t ip1 = 192 | (168 << 8) | (1 << 16) | (5 << 24);
    CHECK(FormatNetworkString(ip1, ConnectionType_Ethernet, "IgnoredSSID") == "LAN · 192.168.1.5");

    // 3. Wi-Fi with SSID -> "Gbm_Bilya_Doroqu · 192.168.0.106"
    uint32_t ip2 = 192 | (168 << 8) | (0 << 16) | (106 << 24);
    CHECK(FormatNetworkString(ip2, ConnectionType_WiFi, "Gbm_Bilya_Doroqu") == "Gbm_Bilya_Doroqu · 192.168.0.106");

    // 4. Wi-Fi with empty SSID -> "192.168.0.106"
    CHECK(FormatNetworkString(ip2, ConnectionType_WiFi, "") == "192.168.0.106");

    return 0;
}

static int test_layout_and_anti_overlap_bounds() {
    // 24h clock, nand text ~56px ("2.58 GB"), wifi text ~140px ("Home · 192.168.1.5")
    auto l1 = ComputeHeaderLayout(false, false, false, 56.f, 140.f);
    CHECK(l1.start_x == 1240.f - 94.f - 90.f); // 1056.f
    CHECK(l1.storage_right == 1046.f);
    CHECK(l1.value_x == 1046.f - 130.f); // 916.f
    CHECK(l1.nand_right == 916.f + 56.f); // 972.f
    CHECK(l1.net_left == 972.f + 12.f); // 984.f
    CHECK(l1.net_w == 1240.f - 984.f); // 256.f

    // 140px fits within 256px -> no scroll needed
    CHECK(!l1.should_scroll);
    // Right-aligned start position is 1240 - 140 = 1100, which is > 984 (safe gap of 116px from NAND)
    CHECK(1240.f - 140.f >= l1.net_left);

    // Long SSID string (width 270px, e.g. "Gbm_Bilya_Doroqu · 192.168.0.106")
    auto l2 = ComputeHeaderLayout(false, false, false, 56.f, 270.f);
    CHECK(l2.net_w == 256.f);
    // 270px > 256px -> overlap condition met -> scrolling activated!
    CHECK(l2.should_scroll);
    // When scrolling, scissor clips at net_left (984.f), so text never renders < 984.f (NAND ends at 972.f)
    CHECK(l2.net_left - l2.nand_right >= 12.f);

    // 12h clock
    auto l3 = ComputeHeaderLayout(true, false, false, 56.f, 200.f);
    CHECK(l3.start_x == 1240.f - 94.f - 132.f); // 1014.f
    CHECK(l3.storage_right == 1004.f);
    CHECK(l3.value_x == 874.f);
    CHECK(l3.nand_right == 930.f);
    CHECK(l3.net_left == 942.f);
    CHECK(l3.net_w == 298.f);
    // 200px fits within 298px -> no scroll needed
    CHECK(!l3.should_scroll);

    // Applet mode [A] active
    auto l4 = ComputeHeaderLayout(false, false, true, 56.f, 200.f);
    CHECK(l4.start_x == 1240.f - 94.f - 90.f - 25.f); // 1031.f
    CHECK(l4.storage_right == 1021.f);
    CHECK(l4.net_w == 281.f);
    CHECK(!l4.should_scroll);

    return 0;
}

int main() {
    if (test_network_string_formatting()) return 1;
    if (test_layout_and_anti_overlap_bounds()) return 1;

    std::printf("ok  header_network_layout: %d checks passed\n", g_checks);
    return 0;
}
