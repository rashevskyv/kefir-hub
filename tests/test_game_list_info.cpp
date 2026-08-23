#include "ui/menus/game_list_info.hpp"

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

static int test_game_list_info() {
    using sphaira::ui::menu::game::FormatGameListInfo;

    CHECK(FormatGameListInfo(true, false, false, true, false, false, false, "1.38 GB")
        == "[S|b]  1.38 GB");
    CHECK(FormatGameListInfo(false, true, false, true, true, false, false, "294 KB")
        == "[N|bu]  294 KB");
    CHECK(FormatGameListInfo(true, true, false, true, false, true, true, "12.00 GB")
        == "[SN|bdL]  12.00 GB");
    CHECK(FormatGameListInfo(false, false, false, true, false, false, false, "")
        == "[|b]  ");

    CHECK(FormatGameListInfo(false, false, true, true, false, false, false, "8.00 GB")
        == "[GC]  8.00 GB");
    CHECK(FormatGameListInfo(true, false, true, true, true, false, false, "8.00 GB")
        == "[GC]  8.00 GB");
    CHECK(FormatGameListInfo(false, false, true, true, false, false, false, "")
        == "[GC]");

    return 0;
}

int main() {
    if (const int rc = test_game_list_info()) {
        return rc;
    }
    std::printf("OK %d checks\n", g_checks);
    return 0;
}
