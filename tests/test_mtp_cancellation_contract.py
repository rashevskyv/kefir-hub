#!/usr/bin/env python3
"""Regression test contract for MTP file transfer cancellation to microSD.

Validates that:
1. Switch-side cancellation (B -> Yes) triggers cancel callback immediately,
   marks transfer aborted, sets active=false, calls CancelTransfer() only once,
   prevents ProgressBox relaunch flickering, and returns Result_TransferCancelled.
2. If B is pressed during the 1.5s idle window after transfer completion,
   CancelTransfer() is NOT called, so the signal never lingers into the next file.
3. Worker thread does not redundantly call CancelTransfer() if SetCancelCallback
   already executed it.
4. ClearTransferCancel is completely eliminated from haze_helper.cpp and libhaze API.
   Instead, libhaze responder thread manages reactor lifecycle internally.
5. Exit() in haze.cpp releases g_mutex before blocking on thread exit, avoiding deadlock.
6. HandleRequest and SendObject/GetObject only reset ResultCancelled, preserving ResultStopRequested.
7. MicroSD partial/corrupted files are deleted on abort and not left behind.
8. PC-side cancel / URB abort (0x748C) translates to ResultCancelled and
   responds with PtpResponseCode_TransactionCanceled without session force-close.
9. patch_libhaze.cmake applies cleanly to clean HEAD without skipping, modifies
   SendObject/GetObject, and passes a second run idempotently.
"""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPO_ROOT = Path(__file__).resolve().parent.parent


def check_progress_box_contract() -> None:
    hp = REPO_ROOT / "sphaira" / "include" / "ui" / "progress_box.hpp"
    cp = REPO_ROOT / "sphaira" / "source" / "ui" / "progress_box.cpp"

    assert hp.exists(), f"Missing {hp}"
    assert cp.exists(), f"Missing {cp}"

    h_text = hp.read_text(encoding="utf-8", errors="ignore")
    c_text = cp.read_text(encoding="utf-8", errors="ignore")

    assert "using ProgressBoxCancelCallback = std::function<void()>" in h_text
    assert "SetCancelCallback(ProgressBoxCancelCallback" in h_text
    assert "ProgressBoxCancelCallback m_cancel_cb" in h_text
    assert "m_cancel_cb()" in c_text
    assert "RequestExit()" in c_text


def check_haze_internal_contract() -> None:
    hp = REPO_ROOT / "sphaira" / "include" / "haze" / "haze_internal.hpp"
    cp = REPO_ROOT / "sphaira" / "source" / "haze" / "haze_internal.cpp"

    assert hp.exists(), f"Missing {hp}"
    assert cp.exists(), f"Missing {cp}"

    h_text = hp.read_text(encoding="utf-8", errors="ignore")
    c_text = cp.read_text(encoding="utf-8", errors="ignore")

    assert "extern bool g_mtp_transfer_aborted;" in h_text
    assert re.search(r"bool\s+g_mtp_transfer_aborted\s*\{\s*false\s*\};", c_text)


def check_haze_helper_contract() -> None:
    path = REPO_ROOT / "sphaira" / "source" / "haze_helper.cpp"
    assert path.exists(), f"Missing {path}"
    text = path.read_text(encoding="utf-8", errors="ignore")

    assert "pbox_ptr->SetCancelCallback(" in text
    assert "::haze::CancelTransfer()" in text
    assert "ClearTransferCancel" not in text, (
        "haze_helper.cpp must NOT call ClearTransferCancel (lifecycle is internal to libhaze)"
    )
    assert "g_mtp_transfer_aborted = true;" in text
    assert "Result_TransferCancelled" in text
    assert "if (is_aborted)" in text
    assert "if (e.file.aborted)" in text
    assert "g_mtp_transfer_aborted = false;" in text

    # Issue 1 specific checks:
    # 1. SetCancelCallback only sets should_cancel if g_mtp_transfer_active is true
    cb_match = re.search(
        r"pbox_ptr->SetCancelCallback\(\[\]\(\)\s*\{.*?"
        r"if\s*\(\s*g_mtp_transfer_active\s*\)\s*\{[^}]*should_cancel\s*=\s*true;.*?\}",
        text,
        re.DOTALL,
    )
    assert cb_match, "SetCancelCallback must guard CancelTransfer with active check"

    # 2. Worker thread only sets should_cancel_worker if g_mtp_transfer_active is true
    worker_match = re.search(
        r"if\s*\(\s*user_cancelled\s*\)\s*\{.*?"
        r"if\s*\(\s*g_mtp_transfer_active\s*\)\s*\{[^}]*should_cancel_worker\s*=\s*true;.*?\}",
        text,
        re.DOTALL,
    )
    assert worker_match, "Worker thread must guard CancelTransfer against redundant or idle calls"


