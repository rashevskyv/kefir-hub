#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>

namespace {

enum class Outcome {
    Installed,
    SkippedUser,
    SkippedAlreadyInstalled,
    Cancelled,
    Failed,
};

struct Stats {
    size_t installed{};
    size_t skipped{};
    size_t failed{};
};

Outcome ClassifyPackageOutcome(
    uint32_t rc,
    bool cancel_requested,
    bool skip_requested,
    bool already_installed_skipped,
    bool is_transfer_cancelled)
{
    const bool user_skipped = skip_requested;
    const bool cancelled = cancel_requested || (!user_skipped && is_transfer_cancelled);

    if (user_skipped) {
        return Outcome::SkippedUser;
    }
    if (rc == 0) { // Success
        if (already_installed_skipped) {
            return Outcome::SkippedAlreadyInstalled;
        }
        return Outcome::Installed;
    }
    if (cancelled) {
        return Outcome::Cancelled;
    }
    return Outcome::Failed;
}

void ApplyOutcome(Outcome outcome, Stats& stats, bool& selected, bool& installed) {
    switch (outcome) {
        case Outcome::SkippedUser:
            stats.skipped++;
            installed = false;
            break;
        case Outcome::SkippedAlreadyInstalled:
            stats.skipped++;
            selected = false;
            installed = true;
            break;
        case Outcome::Installed:
            stats.installed++;
            selected = false;
            installed = true;
            break;
        case Outcome::Cancelled:
            installed = false;
            break;
        case Outcome::Failed:
            stats.failed++;
            installed = false;
            break;
    }
}

} // namespace

int main() {
    // 1. User skips package
    {
        Stats stats{};
        bool selected = true;
        bool installed = false;
        auto outcome = ClassifyPackageOutcome(0x1234, false, true, false, true);
        assert(outcome == Outcome::SkippedUser);
        ApplyOutcome(outcome, stats, selected, installed);
        assert(stats.skipped == 1);
        assert(stats.installed == 0);
        assert(stats.failed == 0);
        assert(installed == false);
    }

    // 2. User cancels queue
    {
        Stats stats{};
        bool selected = true;
        bool installed = false;
        auto outcome = ClassifyPackageOutcome(0x1234, true, false, false, true);
        assert(outcome == Outcome::Cancelled);
        ApplyOutcome(outcome, stats, selected, installed);
        assert(stats.skipped == 0);
        assert(stats.installed == 0);
        assert(stats.failed == 0);
    }

    // 3. Normal install success
    {
        Stats stats{};
        bool selected = true;
        bool installed = false;
        auto outcome = ClassifyPackageOutcome(0, false, false, false, false);
        assert(outcome == Outcome::Installed);
        ApplyOutcome(outcome, stats, selected, installed);
        assert(stats.installed == 1);
        assert(stats.skipped == 0);
        assert(stats.failed == 0);
        assert(selected == false);
        assert(installed == true);
    }

    // 4. Already installed skip
    {
        Stats stats{};
        bool selected = true;
        bool installed = false;
        auto outcome = ClassifyPackageOutcome(0, false, false, true, false);
        assert(outcome == Outcome::SkippedAlreadyInstalled);
        ApplyOutcome(outcome, stats, selected, installed);
        assert(stats.skipped == 1);
        assert(stats.installed == 0);
        assert(stats.failed == 0);
        assert(selected == false);
        assert(installed == true);
    }

    // 5. Install failure (not cancelled, not skipped)
    {
        Stats stats{};
        bool selected = true;
        bool installed = false;
        auto outcome = ClassifyPackageOutcome(0xCAFE, false, false, false, false);
        assert(outcome == Outcome::Failed);
        ApplyOutcome(outcome, stats, selected, installed);
        assert(stats.failed == 1);
        assert(stats.installed == 0);
        assert(stats.skipped == 0);
    }

    // 6. Skip confirmation bound to active package index (race condition check)
    {
        auto should_skip = [](size_t active_pkg, size_t expected_pkg, bool installing) {
            return installing && (active_pkg == expected_pkg);
        };
        assert(should_skip(0, 0, true) == true);
        assert(should_skip(1, 0, true) == false); // package 0 finished before prompt confirmation
        assert(should_skip(0, 0, false) == false); // state left installing before confirmation
    }

    std::puts("ok  queue_outcome: all checks passed");
    return 0;
}
