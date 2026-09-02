#pragma once

#include <cstdint>

namespace sphaira::ui::menu::dbi {

constexpr int64_t AbsDiff(int64_t a, int64_t b) {
    return a >= b ? a - b : b - a;
}

// Which target one package should go to, given the budgets left after the
// packages before it in the queue were placed. Pure, so the packing rules can
// be checked without a console (see tests/test_install_plan.cpp).
//
// loc matches the "Install location" setting:
//   0 SD only, 1 system only, 2 system first, 3 SD first, 4 automatic.
// Returns true for microSD.
// Callers pass usable free (free - reserve); this helper does not bake reserve in.
constexpr bool PlanPickSd(long loc, int64_t size, int64_t free_sd, int64_t free_nand) {
    switch (loc) {
        case 0: return true;
        case 1: return false;
        case 2: return free_nand < size;   // fill system, spill to SD
        case 3: return free_sd >= size;    // fill SD, spill to system
        default: {                         // automatic: balance remaining usable
            const bool fits_sd = free_sd >= size;
            const bool fits_nand = free_nand >= size;
            if (fits_sd && fits_nand) {
                const auto gap_sd = AbsDiff(free_sd - size, free_nand);
                const auto gap_nand = AbsDiff(free_nand - size, free_sd);
                return gap_sd <= gap_nand; // tie → SD
            }
            if (fits_sd) return true;
            if (fits_nand) return false;
            return free_sd >= free_nand;   // neither fits: roomier (tie → SD)
        }
    }
}

// Budgets never go negative: once a target is full everything else lands on the
// other one, and the header bar turns red for whatever no longer fits.
constexpr void PlanTake(int64_t& budget, int64_t size) {
    budget = budget > size ? budget - size : 0;
}

// Evaluates whether a new candidate package fits into available storage given location policy,
// and if so, whether it should go to SD.
struct PlanCandidateResult {
    bool fits;
    bool is_sd;
};

constexpr PlanCandidateResult PlanEvaluateCandidate(long loc, int64_t size, int64_t free_sd, int64_t free_nand) {
    const bool pick_sd = PlanPickSd(loc, size, free_sd, free_nand);
    if (pick_sd) {
        if (free_sd >= size) return {true, true};
        if (loc != 0 && free_nand >= size) return {true, false}; // spill to NAND if not SD-only
        return {false, true};
    } else {
        if (free_nand >= size) return {true, false};
        if (loc != 1 && free_sd >= size) return {true, true}; // spill to SD if not NAND-only
        return {false, false};
    }
}

} // namespace sphaira::ui::menu::dbi
