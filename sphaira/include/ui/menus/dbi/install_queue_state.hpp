#pragma once

#include <cstddef>
#include <string_view>

namespace sphaira::ui::menu::dbi {

enum class TransportOrigin {
    Dbi,
    Mtp,
    Ftp,
    Web,
};

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
    if (origin == TransportOrigin::Ftp) return !install_in_progress && transport_queued_count == 0;
    return !install_in_progress;
}

inline bool ShouldRestartMtp(TransportOrigin origin, bool was_cancelled, bool worker_torn_down) {
    return origin == TransportOrigin::Mtp && was_cancelled && worker_torn_down;
}

} // namespace sphaira::ui::menu::dbi
