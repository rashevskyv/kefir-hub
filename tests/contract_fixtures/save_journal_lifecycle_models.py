# Models for save journal lifecycle contract.
import os
import io
import zipfile
import struct
import zlib

RES_OK = 0
ERR_TARGET_LOCKED = 0x244E02
ERR_VERIFICATION_FAILED = 0xEE01
ERR_CANCELLED = 0xEE02
ERR_INVALID_SIZE = 0xEE03
ERR_INVALID_CHAR = 0xEE04
ERR_UNZ_READ = 0xEE05
ERR_CRC_MISMATCH = 0xEE06
ERR_PREFLIGHT_FAIL = 0xEE07
ERR_JOURNAL_SIZE_MISMATCH = 0xEE08
ERR_PREV_RESTORE_DIRTY = 0xEE09
ERR_REWIND_FAILED = 0xEE0A
RESERVED_NAMES = {".nx_save_meta.bin", ".dbi_save_info.ini", ".dbi_save_extra"}
INVALID_CHARS = set(":*?\"<>|\\")
MAX_S64 = 0x7FFFFFFFFFFFFFFF
SMALL_BUFFER_SIZE = 512 * 1024

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
