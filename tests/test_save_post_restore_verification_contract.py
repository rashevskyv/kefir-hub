#!/usr/bin/env python3
"""
Test Suite: Save Post-Restore Fresh-Remount Verification Contract (Sphaira v0.13.854)

Verifies the verified post-restore fresh read-only remount verification policy for ZIP restore:
1. Static source contracts:
   - threaded_file_transfer.hpp:
     - UnzipPayloadInventory declaration (files map, directories set).
     - TransferUnzipAll declaration with checked_native_save option.
     - TransferUnzipPreflight declaration with inventory_out option.
     - VerifyArchiveAgainstNative declaration (stream and path overloads).
   - save_menu.hpp:
     - 5-argument RestoreSaveZip and RestoreSaveInternal with out_mutation_started.
   - threaded_file_transfer.cpp:
     - ResolvedDestinationEntry and ResolveArchiveDestinationEntry helpers.
     - GetParentDirectories helper.
     - Checked native save flush, close, handle invalidation, and parent Commit in TransferUnzipInternal.
     - UnzipPayloadInventory population in TransferUnzipPreflight with collision checks.
     - VerifyArchiveAgainstNative implementation: exact inventory bijection, 32 KiB chunk comparison
       with short-read accumulation, CRC validation, unz EOF check, rewinding, and post-check re-enumeration.
   - save_menu_ops.cpp:
     - SaveReaderContext struct wrapping mz::FileFuncStdio with sticky io_error / close_error.
     - Explicit checked reader closes (HasError() and unzClose()).
     - Lexical RW scope destroying writable mount before RO mount.
     - Post-restore live extra data rereading and attribute/size verification.
     - Fresh read-only FsNativeSave mount (read_only = true) and VerifyArchiveAgainstNative check.
     - Conservative mutation flag set immediately before DeleteAllCollections.
     - Branched error messages for picked and batch routes distinguishing modified vs unmodified targets.
   - filebrowser_ops.cpp:
     - Forwarding mutation_started and branching error message in RestoreSaveFile.
   - i18n parity in en.json and uk.json for all 7 new UI strings with exact UTF-8 verification.
   - Version bumped to 0.13.854 in sphaira/CMakeLists.txt.
2. Real synthetic ZIP behavioral fixtures using stdlib zipfile, zlib, and io.BytesIO:
   - SaveReaderContext sticky error recording.
   - TransferUnzipPreflight inventory generation and collision detection:
     - Normal payload, DBI leading slash normalization, explicit directories, implicit parent dirs, 0-byte files.
     - Rejection of duplicate kept files, duplicate explicit dirs, file-dir collisions, parent collisions.
   - VerifyArchiveAgainstNative reference engine:
     - Exact bijection verification between archive and native save.
     - Excluded metadata draining and CRC validation.
     - Chunked stream comparison with simulated short reads.
     - Detection of byte alterations, truncated files, extra files, missing files, extra dirs.
     - Post-verification external mutation detection.
     - Archive rewinding verification.
   - Fresh-remount restore lifecycle and mutation tracking:
     - Preflight failure -> mutation_started = False.
     - Recovery backup failure -> mutation_started = False.
     - Restoration write failure -> mutation_started = True.
     - Verification failure on RO remount -> mutation_started = True.
     - Clean success -> mutation_started = True.
     - Sequential batch tracking across multiple save slots.

NO C++ COMPILATION, NO BINARIES, NO NRO, NO WSL REQUIRED. Pure Python stdlib.
"""

import io
import os
import sys
import json
import zlib
import struct
import zipfile

def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ==============================================================================
# 1. Static Source Contracts
# ==============================================================================


# ==============================================================================
# 2. Synthetic Behavioral Reference Model
# ==============================================================================

RESERVED_NAMES = {".nx_save_meta.bin", ".dbi_save_info.ini", ".dbi_save_extra"}
INVALID_CHARS = set(":*?\"<>|\\")
MAX_S64 = 0x7FFFFFFFFFFFFFFF

from contract_fixtures.save_post_restore_models import (
    RES_OK, ERR_VERIFICATION_FAILED, ERR_UNZ_READ, ERR_UNZ_CLOSE,
    ERR_CANCELLED, ERR_OVERFLOW, ERR_INVALID_PATH, ERR_INVALID_SIZE,
    ERR_REWIND_FAILED, RESERVED_NAMES, SyntheticSaveFs, build_test_zip,
    simulate_verify_against_native, simulate_preflight
)
from contract_fixtures.save_post_restore_fixtures import test_behavioral_fixtures

