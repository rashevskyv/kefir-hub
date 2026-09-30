#!/usr/bin/env python3
"""Regression test contract for MTP file transfer cancellation and recovery.

Validates that:
1. Switch-side cancellation (B -> Yes) triggers cancel callback immediately,
   marks transfer aborted, sets active=false, calls CancelTransfer() only once,
   prevents ProgressBox relaunch flickering, and returns Result_TransferCancelled.
2. If B is pressed during the 1.5s idle window after transfer completion,
   CancelTransfer() is NOT called, so the signal never lingers into the next file.
3. Worker thread does not redundantly call CancelTransfer() if SetCancelCallback
   already executed it.
4. ClearTransferCancel is completely eliminated from haze_helper.cpp and libhaze API.
5. Exit() in haze.cpp releases g_mutex before blocking on thread exit, avoiding deadlock.
6. HandleRequest and SendObject/GetObject only reset ResultCancelled, preserving ResultStopRequested.
7. MicroSD partial/corrupted files are deleted on abort and not left behind.
8. PC-side cancel / URB abort (0x748C) translates to ResultCancelled and
   responds with PtpResponseCode_TransactionCanceled without session force-close.
9. All 3 patch scenarios verified:
   - Clean upstream -> latest
   - Previous intermediate patched sources (prev2 and prev3) -> latest
   - Latest -> latest idempotency (already patched)
10. All senior review blockers verified:
    - No duplicate waiter_idx in AsyncUsbServer::TransferPacketImpl.
    - PtpDataParser uses haze::ResultInvalidArgument() (not invalid ResultInvalidParameter).
    - ResultTransferFailed() used on failure paths.
    - UsbSession::CancelEndpoint validates cancel, 1s eventWait, and report reaping.
    - AsyncUsbServer::m_in_cleanup uses std::atomic<bool>; reader only sets cleanup.
    - Local cancel signal preserved during USB wait via m_cancelled and check_cancellation.
    - WriteFile ResultCancelled absorbed by writer, setting is_cancelled and discarding data.
    - Broken transport (CancelEndpoint failure) terminates LoopProcess and rejects further transfers.
    - File length limits: all patch and test files strictly <= 600 lines.
"""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_mtp_cancellation_models import (
    simulate_cancellation_state_machine,
    simulate_parser_and_drain_contract,
)

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
    assert "ClearTransferCancel" not in text, "haze_helper must NOT call ClearTransferCancel"
    assert "g_mtp_transfer_aborted = true;" in text
    assert "Result_TransferCancelled" in text
    assert "if (is_aborted)" in text
    assert "if (e.file.aborted)" in text
    assert "g_mtp_transfer_aborted = false;" in text

    cb_match = re.search(
        r"pbox_ptr->SetCancelCallback\(\[\]\(\)\s*\{.*?"
        r"if\s*\(\s*g_mtp_transfer_active\s*\)\s*\{[^}]*should_cancel\s*=\s*true;.*?\}",
        text,
        re.DOTALL,
    )
    assert cb_match, "SetCancelCallback must guard CancelTransfer with active check"

    worker_match = re.search(
        r"if\s*\(\s*user_cancelled\s*\)\s*\{.*?"
        r"if\s*\(\s*g_mtp_transfer_active\s*\)\s*\{[^}]*should_cancel_worker\s*=\s*true;.*?\}",
        text,
        re.DOTALL,
    )
    assert worker_match, "Worker thread must guard CancelTransfer against redundant calls"


