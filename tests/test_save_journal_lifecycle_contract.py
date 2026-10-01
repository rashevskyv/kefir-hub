#!/usr/bin/env python3
"""
Test Suite: Save Journal Serial Commit Lifecycle Contract (Sphaira v0.13.855)

Verifies the serial per-chunk checked close/commit/reopen lifecycle, metadata cadence,
and declared-journal request cap for checked native save ZIP extraction:

1. Static Source Contracts:
   - sphaira/include/threaded_file_transfer.hpp:
     - TransferUnzipAll declarations append trailing s64 checked_save_journal_size = 0.
   - sphaira/source/threaded_file_transfer.cpp:
     - CreateDirectoryChecked helper:
       - component-by-component creation
       - skip root create
       - native fsFsCreateDirectory primitive
       - checked Commit on primitive success
       - FsError_PathAlreadyExists verified via GetEntryType == FsDirEntryType_Dir
       - Commit failures never classified as already exists or retried
       - cancellation checks between component lifecycles
       - no write file handle open during directory lifecycle
     - TransferUnzipInternal checked lifecycle:
       - checked_native_save opt-in isolated from generic lifecycle work
       - serial only (no TransferInternal worker threads, SetSize, or Append on checked path)
       - negative checked_save_journal_size rejected before extraction mutation
       - required ponytail ceiling comment
       - per-read payload request cap:
         positive declared journal: min(SMALL_BUFFER_SIZE, checked_save_journal_size, remaining)
         zero: min(SMALL_BUFFER_SIZE, remaining)
       - cancellation check immediately before fsFsCreateFile
       - native fsFsCreateFile at validated full s64 size, option 0
       - separate checked Commit before opening payload write handle
       - cancellation check immediately after commit_create_rc
       - unexpected existing destination file rejected (no PathAlreadyExists acceptance/SetSize)
       - explicit close_and_invalidate helper (fsFileClose + zero native and fs pointer)
       - empty file: open, cancel check, checked flush, close/invalidate, checked commit, cancellation checks
       - per-chunk lifecycle:
         - cancellation check at start of loop (before unzReadCurrentFile)
         - bounded ZIP read, validate read_res > 0 and <= to_read and <= remaining
         - cancellation check before write
         - explicit-offset native write (fsFileWrite) with FsWriteOption_None
         - CHECK fsFileFlush
         - fsFileClose (void), invalidate m_native/m_fs ownership
         - CHECK filesystem Commit
         - advance checked offset and byte progress only after successful commit
         - check cancellation after commit
         - reopen FsOpenMode_Write only if more payload remains
       - post-loop size validation and CRC verification
       - cancellation check after CRC verification before return 0
       - no live write handle across any checked metadata/payload/final Commit
       - cleanup on error/cancel closes/invalidates only; never calls File::Close auto-commit
       - first actual error propagates
     - TransferUnzipAll:
       - rejects negative checked_save_journal_size
       - directory branch uses CreateDirectoryChecked
       - file branch forwards checked_save_journal_size
       - path overload forwards checked_save_journal_size
   - sphaira/source/ui/menus/save/save_menu_ops.cpp:
     - DeleteAllCollections followed by explicit checked save_fs.Commit()
     - target_journal_size passing (live.journal_size for existing save, 0 for legacy)
     - final checked save_fs.Commit()
   - sphaira/CMakeLists.txt:
     - version is 0.13.855.

2. Synthetic Behavioral Reference Model:
   - Real synthetic ZIP data (empty file, multi-chunk, short reads, explicit/implicit dirs).
   - Serial event/fault model recording all primitive calls, flushes, closes, commits, reopens.
   - Comprehensive boundary and fault injections:
     - negative journal rejected
     - zero journal keeps per-read commits; no divide by zero
     - positive journal 1, below/equal/above SMALL_BUFFER_SIZE, s64 max
     - file greater than declared journal with exact offset bytes after multiple reopens
     - short reads accumulation and error bounds
     - remaining/offset s64 boundary arithmetic
     - empty file lifecycle
     - metadata create failure, unexpected existing file, existing non-dir parent
     - directory commit failure not retried or swallowed
     - file create commit failure before payload open
     - write, flush, chunk commit, reopen, final commit failures
     - sticky cancellation across all lifecycle points
     - progress invariants: first chunk fail = 0; second chunk fail = previous chunk size
     - zero read, negative read, over-request read independently verified
     - deterministic CRC corruption detection
     - published recovery retention connected to actual engine states
     - simulated single-operation journal exhaustion ceiling illustration.

NOTE: Python synthetic reference models and source checks do not execute C++,
libnx IPC, or Nintendo Switch hardware. Pure Python stdlib; no compiler/build required.
"""

import io
import os
import sys
import zlib
import struct
import zipfile

SMALL_BUFFER_SIZE = 512 * 1024  # 524288 bytes
MAX_S64 = 0x7FFFFFFFFFFFFFFF

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

from contract_fixtures.save_journal_lifecycle_models import (
    RES_OK, ERR_TARGET_LOCKED, ERR_VERIFICATION_FAILED, ERR_CANCELLED,
    ERR_INVALID_SIZE, ERR_INVALID_CHAR, ERR_UNZ_READ, ERR_CRC_MISMATCH,
    ERR_PREFLIGHT_FAIL, ERR_JOURNAL_SIZE_MISMATCH, ERR_PREV_RESTORE_DIRTY,
    ERR_REWIND_FAILED, RESERVED_NAMES, INVALID_CHARS, MAX_S64,
    ModelNativeFile, ModelNativeFs, MockProgressBox, simulate_checked_restore,
    build_fixture_zip
)
from contract_fixtures.save_journal_lifecycle_fixtures import test_behavioral_fixtures

def main():
    print("=== Sphaira v0.13.855: Serial Save Journal Lifecycle Test Suite ===")
    test_behavioral_fixtures()
    print("=== ALL CHECKS PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
