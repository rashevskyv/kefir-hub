#pragma once

#include <cstddef>
#include <string_view>

namespace sphaira::ui::menu::dbi {

enum class TransportOrigin {
    Dbi,
    Usb,
    Mtp,
    Ftp,
    Web,
};

inline constexpr bool HasKnownBatchTotals(TransportOrigin origin) {
    return origin == TransportOrigin::Dbi || origin == TransportOrigin::Usb || origin == TransportOrigin::Web;
}

inline constexpr size_t QueueIndexNotFound = static_cast<size_t>(-1);

template <typename Entries>
size_t FindQueueIndex(const Entries& entries, std::string_view batch_id, std::string_view name) {
    if (!batch_id.empty()) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].batch_id == batch_id) return i;
        }
        return QueueIndexNotFound;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].file_name == name) return i;
    }
    return QueueIndexNotFound;
}

template <typename Entries>
bool AllQueueEntriesTerminal(const Entries& entries) {
    if (entries.empty()) return false;
    for (const auto& entry : entries) {
        if (!entry.install_result.has_value()) return false;
    }
    return true;
}

inline bool CanTransitionToSummary(TransportOrigin origin, bool all_terminal,
                                   bool install_in_progress, size_t transport_queued_count) {
    if (origin == TransportOrigin::Web) return all_terminal;
    if (origin == TransportOrigin::Ftp || origin == TransportOrigin::Mtp) {
        return !install_in_progress && transport_queued_count == 0;
    }
    return !install_in_progress;
}

inline constexpr double SUMMARY_GRACE_PERIOD_SEC = 3.0;

inline bool ShouldStartSummaryGracePeriod(TransportOrigin origin, bool all_terminal,
                                         bool install_in_progress, bool transport_busy) {
    if (HasKnownBatchTotals(origin)) return false;
    return all_terminal && !install_in_progress && !transport_busy;
}

} // namespace sphaira::ui::menu::dbi
