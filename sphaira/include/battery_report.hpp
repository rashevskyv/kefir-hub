#pragma once

// The calibration report in the user's words (owner, 2026-11-01): not "learned capacity" but how much less (or more) the battery
// holds than it did new, what that means and what to do. The numbers are the gauge's FullCap, FullCapNom and DesignCap (the
// same unit, 0.5 mAh per step at 10 mOhm). No libnx: host-tested by tests/test_battery_report.cpp.

#include <cstdint>

namespace sphaira::battery::report {

enum class Wear {
    Unknown,   // no design value to compare with
    New,       // up to 5 % below the factory value
    Normal,    // 5 - 15 %: the ordinary wear of the years
    Worn,      // 15 - 30 %: tired, or a cycle was cut short
    VeryWorn,  // over 30 %: more than wear explains, most likely the calibration went wrong
    AboveFactory, // more than 5 % above the factory value: a battery never holds more than new, the gauge's value is wrong
};

struct Verdict {
    Wear wear{Wear::Unknown};
    int loss_pct{};          // positive = below the factory value, negative = above it
    bool inconsistent{};     // the nominal and the learned capacity differ by more than 20 %: the gauge did not learn cleanly
};

inline constexpr int NEW_MAX_LOSS = 5;
inline constexpr int NORMAL_MAX_LOSS = 15;
inline constexpr int WORN_MAX_LOSS = 30;
inline constexpr int ABOVE_FACTORY_PCT = 5;
inline constexpr int INCONSISTENT_PCT = 20;

inline auto Judge(std::uint16_t full, std::uint16_t nominal, std::uint16_t design) -> Verdict {
    Verdict v;
    if (!design) {
        return v;
    }
    v.loss_pct = (static_cast<int>(design) - static_cast<int>(full)) * 100 / static_cast<int>(design);
    v.wear = v.loss_pct <= -ABOVE_FACTORY_PCT ? Wear::AboveFactory
           : v.loss_pct <= NEW_MAX_LOSS ? Wear::New
           : v.loss_pct <= NORMAL_MAX_LOSS ? Wear::Normal
           : v.loss_pct <= WORN_MAX_LOSS ? Wear::Worn
           : Wear::VeryWorn;
    if (full && nominal) {
        const auto hi = full > nominal ? full : nominal;
        const auto lo = full > nominal ? nominal : full;
        v.inconsistent = static_cast<unsigned>(hi) * 100u > static_cast<unsigned>(lo) * (100u + INCONSISTENT_PCT);
    }
    return v;
}

} // namespace sphaira::battery::report
