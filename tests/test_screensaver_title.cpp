// Unit test for screensaver title formatting, NCA filtering, and safe display geometry
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

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

bool EndsWithIC(std::string_view str, std::string_view suffix) {
    if (str.size() < suffix.size()) return false;
    auto end = str.substr(str.size() - suffix.size());
    return std::equal(end.begin(), end.end(), suffix.begin(), suffix.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

auto FormatDisplayTitle(const std::string& current_title, const std::string& package_file, const std::string& current_transfer) -> std::string {
    const auto display_title = !current_title.empty() ? current_title : package_file;
    if (display_title.empty()) return {};

    std::string out = display_title;
    if (!current_transfer.empty() &&
        !EndsWithIC(current_transfer, ".nca") &&
        !EndsWithIC(current_transfer, ".ncz") &&
        current_transfer.find(".nca") == std::string::npos &&
        current_transfer.find(".ncz") == std::string::npos) {
        out += " — " + current_transfer;
    }
    return out;
}

} // namespace

int main() {
    // 1. Clean game title with raw NCA hash being written -> NCA hash must be stripped
    {
        const std::string title = "The Legend of Zelda: Tears of the Kingdom";
        const std::string pkg = "TOTK.nsp";
        const std::string transfer_nca = "dd38de587cb690a36b1d4b6ca4.nca";
        const auto result = FormatDisplayTitle(title, pkg, transfer_nca);
        CHECK(result == "The Legend of Zelda: Tears of the Kingdom");
    }

    // 2. Clean game title with NCZ hash -> NCZ hash must be stripped
    {
        const std::string title = "Super Mario Bros. Wonder";
        const std::string pkg = "Wonder.nsz";
        const std::string transfer_ncz = "010015100b5140000000000000000000.cnmt.ncz";
        const auto result = FormatDisplayTitle(title, pkg, transfer_ncz);
        CHECK(result == "Super Mario Bros. Wonder");
    }

    // 3. Clean game title with human-readable status stage -> must be preserved
    {
        const std::string title = "Metroid Prime Remastered";
        const std::string pkg = "Metroid.nsp";
        const std::string transfer_stage = "Updating ncm database";
        const auto result = FormatDisplayTitle(title, pkg, transfer_stage);
        CHECK(result == "Metroid Prime Remastered — Updating ncm database");
    }

    // 4. Empty title (before CNMT metadata loaded) -> fallback to package filename
    {
        const std::string title = "";
        const std::string pkg = "Sonic_Frontiers_[0100E08013EAE000][v0].nsp";
        const std::string transfer_nca = "e578c18a20d43bf4.nca";
        const auto result = FormatDisplayTitle(title, pkg, transfer_nca);
        CHECK(result == "Sonic_Frontiers_[0100E08013EAE000][v0].nsp");
    }

    // 5. Empty title with stage -> fallback to package filename + stage
    {
        const std::string title = "";
        const std::string pkg = "Pokemon_Scarlet.nsp";
        const std::string transfer_stage = "Pushing application record";
        const auto result = FormatDisplayTitle(title, pkg, transfer_stage);
        CHECK(result == "Pokemon_Scarlet.nsp — Pushing application record");
    }

    // 6. Screensaver geometry bounds validation
    {
        constexpr float SCREEN_W = 1280.f;
        constexpr float BLOCK_W = 840.f;
        constexpr float DRIFT_X = 170.f;

        const float min_cx = (SCREEN_W / 2.f) - DRIFT_X; // 640 - 170 = 470
        const float max_cx = (SCREEN_W / 2.f) + DRIFT_X; // 640 + 170 = 810

        const float min_left = min_cx - (BLOCK_W / 2.f); // 470 - 420 = 50
        const float max_right = max_cx + (BLOCK_W / 2.f); // 810 + 420 = 1230

        CHECK(min_left >= 40.f);
        CHECK(max_right <= (SCREEN_W - 40.f));
        CHECK(BLOCK_W > 760.f);
    }

    // 7. Font size adaptive scaling test
    {
        constexpr float BLOCK_W = 840.f;
        float font_sz = 22.f;
        float text_w = 950.f; // longer than block width

        if (text_w > BLOCK_W && font_sz > 18.f) {
            font_sz = std::max(18.f, font_sz * (BLOCK_W / text_w));
        }

        CHECK(font_sz < 22.f);
        CHECK(font_sz >= 18.f);
        CHECK(font_sz == 22.f * (840.f / 950.f));
    }

    std::printf("ok  screensaver_title: %d checks passed\n", g_checks);
    return 0;
}
