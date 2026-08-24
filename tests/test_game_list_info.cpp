#include "ui/menus/game_list_info.hpp"

#include <array>
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

static auto JoinBadges(bool on_sd, bool on_nand, bool on_gamecard,
    bool has_base, bool has_update, bool has_dlc, bool layeredfs,
    bool include_storage) -> std::string
{
    using sphaira::ui::menu::game::CollectGameBadgeLabels;
    using sphaira::ui::menu::game::kMaxGameBadges;

    std::array<const char*, kMaxGameBadges> labels{};
    const auto n = CollectGameBadgeLabels(on_sd, on_nand, on_gamecard,
        has_base, has_update, has_dlc, layeredfs, include_storage, labels);
    std::string s;
    for (std::size_t i = 0; i < n; i++) {
        if (i) {
            s += ',';
        }
        s += labels[i];
    }
    return s;
}

static int test_game_list_info() {
    CHECK(JoinBadges(true, false, false, true, false, false, false, true)
        == "SD,Base");
    CHECK(JoinBadges(false, true, false, true, true, false, false, true)
        == "NAND,Base,Update");
    CHECK(JoinBadges(true, true, false, true, false, true, true, true)
        == "SD,NAND,Base,DLC,LayeredFS");
    CHECK(JoinBadges(false, false, false, true, false, false, false, true)
        == "Base");
    CHECK(JoinBadges(false, false, false, false, true, false, false, false)
        == "Update,-");

    CHECK(JoinBadges(false, false, true, true, false, false, false, true)
        == "GC,Base");
    CHECK(JoinBadges(true, false, true, true, true, false, false, true)
        == "SD,GC,Base,Update");
    CHECK(JoinBadges(false, false, true, true, false, false, false, false)
        == "GC,Base");
    CHECK(JoinBadges(true, true, false, true, false, false, false, false)
        == "Base");

    return 0;
}

int main() {
    if (const int rc = test_game_list_info()) {
        return rc;
    }
    std::printf("OK %d checks\n", g_checks);
    return 0;
}
