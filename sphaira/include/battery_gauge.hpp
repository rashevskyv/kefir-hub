#pragma once

// Tools → Battery: what the MAX17050 fuel gauge runs on against the copy HOS keeps in this MMC's
// system settings (set:sys PtmFuelGaugeParameter), and the batt.cal backup file of CTCaer's
// Battery Desync Fix (24 bytes, same layout). Read only. Pure; host test tests/test_battery_gauge.cpp.
// Background: HOS writes its copy into the chip only after the chip lost power (MiscCFG bit 15),
// and sysMMC and emuMMC each keep their own copy.

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::battery {

// the nine learned model parameters, in batt.cal order. Chip registers:
// 0x38 RCOMP0, 0x39 TempCo, 0x10 FullCap, 0x23 FullCapNom, 0x36 IAvgEmpty, 0x12/0x22/0x32/0x42 QRTable00..30.
struct Params {
    std::array<std::uint16_t, 9> v{};
    friend bool operator==(const Params&, const Params&) = default;
};
inline constexpr std::array<std::uint8_t, 9> PARAM_REGS{0x38, 0x39, 0x10, 0x23, 0x36, 0x12, 0x22, 0x32, 0x42};
inline constexpr std::array<const char*, 9> PARAM_NAMES{
    "RCOMP0", "TempCo", "FullCap", "FullCapNom", "IAvgEmpty", "QRTable00", "QRTable10", "QRTable20", "QRTable30"};

// = libnx SetSysPtmFuelGaugeParameter = batt.cal.
struct BattCal {
    std::uint16_t params[9];
    std::uint16_t pad;     // HOS keeps stack garbage here; copied as it is
    std::uint32_t cycles;  // x100: 12345 = 123.45 cycles
};
static_assert(sizeof(BattCal) == 24);

// the backup BDF writes with A: the chip's parameters, HOS's pad and cycle count.
inline auto Pack(const Params& chip, std::uint16_t hos_pad, std::uint32_t hos_cycles) -> BattCal {
    BattCal out{};
    std::memcpy(out.params, chip.v.data(), sizeof(out.params));
    out.pad = hos_pad;
    out.cycles = hos_cycles;
    return out;
}

enum class Finding {
    RestorePending,  // MiscCFG bit 15: the chip lost power; HOS writes this MMC's copy at the next start
    PowerOnReset,    // Status bit 1: the chip restarted and nobody has cleared the flag yet
    CopyDiffers,     // the chip and this MMC's copy differ (detail: which parameters)
    CopyUnknown,     // this MMC's copy could not be read, so the chip is not compared with it
    VersionMismatch, // CAL0 battery version and set:cal disagree
    LiteNoCycles,    // Lite (version 2) with cycle count reliability off: the charge limit falls back
    BrokenModel,     // learned full capacity below half the design capacity: no battery wears that far, the
                     // chip learned a wrong model (detail: percent; "copy" when this MMC's copy is just as low)
    LowCapacity,     // 50..70 % of the design capacity: wear, or the start of a bad model
    NoSign,          // none of the desync signs on this MMC
};

struct Input {
    Params chip;
    Params hos;
    std::uint16_t misccfg{};
    std::uint16_t status{};
    std::uint16_t design_cap{};
    std::uint16_t full_cap{};
    int cal0_version{-1};  // raw CAL0 byte at 0x4310; -1 = not read
    int setcal_version{-1};
    int cycle_reliability{-1}; // set:sys 172; -1 = not read (before 7.0.0)
    bool hos_read{true};        // false: set:sys could not be read; `hos` is not compared
};

struct Diagnosis {
    Finding finding;
    std::string detail; // parameter names for CopyDiffers, percent for LowCapacity
};

inline auto Diagnose(const Input& in) -> std::vector<Diagnosis> {
    std::vector<Diagnosis> out;
    if (in.misccfg & 0x8000) {
        out.push_back({Finding::RestorePending, {}});
    }
    if (in.status & 0x0002) {
        out.push_back({Finding::PowerOnReset, {}});
    }
    std::string differ;
    for (size_t i = 0; in.hos_read && i < in.chip.v.size(); i++) {
        if (in.chip.v[i] != in.hos.v[i]) {
            differ += (differ.empty() ? "" : ", ") + std::string{PARAM_NAMES[i]};
        }
    }
    if (!differ.empty()) {
        out.push_back({Finding::CopyDiffers, differ});
    }
    if (!in.hos_read) {
        out.push_back({Finding::CopyUnknown, {}});
    }
    if (in.cal0_version >= 0 && in.setcal_version >= 0 && in.cal0_version != in.setcal_version) {
        out.push_back({Finding::VersionMismatch, {}});
    }
    if (in.setcal_version == 2 && in.cycle_reliability == 0) {
        out.push_back({Finding::LiteNoCycles, {}});
    }
    // FullCap is the second model parameter; the copy's FullCap is in hos.v[2].
    const bool broken = in.design_cap && in.full_cap * 2u < in.design_cap;
    if (broken) {
        const bool copy_too = in.hos_read && in.hos.v[2] * 2u < in.design_cap;
        out.insert(out.begin(), {Finding::BrokenModel, std::to_string(in.full_cap * 100u / in.design_cap) + (copy_too ? " copy" : "")});
    } else if (in.design_cap && in.full_cap * 10u < in.design_cap * 7u) {
        out.push_back({Finding::LowCapacity, std::to_string(in.full_cap * 100u / in.design_cap)});
    }
    // restore flag, POR, a copy that differs and a broken model are the desync signs.
    const bool no_sign = !(in.misccfg & 0x8000) && !(in.status & 0x0002) && differ.empty() && in.hos_read && !broken;
    if (no_sign) {
        out.insert(out.begin(), {Finding::NoSign, {}});
    }
    return out;
}

} // namespace sphaira::battery
