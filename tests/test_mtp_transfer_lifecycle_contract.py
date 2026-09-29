#!/usr/bin/env python3
"""
Regression contract and static analysis verification for MTP transfer UI lifecycle.

Verifies the fix for:
1. MTP banner lingering after transfers complete.
2. Edge Case 1: Post-transfer idle countdown reset on new transfer sequence so
   rapid/short files that start and finish between worker polls receive a full 1.5s
   display window instead of inheriting stale timestamps.
3. Edge Case 2: Window between worker exit and ProgressBox done callback elimination.
   Ensures Begin in this window does not lose the banner for an active transfer,
   maintains a single ProgressBox, prevents double PushTransfer, and fails closed
   without hanging on PushTransfer refusal.
4. Clean termination on CloseSession and Exit.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INTERNAL_HPP = ROOT / "sphaira/include/haze/haze_internal.hpp"
INTERNAL_CPP = ROOT / "sphaira/source/haze/haze_internal.cpp"
HELPER_CPP = ROOT / "sphaira/source/haze_helper.cpp"


def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


def load_sources() -> tuple[str, str, str]:
    hpp_src = INTERNAL_HPP.read_text(encoding="utf-8")
    cpp_src = INTERNAL_CPP.read_text(encoding="utf-8")
    helper_src = HELPER_CPP.read_text(encoding="utf-8")
    return hpp_src, cpp_src, helper_src


def extract_block(source: str, start_marker: str, end_marker: str) -> str:
    start_idx = source.find(start_marker)
    check(start_idx != -1, f"Start marker not found: {start_marker}")
    end_idx = source.find(end_marker, start_idx + len(start_marker))
    check(end_idx != -1, f"End marker not found: {end_marker}")
    return source[start_idx : end_idx + len(end_marker)]


def test_state_contracts(hpp_src: str, cpp_src: str, helper_src: str) -> None:
    # 1. State declarations in header
    check("extern bool g_mtp_transfer_active;" in hpp_src, "g_mtp_transfer_active missing in haze_internal.hpp")
    check("extern u64 g_mtp_transfer_seq;" in hpp_src, "g_mtp_transfer_seq missing in haze_internal.hpp")
    check("extern u64 g_mtp_handled_seq;" in hpp_src, "g_mtp_handled_seq missing in haze_internal.hpp")
    check("extern bool g_mtp_ui_alive;" in hpp_src, "g_mtp_ui_alive missing in haze_internal.hpp")

    # 2. State definitions in translation unit
    check("bool g_mtp_transfer_active{false};" in cpp_src, "g_mtp_transfer_active definition missing in haze_internal.cpp")
    check("u64 g_mtp_transfer_seq{0};" in cpp_src, "g_mtp_transfer_seq definition missing in haze_internal.cpp")
    check("u64 g_mtp_handled_seq{0};" in cpp_src, "g_mtp_handled_seq definition missing in haze_internal.cpp")
    check("bool g_mtp_ui_alive{false};" in cpp_src, "g_mtp_ui_alive definition missing in haze_internal.cpp")

    # 3. State initialization in Init()
    init_block = extract_block(helper_src, "bool Init()", "return g_is_running = true;")
    check("g_mtp_transfer_active = false;" in init_block, "Init() must reset g_mtp_transfer_active")
    check("g_mtp_transfer_seq = 0;" in init_block, "Init() must reset g_mtp_transfer_seq")
    check("g_mtp_handled_seq = 0;" in init_block, "Init() must reset g_mtp_handled_seq")
    check("g_mtp_ui_alive = false;" in init_block, "Init() must reset g_mtp_ui_alive")


def test_single_file_contract(helper_src: str) -> None:
    # Begin handler
    begin_block = extract_block(helper_src, "case ::haze::CallbackType_ReadBegin:", "break;\n        }")
    check("g_mtp_transfer_active = true;" in begin_block, "Begin must set g_mtp_transfer_active = true")
    check("g_mtp_transfer_seq++;" in begin_block, "Begin must increment g_mtp_transfer_seq++")
    check("!g_mtp_ui_alive" in begin_block, "Begin must check !g_mtp_ui_alive before triggering UI")
    check("ueventSignal(&g_mtp_done_event);" in begin_block, "Begin must signal g_mtp_done_event")
    check("StartMtpProgressBox();" in begin_block, "Begin must call StartMtpProgressBox on trigger_ui")

    # End handler
    end_block = extract_block(helper_src, "case ::haze::CallbackType_ReadEnd:", "break;\n        }")
    check("g_mtp_transfer_active = false;" in end_block, "End must set g_mtp_transfer_active = false")
    check("ueventSignal(&g_mtp_done_event);" in end_block, "End must signal g_mtp_done_event")

    # Worker thread loop
    worker_block = extract_block(helper_src, "auto pbox_ptr = std::make_unique<ui::ProgressBox>", "}, [push_state](Result rc)")
    check("waitSingle(waiterForUEvent(&g_mtp_done_event), 50000000ULL);" in worker_block,
          "Worker must poll done event with 50ms timeout")
    check("!g_mtp_transfer_active && g_mtp_transfer_seq == last_seq" in worker_block,
          "Worker exit check must verify inactive transfer and sequence match")
    check(re.search(r"if\s*\(g_mtp_handled_seq < last_seq\)\s*\{\s*g_mtp_handled_seq = last_seq;", worker_block),
          "Worker must not overwrite a newer CloseSession handled sequence")
    check("g_mtp_pbox = nullptr;" in worker_block,
          "Worker must detach g_mtp_pbox upon loop exit")


def test_rapid_sequence_contract(helper_src: str) -> None:
    # In rapid transfers (like firmware packs with many small NCAs), files can begin and
    # finish between 50ms polls. When g_mtp_transfer_seq != last_seq, idle countdown
    # must reset to now if the file already ended, granting full 1.5s post-completion display.
    worker_block = extract_block(helper_src, "auto pbox_ptr = std::make_unique<ui::ProgressBox>", "}, [push_state](Result rc)")
    check("IDLE_TIMEOUT_NS = 1500000000ULL;" in worker_block, "Worker must use 1.5s idle timeout")

    worker_loop = extract_block(worker_block, "while (!pbox->ShouldExit() && !g_should_exit)", "R_SUCCEED();")

    # No ueventClear inside the while loop (was root cause of dropped WriteEnd signals)
    check("ueventClear" not in worker_loop, "ueventClear must NOT be called inside worker while loop")

    # Sequence change must reset idle_start:
    # idle_start = is_active ? 0 : now;
    expected_idle_reset = re.search(
        r"if\s*\(\s*update_label\s*\)\s*\{\s*idle_start\s*=\s*is_active\s*\?\s*0\s*:\s*now\s*;",
        worker_loop,
    )
    check(expected_idle_reset is not None,
          "Worker loop must reset idle_start = is_active ? 0 : now when update_label is true")

    # When update_label is false, idle_start is zeroed during activity or counts from first idle timestamp
    check("else if (is_active)" in worker_loop, "Worker loop must handle is_active branch when update_label is false")
    check("idle_start = 0;" in worker_loop, "Worker loop must keep idle_start = 0 while active")


def test_window_elimination_and_relaunch_contract(helper_src: str) -> None:
    start_pbox_block = extract_block(helper_src, "void StartMtpProgressBox()", "} // namespace\n\nvoid haze_callback")

    # 1. StartMtpProgressBox queues via evman::push (ensures clean lifecycle on main UI loop)
    check("evman::push(evman::FunctionalEventData" in start_pbox_block,
          "StartMtpProgressBox must use evman::push")

    # 2. StartMtpProgressBox checks exit / terminal state under mutex before creating box
    check("g_should_exit || (!g_mtp_transfer_active && g_mtp_transfer_seq == g_mtp_handled_seq)" in start_pbox_block,
          "StartMtpProgressBox must abort gracefully if session exited/ended while event was queued")

    # 3. Worker waits for ownership; refusal releases it before destruction joins.
    check("push_state = std::make_shared<std::atomic<int>>(0);" in start_pbox_block,
          "StartMtpProgressBox must track PushTransfer acceptance")
    check("while (push_state->load() == 0 && !pbox->ShouldExit())" in start_pbox_block,
          "Worker must wait for PushTransfer decision")

    # 4. Push refusal safety: does NOT hang or loop; fails closed
    push_check = re.search(
        r"if\s*\(!App::PushTransfer\(std::move\(pbox_ptr\)\)\)\s*\{\s*push_state->store\(-1\);\s*SCOPED_MUTEX\(&g_mtp_ui_mutex\);\s*g_mtp_ui_alive\s*=\s*false;\s*\}\s*else\s*\{\s*push_state->store\(1\);\s*\}",
        start_pbox_block,
    )
    check(push_check is not None,
          "StartMtpProgressBox must release worker on refusal and start it only on success")

    # 5. Done callback bridges the window between worker exit and old ProgressBox destruction:
    # If a new transfer started while worker was exiting/destroyed, relaunch a new box
    done_callback = extract_block(start_pbox_block, "[push_state](Result rc)", "if (!App::PushTransfer")
    check("push_state->load() == 1 && !g_should_exit && (g_mtp_transfer_active || g_mtp_transfer_seq != g_mtp_handled_seq)" in done_callback,
          "Done callback must check accepted push, !g_should_exit, and transfer activity or unhandled sequence difference")
    check("g_mtp_ui_alive = true;\n                        relaunch = true;" in done_callback or
          ("relaunch = true;" in done_callback and "g_mtp_ui_alive = true;" in done_callback),
          "Done callback must set relaunch and keep ui_alive")
    check("StartMtpProgressBox();" in done_callback,
          "Done callback must call StartMtpProgressBox() on relaunch")


def test_close_session_contract(helper_src: str) -> None:
    close_block = extract_block(helper_src, "case ::haze::CallbackType_CloseSession:", "break;\n")
    check("g_mtp_transfer_active = false;" in close_block,
          "CloseSession must set g_mtp_transfer_active = false")
    check("g_mtp_handled_seq = g_mtp_transfer_seq;" in close_block,
          "CloseSession must synchronize g_mtp_handled_seq = g_mtp_transfer_seq to prevent stale relaunch")
    check("g_mtp_pbox->RequestExit();" in close_block,
          "CloseSession must request exit on active progress box")
    check("ueventSignal(&g_mtp_done_event);" in close_block,
          "CloseSession must signal g_mtp_done_event")


def test_exit_contract(helper_src: str) -> None:
    exit_block = extract_block(helper_src, "void Exit(bool reinit_usb_host)", "::haze::Exit();")
    check("g_should_exit = true;" in exit_block,
          "Exit must set g_should_exit = true")
    check("g_mtp_transfer_active = false;" in exit_block,
          "Exit must set g_mtp_transfer_active = false")
    check("g_mtp_handled_seq = g_mtp_transfer_seq;" in exit_block,
          "Exit must synchronize g_mtp_handled_seq = g_mtp_transfer_seq")
    check("g_mtp_pbox->RequestExit();" in exit_block,
          "Exit must request exit on active progress box")
    check("ueventSignal(&g_mtp_done_event);" in exit_block,
          "Exit must signal g_mtp_done_event")


def main() -> None:
    hpp_src, cpp_src, helper_src = load_sources()
    test_state_contracts(hpp_src, cpp_src, helper_src)
    test_single_file_contract(helper_src)
    test_rapid_sequence_contract(helper_src)
    test_window_elimination_and_relaunch_contract(helper_src)
    test_close_session_contract(helper_src)
    test_exit_contract(helper_src)
    print("PASS: all MTP transfer lifecycle contracts verified successfully.")


if __name__ == "__main__":
    main()
