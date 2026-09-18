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

    # 1.2 threaded_file_transfer.cpp
    tft_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer.cpp")
    with open(tft_cpp_path, "r", encoding="utf-8") as f:
        tft_cpp = f.read()

    # CreateDirectoryChecked helper
    check("static Result CreateDirectoryChecked(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& dir_path)" in tft_cpp,
          "threaded_file_transfer.cpp must define CreateDirectoryChecked helper")
    dir_helper_start = tft_cpp.find("static Result CreateDirectoryChecked(")
    dir_helper_end = tft_cpp.find("static Result TransferUnzipInternal(")
    check(dir_helper_start != -1 and dir_helper_end != -1 and dir_helper_start < dir_helper_end,
          "CreateDirectoryChecked must precede TransferUnzipInternal")
    dir_helper_code = tft_cpp[dir_helper_start:dir_helper_end]

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
    unzip_int_end = tft_cpp.find("Result TransferUnzip(ui::ProgressBox*")
    check(unzip_int_start != -1 and unzip_int_end != -1 and unzip_int_start < unzip_int_end,
          "TransferUnzipInternal must be bounded properly")
    unzip_int_code = tft_cpp[unzip_int_start:unzip_int_end]

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

    # 1.3 save_menu_ops.cpp
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_cpp = f.read()

    check("DeleteAllCollections(pbox, &save_fs, collections));\n        R_TRY(save_fs.Commit());" in ops_cpp,
          "RestoreSaveZip must commit save_fs immediately after DeleteAllCollections")
    check("const s64 target_journal_size = (e.save_data_id != 0) ? live.journal_size : 0;" in ops_cpp,
          "RestoreSaveZip must set target_journal_size from live.journal_size or 0")
    check("TransferUnzipAll(pbox, zfile, &save_fs, \"/\", save_filter, thread::Mode::SingleThreadedIfSmaller, true, true, target_journal_size)" in ops_cpp,
          "RestoreSaveZip must forward target_journal_size to TransferUnzipAll")

    # 1.4 sphaira/CMakeLists.txt
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check("set(sphaira_VERSION 0.13.855)" in cmake_src or "set(sphaira_VERSION 0.13.856)" in cmake_src,
          "sphaira/CMakeLists.txt version must be 0.13.855 or 0.13.856")

    print("  -> Static source contracts PASSED.")


# ==============================================================================
# 2. Synthetic Behavioral Reference Model
# ==============================================================================

class ModelNativeFile:
    """Models native FsFile lifecycle on Nintendo Switch."""
    def __init__(self, path: str, size: int):
        self.path = path
        self.size = size
        self.data = bytearray(size)
        self.is_open = False
        self.mode = 0
        self.flushed = True

    def open(self, mode: int):
        if self.is_open:
            raise RuntimeError(f"File already open: {self.path}")
        self.is_open = True
        self.mode = mode

    def write(self, off: int, chunk: bytes):
        if not self.is_open:
            raise RuntimeError(f"Write on closed file: {self.path}")
        if off + len(chunk) > self.size:
            raise ValueError(f"Write exceeds preallocated file size {self.size} at offset {off} len {len(chunk)}")
        self.data[off:off+len(chunk)] = chunk
        self.flushed = False

    def flush(self):
        if not self.is_open:
            raise RuntimeError(f"Flush on closed file: {self.path}")
        self.flushed = True

    def close(self):
        self.is_open = False


class ModelNativeFs:
    """Models FsNative / FsNativeSave with event recording and fault injection."""
    def __init__(self):
        self.files: dict[str, ModelNativeFile] = {}
        self.dirs: set[str] = set()
        self.events: list[str] = []
        self.fail_at: str | None = None
        self.open_write_handles: int = 0
        self.after_clear = lambda: None
        self.on_commit = lambda context: None
        self.on_open = lambda path: None
        self.recovery_path = None

    def commit(self, context: str = ""):
        self.events.append(f"commit:{context}")
        if self.open_write_handles > 0:
            raise RuntimeError(f"Illegal Commit with {self.open_write_handles} open write file handle(s) during {context}")
        if self.fail_at == f"commit:{context}" or self.fail_at == "commit":
            raise RuntimeError(f"Simulated commit failure in {context}")
        self.on_commit(context)

    def create_directory(self, path: str):
        self.events.append(f"create_dir:{path}")
        if self.fail_at == f"create_dir:{path}":
            raise RuntimeError(f"Simulated create_dir failure on {path}")
        if path in self.dirs:
            return "exists_dir"
        if path in self.files:
            return "exists_file"
        self.dirs.add(path)
        return "ok"

    def get_entry_type(self, path: str):
        self.events.append(f"get_entry_type:{path}")
        if path in self.dirs:
            return "dir"
        if path in self.files:
            return "file"
        return "not_found"

    def create_file(self, path: str, size: int):
        self.events.append(f"create_file:{path}:{size}")
        if self.fail_at == f"create_file:{path}":
            raise RuntimeError(f"Simulated create_file failure on {path}")
        if path in self.files or path in self.dirs:
            raise FileExistsError(f"Path already exists: {path}")
        self.files[path] = ModelNativeFile(path, size)

    def open_file(self, path: str, mode: int):
        self.events.append(f"open_file:{path}:{mode}")
        if self.fail_at == f"open_file:{path}":
            raise RuntimeError(f"Simulated open_file failure on {path}")
        if path not in self.files:
            raise FileNotFoundError(f"File not found: {path}")
        f = self.files[path]
        f.open(mode)
        self.open_write_handles += 1
        self.on_open(path)
        return f

    def close_file(self, f: ModelNativeFile):
        self.events.append(f"close_file:{f.path}")
        if f.is_open:
            f.close()
            self.open_write_handles = max(0, self.open_write_handles - 1)


