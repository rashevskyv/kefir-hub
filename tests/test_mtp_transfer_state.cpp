// Host test for the "Copying via MTP" box state machine in
// sphaira/include/haze/mtp_transfer_state.hpp (used by haze_helper.cpp, haze_internal.cpp).
// Each scenario calls the transitions in the order the call sites do.
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_mtp_transfer_state.cpp -o /tmp/t && /tmp/t

#include "haze/mtp_transfer_state.hpp"

#include <cstdint>
#include <cstdio>

using namespace sphaira::haze;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

// The box worker leaves its loop (haze_helper.cpp): returns true when it must cancel libhaze.
static bool WorkerExit(MtpTransferState& s, std::uint64_t last_seq, bool user_cancelled) {
    const bool cancel = user_cancelled && OnWorkerCancel(s, last_seq);
    MarkHandled(s, last_seq);
    return cancel;
}

// One file copied, the box closes after the idle timeout.
static int test_normal_transfer() {
    MtpTransferState s;
    CHECK(OnFileStart(s));          // first file opens the box
    CHECK(s.active && s.ui_alive && !s.aborted && s.seq == 1);
    CHECK(OnUiLaunch(s, false));
    CHECK(!IsIdle(s, 1));           // still copying: the idle timer must not close the box

    OnFileDone(s, false);
    CHECK(!s.active && !s.aborted);
    CHECK(IsIdle(s, 1));

    CHECK(!WorkerExit(s, 1, false));
    CHECK(!OnUiClosed(s, true));    // nothing pending: no relaunch
    CHECK(!s.ui_alive && s.handled_seq == 1);
    return 0;
}

// Several files in one copy: one box, the label follows seq.
static int test_second_file_reuses_the_box() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    OnFileDone(s, false);
    CHECK(!OnFileStart(s));         // the box is alive: do not open a second one
    CHECK(s.seq == 2 && s.active);
    CHECK(!IsIdle(s, 1));           // the worker saw file 1 only: not idle
    OnFileDone(s, false);
    CHECK(IsIdle(s, 2));
    return 0;
}

// B -> confirm on the console while a file is copying (checklist A3).
static int test_switch_side_cancel() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    CHECK(OnUiLaunch(s, false));

    CHECK(OnUserCancel(s));         // cancel callback: libhaze must be cancelled
    CHECK(!s.active && s.aborted && s.handled_seq == s.seq);

    CHECK(!WorkerExit(s, 1, true)); // the worker sees the same cancel: no second libhaze cancel
    CHECK(!OnUserCancel(s));        // repeated cancel is a no-op
    OnFileDone(s, true);            // libhaze reports the aborted file
    CHECK(!s.active && s.aborted);

    CHECK(!OnUiClosed(s, true));
    CHECK(!s.ui_alive);

    CHECK(OnFileStart(s));          // the next copy starts clean, in a new box
    CHECK(s.active && !s.aborted);
    return 0;
}

// The worker notices the exit request before the cancel callback ran.
static int test_worker_cancels_first() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    CHECK(WorkerExit(s, 1, true));  // the worker cancels libhaze itself
    CHECK(!s.active && s.aborted && s.handled_seq == 1);
    CHECK(!OnUserCancel(s));        // the callback after it has nothing left to cancel
    CHECK(!OnUiClosed(s, true));
    return 0;
}

// The PC cancels, or the URB fails: libhaze ends the file as aborted (checklist A4).
static int test_pc_cancel() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    OnFileDone(s, true);
    CHECK(!s.active && s.aborted);  // the worker closes the box on `aborted`

    CHECK(!WorkerExit(s, 1, false));
    CHECK(!OnUiClosed(s, true));
    CHECK(!s.ui_alive);

    CHECK(OnFileStart(s));          // a new copy from the PC works right away (checklist A8)
    CHECK(s.active && !s.aborted && s.ui_alive);
    return 0;
}

