# Models, generators, reader, and sentinel for raw save restore contract.
import os
import io
import struct
import zipfile
import tempfile

FS_ERROR_NOT_FOUND = 0x202
FS_ERROR_TARGET_LOCKED = 0x244E02
FS_ERROR_INVALID_PATH = 0x1F4
FS_ERROR_BUFFER_TOO_SMALL = 0x1A2
ERR_RAW_RESTORE_REFUSED = 0xEE01
ERR_RECOVERY_DIRTY = 0xEE02
ERR_SIMULATED_CRASH = 0xEE03

def encode_header_copy(
    buf: bytearray,
    offset: int,
    version: int = 0x00040000,
    disf_magic: bytes = b"DISF",
    remap_offset: int = 0x8000,
    remap_size: int = 0x1000,
    app_id: int = 0x0100000000001000,
    uid: tuple = (0x0123456789ABCDEF, 0xFEDCBA9876543210),
    sys_id: int = 0,
    save_type: int = 1,  # Account
    rank: int = 0,
    index: int = 0,
    data_size: int = 0x10000,
    journal_size: int = 0x8000,
    commit_id: int = 1
):
    """Encodes a single 0x4000 DISA header copy at `offset` per Switchbrew documentation."""
    # 0x000..0x0FF: CMAC dummy bytes
    buf[offset + 0x00:offset + 0x100] = b"\xCC" * 0x100

    # 0x100: DISF magic (4 bytes)
    buf[offset + 0x100:offset + 0x100 + len(disf_magic)] = disf_magic

    # 0x104: Version (u32 little-endian)
    struct.pack_into("<I", buf, offset + 0x104, version & 0xFFFFFFFF)

    # 0x128: Main remap table offset (u64 little-endian)
    struct.pack_into("<Q", buf, offset + 0x128, remap_offset & 0xFFFFFFFFFFFFFFFF)

    # 0x130: Main remap table size (u64 little-endian)
    struct.pack_into("<Q", buf, offset + 0x130, remap_size & 0xFFFFFFFFFFFFFFFF)

    # ExtraData A at 0x6D8 and ExtraData B at 0x8D8
    for extra_off in (0x6D8, 0x8D8):
        base = offset + extra_off
        struct.pack_into("<Q", buf, base + 0x00, app_id & 0xFFFFFFFFFFFFFFFF)
        struct.pack_into("<QQ", buf, base + 0x08, uid[0] & 0xFFFFFFFFFFFFFFFF, uid[1] & 0xFFFFFFFFFFFFFFFF)
        struct.pack_into("<Q", buf, base + 0x18, sys_id & 0xFFFFFFFFFFFFFFFF)
        buf[base + 0x20] = save_type & 0xFF
        buf[base + 0x21] = rank & 0xFF
        struct.pack_into("<H", buf, base + 0x22, index & 0xFFFF)
        struct.pack_into("<q", buf, base + 0x58, data_size)
        struct.pack_into("<q", buf, base + 0x60, journal_size)
        struct.pack_into("<Q", buf, base + 0x68, commit_id & 0xFFFFFFFFFFFFFFFF)


def create_disa_matching_looking_artifact(dest_path: str, app_id=0x0100000000001000, uid=(0x111, 0x222), save_type=1, rank=0, index=0):
    """
    Creates a matching-looking DISA container fixture:
    - total size 0x8000 (32 KiB) containing two 0x4000 header copies.
    - unauthenticated CMAC, dummy IVFC.
    """
    buf = bytearray(0x8000)
    # Header copy 0 at 0x0000
    encode_header_copy(buf, 0x0000, app_id=app_id, uid=uid, save_type=save_type, rank=rank, index=index)
    # Header copy 1 at 0x4000
    encode_header_copy(buf, 0x4000, app_id=app_id, uid=uid, save_type=save_type, rank=rank, index=index)
    with open(dest_path, "wb") as f:
        f.write(buf)


