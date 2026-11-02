#pragma once

// Battery calibration, the zero (owner, 2026-10-29): the console is drained until it switches itself off; the last voltage
// the drain log saw before the gap is the real empty battery. That voltage goes into the gauge twice:
//   V_empty (0x3A), bits 15..7 = VE in 10 mV: the threshold the chip takes as empty in every later discharge;
//   RemCapMIX (0x0F) -> RemCapREP (0x05) -> RepSOC (0x06): the charge counters, so the shown percent is 0 now and the next
//   full charge teaches the capacity from the real zero (the chip would otherwise learn it from a wrong 35 %).
// The counters get the charge that came in since the cut-off (QH, 0x4D, from the drain page's last note on battery, else its value at the Hub start),
// not plain 0: the console is usually switched on with the charger already in. A battery that is full by now is written as 100 %.
// No libnx: host-tested by tests/test_battery_zero.cpp.

#include <cstdint>
#include <optional>

namespace sphaira::battery::zero {

inline constexpr int MIN_MV = 3000; // a cut-off voltage outside this range is a log error, not a zero
inline constexpr int MAX_MV = 3400; // the real cut-offs: 3.19-3.21 V; a page left by HOME at 3.45 V must not become the zero
inline constexpr std::uint16_t VE_DEFAULT = 0xA05F; // 3.20 V / 3.80 V, the console's own value (BDF's X sequence verifies it)

inline auto InRange(int mv) -> bool {
    return mv >= MIN_MV && mv <= MAX_MV;
}

// 0x3A with VE replaced by mv (10 mV steps, bits 15..7); VR (bits 6..0, the recovery voltage) stays what it was
inline auto VEmptyWith(std::uint16_t old, int mv) -> std::uint16_t {
    const auto ve = static_cast<std::uint16_t>((mv / 10) & 0x1FF);
    return static_cast<std::uint16_t>((ve << 7) | (old & 0x7F));
}

inline auto VEmptyMv(std::uint16_t reg) -> int {
    return (reg >> 7) * 10;
}

// The console slept (or was off) at the cut-off and the charger filled the battery meanwhile: when the Hub looks, the cell is at
// the top voltage and the current has tapered. That is a full battery whatever the counters say; owner's console, 2026-10-31:
// 4200 mV, 276 mA, the chip 99 %, and the zero wrote 54 % from a QH difference taken at the Hub's start the evening before.
inline constexpr int FULL_MV = 4150;
inline constexpr int FULL_TAPER_MA = 600; // above this the charger is still in its constant-current phase

inline auto FullNow(int mv, int avg_ma) -> bool {
    return mv >= FULL_MV && avg_ma <= FULL_TAPER_MA; // a negative (discharging) current at this voltage is a full battery too
}

// The charge page's repair (a chip far below a battery that is full) must not mistake the constant-current phase of a healthy
// charge for a full battery: only the constant-voltage tail counts, 4.19 V and an averaged current of 350 mA or less.
inline constexpr int HEAL_MV = 4190;
inline constexpr int HEAL_TAPER_MA = 350;

inline auto FullForHeal(int mv, int avg_ma) -> bool {
    return mv >= HEAL_MV && avg_ma <= HEAL_TAPER_MA;
}

// The capacity, measured (owner, 2026-10-31): the two facts are the cut-off (empty) and the full charge (the cell takes no more
// current). The net charge that came in between them, counted by QH (0x4D, the unit of FullCap: 0.5 mAh at 10 mOhm), is the real
// capacity of this battery. The chip's own learning took 137 % of the design from a zero that was not the console's cut-off.
// The charge is read at the constant-voltage tail (FullForHeal), a little before the termination current: about 1-2 % low.
inline constexpr int CAP_MIN_PCT = 50;  // of DesignCap; outside is a wrong baseline (a gauge reset, a lost note), not a battery
inline constexpr int CAP_MAX_PCT = 125;

inline constexpr std::uint16_t STATUS_POR = 0x0002; // Status (0x00) bit 1: the gauge restarted, QH began again from 0

struct Capacity {
    bool ok{};
    std::uint16_t raw{}; // FullCap units
};

// qh_cut: QH at the cut-off, qh_full: QH at the full battery; a 16-bit difference, so the counter may wrap
inline auto MeasureCapacity(std::uint16_t qh_cut, std::uint16_t qh_full, std::uint16_t design) -> Capacity {
    const auto d = static_cast<std::uint16_t>(qh_full - qh_cut);
    if (!design || d >= 0x8000) {
        return {}; // no design value, or the counter went down
    }
    const std::uint32_t x100 = static_cast<std::uint32_t>(d) * 100u;
    if (x100 < static_cast<std::uint32_t>(design) * CAP_MIN_PCT || x100 > static_cast<std::uint32_t>(design) * CAP_MAX_PCT) {
        return {};
    }
    return {true, d};
}

struct CountersIn {
    std::uint16_t full_cap{};     // 0x10
    std::uint16_t full_cap_nom{}; // 0x23
    std::uint16_t qh_at_start{};  // 0x4D at the cut-off: the last value the drain page saw on battery (else when the Hub started)
    std::uint16_t qh_now{};       // 0x4D now: the difference is what the charger put in since
    bool full{};                  // FullNow(): the counters say 100 %
};
struct Counters {
    bool ok{};
    std::uint16_t mix{}, rep{}, soc{}; // 0x0F, 0x05, 0x06
};

// the charge since the cut-off, as the three counters; two's complement QH so a discharge since then reads as 0
inline auto PlanCounters(const CountersIn& in) -> Counters {
    if (!in.full_cap || !in.full_cap_nom) {
        return {};
    }
    if (in.full) {
        return {true, in.full_cap_nom, in.full_cap, 0x6400};
    }
    const auto delta = static_cast<std::int16_t>(in.qh_now - in.qh_at_start);
    const std::uint32_t charged = delta > 0 ? static_cast<std::uint32_t>(delta) : 0u;
    Counters c;
    c.mix = static_cast<std::uint16_t>(charged < in.full_cap_nom ? charged : in.full_cap_nom);
    c.rep = static_cast<std::uint16_t>(static_cast<std::uint32_t>(c.mix) * in.full_cap / in.full_cap_nom);
    c.soc = static_cast<std::uint16_t>(static_cast<std::uint32_t>(c.rep) * 0x6400 / in.full_cap); // 1/256 %
    c.ok = true;
    return c;
}

} // namespace sphaira::battery::zero
