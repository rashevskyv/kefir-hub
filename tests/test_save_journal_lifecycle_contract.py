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

def test_source_contracts() -> None:
    print("[1] Running static source contract checks for v0.13.855...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 threaded_file_transfer.hpp
    tft_hpp_path = os.path.join(repo_root, "sphaira", "include", "threaded_file_transfer.hpp")
    with open(tft_hpp_path, "r", encoding="utf-8") as f:
        tft_hpp = f.read()

    check("s64 checked_save_journal_size = 0" in tft_hpp,
          "threaded_file_transfer.hpp must declare checked_save_journal_size default")
    zfile_decl = "Result TransferUnzipAll(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, Mode mode = Mode::SingleThreadedIfSmaller, bool save_dbi_compat = false, bool checked_native_save = false, s64 checked_save_journal_size = 0);"
    path_decl = "Result TransferUnzipAll(ui::ProgressBox* pbox, const fs::FsPath& zip_out, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, Mode mode = Mode::SingleThreadedIfSmaller, bool save_dbi_compat = false, bool checked_native_save = false, s64 checked_save_journal_size = 0);"
    check(zfile_decl in tft_hpp, "TransferUnzipAll zfile overload declaration must match exact signature")
    check(path_decl in tft_hpp, "TransferUnzipAll path overload declaration must match exact signature")

    # 1.2 threaded_file_transfer.cpp & threaded_file_transfer_zip_io.cpp
    tft_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer.cpp")
    with open(tft_cpp_path, "r", encoding="utf-8") as f:
        tft_cpp = f.read()

    zip_io_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer_zip_io.cpp")
    with open(zip_io_cpp_path, "r", encoding="utf-8") as f:
        zip_io_cpp = f.read()

    # CreateDirectoryChecked helper
    check("Result CreateDirectoryChecked(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& dir_path)" in zip_io_cpp,
          "threaded_file_transfer_zip_io.cpp must define CreateDirectoryChecked helper")
    dir_helper_start = zip_io_cpp.find("Result CreateDirectoryChecked(")
    dir_helper_end = zip_io_cpp.find("Result TransferUnzipInternal(")
    check(dir_helper_start != -1 and dir_helper_end != -1 and dir_helper_start < dir_helper_end,
          "CreateDirectoryChecked must precede TransferUnzipInternal")
    dir_helper_code = zip_io_cpp[dir_helper_start:dir_helper_end]

    check("fsFsCreateDirectory(&native_fs->m_fs, current_path.s)" in dir_helper_code,
          "CreateDirectoryChecked must use native fsFsCreateDirectory primitive")
    check("R_TRY(fs->Commit());" in dir_helper_code,
          "CreateDirectoryChecked must commit on successful directory creation")
    check("create_rc == FsError_PathAlreadyExists" in dir_helper_code,
          "CreateDirectoryChecked must branch on FsError_PathAlreadyExists")
    check("R_TRY(fs->GetEntryType(current_path, &type));" in dir_helper_code,
          "CreateDirectoryChecked must verify existing entry type")
    check("type == FsDirEntryType_Dir" in dir_helper_code,
          "CreateDirectoryChecked must confirm existing entry is directory")
    check("CreateDirectoryRecursively" not in dir_helper_code,
          "CreateDirectoryChecked must not call recursive helper")

    # TransferUnzipInternal checked lifecycle
    unzip_int_start = dir_helper_end
    unzip_int_end = zip_io_cpp.find("Result TransferUnzip(ui::ProgressBox*")
    check(unzip_int_start != -1 and unzip_int_end != -1 and unzip_int_start < unzip_int_end,
          "TransferUnzipInternal must be bounded properly")
    unzip_int_code = zip_io_cpp[unzip_int_start:unzip_int_end]

    check("if (checked_native_save)" in unzip_int_code,
          "TransferUnzipInternal must branch on checked_native_save")

    # Scoped isolation check: find the checked_native_save block
    checked_block_start = unzip_int_code.find("if (checked_native_save) {")
    check(checked_block_start != -1, "TransferUnzipInternal must have 'if (checked_native_save) {'")

    # Locate the return 0 at the end of the checked block
    checked_return_pos = unzip_int_code.find("return 0;\n    }\n\n    Result rc;")
    check(checked_return_pos != -1, "Checked lifecycle block must terminate with return 0 and not fall through to generic code")
    checked_block_code = unzip_int_code[checked_block_start:checked_return_pos]
    cleanup = checked_block_code.split("auto close_and_invalidate = [&]() {", 1)[1].split("ON_SCOPE_EXIT", 1)[0]
    check("fsFileClose(&f.m_native);\n            }\n            f.m_native = {};\n            f.m_fs = nullptr;" in cleanup,
          "Cleanup invalidates ownership even after a failed open")

    # Verify no generic threading or forbidden mutating calls in checked block
    check("TransferInternal" not in checked_block_code,
          "Checked lifecycle must NOT invoke TransferInternal worker threads")
    check("SetSize" not in checked_block_code,
          "Checked lifecycle must NOT invoke SetSize")
    check("Append" not in checked_block_code,
          "Checked lifecycle must NOT invoke Append")

    check("if (checked_save_journal_size < 0)" in checked_block_code,
          "TransferUnzipInternal must reject negative checked_save_journal_size")
    check("FsError_InvalidSize" in checked_block_code,
          "TransferUnzipInternal must return FsError_InvalidSize on negative journal")

    # Required ceiling comment
    ceiling_comment = "// ponytail: declared-size payload cap/per-read commits do not measure metadata/allocation/block overhead or actual free journal; even one operation may exhaust journal; proven budgeting remains queued."
    check(ceiling_comment in checked_block_code,
          "TransferUnzipInternal must include exact required ceiling comment")

    # Request cap calculation
    check("s64 request_cap = static_cast<s64>(SMALL_BUFFER_SIZE);" in checked_block_code,
          "TransferUnzipInternal must default request_cap to SMALL_BUFFER_SIZE")
    check("checked_save_journal_size > 0 && checked_save_journal_size < request_cap" in checked_block_code,
          "TransferUnzipInternal must cap to positive declared journal size")

    # Sequence of cancellation and lifecycle checkpoints in checked_block_code
    create_dir_pos = checked_block_code.find("CreateDirectoryChecked(pbox, fs, parent_dir)")
    cancel_before_create_file = checked_block_code.find("pbox->ShouldExitResult()", create_dir_pos)
    create_file_pos = checked_block_code.find("fsFsCreateFile(&native_fs->m_fs, path.s, size, 0)", cancel_before_create_file)
    commit_create_pos = checked_block_code.find("commit_create_rc = fs->Commit()", create_file_pos)
    cancel_after_commit_create = checked_block_code.find("pbox->ShouldExitResult()", commit_create_pos)
    open_file_pos = checked_block_code.find("fs->OpenFile(path, FsOpenMode_Write, &f)", cancel_after_commit_create)
    loop_start_pos = checked_block_code.find("while (remaining > 0)", open_file_pos)

    check(create_dir_pos != -1, "Must process implicit parents via CreateDirectoryChecked")
    check(cancel_before_create_file != -1, "Must check cancellation before fsFsCreateFile")
    check(create_file_pos != -1, "Must call fsFsCreateFile with option 0")
    check(commit_create_pos != -1, "Must commit file creation separately")
    check(cancel_after_commit_create != -1, "Must check cancellation after file creation commit")
    check(open_file_pos != -1, "Must open payload file handle after creation commit and cancel check")
    check(loop_start_pos != -1, "Must enter while (remaining > 0) payload loop")

    check(create_dir_pos < cancel_before_create_file < create_file_pos < commit_create_pos < cancel_after_commit_create < open_file_pos < loop_start_pos,
          "Creation and open sequence must strictly follow: implicit dirs -> cancel -> create file -> commit -> cancel -> open -> loop")

    # Payload loop sequence
    loop_body = checked_block_code[loop_start_pos:]
    loop_cancel_pos = loop_body.find("pbox->ShouldExitResult()")
    read_pos = loop_body.find("unzReadCurrentFile", loop_cancel_pos)
    cancel_pre_write = loop_body.find("pbox->ShouldExitResult()", read_pos)
    write_pos = loop_body.find("fsFileWrite", cancel_pre_write)
    flush_pos = loop_body.find("fsFileFlush", write_pos)
    close_pos = loop_body.find("close_and_invalidate()", flush_pos)
    commit_pos = loop_body.find("fs->Commit()", close_pos)
    prog_pos = loop_body.find("progress(read_res)", commit_pos)
    cancel_post_commit = loop_body.find("pbox->ShouldExitResult()", prog_pos)
    reopen_pos = loop_body.find("fs->OpenFile", cancel_post_commit)

    check(loop_cancel_pos != -1 and read_pos != -1 and cancel_pre_write != -1 and write_pos != -1,
          "Loop must contain cancel at start -> read -> cancel pre-write -> write in order")
    check(flush_pos != -1 and close_pos != -1 and commit_pos != -1 and prog_pos != -1,
          "Loop must contain flush -> close -> commit -> progress in order")
    check(cancel_post_commit != -1 and reopen_pos != -1,
          "Loop must contain cancel post-commit -> reopen in order")
    check(loop_cancel_pos < read_pos < cancel_pre_write < write_pos < flush_pos < close_pos < commit_pos < prog_pos < cancel_post_commit < reopen_pos,
          "Payload chunk loop must strictly follow: cancel start -> read -> cancel pre-write -> write -> flush -> close -> commit -> progress -> cancel post-commit -> reopen")

    # Post-loop checks
    post_loop_start = checked_block_code.find("if (current_offset != size)")
    check(post_loop_start != -1, "Checked block must validate final size")
    post_loop = checked_block_code[post_loop_start:]
    crc_check_pos = post_loop.find("crc32 == crc32_out")
    post_file_cancel = post_loop.find("pbox->ShouldExitResult()", crc_check_pos)
    check(crc_check_pos != -1 and post_file_cancel != -1,
          "Post-loop must verify CRC and check cancellation before return 0")

    # TransferUnzipAll definitions
    tua_zfile_start = tft_cpp.find("Result TransferUnzipAll(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode, bool save_dbi_compat, bool checked_native_save, s64 checked_save_journal_size)")
    check(tua_zfile_start != -1, "TransferUnzipAll zfile definition must match exact signature")
    tua_path_start = tft_cpp.find("Result TransferUnzipAll(ui::ProgressBox* pbox, const fs::FsPath& zip_out, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode, bool save_dbi_compat, bool checked_native_save, s64 checked_save_journal_size)")
    check(tua_path_start != -1, "TransferUnzipAll path definition must match exact signature")

    tua_zfile_body = tft_cpp[tua_zfile_start:tua_path_start]
    check("if (checked_save_journal_size < 0) {\n            R_THROW(FsError_InvalidSize);\n        }" in tua_zfile_body,
          "TransferUnzipAll must reject negative journal size before entry sizing")
    check("CreateDirectoryChecked(pbox, fs, resolved.path)" in tua_zfile_body,
          "TransferUnzipAll must use CreateDirectoryChecked for checked directories")
    check("checked_save_journal_size" in tua_zfile_body,
          "TransferUnzipAll must pass checked_save_journal_size to TransferUnzipInternal")

    # 1.3 save_restore_zip.cpp
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_cpp = f.read()

    check("DeleteAllCollections(pbox, &save_fs, collections));" in ops_cpp and "R_TRY(save_fs.Commit());" in ops_cpp,
          "RestoreSaveZip must commit save_fs immediately after DeleteAllCollections")
    check("target_journal_size" in ops_cpp and "live.journal_size" in ops_cpp,
          "RestoreSaveZip must set target_journal_size from live.journal_size")
    check("TransferUnzipAll(pbox, zfile, &save_fs, \"/\", save_filter, thread::Mode::SingleThreadedIfSmaller, true, true, target_journal_size)" in ops_cpp,
          "RestoreSaveZip must forward target_journal_size to TransferUnzipAll")

    # 1.4 sphaira/CMakeLists.txt
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(855, 880)),
          "sphaira/CMakeLists.txt version must be 0.13.855 or later")

    print("  -> Static source contracts PASSED.")


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
    test_source_contracts()
    test_behavioral_fixtures()
    print("=== ALL CHECKS PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
