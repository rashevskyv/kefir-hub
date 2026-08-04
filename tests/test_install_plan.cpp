// Packing rules for the install queue's storage plan.
//   g++ -std=c++20 -I../sphaira/include test_install_plan.cpp -o t && ./t
#include "ui/menus/install_plan.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>

using sphaira::ui::menu::dbi::PlanPickSd;
using sphaira::ui::menu::dbi::PlanTake;

namespace {

constexpr int64_t GB = 1024LL * 1024 * 1024;

// runs a whole queue through the planner and reports how much landed where.
struct Split { int64_t sd, nand; };

Split Pack(long loc, std::vector<int64_t> sizes, int64_t free_sd, int64_t free_nand) {
    Split out{};
    for (auto size : sizes) {
        const bool sd = PlanPickSd(loc, size, free_sd, free_nand);
        (sd ? out.sd : out.nand) += size;
        PlanTake(sd ? free_sd : free_nand, size);
    }
    return out;
}

} // namespace

int main() {
    // fixed modes ignore the budgets entirely.
    assert(Pack(0, {10 * GB}, 0, 500 * GB).sd == 10 * GB);
    assert(Pack(1, {10 * GB}, 500 * GB, 0).nand == 10 * GB);

    // "system first" fills NAND then spills the rest to SD.
    {
        const auto s = Pack(2, {8 * GB, 8 * GB, 8 * GB}, 100 * GB, 10 * GB);
        assert(s.nand == 8 * GB);
        assert(s.sd == 16 * GB);
    }

    // "SD first" is the mirror image.
    {
        const auto s = Pack(3, {8 * GB, 8 * GB, 8 * GB}, 10 * GB, 100 * GB);
        assert(s.sd == 8 * GB);
        assert(s.nand == 16 * GB);
    }

    // the report that started this: 105 GB of titles, 90 GB free on SD and
    // 10 GB on NAND, automatic mode. NAND has to take some of it -- the old
    // behaviour put all 105 GB on the SD bar and showed it overflowing.
    {
        std::vector<int64_t> sizes(21, 5 * GB); // 105 GB in 5 GB packages
        const auto s = Pack(4, sizes, 90 * GB, 10 * GB);
        assert(s.sd + s.nand == 105 * GB);
        assert(s.nand >= 10 * GB);
        assert(s.sd <= 90 * GB + 5 * GB); // last package may straddle the edge
    }

    // automatic with room everywhere still balances rather than dumping it all
    // on one target.
    {
        const auto s = Pack(4, {10 * GB, 10 * GB}, 200 * GB, 200 * GB);
        assert(s.sd == 10 * GB && s.nand == 10 * GB);
    }

    // budgets never wrap negative.
    {
        int64_t budget = 1 * GB;
        PlanTake(budget, 500 * GB);
        assert(budget == 0);
    }

    std::puts("install plan: ok");
    return 0;
}
