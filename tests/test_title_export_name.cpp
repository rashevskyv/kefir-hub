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

static int test_sanitize_utf8_title_name() {
    using namespace sphaira::title;

    // Cyrillic / Ukrainian preserved with colon sanitized
    CHECK(SanitizeUtf8TitleName("Відьмак 3: Дикий Гін") == "Відьмак 3_ Дикий Гін");

    // Japanese / CJK preserved
    CHECK(SanitizeUtf8TitleName("ゼルダの伝説: ティアーズ オブ ザ キングダム") == "ゼルダの伝説_ ティアーズ オブ ザ キングダム");

    // Emoji preserved
    CHECK(SanitizeUtf8TitleName("Super Mario 🍄: Star Edition") == "Super Mario 🍄_ Star Edition");

    // Illegal characters replaced
    CHECK(SanitizeUtf8TitleName("A/B\\C:D*E?F\"G<H>I|J") == "A_B_C_D_E_F_G_H_I_J");

    return 0;
}

static int test_truncate_utf8() {
    using namespace sphaira::title;

    // ASCII truncation
    CHECK(TruncateUtf8("Hello World", 5) == "Hello");

    // Ukrainian Cyrillic (2 bytes per char) - 7 bytes budget fits 3 chars = 6 bytes
    std::string ukr = "Привіт Світ";
    CHECK(TruncateUtf8(ukr, 6) == "При");
    CHECK(TruncateUtf8(ukr, 7) == "При"); // Doesn't cut into 'в'

    // Emoji (4 bytes)
    std::string emoji = "🎮🕹️🎲";
    CHECK(TruncateUtf8(emoji, 4) == "🎮");
    CHECK(TruncateUtf8(emoji, 6) == "🎮");

    return 0;
}

static int test_format_mtp_game_dir_name() {
    using namespace sphaira::title;

    const uint64_t app_id = 0x0100000000010000ULL;

    // 1. Localized Cyrillic name preserved
    CHECK(FormatMtpGameDirName("Відьмак 3: Дикий Гін", "The Witcher 3", "The Witcher 3 (UK)", app_id)
          == "Відьмак 3_ Дикий Гін [0100000000010000]");

    // 2. Emoji preserved
    CHECK(FormatMtpGameDirName("Super Mario 🍄", "Super Mario", "Super Mario (UK)", app_id)
          == "Super Mario 🍄 [0100000000010000]");

    // 3. Fallback to English slot 0 if localized empty/unusable
    CHECK(FormatMtpGameDirName("", "Super Mario Odyssey", "Super Mario Odyssey (UK)", app_id)
          == "Super Mario Odyssey [0100000000010000]");
    CHECK(FormatMtpGameDirName("___", "Super Mario Odyssey", "Super Mario Odyssey (UK)", app_id)
          == "Super Mario Odyssey [0100000000010000]");

    // 4. Fallback to English slot 1 if localized and slot 0 empty
    CHECK(FormatMtpGameDirName(nullptr, nullptr, "Mario Kart 8 Deluxe", app_id)
          == "Mario Kart 8 Deluxe [0100000000010000]");

    // 5. Fallback to Title ID if all empty or unusable
    CHECK(FormatMtpGameDirName(nullptr, nullptr, nullptr, app_id)
          == "[0100000000010000]");
    CHECK(FormatMtpGameDirName("...", "___", "   ", app_id)
          == "[0100000000010000]");

    // 6. Safe UTF-8 truncation with suffix budget
    std::string long_ukr(50, ' '); // 50 chars of 2-byte Cyrillic
    std::string long_cyrillic;
    for (int i = 0; i < 40; ++i) {
        long_cyrillic += "Гра"; // 6 bytes each
    }
    std::string formatted = FormatMtpGameDirName(long_cyrillic.c_str(), nullptr, nullptr, app_id, 50);
    CHECK(formatted.size() <= 50);
    CHECK(formatted.ends_with(" [0100000000010000]"));

    return 0;
}

int main() {
    if (test_is_usable_title_name() != 0) return 1;
    if (test_sanitize_ascii_title_name() != 0) return 1;
    if (test_resolve_export_title_name_hierarchy() != 0) return 1;
    if (test_sanitize_utf8_title_name() != 0) return 1;
    if (test_truncate_utf8() != 0) return 1;
    if (test_format_mtp_game_dir_name() != 0) return 1;

    std::printf("ok  title_export_name: %d checks passed\n", g_checks);
    return 0;
}
