#pragma once

// Tools → Battery: what the MAX17050 fuel gauge runs on against the copy HOS keeps in this MMC's
// system settings (set:sys PtmFuelGaugeParameter), and the batt.cal backup file of CTCaer's
// Battery Desync Fix (24 bytes, same layout). Read only. Pure; host test tests/test_battery_gauge.cpp.
// Background: HOS writes its copy into the chip only after the chip lost power (MiscCFG bit 15),
// and sysMMC and emuMMC each keep their own copy.

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
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

// this MMC's set:sys copy as the page saves it on every open (<mmc>-copy.cal), so the page opened in the
// other MMC can compare the chip with it.
struct CopyFile {
    BattCal cal;
    std::uint64_t saved_at; // unix time
};
static_assert(sizeof(CopyFile) == 32);

inline auto ParamsOf(const BattCal& c) -> Params {
    Params p;
    std::memcpy(p.v.data(), c.params, sizeof(c.params));
    return p;
}

// SOC points between the charge the chip reports and the charge its voltage model gives. On battery a bigger
// gap is the wrong-% symptom (the voltage model allows for the discharge current; a Switch in use always
// draws 0.5..1 A, seen -638 mA on .141). While charging the cell sits above its rest voltage: only a hint.
inline constexpr int SOC_GAP = 10;
inline constexpr int CHARGING_MA = 100;

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
    OtherDiffers,    // the other MMC's saved copy differs from the chip: if the chip loses power while that
                     // MMC runs, HOS writes the old copy back (detail: parameter names)
    OtherUnknown,    // the page was not opened in the other MMC yet
    SocGap,          // on battery: reported % and %-by-voltage differ by SOC_GAP or more (detail: "84 73")
    SocGapLoaded,    // the same gap while charging: read again with the charger out (detail: "84 73")
    NoSign,          // none of the desync signs; still says what was not checked (OtherUnknown, SocGapLoaded)
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
    std::optional<Params> other; // the other MMC's copy, when its page was opened
    int rep_soc{-1};             // % the chip reports (RepSOC high byte)
    int vf_soc{-1};              // % from the voltage model (VFSOC high byte)
    int avg_current_ma{};        // AvgCurrent, + = charging
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
    std::string other_differ;
    for (size_t i = 0; in.other && i < in.chip.v.size(); i++) {
        if (in.chip.v[i] != in.other->v[i]) {
            other_differ += (other_differ.empty() ? "" : ", ") + std::string{PARAM_NAMES[i]};
        }
    }
    if (!other_differ.empty()) {
        out.push_back({Finding::OtherDiffers, other_differ});
    }
    bool gap_at_rest = false;
    if (in.rep_soc >= 0 && in.vf_soc >= 0 && (in.rep_soc - in.vf_soc >= SOC_GAP || in.vf_soc - in.rep_soc >= SOC_GAP)) {
        const auto soc = std::to_string(in.rep_soc) + " " + std::to_string(in.vf_soc);
        gap_at_rest = in.avg_current_ma < CHARGING_MA;
        out.push_back({gap_at_rest ? Finding::SocGap : Finding::SocGapLoaded, soc});
    }
    if (!in.other) {
        out.push_back({Finding::OtherUnknown, {}});
    }
    // restore flag, POR, copies that differ, a broken model and a gap at rest are the desync signs.
    const bool no_sign = !(in.misccfg & 0x8000) && !(in.status & 0x0002) && differ.empty() && in.hos_read && !broken
        && other_differ.empty() && !gap_at_rest;
    if (no_sign) {
        out.insert(out.begin(), {Finding::NoSign, {}});
    }
    return out;
}

} // namespace sphaira::battery
