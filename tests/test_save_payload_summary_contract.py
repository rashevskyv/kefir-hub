#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for
P2-B checked ZIP payload accounting and summary preflight.

NOTE ON SCOPE AND COVERAGE:
This test executes static source contracts and a Python behavioral model of the
ZIP preflight accounting, classification, integer overflow prevention, error
propagation, and summary publication semantics.

It validates:
- C++ source contract patterns restricted to the open-handle TransferUnzipPreflight
  body in threaded_file_transfer.cpp, header declarations in threaded_file_transfer.hpp,
  metadata constants in save_paths.hpp, filter/diagnostic wiring in save_menu_ops.cpp, and
  shared restore ownership across save_menu_ops.cpp and filebrowser_ops.cpp.
- Synthetic behavioral fixtures covering reachable preflight scenarios:
  regular/zero-byte files, explicit directory vs implicit parent distinction,
  actual NX/DBI metadata filtering and leading-slash normalization, corrupt excluded
  metadata rejection, character sanitization (SanitizeZipEntryName), empty destination
  rejection, exact INT64_MAX accounting, 1-byte aggregate overflow, aggregate overflow
  with null caller output, declared-size excess, drained-byte mismatch, read errors,
  CRC errors, non-CRC close errors, cancellation after prior local accumulation, and
  final rewind failure.
- Defensive arithmetic guards for counter overflow and u64 wrapping before addition,
  distinguished from reachable archive fixtures.

PATH VALIDATION SIMPLIFICATIONS:
The Python behavioral model simulates save-entry single-leading-slash normalization
and character sanitization (SanitizeZipEntryName). Full C++ path containment and HOS
filesystem validation (via IsSafeExtractionDestination, IsSafeDestinationPath,
NormalizeAbsoluteSdPath, mount prefix resolution, and libnx filesystem rules) are
tested by host-native tests (e.g. test_path_util.cpp). The Python model checks relative
safety and base-path prefix containment.

NON-EQUIVALENCE OF RESULT CODES:
Numeric result codes in this file are distinct synthetic diagnostic tokens used purely
for model verification. They do not claim numeric equivalence with libnx or C++ Result types.

It explicitly does NOT execute C++ binaries, libnx IPC calls, target Switch hardware,
or measure save data sizing/growth heuristics.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from contract_fixtures.save_payload_summary_models import (
    INT64_MAX, UINT64_MAX, RES_OK, ERR_INVALID_SIZE, ERR_INVALID_CHARACTER,
    ERR_UNZ_OPEN2_64, ERR_UNZ_GET_GLOBAL_INFO64, ERR_UNZ_GO_TO_FIRST_FILE,
    ERR_UNZ_GO_TO_NEXT_FILE, ERR_UNZ_GET_CURRENT_FILE_INFO64,
    ERR_UNZ_OPEN_CURRENT_FILE, ERR_UNZ_READ_CURRENT_FILE, ERR_CRC_MISMATCH,
    ERR_CANCELLED, check_counter_overflow, check_aggregate_overflow,
    check_drained_read_overflow, BehavioralPreflightModel
)

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ---------------------------------------------------------------------------
# Source contract checks
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# Behavioral test suite
# ---------------------------------------------------------------------------