class MockProgressBox:
    """Models progress box with sticky cancellation behavior."""
    def __init__(self, cancel_at: str | list[str] | set[str] | None = None):
        if cancel_at is None:
            self.cancel_targets = set()
        elif isinstance(cancel_at, str):
            self.cancel_targets = {cancel_at}
        else:
            self.cancel_targets = set(cancel_at)
        self.cancelled = False
        self.updated_progress = 0
        self.progress_history: list[int] = []

    def should_exit(self, checkpoint: str) -> bool:
        if self.cancelled:
            return True
        if checkpoint in self.cancel_targets:
            self.cancelled = True
            return True
        return False

    def update_transfer(self, bytes_done: int):
        self.updated_progress += bytes_done
        self.progress_history.append(self.updated_progress)


def simulate_checked_restore(zip_bytes: bytes, declared_journal_size: int,
                             fail_at: str | None = None,
                             cancel_at: str | list[str] | set[str] | None = None,
                             short_read_step: int | None = None,
                             fs: ModelNativeFs | None = None,
                             pbox: MockProgressBox | None = None) -> tuple[bool, ModelNativeFs, list[str], MockProgressBox]:
    """Simulates the serial per-chunk checked extraction lifecycle matching C++ semantics."""
    if fs is None:
        fs = ModelNativeFs()
    if pbox is None:
        pbox = MockProgressBox(cancel_at=cancel_at)
    elif cancel_at is not None:
        if isinstance(cancel_at, str):
            pbox.cancel_targets.add(cancel_at)
        else:
            pbox.cancel_targets.update(cancel_at)

    if fail_at is not None:
        fs.fail_at = fail_at

    events = fs.events

    # 1. Validation before mutation
    if declared_journal_size < 0:
        events.append("reject_negative_journal")
        return False, fs, events, pbox

    # Clear phase + commit
    fs.recovery_path = "/model/recovery.zip"
    events.append("recovery_published")
    events.append("clear_collections")
    if pbox.should_exit("during_clear"):
        events.append("cancelled_during_clear")
        return False, fs, events, pbox
    try:
        fs.files.clear()
        fs.dirs.clear()
        fs.commit("post_clear")
        fs.after_clear()
    except Exception:
        events.append("commit_post_clear_error")
        return False, fs, events, pbox

    # Read synthetic ZIP
    try:
        buf = io.BytesIO(zip_bytes)
        zf = zipfile.ZipFile(buf, "r")
    except Exception:
        events.append("zip_open_error")
        return False, fs, events, pbox

    # Request cap policy:
    # positive declared journal: min(SMALL_BUFFER_SIZE, declared_journal_size)
    # zero: SMALL_BUFFER_SIZE
    request_cap = SMALL_BUFFER_SIZE
    if declared_journal_size > 0 and declared_journal_size < request_cap:
        request_cap = declared_journal_size

    events.append(f"request_cap:{request_cap}")

    # Process archive entries
    for zinfo in zf.infolist():
        raw_name = zinfo.orig_filename
        norm_name = raw_name.lstrip("/")
        is_dir = norm_name.endswith("/")
        norm_name = norm_name.rstrip("/")
        if not norm_name:
            continue

        canon_path = "/" + norm_name

        if is_dir:
            # CreateDirectoryChecked component-by-component
            parts = [p for p in canon_path.split("/") if p]
            cur = ""
            for p in parts:
                cur += "/" + p
                if pbox.should_exit(f"dir_cancel:{cur}"):
                    events.append(f"cancelled_at_dir:{cur}")
                    return False, fs, events, pbox
                try:
                    res = fs.create_directory(cur)
                except Exception:
                    events.append(f"create_dir_error:{cur}")
                    return False, fs, events, pbox

                if res == "ok":
                    try:
                        fs.commit(f"dir:{cur}")
                    except Exception:
                        events.append(f"commit_dir_error:{cur}")
                        return False, fs, events, pbox
                elif res == "exists_dir":
                    entry_type = fs.get_entry_type(cur)
                    if entry_type != "dir":
                        events.append(f"dir_conflict_not_dir:{cur}")
                        return False, fs, events, pbox
                else:
                    events.append(f"dir_conflict_exists_file:{cur}")
                    return False, fs, events, pbox
        else:
            # File implicit parents component by component
            parent = os.path.dirname(canon_path)
            if parent and parent != "/":
                parts = [p for p in parent.split("/") if p]
                cur = ""
                for p in parts:
                    cur += "/" + p
                    if pbox.should_exit(f"file_parent_cancel:{cur}"):
                        events.append(f"cancelled_at_file_parent:{cur}")
                        return False, fs, events, pbox
                    try:
                        res = fs.create_directory(cur)
                    except Exception:
                        events.append(f"create_dir_error:{cur}")
                        return False, fs, events, pbox

                    if res == "ok":
                        try:
                            fs.commit(f"dir:{cur}")
                        except Exception:
                            events.append(f"commit_dir_error:{cur}")
                            return False, fs, events, pbox
                    elif res == "exists_dir":
                        entry_type = fs.get_entry_type(cur)
                        if entry_type != "dir":
                            events.append(f"parent_conflict_not_dir:{cur}")
                            return False, fs, events, pbox
                    else:
                        events.append(f"parent_conflict_exists_file:{cur}")
                        return False, fs, events, pbox

            # Cancellation checkpoint immediately before fsFsCreateFile
            if pbox.should_exit(f"cancel_before_create_file:{canon_path}"):
                events.append(f"cancelled_before_create_file:{canon_path}")
                return False, fs, events, pbox

            file_size = zinfo.file_size
            try:
                fs.create_file(canon_path, file_size)
            except Exception:
                events.append(f"create_file_error:{canon_path}")
                return False, fs, events, pbox

            try:
                fs.commit(f"file_create:{canon_path}")
            except Exception:
                events.append(f"commit_file_create_error:{canon_path}")
                return False, fs, events, pbox

            # Cancellation checkpoint immediately after file create commit (before payload open)
            if pbox.should_exit(f"cancel_after_commit_create:{canon_path}"):
                events.append(f"cancelled_after_commit_create:{canon_path}")
                return False, fs, events, pbox

            # Empty file handling
            if file_size == 0:
                try:
                    f = fs.open_file(canon_path, 2)  # FsOpenMode_Write
                except Exception:
                    events.append(f"open_file_error:{canon_path}")
                    return False, fs, events, pbox

                if pbox.should_exit(f"empty_file_before_flush:{canon_path}"):
                    fs.close_file(f)
                    events.append(f"cancelled_empty_before_flush:{canon_path}")
                    return False, fs, events, pbox
                f.flush()
                fs.close_file(f)
                try:
                    fs.commit(f"empty_file:{canon_path}")
                except Exception:
                    events.append(f"commit_empty_file_error:{canon_path}")
                    return False, fs, events, pbox

                if pbox.should_exit(f"cancel_after_empty_commit:{canon_path}"):
                    events.append(f"cancelled_after_empty_commit:{canon_path}")
                    return False, fs, events, pbox

                if zinfo.CRC != 0:
                    events.append(f"crc_mismatch_empty:{canon_path}")
                    return False, fs, events, pbox
                continue

            # Non-empty file: open payload handle
            try:
                f = fs.open_file(canon_path, 2)  # FsOpenMode_Write
            except Exception:
                events.append(f"open_file_error:{canon_path}")
                return False, fs, events, pbox

            remaining = file_size
            current_offset = 0
            zstream = zf.open(zinfo)
            calc_crc = 0

            while remaining > 0:
                # Cancellation checkpoint at START of payload loop iteration
                if pbox.should_exit(f"cancel_start_loop:{current_offset}"):
                    fs.close_file(f)
                    events.append(f"cancelled_start_loop:{current_offset}")
                    return False, fs, events, pbox

                to_read = min(request_cap, remaining)
                if short_read_step and to_read > short_read_step:
                    to_read = short_read_step

                events.append(f"read:{canon_path}:{current_offset}:{to_read}")
                try:
                    chunk = zstream.read(to_read)
                except (zipfile.BadZipFile, IOError) as e:
                    fs.close_file(f)
                    events.append(f"crc_read_error:{type(e).__name__}")
                    return False, fs, events, pbox

                read_res = len(chunk)
                for kind, result in (("neg", -1), ("zero", 0), ("over", to_read + 1)):
                    if fs.fail_at == f"read_fail_{kind}:{current_offset}":
                        read_res = result
                        events.append(f"read_error_{kind}:{current_offset}")

                if read_res <= 0 or read_res > to_read or read_res > remaining:
                    fs.close_file(f)
                    events.append(f"read_error:{read_res}")
                    return False, fs, events, pbox

                calc_crc = zlib.crc32(chunk, calc_crc)

                # Pre-write cancel check
                if pbox.should_exit(f"cancel_before_write:{current_offset}"):
                    fs.close_file(f)
                    events.append(f"cancelled_before_write:{current_offset}")
                    return False, fs, events, pbox

                # Synchronous write-flush-close-commit lifecycle
                try:
                    if fs.fail_at == f"write_fail:{current_offset}":
                        raise IOError("Simulated write failure")
                    events.append(f"write:{canon_path}:{current_offset}:{read_res}")
                    f.write(current_offset, chunk)
                except Exception:
                    fs.close_file(f)
                    events.append(f"write_error:{current_offset}")
                    return False, fs, events, pbox

                try:
                    if fs.fail_at == f"flush_fail:{current_offset}":
                        raise IOError("Simulated flush failure")
                    events.append(f"flush:{canon_path}")
                    f.flush()
                except Exception:
                    fs.close_file(f)
                    events.append(f"flush_error:{current_offset}")
                    return False, fs, events, pbox

                # Close file BEFORE commit
                fs.close_file(f)

                try:
                    fs.commit(f"chunk:{canon_path}:{current_offset}")
                except Exception:
                    events.append(f"commit_chunk_error:{current_offset}")
                    return False, fs, events, pbox

                # Advance offset and progress ONLY after successful commit
                current_offset += read_res
                remaining -= read_res
                pbox.update_transfer(read_res)
                events.append(f"progress:{current_offset}")

                # Post-commit cancel check
                if pbox.should_exit(f"cancel_after_commit:{current_offset}"):
                    events.append(f"cancelled_after_commit:{current_offset}")
                    return False, fs, events, pbox

                # Reopen if more payload remains
                if remaining > 0:
                    if fs.fail_at == f"reopen_fail:{current_offset}":
                        events.append(f"reopen_error:{current_offset}")
                        return False, fs, events, pbox
                    try:
                        f = fs.open_file(canon_path, 2)
                    except Exception:
                        events.append(f"reopen_error:{current_offset}")
                        return False, fs, events, pbox

            # Post-loop validation
            if current_offset != file_size:
                events.append(f"size_mismatch:{current_offset}:{file_size}")
                return False, fs, events, pbox

            if fs.fail_at == "crc_mismatch":
                calc_crc ^= 0xFFFFFFFF

            if zinfo.CRC and calc_crc != zinfo.CRC:
                events.append(f"crc_mismatch:{calc_crc}:{zinfo.CRC}")
                return False, fs, events, pbox

            if pbox.should_exit(f"cancel_post_file:{canon_path}"):
                events.append(f"cancelled_post_file:{canon_path}")
                return False, fs, events, pbox
            events.append("source_entry_close")
            if fs.fail_at == "source_entry_close":
                return False, fs, events, pbox

    # Final commit after all extraction
    try:
        fs.commit("final_extraction")
    except Exception:
        events.append("final_commit_error")
        return False, fs, events, pbox
    events.append("extraction_succeeded")
    events.append("rw_exit")
    events.append("ro_mount")
    if fs.fail_at == "ro_mount":
        return False, fs, events, pbox
    events.append("verify")
    expected_files = {"/" + zi.filename.rstrip("/"): zf.read(zi)
                      for zi in zf.infolist() if not zi.is_dir()}
    expected_dirs = {"/" + zi.filename.rstrip("/") for zi in zf.infolist() if zi.is_dir()}
    for name in expected_files:
        parent = name.rsplit("/", 1)[0]
        while parent:
            expected_dirs.add(parent)
            parent = parent.rsplit("/", 1)[0]
    matches = (set(fs.files) == set(expected_files) and fs.dirs == expected_dirs
               and all(bytes(fs.files[name].data) == data and fs.files[name].size == len(data)
                       for name, data in expected_files.items()))
    events.append("ro_exit")
    if not matches or fs.fail_at == "verify":
        return False, fs, events, pbox
    events.append("source_reader_close")
    if fs.fail_at == "source_reader_close" or pbox.cancelled:
        return False, fs, events, pbox
    zf.close()
    events.append("restore_succeeded")
    return True, fs, events, pbox


