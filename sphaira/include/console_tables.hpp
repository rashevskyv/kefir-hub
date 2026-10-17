#pragma once

// Names behind the numbers of System information → Console: the DRAM chip of a DramId, the firmware a burnt-fuse
// count belongs to, the name of a system language code. Pure; host test tests/test_console_tables.cpp.
// DRAM: CTCaer/hekate bdk/mem/sdram.h (DBI shows the same, without the board). Fuses: Atmosphère
// libexosphere fuse_api.cpp `FuseVersionIncrementFirmwares` (a console that ran the firmware has at least that count).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sphaira::console {

// "Samsung K4U6E3S4AB-MGCL 4GB"; empty when the id is not in the table (16 is a retired id).
inline auto DramChip(std::uint64_t id) -> std::string_view {
    static constexpr std::string_view CHIPS[] = {
        /* 0 */ "Samsung K4F6E304HB-MGCH 4GB",
        /* 1 */ "Hynix H9HCNNNBPUMLHR-NLE 4GB",
        /* 2 */ "Micron MT53B512M32D2NP-062 WTC 4GB",
        /* 3 */ "Hynix H9HCNNNBKMMLXR-NEE 4GB",
        /* 4 */ "Samsung K4FHE3D4HM-MGCH 6GB",
        /* 5 */ "Hynix H9HCNNNBKMMLXR-NEE 4GB",
        /* 6 */ "Hynix H9HCNNNBKMMLXR-NEE 4GB",
        /* 7 */ "Samsung K4FBE3D4HM-MGXX 8GB",
        /* 8 */ "Samsung K4U6E3S4AM-MGCJ 4GB",
        /* 9 */ "Samsung K4UBE3D4AM-MGCJ 8GB",
        /* 10 */ "Hynix H9HCNNNBKMMLHR-NME 4GB",
        /* 11 */ "Micron MT53E512M32D2NP-046 WTE 4GB",
        /* 12 */ "Samsung K4U6E3S4AM-MGCJ 4GB",
        /* 13 */ "Samsung K4UBE3D4AM-MGCJ 8GB",
        /* 14 */ "Hynix H9HCNNNBKMMLHR-NME 4GB",
        /* 15 */ "Micron MT53E512M32D2NP-046 WTE 4GB",
        /* 16 */ "",
        /* 17 */ "Samsung K4U6E3S4AA-MGCL 4GB",
        /* 18 */ "Samsung K4UBE3D4AA-MGCL 8GB",
        /* 19 */ "Samsung K4U6E3S4AA-MGCL 4GB",
        /* 20 */ "Samsung K4U6E3S4AB-MGCL 4GB",
        /* 21 */ "Samsung K4U6E3S4AB-MGCL 4GB",
        /* 22 */ "Samsung K4U6E3S4AB-MGCL 4GB",
        /* 23 */ "Samsung K4UBE3D4AA-MGCL 8GB",
        /* 24 */ "Samsung K4U6E3S4AA-MGCL 4GB",
        /* 25 */ "Micron MT53E512M32D2NP-046 WTF 4GB",
        /* 26 */ "Micron MT53E512M32D2NP-046 WTF 4GB",
        /* 27 */ "Micron MT53E512M32D2NP-046 WTF 4GB",
        /* 28 */ "Samsung K4UBE3D4AA-MGCL 8GB",
        /* 29 */ "Hynix H54G46CYRBX267 4GB",
        /* 30 */ "Hynix H54G46CYRBX267 4GB",
        /* 31 */ "Hynix H54G46CYRBX267 4GB",
        /* 32 */ "Micron MT53E512M32D1NP-046 WTB 4GB",
        /* 33 */ "Micron MT53E512M32D1NP-046 WTB 4GB",
        /* 34 */ "Micron MT53E512M32D1NP-046 WTB 4GB",
    };
    return id < std::size(CHIPS) ? CHIPS[id] : std::string_view{};
}

// The firmware whose update raised the fuse count to `burnt` (so the console ran at least this firmware); empty
// for 0 or above the table.
inline auto FuseFirmware(std::uint64_t burnt) -> std::string_view {
    static constexpr std::string_view FIRMWARES[] = {
        "1.0.0", "2.0.0", "3.0.0", "3.0.2", "4.0.0", "5.0.0", "6.0.0", "6.2.0", "7.0.0", "8.1.0", "9.0.0", "9.1.0",
        "10.0.0", "11.0.0", "12.0.2", "13.2.1", "15.0.0", "16.0.0", "17.0.0", "19.0.0", "20.0.0", "21.0.0", "22.0.0", "23.0.0",
    };
    return burnt >= 1 && burnt <= std::size(FIRMWARES) ? FIRMWARES[burnt - 1] : std::string_view{};
}

// English name of a system language code ("ru" → "Russian"); the code itself when unknown.
inline auto LanguageName(std::string_view code) -> std::string {
    struct Entry { std::string_view code, name; };
    static constexpr Entry NAMES[] = {
        {"ja", "Japanese"}, {"en-US", "English (US)"}, {"en-GB", "English (UK)"}, {"fr", "French"}, {"de", "German"},
        {"es-419", "Spanish (Latin America)"}, {"es", "Spanish"}, {"it", "Italian"}, {"nl", "Dutch"},
        {"fr-CA", "French (Canada)"}, {"pt", "Portuguese"}, {"ru", "Russian"}, {"ko", "Korean"},
        {"zh-Hans", "Chinese (Simplified)"}, {"zh-Hant", "Chinese (Traditional)"}, {"pt-BR", "Portuguese (Brazil)"},
        {"zh-CN", "Chinese (Simplified)"}, {"zh-TW", "Chinese (Traditional)"},
    };
    for (const auto& e : NAMES) {
        if (e.code == code) {
            return std::string{e.name};
        }
    }
    return std::string{code};
}

} // namespace sphaira::console
