#!/usr/bin/env python3
"""Applies the real sphaira/cmake/patch_libhaze*.cmake chain to upstream libhaze:
clean, intermediate (prev2, prev3, prev4, 09c04c20) and already patched sources (idempotency).
The MTP box state machine is tested in tests/test_mtp_transfer_state.cpp. Limits <= 600 lines.
"""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPO_ROOT = Path(__file__).resolve().parent.parent


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
        assert "#include <atomic>" in (temp_dir / "include" / "haze" / "console_main_loop.hpp").read_text(encoding="utf-8")

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

        # Test 09c04c20 drain shape upgrade -> latest
        log_cancel_new = 'log_write("[LIBHAZE] local cancel detected for transfer: %s\\n", obj->GetName());'
        log_drain_old = 'log_write("[LIBHAZE] starting transport cleanup for cancelled transfer: %s\\n", obj->GetName());'
        log_end_new = 'log_write("[LIBHAZE] cancelled transfer completed: %s\\n", obj->GetName());'
        log_end_old = 'log_write("[LIBHAZE] transport cleanup finished for cancelled transfer: %s\\n", obj->GetName());'
        r_pre_new = (
            "check_cancellation();\n"
            "                if (is_cancelled.load(std::memory_order_acquire)) {\n"
            "                    R_THROW(haze::ResultTransferFailed());\n"
            "                }\n\n"
            "                /* Read as many bytes as we can. */\n"
            "                u32 bytes_received = 0;\n"
            "                Result read_res = dp.ReadBuffer((u8*)data, size, std::addressof(bytes_received));\n\n"
            "                check_cancellation();"
        )
        r_pre_old = (
            "check_cancellation();\n\n"
            "                /* Read as many bytes as we can. */\n"
            "                u32 bytes_received = 0;\n"
            "                Result read_res = dp.ReadBuffer((u8*)data, size, std::addressof(bytes_received));\n\n"
            "                check_cancellation();"
        )
        w_throw_new = "if (is_cancelled.load(std::memory_order_acquire)) {\n                    R_THROW(haze::ResultTransferFailed());\n                }"
        w_throw_old = "if (is_cancelled.load(std::memory_order_acquire)) {\n                    offset += size;\n                    R_SUCCEED();\n                }"

        inter_09c = ops_text3.replace(log_cancel_new, log_drain_old).replace(log_end_new, log_end_old).replace(r_pre_new, r_pre_old).replace(w_throw_new, w_throw_old)
        assert inter_09c != ops_text3, "Failed to craft 09c04c20 drain shape"
        (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").write_text(inter_09c, encoding="utf-8")

        res2d = subprocess.run(patch_cmd, capture_output=True, text=True)
        assert res2d.returncode == 0, f"Upgrade from 09c04c20 drain shape failed: {res2d.stderr}"
        assert "applied ptp_responder_ptp_operations.cpp cancel patch" in res2d.stdout

        ops_text_09c = (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert log_drain_old not in ops_text_09c and log_end_old not in ops_text_09c
        assert "local cancel detected for transfer" in ops_text_09c and "cancelled transfer completed" in ops_text_09c
        assert "check_cancellation();\n                if (is_cancelled.load(std::memory_order_acquire)) {\n                    R_THROW(haze::ResultTransferFailed());\n                }\n\n                /* Read as many bytes as we can. */" in ops_text_09c

        # Also test upgrading from 09c mid-shape (missing pre-read check)
        inter_09c_mid = ops_text_09c.replace(r_pre_new, r_pre_old)
        assert inter_09c_mid != ops_text_09c
        (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").write_text(inter_09c_mid, encoding="utf-8")
        res2e = subprocess.run(patch_cmd, capture_output=True, text=True)
        assert res2e.returncode == 0, f"Upgrade from 09c mid-shape failed: {res2e.stderr}"
        ops_text_09c_mid = (temp_dir / "source" / "ptp_responder_ptp_operations.cpp").read_text(encoding="utf-8")
        assert "check_cancellation();\n                if (is_cancelled.load(std::memory_order_acquire)) {\n                    R_THROW(haze::ResultTransferFailed());\n                }\n\n                /* Read as many bytes as we can. */" in ops_text_09c_mid

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
    test_patch_application_scenarios()
    print("PASS: libhaze patch chain applies to every upgrade scenario.")


if __name__ == "__main__":
    main()
