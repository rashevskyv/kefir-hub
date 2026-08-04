#pragma once

#include <cstdint>

namespace sphaira::ui::menu::dbi {

// Which target one package should go to, given the budgets left after the
// packages before it in the queue were placed. Pure, so the packing rules can
// be checked without a console (see tests/test_install_plan.cpp).
//
// loc matches the "Install location" setting:
//   0 SD only, 1 system only, 2 system first, 3 SD first, 4 automatic.
// Returns true for microSD.
constexpr bool PlanPickSd(long loc, int64_t size, int64_t free_sd, int64_t free_nand) {
    switch (loc) {
        case 0: return true;
        case 1: return false;
        case 2: return free_nand < size;   // fill system, spill to SD
        case 3: return free_sd >= size;    // fill SD, spill to system
        default:                           // automatic: keep filling the roomier one
            return free_sd >= free_nand ? free_sd >= size : free_nand < size;
    }
}

// Budgets never go negative: once a target is full everything else lands on the
// other one, and the header bar turns red for whatever no longer fits.
constexpr void PlanTake(int64_t& budget, int64_t size) {
    budget = budget > size ? budget - size : 0;
}

} // namespace sphaira::ui::menu::dbi
