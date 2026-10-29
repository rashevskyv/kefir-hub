#pragma once

// Battery calibration, the zero (owner, 2026-10-29): the console is drained until it switches itself off; the last voltage
// the drain log saw before the gap is the real empty battery. That voltage goes into the gauge twice:
//   V_empty (0x3A), bits 15..7 = VE in 10 mV: the threshold the chip takes as empty in every later discharge;
//   RemCapMIX (0x0F) -> RemCapREP (0x05) -> RepSOC (0x06): the charge counters, so the shown percent is 0 now and the next
//   full charge teaches the capacity from the real zero (the chip would otherwise learn it from a wrong 35 %).
// The counters get the charge that came in since the console was switched on (QH, 0x4D, from its value at the Hub start),
// not plain 0: the console is usually switched on with the charger already in.
// No libnx: host-tested by tests/test_battery_zero.cpp.

#include <cstdint>
#include <optional>

namespace sphaira::battery::zero {

inline constexpr int MIN_MV = 3000; // a cut-off voltage outside this range is a log error, not a zero
inline constexpr int MAX_MV = 3500;
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

struct CountersIn {
    std::uint16_t full_cap{};     // 0x10
    std::uint16_t full_cap_nom{}; // 0x23
    std::uint16_t qh_at_start{};  // 0x4D when the Hub started (the console had just been switched on)
    std::uint16_t qh_now{};       // 0x4D now: the difference is what the charger put in since
};
struct Counters {
    bool ok{};
    std::uint16_t mix{}, rep{}, soc{}; // 0x0F, 0x05, 0x06
};

// the charge since the switch-on, as the three counters; two's complement QH so a discharge since the start reads as 0
inline auto PlanCounters(const CountersIn& in) -> Counters {
    if (!in.full_cap || !in.full_cap_nom) {
        return {};
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