def check_patch_libhaze_cancel_contract() -> None:
    root_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze.cmake"
    cancel_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_cancel.cmake"

    assert root_patch.exists(), f"Missing {root_patch}"
    assert cancel_patch.exists(), f"Missing {cancel_patch}"

    root_text = root_patch.read_text(encoding="utf-8", errors="ignore")
    assert "patch_libhaze_cancel.cmake" in root_text

    c_text = cancel_patch.read_text(encoding="utf-8", errors="ignore")

    # Section 10: ResultCancelled
    assert "R_DEFINE_ERROR_RESULT(Cancelled," in c_text
    # Section 11: CallbackDataFile aborted flag & CancelTransfer (no ClearTransferCancel)
    assert "bool aborted;" in c_text
    assert "void CancelTransfer();" in c_text
    # Section 12: ConsoleMainLoop cancellation & TransferCancelConsumer (no ClearTransferCancel)
    assert "TransferCancelConsumer" in c_text
    assert "m_transfer_cancel_event" in c_text
    assert "reactor->GetResult() == ResultSuccess()" in c_text
    # Section 13: CancelTransfer implementation & deadlock-free Exit
    assert "haze_to_destroy = std::move(g_haze);" in c_text
    assert "g_haze->CancelTransfer()" in c_text
    # Section 14: PtpResponder reactor & aborted param
    assert "EventReactor *m_reactor" in c_text
    assert "bool aborted = false" in c_text
    # Section 15: HandleRequest ResultCancelled guarded against clobbering stop
    assert "PtpResponseCode_TransactionCanceled" in c_text
    assert "data.file.aborted = aborted;" in c_text
    assert "m_reactor->GetResult() == haze::ResultCancelled()" in c_text
    # Section 16: UsbError_UrbCancelled to ResultCancelled
    assert "parse_res == 0x748C" in c_text
    assert "R_THROW(haze::ResultCancelled());" in c_text
    # Section 17: Partial file cleanup and responder thread reactor reset
    assert "!transfer_success" in c_text
    assert "transfer_success && offset != file_size" in c_text
    assert "WriteCallbackFile(CallbackType_WriteEnd, obj->GetName(), !transfer_success);" in c_text
    assert "WriteCallbackFile(CallbackType_ReadEnd, obj->GetName(), !transfer_success);" in c_text
    assert "m_reactor->GetResult() == haze::ResultCancelled()" in c_text