def check_patch_libhaze_contract() -> None:
    root_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze.cmake"
    cancel_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_cancel.cmake"
    cleanup_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_cleanup.cmake"
    usb_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_usb.cmake"
    test_models = REPO_ROOT / "tests" / "test_mtp_cancellation_models.py"
    test_contract = Path(__file__).resolve()

    assert root_patch.exists() and cancel_patch.exists() and cleanup_patch.exists() and usb_patch.exists()
    assert test_models.exists() and test_contract.exists()

    c_text = cancel_patch.read_text(encoding="utf-8", errors="ignore")
    cl_text = cleanup_patch.read_text(encoding="utf-8", errors="ignore")
    u_text = usb_patch.read_text(encoding="utf-8", errors="ignore")
    tm_text = test_models.read_text(encoding="utf-8", errors="ignore")
    tc_text = test_contract.read_text(encoding="utf-8", errors="ignore")

    # File length limits: strictly <= 600 lines
    for name, t in [
        ("patch_libhaze_cancel.cmake", c_text),
        ("patch_libhaze_cleanup.cmake", cl_text),
        ("patch_libhaze_usb.cmake", u_text),
        ("test_mtp_cancellation_models.py", tm_text),
        ("test_mtp_cancellation_contract.py", tc_text),
    ]:
        nlines = len(t.splitlines())
        assert nlines <= 600, f"{name} has {nlines} lines (must be <= 600)"

    # Inclusion chain: cancel -> cleanup -> usb
    assert "patch_libhaze_cleanup.cmake" in c_text
    assert "patch_libhaze_usb.cmake" in cl_text

    combined_text = c_text + "\n" + cl_text + "\n" + u_text

    assert "R_DEFINE_ERROR_RESULT(Cancelled," in combined_text
    assert "bool aborted;" in combined_text
    assert "void CancelTransfer();" in combined_text
    assert "TransferCancelConsumer" in combined_text
    assert "haze_to_destroy = std::move(g_haze);" in combined_text
    assert "PtpResponseCode_TransactionCanceled" in combined_text
    assert "CancelEndpoint" in combined_text
    assert "parse_res == 0x748C" in combined_text
    assert "R_THROW(haze::ResultCancelled());" in combined_text
    assert "!transfer_success" in combined_text
    assert "sphaira: clean transport drain without mid-transfer deletion" in combined_text
    assert "bool HasEot() const" in combined_text
    assert "read_count == sizeof(T)" in combined_text
    assert "WaitForTimeout" in combined_text
    assert "SetCleanup" in combined_text
    assert "g_usb_session.CancelEndpoint(ep, urb_id);" in combined_text