def create_magic_only_artifact(dest_path: str):
    """Exactly 0x200 bytes (512 bytes) with DISF at 0x100."""
    buf = bytearray(0x200)
    buf[0x100:0x104] = b"DISF"
    struct.pack_into("<I", buf, 0x104, 0x00040000)
    with open(dest_path, "wb") as f:
        f.write(buf)


def create_unsupported_version_artifact(dest_path: str):
    """0x8000 bytes with DISF at 0x100, but unsupported version 0xFFFFFFFF at 0x104."""
    buf = bytearray(0x8000)
    encode_header_copy(buf, 0x0000, version=0xFFFFFFFF)
    encode_header_copy(buf, 0x4000, version=0xFFFFFFFF)
    with open(dest_path, "wb") as f:
        f.write(buf)


def create_remap_out_of_bounds_artifact(dest_path: str):
    """0x8000 bytes with main remap table offset pointing far out-of-file (0x00100000)."""
    buf = bytearray(0x8000)
    encode_header_copy(buf, 0x0000, remap_offset=0x00100000)
    encode_header_copy(buf, 0x4000, remap_offset=0x00100000)
    with open(dest_path, "wb") as f:
        f.write(buf)


def create_remap_uint64_overflow_artifact(dest_path: str):
    """0x8000 bytes with main remap table offset set to UINT64_MAX."""
    buf = bytearray(0x8000)
    encode_header_copy(buf, 0x0000, remap_offset=0xFFFFFFFFFFFFFFFF)
    encode_header_copy(buf, 0x4000, remap_offset=0xFFFFFFFFFFFFFFFF)
    with open(dest_path, "wb") as f:
        f.write(buf)


def create_raw_fixture_file(dest_path: str, size: int, disf_at_100: bool = False):
    """Creates an arbitrary raw binary file with or without DISF at 0x100."""
    buf = bytearray(size)
    if disf_at_100 and size >= 0x104:
        buf[0x100:0x104] = b"DISF"
    with open(dest_path, "wb") as f:
        f.write(buf)