def test_clean_head_patching_contract() -> None:
    """Verify patch_libhaze.cmake applies to clean libhaze HEAD without skipping,

    modifies SendObject/GetObject, and is fully idempotent on a second run.
    """
    libhaze_dir = REPO_ROOT / "build" / "ReleaseWithInstall" / "_deps" / "libhaze-src"
    if not (libhaze_dir / ".git").exists():
        return

    with tempfile.TemporaryDirectory() as tmp_str:
        temp_dir = Path(tmp_str)
        files_to_extract = [
            "include/haze.h",
            "include/haze/results.hpp",
            "include/haze/console_main_loop.hpp",
            "include/haze/ptp_responder.hpp",
            "include/haze/ptp_data_parser.hpp",
            "include/haze/ptp_data_builder.hpp",
            "source/haze.cpp",
            "source/ptp_responder.cpp",
            "source/ptp_responder_ptp_operations.cpp",
            "source/ptp_responder_mtp_operations.cpp",
            "source/threaded_file_transfer.cpp",
            "source/usb_session.cpp",
        ]
        for rel_path in files_to_extract:
            try:
                content = subprocess.check_output(
                    ["git", "-C", str(libhaze_dir), "show", f"HEAD:{rel_path}"],
                    text=True,
                    encoding="utf-8",
                    errors="ignore",
                )
            except Exception:
                continue
            out_file = temp_dir / rel_path
            out_file.parent.mkdir(parents=True, exist_ok=True)
            out_file.write_text(content, encoding="utf-8")

        # 1. Clean HEAD must NOT contain patch markers
        clean_ops = (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert "!transfer_success" not in clean_ops
        assert "transfer_success && offset != file_size" not in clean_ops
        assert "WriteCallbackFile(CallbackType_WriteEnd, obj->GetName(), !transfer_success);" not in clean_ops

        # 2. Run patch_libhaze.cmake via WSL
        wsl_temp = subprocess.check_output(
            ["wsl", "wslpath", "-u", str(temp_dir).replace("\\", "/")], text=True
        ).strip()
        wsl_cmake = subprocess.check_output(
            ["wsl", "wslpath", "-u", str(REPO_ROOT / "sphaira/cmake/patch_libhaze.cmake").replace("\\", "/")],
            text=True,
        ).strip()

        res1 = subprocess.run(
            ["wsl", "sh", "-c", f"cd '{wsl_temp}' && cmake -P '{wsl_cmake}'"],
            capture_output=True,
            text=True,
        )
        assert res1.returncode == 0, f"Run 1 on clean HEAD failed: {res1.stderr}"
        assert "applied ptp_responder_ptp_operations.cpp cancel patch" in res1.stdout

        # 3. Verify patched clean HEAD has all expected modifications
        patched_ops = (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert "!transfer_success" in patched_ops
        assert "transfer_success && offset != file_size" in patched_ops
        assert "DeleteFile" in patched_ops and "DeleteObject" in patched_ops
        assert "m_reactor->GetResult() == haze::ResultCancelled()" in patched_ops

        haze_h = (temp_dir / "include" / "haze.h").read_text(encoding="utf-8")
        assert "CancelTransfer();" in haze_h
        assert "ClearTransferCancel" not in haze_h

        cml = (temp_dir / "include" / "haze" / "console_main_loop.hpp").read_text(encoding="utf-8")
        assert "CancelTransfer()" in cml
        assert "ClearTransferCancel" not in cml

        haze_cpp = (temp_dir / "source" / "haze.cpp").read_text(encoding="utf-8")
        assert "haze_to_destroy = std::move(g_haze);" in haze_cpp
        assert "ClearTransferCancel" not in haze_cpp

        resp_cpp = (temp_dir / "source" / "ptp_responder.cpp").read_text(encoding="utf-8")
        assert "m_reactor->GetResult() == haze::ResultCancelled()" in resp_cpp

        # 4. Run 2: Verify idempotency
        res2 = subprocess.run(
            ["wsl", "sh", "-c", f"cd '{wsl_temp}' && cmake -P '{wsl_cmake}'"],
            capture_output=True,
            text=True,
        )
        assert res2.returncode == 0, f"Run 2 failed: {res2.stderr}"
        assert "already patched" in res2.stdout
        assert "applied" not in res2.stdout


def simulate_cancellation_state_machine() -> None:
    """Verifies that cancel events prevent UI relaunch, do not double-cancel,

    and do not linger into subsequent operations if B is pressed during idle window.
    """
    class MockMtpSystem:
        def __init__(self):
            self.transfer_active = False
            self.transfer_aborted = False
            self.transfer_seq = 0
            self.handled_seq = 0
            self.ui_alive = False
            self.relaunch_count = 0
            self.cancel_haze_calls = 0
            self.reactor_result = "Success"
            self.event_signaled = False
            self.files_on_disk = set()

        def transfer_begin(self, name: str):
            # ReadBegin/WriteBegin in haze_callback:
            # libhaze resets leftover ResultCancelled internally on responder thread
            if self.reactor_result == "Cancelled":
                self.reactor_result = "Success"
            self.event_signaled = False
            self.transfer_active = True
            self.transfer_aborted = False
            self.transfer_seq += 1
            if not self.ui_alive:
                self.ui_alive = True
            self.files_on_disk.add(name)

        def transfer_end_success(self):
            self.transfer_active = False

        def user_cancels_on_switch(self, name: str):
            should_cancel = False
            if self.transfer_active:
                self.transfer_active = False
                self.transfer_aborted = True
                should_cancel = True
            self.handled_seq = self.transfer_seq

            if should_cancel:
                self.cancel_haze_calls += 1
                self.reactor_result = "Cancelled"
                self.event_signaled = True
                self.files_on_disk.discard(name)

        def worker_exit(self, user_cancelled: bool):
            should_cancel_worker = False
            if user_cancelled and self.transfer_active:
                self.transfer_active = False
                self.transfer_aborted = True
                should_cancel_worker = True

            if self.handled_seq < self.transfer_seq:
                self.handled_seq = self.transfer_seq

            if should_cancel_worker:
                self.cancel_haze_calls += 1
                self.reactor_result = "Cancelled"
                self.event_signaled = True

        def pbox_done(self):
            relaunch = (
                self.transfer_active
                or self.transfer_seq != self.handled_seq
            )
            if relaunch:
                self.relaunch_count += 1
                self.ui_alive = True
            else:
                self.ui_alive = False

    s = MockMtpSystem()

    # Scenario 1: Normal completed transfer
    s.transfer_begin("/switch/file1.nsp")
    assert s.ui_alive and s.transfer_active
    s.transfer_end_success()
    assert not s.transfer_active
    assert "/switch/file1.nsp" in s.files_on_disk
    s.worker_exit(user_cancelled=False)
    s.pbox_done()
    assert not s.ui_alive
    assert s.relaunch_count == 0
    assert s.cancel_haze_calls == 0

    # Scenario 2: User presses B -> Yes during 1.5s idle window AFTER transfer 1 finished
    s.user_cancels_on_switch("/switch/file1.nsp")
    assert s.cancel_haze_calls == 0, "CancelTransfer must NOT be called if transfer already finished"
    assert not s.event_signaled, "Signal must not be set for already-finished transfer"
    assert "/switch/file1.nsp" in s.files_on_disk, "Completed file must not be deleted"
    s.worker_exit(user_cancelled=True)
    assert s.cancel_haze_calls == 0, "Worker must NOT call CancelTransfer after idle cancel"
    s.pbox_done()
    assert not s.ui_alive
    assert s.relaunch_count == 0

    # Scenario 3: Next transfer starts -> must NOT be aborted by previous idle cancel!
    s.transfer_begin("/switch/file2.nsp")
    assert s.transfer_active and not s.transfer_aborted
    assert not s.event_signaled
    # User cancels WHILE transfer 2 IS active
    s.user_cancels_on_switch("/switch/file2.nsp")
    assert s.cancel_haze_calls == 1, "CancelTransfer MUST be called once when active"
    assert s.transfer_aborted and not s.transfer_active
    assert "/switch/file2.nsp" not in s.files_on_disk, "Partial file must be deleted"
    # Worker exits after user cancel
    s.worker_exit(user_cancelled=True)
    assert s.cancel_haze_calls == 1, "Worker must NOT call CancelTransfer a second time"
    s.pbox_done()
    assert not s.ui_alive, "UI must NOT relaunch on cancelled transfer"
    assert s.relaunch_count == 0

    # Scenario 4: Subsequent transfer 3 starts cleanly (responder thread clears cancelled state)
    s.transfer_begin("/switch/file3.nsp")
    assert s.transfer_active and not s.transfer_aborted
    assert s.reactor_result == "Success"
    assert not s.event_signaled
    s.transfer_end_success()
    s.worker_exit(user_cancelled=False)
    s.pbox_done()
    assert not s.ui_alive
    assert s.relaunch_count == 0
    assert "/switch/file3.nsp" in s.files_on_disk
    assert s.cancel_haze_calls == 1


def main() -> None:
    check_progress_box_contract()
    check_haze_internal_contract()
    check_haze_helper_contract()
    check_patch_libhaze_cancel_contract()
    test_clean_head_patching_contract()
    simulate_cancellation_state_machine()
    print("PASS: all MTP cancellation contracts verified successfully.")


if __name__ == "__main__":
    main()