// B -> confirm in the 1.5 s idle window after a finished file: nothing to cancel in libhaze.
static int test_cancel_in_idle_window() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    OnFileDone(s, false);

    CHECK(!OnUserCancel(s));
    CHECK(!s.aborted);              // the finished file is not marked as cancelled
    CHECK(!WorkerExit(s, 1, true));
    CHECK(!OnUiClosed(s, true));

    CHECK(OnFileStart(s));          // the next file is not hit by the earlier cancel
    CHECK(s.active && !s.aborted);
    return 0;
}

// A file arrives after the worker decided to close but before the box is gone.
static int test_relaunch_after_late_file() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    OnFileDone(s, false);
    CHECK(!WorkerExit(s, 1, false)); // idle timeout: the worker leaves with last_seq = 1

    CHECK(!OnFileStart(s));          // file 2: the old box still counts as alive
    CHECK(OnUiClosed(s, true));      // so the close must open a new box
    CHECK(s.ui_alive);
    CHECK(OnUiLaunch(s, false));

    OnFileDone(s, false);
    CHECK(!WorkerExit(s, 2, false));
    CHECK(!OnUiClosed(s, true));
    return 0;
}

// A queued "open the box" event that runs after the work is already over.
static int test_stale_launch() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    OnFileDone(s, false);
    CHECK(OnUiLaunch(s, false));    // finished but never shown: still show it
    OnSessionEnd(s);                // the PC disconnects before the event runs
    CHECK(!OnUiLaunch(s, false));
    CHECK(!s.ui_alive);
    return 0;
}

// MTP is switched off (or the PC closes the session) while a file is copying.
static int test_exit_during_transfer() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    OnSessionEnd(s);
    CHECK(!s.active && !s.aborted && s.handled_seq == s.seq);

    CHECK(!WorkerExit(s, 1, false)); // exit is not a user cancel
    CHECK(!OnUiClosed(s, false));    // shutting down: never relaunch
    CHECK(!s.ui_alive);

    MtpTransferState queued;         // the box was only queued when exit came
    CHECK(OnFileStart(queued));
    CHECK(!OnUiLaunch(queued, true));
    CHECK(!queued.ui_alive);
    return 0;
}

// The app could not show the box (PushTransfer failed): the next file tries again.
static int test_push_failed() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    CHECK(!OnUiClosed(s, false));
    CHECK(!s.ui_alive);
    CHECK(OnFileStart(s));
    return 0;
}

static int test_init_resets() {
    MtpTransferState s;
    CHECK(OnFileStart(s));
    CHECK(!OnFileStart(s));
    OnInit(s);
    CHECK(!s.active && !s.ui_alive && s.seq == 0 && s.handled_seq == 0);
    CHECK(!HasPendingWork(s));
    return 0;
}

// Progress callback: libhaze reports (offset before the chunk, chunk size).
static int test_transferred_bytes() {
    CHECK(TransferredBytes(0, 524288) == 524288);
    CHECK(TransferredBytes(524288, 524288) == 1048576);
    const std::int64_t five_gib = 5368709120LL;
    CHECK(TransferredBytes(five_gib, 1048576) == five_gib + 1048576);

    // Overflow and bad input keep the offset.
    CHECK(TransferredBytes(INT64_MAX, 1) == INT64_MAX);
    CHECK(TransferredBytes(INT64_MAX - 10, 20) == INT64_MAX - 10);
    CHECK(TransferredBytes(-1, 100) == -1);
    CHECK(TransferredBytes(100, -1) == 100);
    return 0;
}

int main() {
    if (test_normal_transfer()) return 1;
    if (test_second_file_reuses_the_box()) return 1;
    if (test_switch_side_cancel()) return 1;
    if (test_worker_cancels_first()) return 1;
    if (test_pc_cancel()) return 1;
    if (test_cancel_in_idle_window()) return 1;
    if (test_relaunch_after_late_file()) return 1;
    if (test_stale_launch()) return 1;
    if (test_exit_during_transfer()) return 1;
    if (test_push_failed()) return 1;
    if (test_init_resets()) return 1;
    if (test_transferred_bytes()) return 1;

    std::printf("ok  mtp_transfer_state: %d checks passed\n", g_checks);
    return 0;
}
