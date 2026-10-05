#include "ui/menus/save_list_info.hpp"

#include <array>
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

using namespace sphaira::ui::menu::save;

static auto JoinSaveBadges(uint32_t mask) -> std::string {
    std::array<const char*, kMaxSaveBadges> labels{};
    const auto count = CollectSaveBadgeLabels(mask, labels);
    std::string s;
    for (std::size_t i = 0; i < count; i++) {
        if (i) {
            s += ',';
        }
        s += labels[i];
    }
    return s;
}

static int test_save_badges_collection() {
    // 1. A game with no saves shows no save-type badge
    std::array<const char*, kMaxSaveBadges> labels{};
    CHECK(CollectSaveBadgeLabels(0, labels) == 0);
    CHECK(JoinSaveBadges(0) == "");
    CHECK(FormatSaveTypesSummary(0) == "-");

    // 2. BCAT + Device produces both badges
    const uint32_t bcat_device = SaveTypeToMask(2 /* BCAT */) | SaveTypeToMask(3 /* Device */);
    CHECK(JoinSaveBadges(bcat_device) == "Device,BCAT");
    CHECK(FormatSaveTypesSummary(bcat_device) == "Device, BCAT");

    // 3. Adding a real Account slot adds Account
    const uint32_t acc_bcat_dev = bcat_device | SaveTypeToMask(1 /* Account */);
    CHECK(JoinSaveBadges(acc_bcat_dev) == "Acc,Device,BCAT");
    CHECK(FormatSaveTypesSummary(acc_bcat_dev) == "Acc, Device, BCAT");

    // 4. Repeated Account slots still produce one Account badge
    const uint32_t repeated_acc = acc_bcat_dev | SaveTypeToMask(1 /* Account */);
    CHECK(JoinSaveBadges(repeated_acc) == "Acc,Device,BCAT");

    // 5. BCAT-only remains BCAT-only
    const uint32_t bcat_only = SaveTypeToMask(2 /* BCAT */);
    CHECK(JoinSaveBadges(bcat_only) == "BCAT");
    CHECK(FormatSaveTypesSummary(bcat_only) == "BCAT");

    // 6. Device-only remains Device-only
    const uint32_t dev_only = SaveTypeToMask(3 /* Device */);
    CHECK(JoinSaveBadges(dev_only) == "Device");

    // 7. Cache indices/spaces do not duplicate the Cache badge
    uint32_t cache_mask = 0;
    cache_mask |= SaveTypeToMask(5 /* Cache index 0 */);
    cache_mask |= SaveTypeToMask(5 /* Cache index 1 */);
    cache_mask |= SaveTypeToMask(5 /* Cache in different space */);
    CHECK(JoinSaveBadges(cache_mask) == "Cache");
    CHECK(FormatSaveTypesSummary(cache_mask) == "Cache");

    CHECK(SaveTypeToMask(7) == 0);
    CHECK(SaveTypeToMask(255) == 0);

    // 8. Other types: Temporary, System, System BCAT
    CHECK(JoinSaveBadges(SaveTypeToMask(4 /* Temporary */)) == "Temp");
    CHECK(JoinSaveBadges(SaveTypeToMask(0 /* System */)) == "Sys");
    CHECK(JoinSaveBadges(SaveTypeToMask(6 /* System BCAT */)) == "SysBCAT");

    return 0;
}

int main() {
    if (const int rc = test_save_badges_collection()) {
        return rc;
    }
    std::printf("OK %d checks\n", g_checks);
    return 0;
}
