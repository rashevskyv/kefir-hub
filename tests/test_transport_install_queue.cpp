#include <ui/menus/dbi/install_queue_state.hpp>
#include <cassert>
#include <optional>
#include <string>
#include <vector>

using namespace sphaira::ui::menu::dbi;

namespace {
struct Entry {
    std::string batch_id;
    std::string file_name;
    std::optional<unsigned> install_result;
};
}

int main() {
    assert(HasKnownBatchTotals(TransportOrigin::Dbi));
    assert(HasKnownBatchTotals(TransportOrigin::Usb));
    assert(HasKnownBatchTotals(TransportOrigin::Web));
    assert(!HasKnownBatchTotals(TransportOrigin::Mtp));
    assert(!HasKnownBatchTotals(TransportOrigin::Ftp));

    std::vector<Entry> entries{
        {"first", "same.nsp", std::nullopt},
        {"second", "same.nsp", std::nullopt},
    };

    assert(FindQueueIndex(entries, "first", "same.nsp") == 0);
    assert(FindQueueIndex(entries, "second", "same.nsp") == 1);
    assert(FindQueueIndex(entries, "missing", "same.nsp") == QueueIndexNotFound);
    assert(!AllQueueEntriesTerminal(entries));
    assert(!CanTransitionToSummary(TransportOrigin::Web, false, false, 0));

    entries[0].install_result = 0;
    assert(!AllQueueEntriesTerminal(entries));
    entries[1].install_result = 0;
    assert(AllQueueEntriesTerminal(entries));
    assert(CanTransitionToSummary(TransportOrigin::Web, true, false, 0));

    assert(!CanTransitionToSummary(TransportOrigin::Ftp, false, true, 0));
    assert(!CanTransitionToSummary(TransportOrigin::Ftp, false, false, 1));
    assert(CanTransitionToSummary(TransportOrigin::Ftp, false, false, 0));

    assert(!CanTransitionToSummary(TransportOrigin::Mtp, false, true, 0));
    assert(!CanTransitionToSummary(TransportOrigin::Mtp, false, false, 1));
    assert(CanTransitionToSummary(TransportOrigin::Mtp, false, false, 0));

    assert(SUMMARY_GRACE_PERIOD_SEC == 3.0);
    assert(!ShouldStartSummaryGracePeriod(TransportOrigin::Dbi, true, false, false));
    assert(!ShouldStartSummaryGracePeriod(TransportOrigin::Usb, true, false, false));
    assert(!ShouldStartSummaryGracePeriod(TransportOrigin::Web, true, false, false));
    assert(!ShouldStartSummaryGracePeriod(TransportOrigin::Mtp, false, false, false));
    assert(!ShouldStartSummaryGracePeriod(TransportOrigin::Mtp, true, true, false));
    assert(!ShouldStartSummaryGracePeriod(TransportOrigin::Mtp, true, false, true));
    assert(ShouldStartSummaryGracePeriod(TransportOrigin::Mtp, true, false, false));
    assert(ShouldStartSummaryGracePeriod(TransportOrigin::Ftp, true, false, false));
}