def create_valid_zip_fixture(dest_path: str, files: dict):
    """Creates a valid, well-formed ZIP archive."""
    with zipfile.ZipFile(dest_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for fname, data in files.items():
            zf.writestr(fname, data)


# ==============================================================================
# 2. Bounded Artifact Reader with Injected Faults
# ==============================================================================

class BoundedArtifactReader:
    """
    Simulates Sphaira's IsDisaSaveFile detection:
    - Open file in Read mode.
    - file.GetSize(&size) -> size >= 0x200.
    - file.Read(0, buf, 0x104) -> bytes_read >= 0x104.
    - memcmp(buf + 0x100, "DISF", 4) == 0.
    Supports real reader faults:
    - 'io_error_open': fails to open file.
    - 'io_error_read': fails to read.
    - 'short_read': reads only 0x80 bytes (< 0x104).
    - 'size_drift_truncate': physically truncates the file to 0x80 bytes after size check.
    """
    def __init__(self, filepath: str, fault: str = None):
        self.filepath = filepath
        self.fault = fault

    def probe_is_disa(self) -> bool:
        if self.fault == "io_error_open":
            return False
        if not os.path.exists(self.filepath):
            return False

        try:
            size = os.path.getsize(self.filepath)
        except Exception:
            return False

        if size < 0x200:
            return False

        # Physical size drift: truncate the file before reading
        if self.fault == "size_drift_truncate":
            try:
                with open(self.filepath, "r+b") as f:
                    f.truncate(0x80)
            except Exception:
                return False

        if self.fault == "io_error_read":
            return False

        try:
            with open(self.filepath, "rb") as f:
                if self.fault == "short_read":
                    buf = f.read(0x80)
                else:
                    buf = f.read(0x104)

            if len(buf) < 0x104:
                return False

            return buf[0x100:0x104] == b"DISF"
        except Exception:
            return False


# ==============================================================================
# 3. Minimal Common Gate: Strict ZIP Admission & Route Simulator
# ==============================================================================

def strict_zip_admission(filepath: str) -> bool:
    """
    Minimal common strict ZIP admission gate:
    - zipfile.is_zipfile must be True.
    - Must open with ZipFile.
    - Must have at least one non-directory payload.
    - Safe relative paths (no leading slash, no '..').
    - Reads every payload to EOF to verify integrity / CRC.
    """
    if not os.path.exists(filepath):
        return False
    if not zipfile.is_zipfile(filepath):
        return False

    try:
        with zipfile.ZipFile(filepath, "r") as zf:
            infolist = zf.infolist()
            payload_files = [info for info in infolist if not info.is_dir()]
            if not payload_files:
                return False

            for info in payload_files:
                # Path safety check
                fname = info.filename.replace("\\", "/")
                if fname.startswith("/") or fname.startswith("../") or "/../" in fname or fname == "..":
                    return False

                # Exhaustive stream read to verify CRC and detect truncation
                with zf.open(info) as item_file:
                    while True:
                        chunk = item_file.read(65536)
                        if not chunk:
                            break
        return True
    except Exception:
        return False


class TargetSaveSentinel:
    """
    Real target save file on disk with verified non-mutation.
    write_data ACTUALLY writes bytes to disk after appending to event log.
    verify_unchanged asserts 0 operations were logged and file data matches initial data.
    """
    def __init__(self, path: str, initial_data: bytes, shared_event_log: list = None):
        self.path = path
        self.initial_data = initial_data
        with open(path, "wb") as f:
            f.write(initial_data)
        if shared_event_log is not None:
            self.event_log = shared_event_log
        else:
            self.event_log = []

    def open_write(self):
        self.event_log.append("open_write")

    def delete_file(self):
        self.event_log.append("delete")

    def create_file(self):
        self.event_log.append("create")

    def write_data(self, data: bytes):
        self.event_log.append(f"write:{len(data)}")
        # ACTUALLY modify the file on disk
        with open(self.path, "wb") as f:
            f.write(data)

    def commit(self):
        self.event_log.append("commit")

    def auto_backup(self):
        self.event_log.append("auto_backup")

    def verify_unchanged(self, allow_non_mutating: bool = False):
        mutating = [e for e in self.event_log if e in ("open_write", "delete", "create", "commit") or e.startswith("write:")]
        assert len(mutating) == 0, f"Target mutating operations occurred: {mutating}"
        if not allow_non_mutating:
            assert len(self.event_log) == 0, f"Target operations occurred: {self.event_log}"
        with open(self.path, "rb") as f:
            current = f.read()
        assert current == self.initial_data, "Target sentinel file data was modified on disk!"


def run_model_filebrowser_restore(reader: BoundedArtifactReader, target: TargetSaveSentinel, cancel_at_confirm: bool = False):
    """Models FsView::RestoreSaveFile route."""
    # 1. Probing
    if reader.probe_is_disa():
        # Upfront refusal: NO target enumeration, NO prompt, NO worker, NO auto-backup, NO write
        return "REFUSED_RAW_UPFRONT"

    # 2. Admission
    if not strict_zip_admission(reader.filepath):
        return "REJECTED_NOT_VALID_BACKUP"

    # 3. Explicit Target Picker & Confirmation
    if cancel_at_confirm:
        return "CANCELLED_BEFORE_WORKER"

    # 4. Worker Execution for Valid ZIP
    target.auto_backup()
    target.open_write()
    target.write_data(b"RESTORED_FROM_ZIP")
    target.commit()
    return "RESTORE_SUCCESS"


def run_model_savemenu_picked_restore(reader: BoundedArtifactReader, target: TargetSaveSentinel, cancel_at_confirm: bool = False):
    """Models Menu::RestoreSavesPicked route."""
    # 1. Upfront refusal check
    if reader.probe_is_disa():
        return "REFUSED_RAW_UPFRONT"

    # 2. Admission
    if not strict_zip_admission(reader.filepath):
        return "REJECTED_NOT_VALID_BACKUP"

    # 3. Confirmation Dialog
    if cancel_at_confirm:
        return "CANCELLED_BEFORE_WORKER"

    # 4. Worker Execution
    target.auto_backup()
    target.open_write()
    target.write_data(b"RESTORED_FROM_ZIP")
    target.commit()
    return "RESTORE_SUCCESS"


def run_model_batch_restore_item(
    reader: BoundedArtifactReader,
    target: TargetSaveSentinel,
    recovery_path: str = None,
    worker_cancelled: bool = False,
    shared_event_log: list = None,
    recovery_fail_mode: str = None
):
    """
    Models Menu::RestoreSaves single batch item execution with strict ordering:
    1. Worker cancellation check -> cancel before mutation.
    2. RAW container refusal -> fail-closed BEFORE auto-backup or target write.
    3. Strict ZIP admission gate -> reject malformed/unsafe ZIP.
    4. Safety recovery creation & verification BEFORE target mutation:
       - creates real recovery ZIP containing target's current sentinel bytes;
       - closes archive;
       - reopens, reads, verifies CRC, and matches bytes against target;
       - records 'recovery_ready' in event log.
    5. Modeled target write:
       - open_write -> write -> commit (recorded after recovery_ready).
    """
    if shared_event_log is None:
        shared_event_log = target.event_log

    if worker_cancelled:
        return "CANCELLED_WORKER", False, False, None

    is_raw = reader.probe_is_disa()
    mutation_started = False

    if is_raw:
        # Refusal happens inside worker BEFORE auto-backup or target mutation
        return "REFUSED_RAW_BATCH_ITEM", True, mutation_started, None

    if not strict_zip_admission(reader.filepath):
        return "REJECTED_ZIP_ADMISSION", False, mutation_started, None

    shared_event_log.append("admission")

    verified_recovery_path = None
    if recovery_path:
        if recovery_fail_mode == "callback_fail":
            return "RECOVERY_CREATION_FAILED", False, mutation_started, None

        # 1. Create real recovery ZIP from current sentinel bytes
        with zipfile.ZipFile(recovery_path, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("original_target.bin", target.initial_data)

        if recovery_fail_mode == "corrupt_archive":
            # Physically corrupt the recovery archive file on disk before reopening
            with open(recovery_path, "wb") as f:
                f.write(b"CORRUPTED_INCOMPLETE_ARCHIVE")

        # 2. Reopen, read, verify CRC, and compare backup bytes with sentinel
        try:
            with zipfile.ZipFile(recovery_path, "r") as zf:
                if not zf.namelist():
                    raise ValueError("Empty recovery archive")
                backed_bytes = zf.read("original_target.bin")
                if backed_bytes != target.initial_data:
                    raise ValueError("Backup bytes mismatch")
        except Exception:
            # Clean up corrupted file so it's not advertised
            if os.path.exists(recovery_path):
                try:
                    os.remove(recovery_path)
                except Exception:
                    pass
            return "RECOVERY_VERIFICATION_FAILED", False, mutation_started, None

        shared_event_log.append("recovery_ready")
        verified_recovery_path = recovery_path

    # Only after recovery is ready: perform target write
    mutation_started = True
    target.open_write()
    target.write_data(b"RESTORED_ZIP_BATCH")
    target.commit()
    return "SUCCESS_ZIP_ITEM", False, mutation_started, verified_recovery_path


def run_model_internal_defense(reader: BoundedArtifactReader, target: TargetSaveSentinel, worker_cancelled: bool = False):
    """Models Menu::RestoreSaveInternal defense-in-depth."""
    mutation_started = False
    if worker_cancelled:
        return "CANCELLED_INTERNAL", mutation_started

    if reader.probe_is_disa():
        # Logs refusal and returns Result_RawSaveRestoreUnsupported without modifying target
        return "RESULT_RAW_SAVE_RESTORE_UNSUPPORTED", mutation_started

    if not strict_zip_admission(reader.filepath):
        return "REJECTED_ZIP_ADMISSION", mutation_started

    mutation_started = True
    target.open_write()
    target.write_data(b"RESTORED_INTERNAL")
    target.commit()
    return "SUCCESS_INTERNAL", mutation_started


# ==============================================================================
# 4. Test Suites
# ==============================================================================
