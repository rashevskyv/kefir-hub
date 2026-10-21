#pragma once

// Tools → Battery: what the MAX17050 fuel gauge runs on against the copy HOS keeps in this MMC's
// system settings (set:sys PtmFuelGaugeParameter), and the batt.cal backup file of CTCaer's
// Battery Desync Fix (24 bytes, same layout). Read only. Pure; host test tests/test_battery_gauge.cpp.
// Background: HOS writes its copy into the chip only after the chip lost power (MiscCFG bit 15),
// and sysMMC and emuMMC each keep their own copy.

#include <array>
#include <cstdint>
#include <cstdio>
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
// learned full capacity this many % under the design capacity counts as desync (user, 2026-10-16).
inline constexpr unsigned CAPACITY_LOSS = 10;

inline auto CapacityLow(std::uint16_t full, std::uint16_t design) -> bool {
    return design && full * 100u < design * (100u - CAPACITY_LOSS);
}
inline constexpr int CHARGING_MA = 100;
// learned full capacity this many % over the design capacity: a larger battery was built in (an 8600 mAh cell learns 7500-8000
// against the 4300 of the original), not a fault.
inline constexpr unsigned LARGER_BATTERY = 130;

inline auto CapacityHigh(std::uint16_t full, std::uint16_t design) -> bool {
    return design && full * 100u > design * LARGER_BATTERY;
}

enum class Finding {
    RestorePending,  // MiscCFG bit 15: the chip lost power; HOS writes this MMC's copy at the next start
    PowerOnReset,    // Status bit 1: the chip restarted and nobody has cleared the flag yet
    CopyDiffers,     // the chip and this MMC's copy differ (detail: which parameters)
    CopyUnknown,     // this MMC's copy could not be read, so the chip is not compared with it
    VersionMismatch, // CAL0 battery version and set:cal disagree
    LiteNoCycles,    // Lite (version 2) with cycle count reliability off: the charge limit falls back
    BrokenModel,     // learned full capacity more than CAPACITY_LOSS % under the design capacity: the chip's
                     // model is off (detail: percent; "copy" when this MMC's copy is just as low)
    OtherDiffers,    // the other MMC's saved copy differs from the chip: if the chip loses power while that
                     // MMC runs, HOS writes the old copy back (detail: parameter names)
    OtherUnknown,    // the page was not opened in the other MMC yet
    CyclesDiffer,    // the MMCs count different charge cycles (detail: "294.97 371.96", this MMC first);
                     // on a Lite the charge voltage limit follows the count, so it changes with the MMC
    SocGap,          // on battery: reported % and %-by-voltage differ by SOC_GAP or more (detail: "84 73")
    SocGapLoaded,    // the same gap while charging: read again with the charger out (detail: "84 73")
    LargerBattery,   // the learned capacity is far above the design capacity (detail: percent; " lipo" when the type
                     // is not LiHV: the charge limit is the original 4.2 V)
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
    std::uint32_t hos_cycles{};   // this MMC's cycle count x100
    std::uint32_t other_cycles{}; // the other MMC's, x100 (with `other`)
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
    if (CapacityHigh(in.full_cap, in.design_cap)) {
        out.push_back({Finding::LargerBattery, std::to_string(in.full_cap * 100u / in.design_cap) + (in.setcal_version == 2 ? "" : " lipo")});
    }
    // FullCap is the second model parameter; the copy's FullCap is in hos.v[2].
    const bool broken = CapacityLow(in.full_cap, in.design_cap);
    if (broken) {
        const bool copy_too = in.hos_read && CapacityLow(in.hos.v[2], in.design_cap);
        out.insert(out.begin(), {Finding::BrokenModel, std::to_string(in.full_cap * 100u / in.design_cap) + (copy_too ? " copy" : "")});
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
    // a full cycle apart or more (x100 units); seen on .141: sysMMC 294.97, emuMMC 371.96
    const bool cycles_differ = in.other && in.hos_read
        && (in.hos_cycles > in.other_cycles ? in.hos_cycles - in.other_cycles : in.other_cycles - in.hos_cycles) >= 100;
    if (cycles_differ) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.2f %.2f", in.hos_cycles / 100.0, in.other_cycles / 100.0);
        out.push_back({Finding::CyclesDiffer, buf});
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
        && other_differ.empty() && !gap_at_rest && !cycles_differ;
    if (no_sign) {
        out.insert(out.begin(), {Finding::NoSign, {}});
    }
    return out;
}

// "Align the charge to the voltage" (the MAX17050's own open-circuit-voltage estimate VFSOC, as Atmosphère seeds the chip
// after a power-on reset): RemCapMIX = VFSOC x FullCapNom, RemCapREP = MIX x FullCap / FullCapNom, RepSOC = VFSOC.
// Learned parameters, cycles and dQacc/dPacc stay as they are.
struct AlignInput {
    std::uint16_t vfsoc{};      // 0xFF: 1/256 %
    std::uint16_t repsoc{};     // 0x06: 1/256 %
    std::uint16_t full_cap{};   // 0x10
    std::uint16_t full_cap_nom{}; // 0x23
    bool charger{};
};
struct AlignPlan {
    bool ok{};
    std::uint16_t mix{}, rep{}, soc{};
};
// only when the chip reports far more than the voltage says, the voltage says near empty, and no charger is in
inline auto PlanAlign(const AlignInput& in) -> AlignPlan {
    constexpr std::uint16_t MAX_TARGET = 10u << 8;  // the voltage says at most 10 %
    constexpr std::uint16_t MIN_GAP = 15u << 8;     // and the chip is 15 points or more above it
    if (in.charger || !in.full_cap || !in.full_cap_nom || in.vfsoc > MAX_TARGET || in.repsoc < in.vfsoc + MIN_GAP) {
        return {};
    }
    AlignPlan p;
    p.soc = in.vfsoc;
    p.mix = static_cast<std::uint16_t>(static_cast<std::uint32_t>(in.vfsoc) * in.full_cap_nom / 0x6400);
    p.rep = static_cast<std::uint16_t>(static_cast<std::uint32_t>(p.mix) * in.full_cap / in.full_cap_nom);
    p.ok = true;
    return p;
}

} // namespace sphaira::battery
