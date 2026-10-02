#pragma once

// State of the "Copying via MTP" progress box (haze_helper.cpp, haze_internal.cpp)
// as pure transitions, no libnx. The caller holds g_mtp_ui_mutex around every call
// and performs the returned action (cancel libhaze, open the box) after unlocking.
// Host test: tests/test_mtp_transfer_state.cpp.

#include <cstdint>

namespace sphaira::haze {

struct MtpTransferState {
    bool active{};               // libhaze is moving a file right now
    bool aborted{};              // the current/last file was cancelled or failed
    bool ui_alive{};             // a progress box is open or queued to open
    std::uint64_t seq{};         // files announced by libhaze
    std::uint64_t handled_seq{}; // files the box has shown, or the user dismissed
};

// A file is in flight, or one was announced that no box has covered yet.
constexpr bool HasPendingWork(const MtpTransferState& s) {
    return s.active || s.seq != s.handled_seq;
}

// The box worker saw files up to last_seq and nothing has happened since.
constexpr bool IsIdle(const MtpTransferState& s, std::uint64_t last_seq) {
    return !s.active && s.seq == last_seq;
}

constexpr void MarkHandled(MtpTransferState& s, std::uint64_t last_seq) {
    if (s.handled_seq < last_seq) {
        s.handled_seq = last_seq;
    }
}

// libhaze started a file. Returns true when a progress box must be opened.
constexpr bool OnFileStart(MtpTransferState& s) {
    s.active = true;
    s.aborted = false;
    s.seq++;
    if (s.ui_alive) {
        return false;
    }
    s.ui_alive = true;
    return true;
}

// libhaze finished a file; aborted = it was cancelled (either side) or the transport failed.
constexpr void OnFileDone(MtpTransferState& s, bool aborted) {
    s.active = false;
    if (aborted) {
        s.aborted = true;
    }
}

// The user confirmed the cancel (box cancel callback, haze::CancelTransfer()).
// Returns true when libhaze has a transfer to cancel; false = nothing in flight (idle window, repeated cancel).
constexpr bool OnUserCancel(MtpTransferState& s) {
    const bool cancel_worker = s.active;
    if (s.active) {
        s.active = false;
        s.aborted = true;
    }
    s.handled_seq = s.seq;
    return cancel_worker;
}

// The box worker noticed the cancel; it only knows the files up to last_seq.
constexpr bool OnWorkerCancel(MtpTransferState& s, std::uint64_t last_seq) {
    const bool cancel_worker = s.active;
    if (s.active) {
        s.active = false;
        s.aborted = true;
    }
    MarkHandled(s, last_seq);
    return cancel_worker;
}

// The queued "open the box" event runs on the UI thread. Returns false when the box is no longer needed.
constexpr bool OnUiLaunch(MtpTransferState& s, bool exiting) {
    if (exiting || !HasPendingWork(s)) {
        s.ui_alive = false;
        return false;
    }
    return true;
}

// The box is gone. can_relaunch = it was really shown and MTP is not shutting down.
// Returns true when a new box must be opened for a file that arrived meanwhile.
constexpr bool OnUiClosed(MtpTransferState& s, bool can_relaunch) {
    s.ui_alive = can_relaunch && HasPendingWork(s);
    return s.ui_alive;
}

// The PC closed the session, or MTP is being switched off: nothing is left to show.
constexpr void OnSessionEnd(MtpTransferState& s) {
    s.active = false;
    s.aborted = false;
    s.handled_seq = s.seq;
}

// MTP is starting. `aborted` is cleared separately at the top of Init().
constexpr void OnInit(MtpTransferState& s) {
    s.ui_alive = false;
    s.active = false;
    s.seq = 0;
    s.handled_seq = 0;
}

// Bytes done after a progress callback: libhaze reports (offset before the chunk, chunk size).
constexpr std::int64_t TransferredBytes(std::int64_t offset, std::int64_t chunk) {
    return (offset >= 0 && chunk >= 0 && offset <= INT64_MAX - chunk) ? (offset + chunk) : offset;
}

} // namespace sphaira::haze