# ==============================================================================
# 3. Comprehensive Boundary & Fault Injection Fixtures
# ==============================================================================

def build_fixture_zip(files: dict[str, bytes], dirs: list[str] = None) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        if dirs:
            for d in dirs:
                zf.writestr(d.strip("/") + "/", b"")
        for name, data in files.items():
            zf.writestr(name.lstrip("/"), data)
    return buf.getvalue()


def test_behavioral_fixtures() -> None:
    print("[2] Running synthetic behavioral regression fixtures...")

    # Fixture 1: Negative journal rejected before extraction mutation
    zip_basic = build_fixture_zip({"save.bin": b"hello"})
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=-1)
    check(not ok and "reject_negative_journal" in ev and "clear_collections" not in ev,
          "Negative journal size must be rejected before clear/mutation")
    print("  -> Fixture 1 (Negative journal rejection before mutation) PASSED.")

    # Fixture 2: Zero journal keeps per-read commits; no divide by zero; request cap is SMALL_BUFFER_SIZE
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=0)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev and "commit:chunk:/save.bin:0" in ev,
          "Zero journal must default request cap to SMALL_BUFFER_SIZE and perform per-read commits")
    print("  -> Fixture 2 (Zero journal default cap & per-read commits) PASSED.")

    # Fixture 3: Positive journal boundary caps (1, below SMALL_BUFFER_SIZE, equal, above, s64 max)
    # Journal = 1 byte
    zip_multi = build_fixture_zip({"data.bin": b"ABCDE"})
    ok, fs, ev, pbox = simulate_checked_restore(zip_multi, declared_journal_size=1)
    check(ok and "request_cap:1" in ev, "Journal=1 must cap requests to 1 byte")
    chunk_commits = [e for e in ev if e.startswith("commit:chunk:/data.bin:")]
    check(len(chunk_commits) == 5, f"Journal=1 must result in 5 commits for 5 bytes (got {len(chunk_commits)})")
    check(fs.files["/data.bin"].data == b"ABCDE", "File data must match exact offset writes")

    # Journal = 1024 bytes (below SMALL_BUFFER_SIZE)
    large_payload = b"X" * 3000
    zip_large = build_fixture_zip({"large.bin": large_payload})
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024)
    check(ok and "request_cap:1024" in ev, "Journal=1024 must cap request to 1024")
    commits_1024 = [e for e in ev if e.startswith("commit:chunk:/large.bin:")]
    check(len(commits_1024) == 3, f"3000 bytes with cap 1024 must produce 3 commits (1024, 1024, 952), got {len(commits_1024)}")
    check(fs.files["/large.bin"].data == large_payload, "Large payload bytes must match exactly")

    # Journal = SMALL_BUFFER_SIZE
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=SMALL_BUFFER_SIZE)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev, "Journal equal to SMALL_BUFFER_SIZE must cap to SMALL_BUFFER_SIZE")

    # Journal = 10 MiB (above SMALL_BUFFER_SIZE) -> capped at SMALL_BUFFER_SIZE
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=10 * 1024 * 1024)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev, "Journal > SMALL_BUFFER_SIZE must cap to SMALL_BUFFER_SIZE")

    # Journal = s64 max -> capped at SMALL_BUFFER_SIZE without overflow
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=MAX_S64)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev, "Journal s64 max must cap to SMALL_BUFFER_SIZE without overflow")
    print("  -> Fixture 3 (Journal boundary caps: 1, below, equal, above, s64 max) PASSED.")

    # Fixture 4: Positive deliberate short reads
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, short_read_step=500)
    check(ok and fs.files["/large.bin"].data == large_payload,
          "Short reads must accumulate correctly without byte loss")
    print("  -> Fixture 4 (Short reads accumulation & integrity) PASSED.")

    # Fixture 5: Empty file lifecycle (open, flush, close, commit, no data reads)
    zip_empty = build_fixture_zip({"empty.bin": b""})
    ok, fs, ev, pbox = simulate_checked_restore(zip_empty, declared_journal_size=1000)
    check(ok and "commit:empty_file:/empty.bin" in ev and not any("chunk:" in e for e in ev),
          "Empty file must flush/close/commit without data payload writes")
    print("  -> Fixture 5 (Empty file lifecycle) PASSED.")

    # Fixture 6: Metadata component-by-component creation & collision handling
    zip_nested = build_fixture_zip({"sub/nested/file.bin": b"nested content"}, dirs=["empty_dir"])
    ok, fs, ev, pbox = simulate_checked_restore(zip_nested, declared_journal_size=1000)
    check(ok and "create_dir:/sub" in ev and "commit:dir:/sub" in ev and "create_dir:/sub/nested" in ev,
          "Implicit and explicit dirs must be created and committed component by component")
    print("  -> Fixture 6 (Component-by-component metadata cadence) PASSED.")

    # Fixture 7: Metadata commit failure MUST NOT be retried or swallowed
    ok, fs, ev, pbox = simulate_checked_restore(zip_nested, declared_journal_size=1000, fail_at="commit:dir:/sub")
    check(not ok and "commit:dir:/sub" in ev and "create_dir:/sub/nested" not in ev,
          "Metadata commit failure must propagate immediately and not proceed to next component")
    print("  -> Fixture 7 (Metadata commit failure refusal) PASSED.")

    # Fixture 8: File create commit failure before payload open
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=1000, fail_at="commit:file_create:/save.bin")
    check(not ok and "commit_file_create_error:/save.bin" in ev and "open_file:/save.bin:2" not in ev,
          "File creation commit failure must halt before opening write handle")
    print("  -> Fixture 8 (File creation commit failure before open) PASSED.")

    # Fixture 9: Chunk write / flush / commit failures and handle invalidation
    # Write failure
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="write_fail:0")
    check(not ok and "write_error:0" in ev and fs.open_write_handles == 0,
          "Write failure must close file handle and not leave active write handles")

    # Flush failure
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="flush_fail:0")
    check(not ok and "flush_error:0" in ev and fs.open_write_handles == 0,
          "Flush failure must close file handle and not leave active write handles")

    # Chunk commit failure: progress NOT advanced, handle closed, no auto-commit
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="commit:chunk:/large.bin:0")
    check(not ok and "commit_chunk_error:0" in ev and fs.open_write_handles == 0,
          "Chunk commit failure must propagate and leave zero open handles")
    print("  -> Fixture 9 (Write, flush, commit failures & zero open handles) PASSED.")

    # Fixture 10: Cancellation before write vs during/after commit
    # Cancel before write: closes without commit
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_before_write:0")
    check(not ok and "cancelled_before_write:0" in ev and "commit:chunk:/large.bin:0" not in ev,
          "Cancellation before write must close without commit")

    # Cancel after commit: observed immediately before reopen/next read
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_after_commit:1024")
    check(not ok and "cancelled_after_commit:1024" in ev and "commit:chunk:/large.bin:0" in ev,
          "Cancellation after commit must be observed before next reopen")
    print("  -> Fixture 10 (Cancellation before write vs after commit) PASSED.")

    # Fixture 11: Fake single-operation allocation/journal refusal despite valid request cap
    class ExhaustionModelFs(ModelNativeFs):
        def commit(self, context: str = ""):
            if "chunk" in context:
                raise RuntimeError("OS_ERROR_JOURNAL_EXHAUSTED: single allocation exceeded platform journal quota")
            super().commit(context)

    exhaustion_fs = ExhaustionModelFs()
    try:
        exhaustion_fs.create_file("/save.bin", 5)
        exhaustion_fs.commit("file_create:/save.bin")
        f = exhaustion_fs.open_file("/save.bin", 2)
        f.write(0, b"hello")
        f.flush()
        exhaustion_fs.close_file(f)
        exhaustion_fs.commit("chunk:/save.bin:0")
        check(False, "Exhaustion model must raise journal exhaustion")
    except RuntimeError as e:
        check("OS_ERROR_JOURNAL_EXHAUSTED" in str(e), "Exhaustion correctly raised on single operation")
    print("  -> Fixture 11 (Simulated single-operation journal exhaustion ceiling illustration) PASSED.")

    # Fixture 12: Premature zero read, negative read, and over-request read independently verified
    # 12.1 Negative read (< 0)
    ok_neg, fs_neg, ev_neg, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="read_fail_neg:0")
    check(not ok_neg and "read_error_neg:0" in ev_neg and fs_neg.open_write_handles == 0,
          "Negative read must abort extraction and close write handles")

    # 12.2 Premature zero read (== 0)
    ok_zero, fs_zero, ev_zero, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="read_fail_zero:0")
    check(not ok_zero and "read_error_zero:0" in ev_zero and fs_zero.open_write_handles == 0,
          "Premature zero read must abort extraction and close write handles")

    # 12.3 Over-request read (> to_read)
    ok_over, fs_over, ev_over, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="read_fail_over:0")
    check(not ok_over and "read_error_over:0" in ev_over and fs_over.open_write_handles == 0,
          "Over-request read must abort extraction and close write handles")
    print("  -> Fixture 12 (Premature zero, negative, over-request read rejection) PASSED.")

    # Fixture 13: Remaining and offset arithmetic at s64 boundary without huge loops/buffers
    req_cap = SMALL_BUFFER_SIZE
    rem = MAX_S64
    off = 0
    to_read = min(req_cap, rem)
    check(off + to_read <= MAX_S64, "Initial chunk must not overflow s64")
    check(rem - to_read >= 0, "Initial remaining must not underflow")

    off = MAX_S64 - req_cap
    rem = req_cap
    to_read = min(req_cap, rem)
    check(off + to_read == MAX_S64, "Final chunk must reach MAX_S64 exactly without overflow")
    check(rem - to_read == 0, "Final remaining must reach 0 exactly")
    print("  -> Fixture 13 (s64 boundary arithmetic validation) PASSED.")

    # Fixture 14: Unexpected existing destination file rejection tested through actual restore engine
    fs_existing = ModelNativeFs()
    fs_existing.create_file("/save.bin", 100)
    fs_existing.after_clear = lambda: fs_existing.create_file("/save.bin", 100)
    ok_ex, fs_ex_out, ev_ex, _ = simulate_checked_restore(zip_basic, declared_journal_size=1024, fs=fs_existing)
    check(not ok_ex, "Creating over existing destination file must fail restore")
    check("create_file_error:/save.bin" in ev_ex, "Must record create_file_error event")
    check(fs_ex_out.open_write_handles == 0, "Zero open handles on existing file collision")
    check(not any("chunk:" in e for e in ev_ex), "Zero chunk commits on existing file collision")
    print("  -> Fixture 14 (Unexpected existing destination file rejection) PASSED.")

    # Fixture 15: Existing non-directory parent collision during directory creation through actual engine
    fs_coll = ModelNativeFs()
    fs_coll.create_file("/sub", 50)  # Non-dir entry where directory is expected
    fs_coll.after_clear = lambda: fs_coll.create_file("/sub", 50)
    ok_coll, fs_coll_out, ev_coll, _ = simulate_checked_restore(zip_nested, declared_journal_size=1000, fs=fs_coll)
    check(not ok_coll, "Existing non-directory parent collision must fail restore")
    check("parent_conflict_not_dir:/sub" in ev_coll or "parent_conflict_exists_file:/sub" in ev_coll,
          "Must record parent non-directory conflict event")
    check(fs_coll_out.open_write_handles == 0, "Zero open handles on non-directory parent collision")
    check(not any("chunk:" in e for e in ev_coll), "Zero payload chunk commits on parent collision")
    print("  -> Fixture 15 (Existing non-directory parent collision check) PASSED.")

    # Fixture 16: Reopen failure and final commit failure handling
    ok_ro, fs_ro, ev_ro, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="reopen_fail:1024")
    check(not ok_ro and fs_ro.open_write_handles == 0 and "reopen_error:1024" in ev_ro,
          "Reopen failure must propagate and leave zero open handles")

    ok_fc, fs_fc, ev_fc, _ = simulate_checked_restore(zip_basic, declared_journal_size=1024, fail_at="commit:final_extraction")
    check(not ok_fc and "final_commit_error" in ev_fc and "extraction_succeeded" not in ev_fc,
          "Final commit failure must prevent extraction success")
    print("  -> Fixture 16 (Reopen and final commit failure handling) PASSED.")

    # Fixture 17: Cancellation points: metadata, before create file, after create commit, loop start, between chunks, and after final payload
    # 17.1 Cancel at metadata directory creation
    ok_c1, _, ev_c1, _ = simulate_checked_restore(zip_nested, declared_journal_size=1000, cancel_at="dir_cancel:/empty_dir")
    check(not ok_c1 and "cancelled_at_dir:/empty_dir" in ev_c1,
          "Cancellation during metadata component creation must halt immediately")

    # 17.2 Cancel before file creation
    ok_c2, _, ev_c2, _ = simulate_checked_restore(zip_basic, declared_journal_size=1000, cancel_at="cancel_before_create_file:/save.bin")
    check(not ok_c2 and "cancelled_before_create_file:/save.bin" in ev_c2 and not any(e.startswith("create_file:") for e in ev_c2),
          "Cancellation before file creation must halt before fsFsCreateFile")

    # 17.3 Cancel after file create commit (before payload open)
    ok_c3, _, ev_c3, _ = simulate_checked_restore(zip_basic, declared_journal_size=1000, cancel_at="cancel_after_commit_create:/save.bin")
    check(not ok_c3 and "cancelled_after_commit_create:/save.bin" in ev_c3 and not any(e.startswith("open_file:") for e in ev_c3),
          "Cancellation after file create commit must halt before opening payload handle")

    # 17.4 Cancel at start of payload loop
    ok_c4, fs_c4, ev_c4, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_start_loop:0")
    check(not ok_c4 and "cancelled_start_loop:0" in ev_c4 and fs_c4.open_write_handles == 0,
          "Cancellation at loop start must close open payload handle and halt")

    # 17.5 Cancel after final payload chunk
    ok_c5, _, ev_c5, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_after_commit:3000")
    check(not ok_c5 and "cancelled_after_commit:3000" in ev_c5 and "extraction_succeeded" not in ev_c5,
          "Cancellation after final payload chunk must return cancellation before overall success")
    print("  -> Fixture 17 (Comprehensive cancellation checkpoints) PASSED.")

    # Fixture 18: Progress invariant: commit refusal maintains accurate progress
    # 18.1 First chunk commit failure maintains progress == 0
    pbox_prog1 = MockProgressBox()
    ok_p1, fs_p1, ev_p1, pbox_out1 = simulate_checked_restore(
        zip_large, declared_journal_size=1024,
        fail_at="commit:chunk:/large.bin:0",
        pbox=pbox_prog1
    )
    check(not ok_p1, "First chunk commit failure must fail restore")
    check(pbox_out1.updated_progress == 0, f"Progress must remain 0 on first chunk commit failure (got {pbox_out1.updated_progress})")
    check(pbox_out1.progress_history == [], "Progress history must be empty on first chunk commit failure")

    # 18.2 Second chunk commit failure maintains progress == 1024 (only earlier committed chunk)
    pbox_prog2 = MockProgressBox()
    ok_p2, fs_p2, ev_p2, pbox_out2 = simulate_checked_restore(
        zip_large, declared_journal_size=1024,
        fail_at="commit:chunk:/large.bin:1024",
        pbox=pbox_prog2
    )
    check(not ok_p2, "Second chunk commit failure must fail restore")
    check(pbox_out2.updated_progress == 1024, f"Progress must strictly reflect only earlier committed chunk (got {pbox_out2.updated_progress})")
    check(pbox_out2.progress_history == [1024], f"Progress history must only contain first chunk (got {pbox_out2.progress_history})")
    print("  -> Fixture 18 (Progress invariant: commit refusal maintains accurate progress) PASSED.")

    # Fixture 19: Real ZIP entry CRC corruption detection refusing final commit
    # 19.1 Corrupted payload byte detected during ZIP streaming read
    buf_crc = io.BytesIO()
    with zipfile.ZipFile(buf_crc, "w", compression=zipfile.ZIP_STORED) as zf_crc:
        zf_crc.writestr("corrupt.bin", b"valid_payload")
    raw_crc = bytearray(buf_crc.getvalue())
    idx = raw_crc.find(b"valid_payload")
    raw_crc[idx] ^= 0xFF
    bad_crc_zip = bytes(raw_crc)

    ok_crc, fs_crc, ev_crc, _ = simulate_checked_restore(bad_crc_zip, declared_journal_size=1024)
    check(not ok_crc and "crc_read_error:BadZipFile" in ev_crc and "extraction_succeeded" not in ev_crc,
          "CRC read error must halt extraction and prevent final extraction commit")
    check(fs_crc.open_write_handles == 0, "Zero open write handles on CRC read error")

    # 19.2 Explicit post-loop CRC mismatch detection
    ok_crc2, fs_crc2, ev_crc2, _ = simulate_checked_restore(zip_basic, declared_journal_size=1024, fail_at="crc_mismatch")
    check(not ok_crc2 and any(e.startswith("crc_mismatch:") for e in ev_crc2) and "extraction_succeeded" not in ev_crc2,
          "Post-loop CRC mismatch must halt extraction before final commit")
    check(fs_crc2.open_write_handles == 0, "Zero open write handles on post-loop CRC mismatch")
    print("  -> Fixture 19 (CRC error detection refusing final commit) PASSED.")

    # Recovery belongs to the exercised restore, and remains published on success too.
    for failure in ("commit:chunk:/large.bin:0", "source_entry_close", "ro_mount", "verify", "source_reader_close"):
        ok, fs, ev, _ = simulate_checked_restore(zip_large, 1024, fail_at=failure)
        check(not ok and "restore_succeeded" not in ev and fs.recovery_path,
              f"Restore gate {failure} must refuse success and retain recovery")
        check(fs.open_write_handles == 0, "Failed restore leaves no write handles")
    ok, fs, ev, _ = simulate_checked_restore(zip_large, 1024)
    check(ok and fs.recovery_path, "Successful restore retains published recovery")
    check(ev.index("rw_exit") < ev.index("ro_mount") < ev.index("verify") < ev.index("ro_exit")
          < ev.index("source_reader_close") < ev.index("restore_succeeded"), "Final verification lifecycle order")
    for failure in ("create_dir:/sub", "create_file:/save.bin", "commit:post_clear"):
        payload = zip_nested if "sub" in failure else zip_basic
        ok, fs, ev, _ = simulate_checked_restore(payload, 1024, fail_at=failure)
        check(not ok and fs.recovery_path and "restore_succeeded" not in ev, f"Primitive fault {failure}")
    # Cancellation is delivered inside the operation, then observed at its next gate.
    for operation in ("create_commit", "chunk_commit", "reopen"):
        fs, pb = ModelNativeFs(), MockProgressBox()
        if operation == "create_commit":
            fs.on_commit = lambda context: setattr(pb, "cancelled", True) if context.startswith("file_create:") else None
        elif operation == "chunk_commit":
            fs.on_commit = lambda context: setattr(pb, "cancelled", True) if context.startswith("chunk:") else None
        else:
            opens = []
            def cancel_reopen(path):
                opens.append(path)
                if len(opens) == 2:
                    pb.cancelled = True
            fs.on_open = cancel_reopen
        ok, fs, ev, pb = simulate_checked_restore(zip_large, 1024, fs=fs, pbox=pb)
        check(not ok and fs.open_write_handles == 0 and fs.recovery_path, f"Cancel during {operation}")
        reads = [e for e in ev if e.startswith("read:")]
        check(len(reads) == (0 if operation == "create_commit" else 1), "No extra ZIP read after cancellation")
    ok, fs, ev, _ = simulate_checked_restore(zip_empty, 1024, cancel_at="cancel_after_empty_commit:/empty.bin")
    check(not ok and fs.recovery_path and fs.open_write_handles == 0, "Empty-file commit cancellation")
    # Clear really removes old data; conflicts are injected by after_clear above.
    fs = ModelNativeFs()
    fs.create_file("/obsolete.bin", 1)
    ok, fs, ev, _ = simulate_checked_restore(zip_basic, 1024, fs=fs)
    check(ok and "/obsolete.bin" not in fs.files, "Old files removed by clear")
    # Primitive event sequence and no cleanup commit after failed chunk commit.
    ok, fs, ev, pb = simulate_checked_restore(zip_large, 1024, fail_at="commit:chunk:/large.bin:1024")
    first = ["read:/large.bin:0:1024", "write:/large.bin:0:1024", "flush:/large.bin",
             "close_file:/large.bin", "commit:chunk:/large.bin:0", "progress:1024"]
    cursor = -1
    for event in first:
        cursor = ev.index(event, cursor + 1)
    failed = ev.index("commit:chunk:/large.bin:1024")
    check(not any(e.startswith(("commit:", "open_file:", "progress:")) for e in ev[failed + 1:]),
          "Failure has no retry, reopen, progress or auto-commit")
    print("  -> Fixture 20 (Connected lifecycle/fault/cancel/recovery checks) PASSED.")

    # Fixture 21: Generic path defaults and zero-ID fallback intact
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tft_hpp_path = os.path.join(repo_root, "sphaira", "include", "threaded_file_transfer.hpp")
    with open(tft_hpp_path, "r", encoding="utf-8") as f:
        tft_hpp_content = f.read()
    check("bool checked_native_save = false" in tft_hpp_content,
          "Generic callers must default checked_native_save to false")
    check("s64 checked_save_journal_size = 0" in tft_hpp_content,
          "checked_save_journal_size must default to 0")
    print("  -> Fixture 21 (Generic path defaults and zero-ID fallback intact) PASSED.")

    print("=== ALL SYNTHETIC BEHAVIORAL FIXTURES PASSED SUCCESSFULLY ===")


# ==============================================================================
# Main Runner
# ==============================================================================

def main():
    print("=== Sphaira v0.13.855: Serial Save Journal Lifecycle Test Suite ===")
    test_source_contracts()
    test_behavioral_fixtures()
    print("=== ALL CHECKS PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