def test_behavioral_fixtures():
    model = BehavioralPreflightModel()

    def assert_sentinel_untouched(out: dict, label: str):
        check(out["file_bytes"] == -1 and out["file_count"] == -1 and out["directory_count"] == -1,
              f"{label}: all three sentinel fields must remain untouched")

    # Fixture 1: Regular files and zero-byte files
    entries_1 = [
        {"name": "slot.dat", "uncompressed_size": 2048, "chunks": [1024, 1024]},
        {"name": "marker.empty", "uncompressed_size": 0, "chunks": []},
        {"name": "save.bin", "uncompressed_size": 512, "chunks": [512]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_1, output_summary=out)
    check(rc == RES_OK, "Regular files and zero-byte files must succeed")
    check(res["file_bytes"] == 2560, "file_bytes must sum only regular files")
    check(res["file_count"] == 3, "file_count must include zero-byte files")
    check(res["directory_count"] == 0, "directory_count must be 0")

    # Fixture 2: Explicit directory vs implicit parent distinction
    entries_2 = [
        {"name": "dir1/", "uncompressed_size": 0, "chunks": []},
        {"name": "dir1/subdir/", "uncompressed_size": 0, "chunks": []},
        {"name": "dir1/subdir/file.txt", "uncompressed_size": 100, "chunks": [100]},
        {"name": "unseen_parent/leaf.bin", "uncompressed_size": 50, "chunks": [50]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_2, output_summary=out)
    check(rc == RES_OK, "Explicit directories must succeed")
    check(res["file_bytes"] == 150, "Explicit directories must not contribute to file_bytes")
    check(res["file_count"] == 2, "Only actual file entries counted in file_count")
    check(res["directory_count"] == 2, "Only explicit directory entries counted in directory_count")

    # Fixture 3: Character sanitization (SanitizeZipEntryName)
    entries_3 = [
        {"name": "data*01?.sav", "uncompressed_size": 256, "chunks": [256]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_3, output_summary=out)
    check(rc == RES_OK, "Sanitized filename must succeed preflight")
    check(res["file_bytes"] == 256 and res["file_count"] == 1, "Sanitized entry counted in summary")

    # Fixture 4: Actual NX/DBI metadata filtering and leading-slash normalization
    # - Case-insensitive match for reserved root metadata:
    #   .nx_save_meta.bin, .NX_SAVE_META.BIN, .dbi_save_info.ini, .dbi_save_extra
    #   all excluded from restore payload!
    # - Leading slash normalized before filter evaluation
    # - Nested files with matching basename (e.g. nested/.nx_save_meta.bin) remain payload!
    entries_4 = [
        {"name": ".nx_save_meta.bin", "uncompressed_size": 512, "chunks": [512]},
        {"name": "/.nx_save_meta.bin", "uncompressed_size": 512, "chunks": [512]},
        {"name": "/.dbi_save_info.ini", "uncompressed_size": 256, "chunks": [256]},
        {"name": "/.DBI_SAVE_INFO.INI", "uncompressed_size": 256, "chunks": [256]},
        {"name": "/.dbi_save_extra", "uncompressed_size": 128, "chunks": [128]},
        {"name": "/.DBI_save_EXTRA", "uncompressed_size": 128, "chunks": [128]},
        {"name": ".NX_SAVE_META.BIN", "uncompressed_size": 77, "chunks": [77]},  # Case-insensitive root -> excluded!
        {"name": "nested/.nx_save_meta.bin", "uncompressed_size": 50, "chunks": [50]},  # Nested basename -> kept as payload!
        {"name": "/user_data.sav", "uncompressed_size": 1000, "chunks": [1000]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_4, filter_fn=model.save_filter, save_dbi_compat=True, output_summary=out)
    check(rc == RES_OK, "Actual metadata names with leading-slash normalization must pass preflight")
    check(res["file_bytes"] == 1050, "Skipped metadata excluded; nested metadata and user payload kept (1000 + 50)")
    check(res["file_count"] == 2, "Only user_data.sav and nested/.nx_save_meta.bin counted in file_count")
    check(res["directory_count"] == 0, "No directories in this set")

    # Fixture 5: Corrupt skipped metadata rejects preflight and retains entire sentinel
    entries_5_crc = [
        {"name": "/.nx_save_meta.bin", "uncompressed_size": 512, "chunks": [512], "crc_mismatch": True},
        {"name": "valid.sav", "uncompressed_size": 100, "chunks": [100]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_5_crc, filter_fn=model.save_filter, save_dbi_compat=True, output_summary=out)
    check(rc == ERR_CRC_MISMATCH, "Corrupt skipped metadata CRC mismatch must fail preflight")
    assert_sentinel_untouched(out, "Corrupt skipped metadata CRC mismatch")

    entries_5_read = [
        {"name": "/.dbi_save_info.ini", "uncompressed_size": 256, "chunks": [100], "read_error": True},
        {"name": "valid.sav", "uncompressed_size": 100, "chunks": [100]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_5_read, filter_fn=model.save_filter, save_dbi_compat=True, output_summary=out)
    check(rc == ERR_UNZ_READ_CURRENT_FILE, "Corrupt skipped metadata read error must fail preflight")
    assert_sentinel_untouched(out, "Corrupt skipped metadata read error")

    # Fixture 6: Filter remapping and destination trailing slash classification
    def remapping_filter(name: str, path: str) -> tuple[bool, str]:
        if name == "excluded.bin":
            return False, path
        if name == "dir_target":
            return True, path + "/"
        return True, path

    entries_6 = [
        {"name": "excluded.bin", "uncompressed_size": 9999, "chunks": [9999]},
        {"name": "dir_target", "uncompressed_size": 0, "chunks": []},
        {"name": "normal.dat", "uncompressed_size": 300, "chunks": [300]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_6, filter_fn=remapping_filter, output_summary=out)
    check(rc == RES_OK, "Remapping filter must succeed")
    check(res["file_bytes"] == 300, "Excluded bytes ignored, remapped dir contributes 0 file_bytes")
    check(res["file_count"] == 1, "Only normal.dat is a regular file")
    check(res["directory_count"] == 1, "dir_target classified as directory by trailing slash")

    # Fixture 7: Empty mapped destination rejection
    def empty_dest_filter(name: str, path: str) -> tuple[bool, str]:
        return True, ""

    entries_7 = [{"name": "file.dat", "uncompressed_size": 10, "chunks": [10]}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_7, filter_fn=empty_dest_filter, output_summary=out)
    check(rc == ERR_INVALID_CHARACTER, "Empty mapped destination must fail with ERR_INVALID_CHARACTER")
    assert_sentinel_untouched(out, "Empty mapped destination")

    # Fixture 8: Exact INT64_MAX aggregate size vs 1-byte overflow
    entries_exact = [
        {"name": "big1.dat", "uncompressed_size": INT64_MAX - 100, "chunks": [INT64_MAX - 100]},
        {"name": "big2.dat", "uncompressed_size": 100, "chunks": [100]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_exact, output_summary=out)
    check(rc == RES_OK, "Exact INT64_MAX aggregate size must succeed")
    check(res["file_bytes"] == INT64_MAX, "file_bytes must reach exactly INT64_MAX")

    entries_overflow_1b = [
        {"name": "big1.dat", "uncompressed_size": INT64_MAX - 100, "chunks": [INT64_MAX - 100]},
        {"name": "big2.dat", "uncompressed_size": 101, "chunks": [101]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_overflow_1b, output_summary=out)
    check(rc == ERR_INVALID_SIZE, "Aggregate size exceeding INT64_MAX by 1 byte must fail with ERR_INVALID_SIZE")
    assert_sentinel_untouched(out, "1-byte aggregate overflow")

    # Fixture 9: Aggregate overflow enforced with output_summary=None
    rc, res = model.execute_preflight(entries_overflow_1b, output_summary=None)
    check(rc == ERR_INVALID_SIZE and res is None,
          "Aggregate overflow must be enforced even when output_summary is None")

    # Fixture 10: Declared size excess during read
    entries_drain_excess = [
        {"name": "bad.dat", "uncompressed_size": 10, "chunks": [6, 6]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_drain_excess, output_summary=out)
    check(rc == ERR_INVALID_SIZE, "Drained bytes exceeding declared size during read must fail with ERR_INVALID_SIZE")
    assert_sentinel_untouched(out, "Drained size excess during read")

    # Fixture 11: Drained size mismatch at close
    entries_drain_under = [
        {"name": "bad.dat", "uncompressed_size": 10, "chunks": [9]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_drain_under, output_summary=out)
    check(rc == ERR_INVALID_SIZE, "Drained size fewer than declared size must fail with ERR_INVALID_SIZE")
    assert_sentinel_untouched(out, "Drained size underflow at close")

    # Fixture 12: Read, CRC, non-CRC close, and close CRC errors
    entries_read_err = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [5], "read_error": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_read_err, output_summary=out)
    check(rc == ERR_UNZ_READ_CURRENT_FILE, "Read error must fail with ERR_UNZ_READ_CURRENT_FILE")
    assert_sentinel_untouched(out, "Read error")

    entries_crc_err = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [10], "crc_mismatch": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_crc_err, output_summary=out)
    check(rc == ERR_CRC_MISMATCH, "CRC mismatch must fail with ERR_CRC_MISMATCH")
    assert_sentinel_untouched(out, "CRC mismatch")

    entries_close_err = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [10], "close_error": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_close_err, output_summary=out)
    check(rc == ERR_UNZ_READ_CURRENT_FILE, "Non-CRC close error must fail with ERR_UNZ_READ_CURRENT_FILE")
    assert_sentinel_untouched(out, "Non-CRC close error")

    entries_close_crc = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [10], "close_crc_error": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_close_crc, output_summary=out)
    check(rc == ERR_CRC_MISMATCH, "Close CRC error must fail with ERR_CRC_MISMATCH")
    assert_sentinel_untouched(out, "Close CRC error")

    # Fixture 13: Cancellation during drain after prior entry accumulated locally
    entries_cancel = [
        {"name": "first_ok.dat", "uncompressed_size": 500, "chunks": [500]},
        {"name": "second_cancel.dat", "uncompressed_size": 200, "chunks": [100], "cancel_during_read": True},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_cancel, output_summary=out)
    check(rc == ERR_CANCELLED, "Cancellation during drain must fail with ERR_CANCELLED")
    assert_sentinel_untouched(out, "Cancellation after prior local accumulation")

    # Fixture 14: Final rewind failure
    entries_rewind = [{"name": "ok.dat", "uncompressed_size": 10, "chunks": [10]}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_rewind, rewind_fails=True, output_summary=out)
    check(rc == ERR_UNZ_GO_TO_FIRST_FILE, "Rewind failure must fail with ERR_UNZ_GO_TO_FIRST_FILE")
    assert_sentinel_untouched(out, "Final rewind failure")

    # Fixture 15: Successful run without output summary requested
    rc, res = model.execute_preflight(entries_1, output_summary=None)
    check(rc == RES_OK and res is None, "Preflight with output_summary=None must succeed and return None")

    print("Reachable archive behavioral fixtures: ALL PASS (15 fixture groups)")


def test_defensive_arithmetic():
    # 1. Counter overflow
    check(not check_counter_overflow(0), "Counter 0 must not overflow")
    check(not check_counter_overflow(INT64_MAX - 1), "Counter INT64_MAX - 1 must not overflow")
    check(check_counter_overflow(INT64_MAX), "Counter INT64_MAX must report overflow")

    # 2. Aggregate overflow
    check(not check_aggregate_overflow(INT64_MAX - 100, 100), "Exact INT64_MAX addition must not overflow")
    check(check_aggregate_overflow(INT64_MAX - 100, 101), "INT64_MAX + 1 addition must report overflow")
    check(check_aggregate_overflow(10, -1), "Negative addition must report invalid")

    # 3. Drained read overflow and u64 wrap
    ov, reason = check_drained_read_overflow(UINT64_MAX - 10, 11, UINT64_MAX)
    check(ov and reason == "u64_wrap", "Drained read wrapping u64 must trigger defensive u64_wrap guard")

    ov, reason = check_drained_read_overflow(50, 10, 55)
    check(ov and reason == "declared_excess", "Drained read exceeding declared size must trigger declared_excess")

    ov, reason = check_drained_read_overflow(50, 5, 55)
    check(not ov and reason == "ok", "Valid drained chunk within bounds must be ok")

    print("Defensive arithmetic guards: ALL PASS (3 boundary checks)")


if __name__ == "__main__":
    test_behavioral_fixtures()
    test_defensive_arithmetic()
    print("ALL SAVE PAYLOAD SUMMARY CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")