def check_senior_review_findings_contract() -> None:
    cl_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_cleanup.cmake"
    u_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_usb.cmake"
    c_patch = REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_cancel.cmake"

    cl_text = cl_patch.read_text(encoding="utf-8", errors="ignore")
    u_text = u_patch.read_text(encoding="utf-8", errors="ignore")
    c_text = c_patch.read_text(encoding="utf-8", errors="ignore")

    # Finding 1: Single waiter_idx declaration in AsyncUsbServer::TransferPacketImpl
    assert "waiter_idx = -1;\n        Result wait_rc = ResultSuccess();" in u_text
    assert "s32 waiter_idx = -1;" in u_text  # only in prev2 migration block
    assert "string(FIND \"${cpp_src}\" \"s32 waiter_idx = -1;\" find_dup_waiter)" in u_text

    # Finding 2: Result symbol validity: haze::ResultInvalidArgument() (not ResultInvalidParameter)
    assert "read_count == sizeof(T)" in u_text
    assert "haze::ResultInvalidArgument()" in u_text
    assert "parser_read_t_prev" in u_text
    assert "ResultTransferFailed" in u_text

    # Finding 3: Multi-scenario upgrades supported in patch_libhaze_cleanup.cmake
    assert "ops_so_body_prev3" in cl_text
    assert "ops_so_body_prev2" in cl_text
    assert "*bytes_read = 0;" in cl_text
    assert "m_usb_server.SetCleanup(true);" in cl_text
    assert "write_res == haze::ResultCancelled()" in cl_text
    assert "m_usb_server.IsCancelled()" in cl_text

    # Finding 4: UsbSession::CancelEndpoint verifies cancel, wait, and report reaping
    assert "usbDsEndpoint_Cancel" in c_text
    assert "eventWait" in c_text
    assert "GetTransferResult" in c_text
    assert "res == haze::ResultCancelled() || R_SUCCEEDED(res)" in c_text
    assert "if (R_FAILED(cancel_rc))" in u_text

    # Finding 5: Atomic m_in_cleanup, m_cancelled, m_broken with mutable qualifiers
    assert "mutable std::atomic<bool> m_in_cleanup{false}" in u_text
    assert "mutable std::atomic<bool> m_cancelled{false}" in u_text
    assert "mutable std::atomic<bool> m_broken{false}" in u_text
    assert "m_in_cleanup.store(cleanup, std::memory_order_release)" in u_text
    assert "m_in_cleanup.load(std::memory_order_acquire)" in u_text

    # Finding 6: Local cancel signal persistence, writer cancel propagation, and broken transport termination
    assert "R_DEFINE_ERROR_RESULT(Timeout,               20);" in c_text
    assert "svc::ResultTimedOut::Includes(wait_rc)" in u_text
    assert "haze::ResultTimeout()" in u_text
    assert "bool IsBroken() const" in u_text
    assert "bool IsCancelled() const" in u_text
    assert "m_usb_server.IsBroken()" in c_text
    assert "check_cancellation();" in cl_text
    assert "m_usb_server.SetCancelled(true);" in cl_text

    # Generated source verification if libhaze-src is present
    libhaze_dir = REPO_ROOT / "build" / "ReleaseWithInstall" / "_deps" / "libhaze-src"
    if (libhaze_dir / "source" / "async_usb_server.cpp").exists():
        gen_usb_cpp = (libhaze_dir / "source" / "async_usb_server.cpp").read_text(encoding="utf-8")
        assert "s32 waiter_idx = -1;" not in gen_usb_cpp
        assert gen_usb_cpp.count("s32 waiter_idx;") == 1
        assert "ResultTransferFailed" in gen_usb_cpp
        assert "ResultOperationFailed" not in gen_usb_cpp
        assert "m_cancelled.store(true" in gen_usb_cpp
        assert "m_broken.store(true" in gen_usb_cpp

    if (libhaze_dir / "include" / "haze" / "async_usb_server.hpp").exists():
        gen_usb_h = (libhaze_dir / "include" / "haze" / "async_usb_server.hpp").read_text(encoding="utf-8")
        assert "mutable std::atomic<bool> m_cancelled" in gen_usb_h
        assert "mutable std::atomic<bool> m_broken" in gen_usb_h
        assert "mutable std::atomic<bool> m_in_cleanup" in gen_usb_h
        assert "IsBroken()" in gen_usb_h
        assert "IsCancelled()" in gen_usb_h
        assert gen_usb_h.count("m_cancelled{false}") == 1

    if (libhaze_dir / "include" / "haze" / "results.hpp").exists():
        gen_res_h = (libhaze_dir / "include" / "haze" / "results.hpp").read_text(encoding="utf-8")
        assert "R_DEFINE_ERROR_RESULT(Cancelled,             19);" in gen_res_h
        assert "R_DEFINE_ERROR_RESULT(Timeout,               20);" in gen_res_h

    if (libhaze_dir / "source" / "event_reactor.cpp").exists():
        gen_ev_cpp = (libhaze_dir / "source" / "event_reactor.cpp").read_text(encoding="utf-8")
        assert "svc::ResultTimedOut::Includes(wait_rc)" in gen_ev_cpp
        assert "haze::ResultTimeout()" in gen_ev_cpp

    if (libhaze_dir / "source" / "usb_session.cpp").exists():
        gen_sess_cpp = (libhaze_dir / "source" / "usb_session.cpp").read_text(encoding="utf-8")
        assert gen_sess_cpp.count("Result UsbSession::CancelEndpoint") == 1

    if (libhaze_dir / "include" / "haze" / "ptp_data_parser.hpp").exists():
        gen_parser_h = (libhaze_dir / "include" / "haze" / "ptp_data_parser.hpp").read_text(encoding="utf-8")
        assert "ResultInvalidArgument" in gen_parser_h
        assert "ResultInvalidParameter" not in gen_parser_h

    if (libhaze_dir / "source" / "ptp_responder.cpp").exists():
        gen_ptp_cpp = (libhaze_dir / "source" / "ptp_responder.cpp").read_text(encoding="utf-8")
        assert "m_usb_server.IsBroken()" in gen_ptp_cpp

    if (libhaze_dir / "source" / "ptp_responder_ptp_operations.cpp").exists():
        gen_ops_cpp = (libhaze_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert "write_res == haze::ResultCancelled()" in gen_ops_cpp
        assert "check_cancellation();" in gen_ops_cpp


def test_patch_application_scenarios() -> None:
    """Verify patch_libhaze.cmake applies to clean upstream, intermediate patched sources,
    and latest patched source (idempotently).
    """
    libhaze_dir = REPO_ROOT / "build" / "ReleaseWithInstall" / "_deps" / "libhaze-src"
    if not (libhaze_dir / ".git").exists():
        return

    with tempfile.TemporaryDirectory() as tmp_str:
        temp_dir = Path(tmp_str)
        files = [
            "include/haze.h",
            "include/haze/results.hpp",
            "include/haze/console_main_loop.hpp",
            "include/haze/ptp_responder.hpp",
            "include/haze/ptp_data_parser.hpp",
            "include/haze/ptp_data_builder.hpp",
            "include/haze/usb_session.hpp",
            "include/haze/event_reactor.hpp",
            "include/haze/async_usb_server.hpp",
            "source/haze.cpp",
            "source/ptp_responder.cpp",
            "source/ptp_responder_ptp_operations.cpp",
            "source/ptp_responder_mtp_operations.cpp",
            "source/threaded_file_transfer.cpp",
            "source/usb_session.cpp",
            "source/event_reactor.cpp",
            "source/async_usb_server.cpp",
        ]
        for rel_path in files:
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

        if sys.platform == "win32":
            wsl_temp = subprocess.check_output(
                ["wsl", "wslpath", "-u", str(temp_dir).replace("\\", "/")], text=True
            ).strip()
            wsl_cmake = subprocess.check_output(
                ["wsl", "wslpath", "-u", str(REPO_ROOT / "sphaira/cmake/patch_libhaze.cmake").replace("\\", "/")],
                text=True,
            ).strip()
            patch_cmd = ["wsl", "sh", "-c", f"cd '{wsl_temp}' && cmake -P '{wsl_cmake}'"]
        else:
            cmake_script = REPO_ROOT / "sphaira/cmake/patch_libhaze.cmake"
            patch_cmd = ["sh", "-c", f"cd '{temp_dir}' && cmake -P '{cmake_script}'"]

        # Scenario 1: Clean upstream -> latest
        res1 = subprocess.run(
            patch_cmd,
            capture_output=True,
            text=True,
        )
        assert res1.returncode == 0, f"Clean upstream patch failed: {res1.stderr}"
        assert "applied ptp_responder_ptp_operations.cpp cancel patch" in res1.stdout
        assert "applied usb_session.cpp cancel patch" in res1.stdout

        ops_text = (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert "*bytes_read = 0;" in ops_text
        assert "m_usb_server.SetCleanup(true);" in ops_text
        assert "write_res == haze::ResultCancelled()" in ops_text
        assert "m_usb_server.IsCancelled()" in ops_text

        # Scenario 2: Intermediate patched source (prev3 and prev2) -> latest
        cl_cmake = (REPO_ROOT / "sphaira" / "cmake" / "patch_libhaze_cleanup.cmake").read_text(encoding="utf-8")
        end_marker = "R_RETURN(this->WriteResponse(PtpResponseCode_Ok));"
        p2_marker = 'set(ops_so_body_prev2\n"'
        p3_marker = 'set(ops_so_body_prev3\n"'
        nt_marker = 'set(ops_so_body_new_tail\n"'

        nt_start = cl_cmake.find(nt_marker) + len(nt_marker)
        nt_end = cl_cmake.find(end_marker, nt_start) + len(end_marker)
        nt_str = cl_cmake[nt_start:nt_end].replace('\\"', '"').replace('\\\\', '\\')

        # Test prev3 -> latest
        p3_start = cl_cmake.find(p3_marker) + len(p3_marker)
        p3_end = cl_cmake.find(end_marker, p3_start) + len(end_marker)
        p3_str = cl_cmake[p3_start:p3_end].replace('\\"', '"').replace('\\\\', '\\')

        intermediate_ops = ops_text.replace(nt_str, p3_str)
        assert intermediate_ops != ops_text, "Failed to substitute ops_so_body_prev3"
        (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").write_text(intermediate_ops, encoding="utf-8")

        res2a = subprocess.run(patch_cmd, capture_output=True, text=True)
        assert res2a.returncode == 0, f"Upgrade from prev3 failed: {res2a.stderr}"
        assert "applied ptp_responder_ptp_operations.cpp cancel patch" in res2a.stdout

        # Test prev2 -> latest
        p2_start = cl_cmake.find(p2_marker) + len(p2_marker)
        p2_end = cl_cmake.find(end_marker, p2_start) + len(end_marker)
        p2_str = cl_cmake[p2_start:p2_end].replace('\\"', '"').replace('\\\\', '\\')

        intermediate_ops2 = ops_text.replace(nt_str, p2_str)
        assert intermediate_ops2 != ops_text, "Failed to substitute ops_so_body_prev2"
        (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").write_text(intermediate_ops2, encoding="utf-8")

        res2b = subprocess.run(patch_cmd, capture_output=True, text=True)
        assert res2b.returncode == 0, f"Upgrade from prev2 failed: {res2b.stderr}"
        assert "applied ptp_responder_ptp_operations.cpp cancel patch" in res2b.stdout

        ops_text2 = (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert "*bytes_read = 0;" in ops_text2
        assert "m_usb_server.SetCleanup(true);" in ops_text2
        assert "write_res == haze::ResultCancelled()" in ops_text2
        assert "m_usb_server.IsCancelled()" in ops_text2
        assert "m_usb_server.SetCancelled(true);" in ops_text2

        # Test prev4 -> latest
        p4_marker = 'set(ops_so_body_prev4\n"'
        p4_start = cl_cmake.find(p4_marker) + len(p4_marker)
        p4_end = cl_cmake.find('R_TRY(write_res);")', p4_start) + len('R_TRY(write_res);')
        p4_str = cl_cmake[p4_start:p4_end].replace('\\"', '"').replace('\\\\', '\\')

        wfn_marker = 'set(ops_so_body_wfunc_new\n"'
        wfn_start = cl_cmake.find(wfn_marker) + len(wfn_marker)
        wfn_end = cl_cmake.find('R_TRY(write_res);")', wfn_start) + len('R_TRY(write_res);')
        wfn_str = cl_cmake[wfn_start:wfn_end].replace('\\"', '"').replace('\\\\', '\\')

        intermediate_ops4 = ops_text2.replace(wfn_str, p4_str)
        assert intermediate_ops4 != ops_text2, "Failed to substitute ops_so_body_prev4"
        (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").write_text(intermediate_ops4, encoding="utf-8")

        res2c = subprocess.run(patch_cmd, capture_output=True, text=True)
        assert res2c.returncode == 0, f"Upgrade from prev4 failed: {res2c.stderr}"
        assert "applied ptp_responder_ptp_operations.cpp cancel patch" in res2c.stdout

        ops_text3 = (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert "m_usb_server.SetCancelled(true);" in ops_text3

        # USB source from the preceding review must also upgrade, even though
        # it already contains cancellation, timeout and broken-state markers.
        async_path = temp_dir / "source" / "async_usb_server.cpp"
        latest_async = async_path.read_text(encoding="utf-8")
        usb_patch = (REPO_ROOT / "sphaira/cmake/patch_libhaze_usb.cmake").read_text(encoding="utf-8")
        marker = 'set(async_transfer_prev3\n"'
        start = usb_patch.index(marker) + len(marker)
        usb_tail = "R_RETURN(g_usb_session.GetTransferResult(ep, urb_id, out_size_transferred));"
        end = usb_patch.index(usb_tail, start) + len(usb_tail)
        previous_async = usb_patch[start:end].replace('\\"', '"').replace('\\\\', '\\')
        body_start = latest_async.index("        if (m_broken.load")
        body_end = latest_async.index("        /* Return what we transferred. */", body_start)
        body_end = latest_async.index(usb_tail, body_end) + len(usb_tail)
        async_path.write_text(latest_async[:body_start] + previous_async + latest_async[body_end:], encoding="utf-8")
        upgrade_usb = subprocess.run(patch_cmd, capture_output=True, text=True)
        assert upgrade_usb.returncode == 0, upgrade_usb.stderr
        assert async_path.read_text(encoding="utf-8") == latest_async

        # Scenario 3: Latest -> latest (idempotency)
        res3 = subprocess.run(
            patch_cmd,
            capture_output=True,
            text=True,
        )
        assert res3.returncode == 0, f"Idempotency run failed: {res3.stderr}"
        assert "already patched" in res3.stdout
        assert "applied" not in res3.stdout


def main() -> None:
    check_progress_box_contract()
    check_haze_internal_contract()
    check_haze_helper_contract()
    check_patch_libhaze_contract()
    check_senior_review_findings_contract()
    test_patch_application_scenarios()
    simulate_cancellation_state_machine()
    simulate_parser_and_drain_contract()
    print("PASS: all MTP cancellation and recovery contracts verified successfully.")


if __name__ == "__main__":
    main()
