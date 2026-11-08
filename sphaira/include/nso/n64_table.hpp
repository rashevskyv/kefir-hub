// N64 per-game emulator configs, keyed by the ROM header CRC1/CRC2 (z64 bytes 0x10 and 0x14, big endian).
// THIS FILE IS THE DATA PART: another agent generates the kN64Configs rows; nothing else here changes.
// A game that is not in the table gets kN64Default and the Plan result sets experimental=true.
#pragma once
#include "common.hpp"

namespace nso {

struct N64Config {
    u32 crc1, crc2;
    const char* backup;  // RomOption.BackupType: FLASH EPROM4K EPROM16K SRAM NONE EEPROM4K EEPROM16K
    const char* pak;     // RomOption.SIDevice_PakType: "Rumble Pak" "Controller Pak" "None"
    const char* ram;     // RomOption.RAMSize "8M"/"16M", "" = omit
    int stick_limit;     // RomOption.StickLimit, 0 = omit
    const u32* idle;     // Idle[].JmpAddr values (game idle loop "b ." addresses)
    int idle_count;
};

// UNPROVEN for boot: nobody has run a game with this cfg (no sample mod had Idle:[]).
inline constexpr N64Config kN64Default = {0, 0, "SRAM", "Rumble Pak", "", 0, nullptr, 0};

// Row 0 is a sentinel (crc 0/0), never matched.
inline constexpr N64Config kN64Configs[] = {
    {0, 0, "SRAM", "Rumble Pak", "", 0, nullptr, 0},
};

inline const N64Config* FindN64Config(std::span<const N64Config> table, u32 crc1, u32 crc2) {
    if (crc1 == 0 && crc2 == 0) return nullptr;
    for (const auto& c : table) if (c.crc1 == crc1 && c.crc2 == crc2) return &c;
    return nullptr;
}
inline const N64Config* FindN64Config(u32 crc1, u32 crc2) {
    return FindN64Config(std::span<const N64Config>(kN64Configs), crc1, crc2);
}

}  // namespace nso
