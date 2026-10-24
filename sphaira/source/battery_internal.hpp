#pragma once

// Tools → Battery internals shared by the page (battery_page.cpp) and the chip writes (battery_write.cpp).

#include "battery_gauge.hpp"
#include "battery_wizard.hpp"
#include "fs.hpp"

#include <switch.h>
#include <array>
#include <functional>
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
// the calibration wizard's state file (battery_write.cpp)
auto LoadState() -> wizard::State;
void SaveState(const wizard::State& st);
// HOS's cycle total x100 now (this MMC's set:sys copy + the chip's Cycles delta), without a full Read()
auto CycleTotalNow() -> std::optional<u32>;
// step 5: this MMC's copy takes the chip's model and the larger cycle count; records the match when both cycles are done.
// `failed` gets the reason on an error. No popups: the hop to the other MMC runs it by itself.
auto MatchNow(std::string& failed) -> Result;
// the calibration's step 1: waits on the charger for 100 %, backs up, writes the factory model, saves this MMC's copy and
// starts the wizard's state. `done(true)` when it was written; `done(false)` after a failure (a box said why) or a cancel.
void RunReset(std::function<void(bool)> done);

} // namespace sphaira::battery::detail
