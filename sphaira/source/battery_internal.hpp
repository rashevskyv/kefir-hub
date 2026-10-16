#pragma once

// Tools → Battery internals shared by the page (battery_page.cpp) and the chip writes (battery_write.cpp).

#include "battery_gauge.hpp"
#include "fs.hpp"

#include <switch.h>
#include <array>
#include <optional>
#include <string>

namespace sphaira::battery::detail {

inline constexpr const char* BACKUP_DIR = "/config/kefir/battery";
// CAL0 (PRODINFO) offsets the Battery Desync Fix reads: BatteryLot and the raw BatteryVersion.
inline constexpr s64 CAL0_BATTERY_LOT = 0x2CE0;
inline constexpr s64 CAL0_BATTERY_VERSION = 0x4310;

struct Snapshot {
    bool gauge{};
    std::array<u16, 256> regs{};
    std::array<bool, 256> read{};
    bool hos_ok{};
    SetSysPtmFuelGaugeParameter hos{};
    int reliability{-1};
    int setcal_version{-1};
    int cal0_version{-1};
    char cal0_vendor{};            // 8th character of the CAL0 battery lot: picks the factory table
    std::string lot;
    bool emummc{};
    std::optional<CopyFile> other; // the other MMC's copy file
    int psm_percent{-1};           // what the console shows (psm)
};

// reads everything the page shows; also saves this MMC's copy for the other MMC (SyncCopies).
auto Read() -> Snapshot;
// <stamp>-<mmc>.cal (batt.cal format) + .txt register dump in BACKUP_DIR; `name` without extension.
auto Backup(const Snapshot& s, std::string& name) -> Result;
auto ChipParams(const Snapshot& s) -> Params;
auto HosParams(const Snapshot& s) -> Params;
auto MmcName(const Snapshot& s) -> std::string;

} // namespace sphaira::battery::detail
