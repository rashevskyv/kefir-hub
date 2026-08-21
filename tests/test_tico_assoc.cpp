#include <cstdio>
#include <cassert>
#include <cctype>
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <algorithm>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static auto nro_add_arg(std::string arg) -> std::string {
    if (arg.find(' ') != std::string::npos) {
        return '\"' + arg + '\"';
    }
    return arg;
}

static auto nro_add_arg_file(std::string arg) -> std::string {
    if (!arg.starts_with("sdmc:")) {
        arg = "sdmc:" + arg;
    }
    if (arg.find(' ') != std::string::npos) {
        return '\"' + arg + '\"';
    }
    return arg;
}

struct FileAssocEntry {
    std::string argument{};
    auto GetRomArgs(const std::string& rom_path) const -> std::string {
        const auto file_arg = nro_add_arg_file(rom_path);
        if (argument.empty()) {
            return file_arg;
        }
        return nro_add_arg(argument) + " " + file_arg;
    }
};

static int test_get_rom_args() {
    FileAssocEntry assoc1{};
    assoc1.argument = "";
    CHECK(assoc1.GetRomArgs("/roms/3ds/game.3ds") == "sdmc:/roms/3ds/game.3ds");

    FileAssocEntry assoc2{};
    assoc2.argument = "gb";
    CHECK(assoc2.GetRomArgs("/roms/gb/game.gb") == "gb sdmc:/roms/gb/game.gb");

    FileAssocEntry assoc3{};
    assoc3.argument = "sega-cd";
    CHECK(assoc3.GetRomArgs("/roms/sega cd/game.cue") == "sega-cd \"sdmc:/roms/sega cd/game.cue\"");

    return 0;
}

struct RomDatabaseEntry {
    std::string_view folder{};
    std::string_view database{};
    std::array<std::string_view, 5> alias{};

    auto IsDatabase(std::string_view name) const -> bool {
        auto EqualsIC = [](std::string_view a, std::string_view b) {
            if (a.length() != b.length()) return false;
            for (size_t i = 0; i < a.length(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        };

        if (EqualsIC(name, folder) || EqualsIC(name, database)) {
            return true;
        }
        for (const auto& str : alias) {
            if (!str.empty() && EqualsIC(name, str)) {
                return true;
            }
        }
        return false;
    }
};

static constexpr RomDatabaseEntry TEST_PATHS[]{
    { "segacd", "Sega - Mega CD - Sega CD", { "megacd", "mega-cd", "sega cd", "scd", "sega-cd" } },
    { "fbneo", "FBNeo - Arcade Games", { "fbneo" } },
    { "naomi", "Sega - Naomi", { "naomi" } },
    { "naomi2", "Sega - Naomi 2", { "naomi2" } },
    { "atomiswave", "Atomiswave", { "atomiswave" } },
    { "fds", "Nintendo - Famicom Disk System", { "Nintendo - Family Computer Disk System" } },
    { "fds", "Nintendo - Family Computer Disk System", { "Nintendo - Famicom Disk System" } },
    { "supergrafx", "NEC - PC Engine SuperGrafx", { "pce-sg", "pcesg" } },
    { "neogeo", "SNK - Neo Geo" },
};

static int test_rom_database_aliases() {
    CHECK(TEST_PATHS[0].IsDatabase("sega-cd"));
    CHECK(TEST_PATHS[0].IsDatabase("segacd"));
    CHECK(TEST_PATHS[0].IsDatabase("Sega - Mega CD - Sega CD"));
    CHECK(TEST_PATHS[1].IsDatabase("fbneo"));
    CHECK(TEST_PATHS[1].IsDatabase("FBNeo - Arcade Games"));
    CHECK(TEST_PATHS[2].IsDatabase("naomi"));
    CHECK(TEST_PATHS[3].IsDatabase("naomi2"));
    CHECK(TEST_PATHS[4].IsDatabase("atomiswave"));

    // SuperGrafx
    CHECK(TEST_PATHS[7].IsDatabase("supergrafx"));
    CHECK(TEST_PATHS[7].IsDatabase("pce-sg"));
    CHECK(TEST_PATHS[7].IsDatabase("pcesg"));
    CHECK(TEST_PATHS[7].IsDatabase("NEC - PC Engine SuperGrafx"));

    // Family Computer Disk System / Famicom Disk System
    CHECK(TEST_PATHS[5].IsDatabase("fds"));
    CHECK(TEST_PATHS[5].IsDatabase("Nintendo - Family Computer Disk System"));
    CHECK(TEST_PATHS[6].IsDatabase("Nintendo - Famicom Disk System"));

    // Neo Geo
    CHECK(TEST_PATHS[8].IsDatabase("neogeo"));
    CHECK(TEST_PATHS[8].IsDatabase("SNK - Neo Geo"));

    return 0;
}

int main() {
    if (test_get_rom_args() != 0) return 1;
    if (test_rom_database_aliases() != 0) return 1;

    std::printf("ok  tico_assoc: %d checks passed\n", g_checks);
    return 0;
}
