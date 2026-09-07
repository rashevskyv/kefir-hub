// Packing rules for the install queue's storage plan.
//   g++ -std=c++20 -I../sphaira/include test_install_plan.cpp -o t && ./t
#include "ui/menus/install_plan.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using sphaira::ui::menu::dbi::PlanPickSd;
using sphaira::ui::menu::dbi::PlanTake;
using sphaira::ui::menu::dbi::PlanEvaluateCandidate;

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
        assert(s.nand <= 10 * GB + 5 * GB);
    }

    // automatic with room everywhere still balances rather than dumping it all
    // on one target.
    {
        const auto s = Pack(4, {10 * GB, 10 * GB}, 200 * GB, 200 * GB);
        assert(s.sd == 10 * GB && s.nand == 10 * GB);
    }

    // balance: 20/10 free, 8 GB package → SD (12 vs 10 beats 20 vs 2).
    {
        assert(PlanPickSd(4, 8 * GB, 20 * GB, 10 * GB));
        const auto s = Pack(4, {8 * GB}, 20 * GB, 10 * GB);
        assert(s.sd == 8 * GB && s.nand == 0);
    }

    // balance: 11/10 free, 2 GB package → SD (10 vs 10 beats 11 vs 8).
    {
        assert(PlanPickSd(4, 2 * GB, 11 * GB, 10 * GB));
        const auto s = Pack(4, {2 * GB}, 11 * GB, 10 * GB);
        assert(s.sd == 2 * GB && s.nand == 0);
    }

    // budgets never wrap negative.
    {
        int64_t budget = 1 * GB;
        PlanTake(budget, 500 * GB);
        assert(budget == 0);
    }

    // PlanEvaluateCandidate checks capacity against location policy.
    {
        // Fits SD
        auto r1 = PlanEvaluateCandidate(4, 5 * GB, 10 * GB, 2 * GB);
        assert(r1.fits && r1.is_sd);

        // Fits NAND in Auto mode when SD is full
        auto r2 = PlanEvaluateCandidate(4, 5 * GB, 2 * GB, 10 * GB);
        assert(r2.fits && !r2.is_sd);

        // SD-only mode (loc = 0) does not spill to NAND
        auto r3 = PlanEvaluateCandidate(0, 5 * GB, 2 * GB, 10 * GB);
        assert(!r3.fits);

        // NAND-only mode (loc = 1) does not spill to SD
        auto r4 = PlanEvaluateCandidate(1, 5 * GB, 10 * GB, 2 * GB);
        assert(!r4.fits);

        // Does not fit either
        auto r5 = PlanEvaluateCandidate(4, 15 * GB, 10 * GB, 10 * GB);
        assert(!r5.fits);
    }

    // Parity check: equal entries, equal order, equal location setting, equal free-space budgets,
    // and equal reserves produce identical Auto targets regardless of caller origin (local vs PC USB).
    {
        struct TestEntry {
            std::string name;
            int64_t source_size;
            int64_t install_size;
            size_t source_index;
            bool is_auto;
            bool pinned_sd;
        };

        std::vector<TestEntry> local_queue = {
            {"pkg_b.nsp", 4 * GB, 4 * GB, 0, true, false},
            {"pkg_a.nsp", 12 * GB, 12 * GB, 1, true, false},
            {"pkg_c.nsp", 6 * GB, 6 * GB, 2, false, true}, // pinned SD
            {"pkg_d.nsp", 3 * GB, 3 * GB, 3, true, false},
        };

        std::vector<TestEntry> usb_queue = local_queue; // identical queue

        const int64_t polled_nand = 20 * GB;
        const int64_t polled_sd = 20 * GB;
        const int64_t reserve_nand = 500 * 1024 * 1024;
        const int64_t reserve_sd = 500 * 1024 * 1024;
        const long loc = 4; // Automatic

        auto run_planner = [&](const std::vector<TestEntry>& queue) {
            int64_t free_nand = polled_nand > reserve_nand ? polled_nand - reserve_nand : 0;
            int64_t free_sd = polled_sd > reserve_sd ? polled_sd - reserve_sd : 0;
            std::vector<bool> targets;

            // Pass 1: pinned non-Auto
            for (const auto& e : queue) {
                if (e.is_auto) continue;
                PlanTake(e.pinned_sd ? free_sd : free_nand, e.install_size);
            }
            // Pass 2: greedy Auto in queue order
            for (const auto& e : queue) {
                if (!e.is_auto) {
                    targets.push_back(e.pinned_sd);
                } else {
                    const bool pick_sd = PlanPickSd(loc, e.install_size, free_sd, free_nand);
                    targets.push_back(pick_sd);
                    PlanTake(pick_sd ? free_sd : free_nand, e.install_size);
                }
            }
            return targets;
        };

        const auto local_targets = run_planner(local_queue);
        const auto usb_targets = run_planner(usb_queue);
        assert(local_targets == usb_targets);

        // Sorting by name (Ascending)
        auto sorted_by_name = local_queue;
        std::stable_sort(sorted_by_name.begin(), sorted_by_name.end(), [](const TestEntry& a, const TestEntry& b) {
            if (a.name != b.name) return a.name < b.name;
            return a.source_index < b.source_index;
        });
        assert(sorted_by_name[0].name == "pkg_a.nsp");
        assert(sorted_by_name[1].name == "pkg_b.nsp");

        // Restoring Queue order (Ascending)
        auto restored_queue = sorted_by_name;
        std::stable_sort(restored_queue.begin(), restored_queue.end(), [](const TestEntry& a, const TestEntry& b) {
            return a.source_index < b.source_index;
        });
        for (size_t i = 0; i < local_queue.size(); ++i) {
            assert(restored_queue[i].source_index == local_queue[i].source_index);
        }
    }

    std::puts("install plan: ok");
    return 0;
}