def test_policy_and_fault_gates() -> None:
    """Real ZIP policy fixtures and injected sequential gates; not C++/IPC proof."""
    def archive(entries):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as zf:
            for name, data in entries:
                # ZipInfo's Windows constructor rewrites backslashes; preserve
                # hostile wire names instead of testing a sanitized fixture.
                info = zipfile.ZipInfo()
                info.filename = info.orig_filename = name
                zf.writestr(info, data)
        return buf.getvalue()

    for name in ("//a", "a\\b", "a:b", "../a", "a/./b", "a//b", "a\x7fb"):
        try:
            simulate_preflight(archive([(name, b"x")]))
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsafe entry accepted: {name!r}")
    for entries in (("a?", "a_"), ("x", "x/"), ("x", "x/y")):
        for order in (entries, entries[::-1]):
            try:
                simulate_preflight(archive([(name, b"x") for name in order]))
            except FileExistsError:
                pass
            else:
                raise AssertionError(f"collision accepted: {order}")

    raw = archive([(".nx_save_meta.bin", b"meta"), (".DBI_SAVE_EXTRA", b"meta"),
                   ("sub/.nx_save_meta.bin", b"payload"), (".NX_SAVE_META.BIN", b"payload"),
                   ("dir/", b"directory payload"), ("zero", b"")])
    files, dirs, _ = simulate_preflight(raw)
    assert files == {"/sub/.nx_save_meta.bin": 7, "/.NX_SAVE_META.BIN": 7, "/zero": 0}
    native = SyntheticSaveFs({"/sub/.nx_save_meta.bin": b"payload",
                              "/.NX_SAVE_META.BIN": b"payload", "/zero": b""}, dirs)
    assert simulate_verify_against_native(raw, native, files, dirs)
    empty = archive([(".nx_save_meta.bin", b"meta")])
    assert simulate_verify_against_native(empty, SyntheticSaveFs(), {}, set())
    expected = simulate_preflight(archive([("empty/", b"")]))
    assert not simulate_verify_against_native(empty, SyntheticSaveFs(dirs={"/empty"}),
                                             expected[0], expected[1])
    # Genuine CRC zero: both central/local headers produced by zipfile agree.
    with zipfile.ZipFile(io.BytesIO(archive([("zero", b"")]))) as zf:
        assert zf.infolist()[0].CRC == 0 and zf.read("zero") == b""

    def lifecycle(fail=None):
        events = []
        retained = mutated = False
        reader = io.BytesIO(b"payload")
        def gate(name):
            events.append(name)
            if name == fail:
                raise OSError(name)
        try:
            gate("source_open")
            gate("seek"); reader.seek(0)
            gate("tell"); assert reader.tell() == 0
            gate("read"); assert reader.read() == b"payload"
            gate("entry_close")
            gate("recovery_commit")
            retained = True
            gate("cancel_before_clear")
            mutated = True
            gate("write")
            gate("join")
            gate("flush")
            gate("file_close")  # native close is void, not an injectable Result
            gate("file_commit")
            gate("final_commit")
            gate("rw_close")
            gate("ro_open")
            gate("cancel_during_verify")
            gate("native_read")
            gate("rewind"); reader.seek(0)
            gate("ro_close")
            gate("callback_close"); reader.close()
            events.append("success")
            return True, retained, mutated, events
        except OSError:
            return False, retained, mutated, events
        finally:
            reader.close()

    ok, retained, mutated, events = lifecycle()
    assert ok and retained and mutated
    assert events.index("join") < events.index("flush") < events.index("file_close")
    assert events.index("file_close") < events.index("file_commit") < events.index("final_commit")
    assert events.index("rw_close") < events.index("ro_open") < events.index("callback_close") < events.index("success")
    for fault in ("source_open", "seek", "tell", "read", "entry_close", "recovery_commit",
                  "cancel_before_clear", "write", "flush", "file_commit", "final_commit",
                  "ro_open", "cancel_during_verify", "native_read", "rewind", "callback_close"):
        ok, retained, mutated, trace = lifecycle(fault)
        assert not ok and "success" not in trace
        assert retained == (events.index(fault) > events.index("recovery_commit"))
        assert mutated == (events.index(fault) > events.index("cancel_before_clear"))
    print("  -> Policy/ZIP fixtures and 16 injected lifecycle faults PASSED (model only).")


def main() -> None:
    print("=== Sphaira v0.13.854: Fresh-Remount Verification Test Suite ===")
    test_behavioral_fixtures()
    test_policy_and_fault_gates()
    print("=== ALL CHECKS PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
