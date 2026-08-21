// Host test for sphaira/include/title_export_name.hpp
//
// Build and run command:
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_title_export_name.cpp -o /tmp/test_title_export_name && /tmp/test_title_export_name

#include "title_export_name.hpp"

#include <cassert>
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

static int test_is_usable_title_name() {
    using namespace sphaira::title;

    CHECK(!IsUsableTitleName(""));
    CHECK(!IsUsableTitleName("   "));
    CHECK(!IsUsableTitleName("..."));
    CHECK(!IsUsableTitleName("___"));
    CHECK(!IsUsableTitleName(" . _ . "));
    CHECK(IsUsableTitleName("Game"));
    CHECK(IsUsableTitleName("A"));
    CHECK(IsUsableTitleName("  Game  "));
    CHECK(IsUsableTitleName("123"));
    CHECK(IsUsableTitleName("_Game_"));

    return 0;
}

static int test_sanitize_ascii_title_name() {
    using namespace sphaira::title;

    // Normal ASCII
    CHECK(SanitizeAsciiTitleName("Super Mario Odyssey") == "Super Mario Odyssey");

    // Illegal filesystem chars
    CHECK(SanitizeAsciiTitleName("Game: Subtitle / Part 1 * Special") == "Game_ Subtitle _ Part 1 _ Special");
    CHECK(SanitizeAsciiTitleName("A?B<C>D|E\"F\\G/H") == "A_B_C_D_E_F_G_H");

    // Non-ASCII characters become '_'
    CHECK(SanitizeAsciiTitleName("Pokémon") == "Pok_mon");
    CHECK(SanitizeAsciiTitleName("ゼルダの伝説") == "_");

    return 0;
}

static int test_resolve_export_title_name_hierarchy() {
    using namespace sphaira::title;

    const uint64_t app_id = 0x0100000000010000ULL;

    // 1. English slot 0 present and usable
    CHECK(ResolveExportTitleName("Super Mario Odyssey", "Super Mario Odyssey (UK)", "スーパーマリオ オデッセイ", app_id)
          == "Super Mario Odyssey");

    // 2. Fallback to English slot 1 if slot 0 is empty / unusable
    CHECK(ResolveExportTitleName("", "Mario Kart 8 Deluxe", "マリオカート8 デラックス", app_id)
          == "Mario Kart 8 Deluxe");
    CHECK(ResolveExportTitleName("___", "Mario Kart 8 Deluxe", "マリオカート8 デラックス", app_id)
          == "Mario Kart 8 Deluxe");

    // 3. Fallback to localized name if English slots are empty
    CHECK(ResolveExportTitleName(nullptr, nullptr, "The Legend of Zelda", app_id)
          == "The Legend of Zelda");

    // 4. Localized name sanitization produces unusable string (e.g. Japanese-only or dots) -> fallback to Title ID
    CHECK(ResolveExportTitleName(nullptr, nullptr, "ゼルダの伝説", app_id)
          == "0100000000010000");
    CHECK(ResolveExportTitleName("...", "___", "   ...   ", app_id)
          == "0100000000010000");

    // 5. All names empty -> Title ID
    CHECK(ResolveExportTitleName(nullptr, nullptr, nullptr, app_id)
          == "0100000000010000");

    // 6. Max length truncation preserving valid character budget
    std::string long_name(200, 'A');
    std::string resolved = ResolveExportTitleName(long_name.c_str(), nullptr, nullptr, app_id, 50);
    CHECK(resolved.size() == 50);
    CHECK(resolved == std::string(50, 'A'));

    return 0;
}

int main() {
    if (test_is_usable_title_name() != 0) return 1;
    if (test_sanitize_ascii_title_name() != 0) return 1;
    if (test_resolve_export_title_name_hierarchy() != 0) return 1;

    std::printf("ok  title_export_name: %d checks passed\n", g_checks);
    return 0;
}
