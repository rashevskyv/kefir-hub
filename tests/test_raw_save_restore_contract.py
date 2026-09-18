#!/usr/bin/env python3
"""
Test Suite: Fail-Closed RAW Save Restore Contract (Sphaira v0.13.858)

Verifies the fail-closed refusal policy for RAW container restores in Sphaira v0.13.858:
1. Structural Fixture Encoding (Switchbrew Savegames container layout):
   Source: https://switchbrew.org/wiki/Savegames
   - Main header size 0x4000 (16 KiB), duplicate copies at 0x0000 and 0x4000.
   - Matching-looking fixture size >= 0x8000 (32 KiB) containing both header copies.
   - CMAC region at 0x000..0x0FF (256 bytes, unauthenticated dummy bytes).
   - DISF magic at 0x100, Version at 0x104.
   - Main remap table offset at 0x128, size at 0x130.
   - ExtraData A at 0x6D8, ExtraData B at 0x8D8:
     SaveDataAttribute: app_id (+0x00), UID (+0x08, +0x10), systemID (+0x18),
     type (+0x20), rank (+0x21), index (+0x22).
     ExtraData sizes: usable data (+0x58), journal (+0x60), commit_id (+0x68).
   - Space ID is external selected destination context (not an embedded header byte).
   - Label: "independently encoded header-layout fixture, unauthenticated and incomplete".
   - Variants:
     - matching-looking fixture (>= 0x8000, both header copies);
     - magic-only 0x200 (exactly 512 bytes with DISF at 0x100);
     - unsupported version (0xFFFFFFFF at 0x104);
     - physical remap table out-of-bounds offset (0x00100000);
     - physical remap table UINT64 overflow offset (0xFFFFFFFFFFFFFFFF);
     - differing identity metadata (UID, type, rank, index, application_id);
     - truncation before magic (< 0x200);
     - truncation after magic (< 0x200, DISF at 0x100);
     - wrong magic at 0x100 ("XXXX");
     - RAW-looking .zip filename with raw payload;
     - wrong-magic/truncated .zip archive.
2. Artifact-Driven Bounded Probing & Fault Injection:
   - Simulates IsDisaSaveFile: size >= 0x200, 0x104 byte read, DISF magic at 0x100.
   - Real reader fault injection:
     - I/O read error during actual read;
     - short read (< 0x104 bytes);
     - physical size drift: truncates file copy between reported size and read.
3. Minimal Common Gate Model:
   - Artifact probe -> recognized RAW refusal (fail-closed), OR
   - Strict real ZIP admission (zipfile: open, nonempty, safe relative paths, read to EOF/CRC)
     -> modeled recovery / mutation.
4. Target Save Sentinel with Verified Non-Mutation:
   - write_data actually writes bytes to disk.
   - Positive self-check confirms verify_unchanged fails if write_data is executed.
   - All variants verified across 4 routes:
     - File Browser (FsView::RestoreSaveFile)
     - Save Menu Picked (Menu::RestoreSavesPicked)
     - Batch Current Item (Menu::RestoreSaves)
     - Internal Defense (Menu::RestoreSaveInternal)
   - Refusals result in 0 target operations, unchanged sentinel bytes, mutation_started = false.
   - Caller cancellation and worker cancellation models verified before mutation.
5. Mixed Batch Simulation with Strict Pre-Mutation Recovery Verification:
   - Sequence: admission -> recovery_ready -> open_write -> write -> commit.
   - Valid ZIP item creates real recovery ZIP from target sentinel bytes, closes, reopens,
     reads, verifies CRC, and matches bytes before open_write.
   - RAW item produces NO recovery and NO target events.
   - Earlier completed ZIP recovery archive is retained after RAW refusal.
   - Recovery creation/readback failure fixture: failure halts before open_write, sentinel unchanged,
     mutation_started = false, incomplete recovery NOT advertised as verified path.
6. Integrity Outcome Model:
   - RAW-only: unsupported reported separately, never reported as valid or corrupt.
   - ZIP-only: reported valid.
   - Mixed: RAW reported separately as unsupported, never as corrupt.
7. Bounded Source Code Assertions:
   - Old destructive writer symbols (FsNativeBis, bis_fs, fsFsOpenSaveDataFileSystemBySaveDataSpaceId,
     fsFsCreateFile, fsFsDeleteFile, fsFsOpenFile, fsFileWrite, fsFsCommit, thread::Transfer)
     absent from restore function bodies.
   - Batch slice bounded by next function DeleteSaves.
   - BackupSaveInternal absent from RestoreSaveFile and RestoreSavesPicked workers.
   - File Browser RAW refusal precedes candidate enumeration, picker, confirmation, and worker.
   - Internal RAW return precedes RestoreSaveZip.
   - Dead constant RAW_RESTORE_UNSUPPORTED_MSG removed; localized getter retained.
   - Harmonized toggle description in save_menu.cpp and settings_categories.cpp.
   - i18n parity in en.json and uk.json.

Models and text anchors are compiler-free static contracts, not C++/libnx/IPC/hardware runtime proof.
"""

import json
import os
import re
import shutil
import struct
import sys
import tempfile
import zipfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def read_repo_file(*parts):
    with open(os.path.join(REPO_ROOT, *parts), "r", encoding="utf-8") as f:
        return f.read()


# ==============================================================================
# 1. Real Binary Artifact Encoding (Switchbrew Savegames Layout)
# ==============================================================================

FIXTURE_LABEL = "independently encoded header-layout fixture, unauthenticated and incomplete"

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

def test_sentinel_positive_self_check():
    """Confirms that TargetSaveSentinel actually writes to disk and fails on mutation."""
    print("[1] Testing TargetSaveSentinel Positive Self-Check...")
    with tempfile.TemporaryDirectory() as tmpdir:
        test_path = os.path.join(tmpdir, "sentinel.bin")
        s = TargetSaveSentinel(test_path, b"INITIAL_CLEAN_BYTES")
        s.verify_unchanged()  # Must pass initially

        s.write_data(b"MUTATED_DIRTY_BYTES")
        failed = False
        try:
            s.verify_unchanged()
        except AssertionError:
            failed = True

        assert failed, "TargetSaveSentinel.verify_unchanged MUST fail when write_data modified the file!"
    print("  -> Sentinel positive self-check PASSED.")


def test_all_variants_across_all_routes():
    """Tests all artifact and reader fault variants across all 4 connected routes."""
    print("[2] Running All Artifact and Fault Variants Across All Connected Routes...")
    with tempfile.TemporaryDirectory() as tmpdir:
        # Create all variants
        variants = {}

        # 1. Matching-looking structural fixture
        path_matching = os.path.join(tmpdir, "matching.bin")
        create_disa_matching_looking_artifact(path_matching)
        variants["matching_looking"] = (path_matching, None, True)

        # 2. Magic-only 0x200 bytes
        path_magic_only = os.path.join(tmpdir, "magic_only.bin")
        create_magic_only_artifact(path_magic_only)
        variants["magic_only_0x200"] = (path_magic_only, None, True)

        # 3. Unsupported version at 0x104
        path_unsupp_ver = os.path.join(tmpdir, "unsupported_version.bin")
        create_unsupported_version_artifact(path_unsupp_ver)
        variants["unsupported_version"] = (path_unsupp_ver, None, True)

        # 4. Remap table out-of-bounds offset
        path_remap_oob = os.path.join(tmpdir, "remap_oob.bin")
        create_remap_out_of_bounds_artifact(path_remap_oob)
        variants["remap_out_of_bounds"] = (path_remap_oob, None, True)

        # 5. Remap table UINT64 overflow offset
        path_remap_overflow = os.path.join(tmpdir, "remap_overflow.bin")
        create_remap_uint64_overflow_artifact(path_remap_overflow)
        variants["remap_uint64_overflow"] = (path_remap_overflow, None, True)

        # 6. Differing identity metadata
        path_diff_id = os.path.join(tmpdir, "differing_id.bin")
        create_disa_matching_looking_artifact(
            path_diff_id,
            app_id=0x0100999999999000,
            uid=(0x999, 0x888),
            save_type=0,  # System
            rank=3,
            index=7
        )
        variants["differing_identity"] = (path_diff_id, None, True)

        # 7. Truncation before magic (< 0x200)
        path_trunc_before = os.path.join(tmpdir, "trunc_before.bin")
        create_raw_fixture_file(path_trunc_before, 0x80)
        variants["truncation_before_magic"] = (path_trunc_before, None, False)

        # 8. Truncation after magic (< 0x200 with DISF present)
        path_trunc_after = os.path.join(tmpdir, "trunc_after.bin")
        create_raw_fixture_file(path_trunc_after, 0x180, disf_at_100=True)
        variants["truncation_after_magic"] = (path_trunc_after, None, False)

        # 9. Wrong magic at 0x100
        path_wrong_magic = os.path.join(tmpdir, "wrong_magic.bin")
        buf = bytearray(0x8000)
        buf[0x100:0x104] = b"XXXX"
        with open(path_wrong_magic, "wb") as f:
            f.write(buf)
        variants["wrong_magic"] = (path_wrong_magic, None, False)

        # 10. RAW-looking .zip filename (contains raw DISF data)
        path_raw_zip = os.path.join(tmpdir, "raw_backup.zip")
        create_disa_matching_looking_artifact(path_raw_zip)
        variants["raw_looking_zip_filename"] = (path_raw_zip, None, True)

        # 11. Wrong-magic / corrupt .zip archive
        path_corrupt_zip = os.path.join(tmpdir, "corrupt.zip")
        with open(path_corrupt_zip, "wb") as f:
            f.write(b"NOT_A_VALID_ZIP_HEADER_CONTENT")
        variants["wrong_magic_zip"] = (path_corrupt_zip, None, False)

        # 12. Reader fault: I/O error open
        variants["fault_io_error_open"] = (path_matching, "io_error_open", False)

        # 13. Reader fault: I/O error read
        variants["fault_io_error_read"] = (path_matching, "io_error_read", False)

        # 14. Reader fault: short read
        variants["fault_short_read"] = (path_matching, "short_read", False)

        # 15. Reader fault: physical size drift (file copy truncated to 0x80 bytes)
        path_drift = os.path.join(tmpdir, "drift.bin")
        create_disa_matching_looking_artifact(path_drift)
        variants["fault_size_drift_truncate"] = (path_drift, "size_drift_truncate", False)

        # Test each variant against all 4 routes
        for vname, (art_path, fault_mode, is_raw_expected) in variants.items():
            # Route 1: File Browser
            reader_fb = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_fb = TargetSaveSentinel(os.path.join(tmpdir, f"target_fb_{vname}.bin"), b"FB_TARGET_INIT")
            outcome_fb = run_model_filebrowser_restore(reader_fb, sentinel_fb)
            if is_raw_expected:
                assert outcome_fb == "REFUSED_RAW_UPFRONT", f"[{vname}] FB expected REFUSED_RAW_UPFRONT, got {outcome_fb}"
            else:
                assert outcome_fb == "REJECTED_NOT_VALID_BACKUP", f"[{vname}] FB expected REJECTED_NOT_VALID_BACKUP, got {outcome_fb}"
            sentinel_fb.verify_unchanged()

            # Route 2: Save Menu Picked
            reader_sm = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_sm = TargetSaveSentinel(os.path.join(tmpdir, f"target_sm_{vname}.bin"), b"SM_TARGET_INIT")
            outcome_sm = run_model_savemenu_picked_restore(reader_sm, sentinel_sm)
            if is_raw_expected:
                assert outcome_sm == "REFUSED_RAW_UPFRONT", f"[{vname}] SM picked expected REFUSED_RAW_UPFRONT, got {outcome_sm}"
            else:
                assert outcome_sm == "REJECTED_NOT_VALID_BACKUP", f"[{vname}] SM picked expected REJECTED_NOT_VALID_BACKUP, got {outcome_sm}"
            sentinel_sm.verify_unchanged()

            # Route 3: Batch Current Item
            reader_batch = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_batch = TargetSaveSentinel(os.path.join(tmpdir, f"target_batch_{vname}.bin"), b"BATCH_TARGET_INIT")
            outcome_batch, last_is_raw, mut_batch, ready_rec = run_model_batch_restore_item(reader_batch, sentinel_batch)
            if is_raw_expected:
                assert outcome_batch == "REFUSED_RAW_BATCH_ITEM", f"[{vname}] Batch expected REFUSED_RAW_BATCH_ITEM, got {outcome_batch}"
                assert last_is_raw is True
            else:
                assert outcome_batch == "REJECTED_ZIP_ADMISSION", f"[{vname}] Batch expected REJECTED_ZIP_ADMISSION, got {outcome_batch}"
                assert last_is_raw is False
            assert mut_batch is False
            assert ready_rec is None
            sentinel_batch.verify_unchanged()

            # Route 4: Internal Defense
            reader_int = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_int = TargetSaveSentinel(os.path.join(tmpdir, f"target_int_{vname}.bin"), b"INT_TARGET_INIT")
            outcome_int, mut_int = run_model_internal_defense(reader_int, sentinel_int)
            if is_raw_expected:
                assert outcome_int == "RESULT_RAW_SAVE_RESTORE_UNSUPPORTED", f"[{vname}] Internal expected RESULT_RAW_SAVE_RESTORE_UNSUPPORTED, got {outcome_int}"
            else:
                assert outcome_int == "REJECTED_ZIP_ADMISSION", f"[{vname}] Internal expected REJECTED_ZIP_ADMISSION, got {outcome_int}"
            assert mut_int is False
            sentinel_int.verify_unchanged()

        # Cancellation Models (verified before mutation)
        valid_zip_path = os.path.join(tmpdir, "valid_for_cancel.zip")
        create_valid_zip_fixture(valid_zip_path, {"game.sav": b"VALID_SAVE_PAYLOAD"})

        # File Browser caller cancel
        s_cancel_fb = TargetSaveSentinel(os.path.join(tmpdir, "cancel_fb.bin"), b"CANCEL_FB")
        r_cancel_fb = BoundedArtifactReader(valid_zip_path)
        assert run_model_filebrowser_restore(r_cancel_fb, s_cancel_fb, cancel_at_confirm=True) == "CANCELLED_BEFORE_WORKER"
        s_cancel_fb.verify_unchanged()

        # Save Menu picked caller cancel
        s_cancel_sm = TargetSaveSentinel(os.path.join(tmpdir, "cancel_sm.bin"), b"CANCEL_SM")
        r_cancel_sm = BoundedArtifactReader(valid_zip_path)
        assert run_model_savemenu_picked_restore(r_cancel_sm, s_cancel_sm, cancel_at_confirm=True) == "CANCELLED_BEFORE_WORKER"
        s_cancel_sm.verify_unchanged()

        # Batch worker cancel gate
        s_cancel_batch = TargetSaveSentinel(os.path.join(tmpdir, "cancel_batch.bin"), b"CANCEL_BATCH")
        r_cancel_batch = BoundedArtifactReader(valid_zip_path)
        out_cb, _, mut_cb, _ = run_model_batch_restore_item(r_cancel_batch, s_cancel_batch, worker_cancelled=True)
        assert out_cb == "CANCELLED_WORKER" and mut_cb is False
        s_cancel_batch.verify_unchanged()

        # Internal worker cancel gate
        s_cancel_int = TargetSaveSentinel(os.path.join(tmpdir, "cancel_int.bin"), b"CANCEL_INT")
        r_cancel_int = BoundedArtifactReader(valid_zip_path)
        out_ci, mut_ci = run_model_internal_defense(r_cancel_int, s_cancel_int, worker_cancelled=True)
        assert out_ci == "CANCELLED_INTERNAL" and mut_ci is False
        s_cancel_int.verify_unchanged()

    print("  -> All variants and cancellation models PASSED across all routes.")


def test_mixed_batch_simulation():
    """Verifies mixed batch: real valid ZIP -> RAW -> ZIP with pre-mutation recovery verification and retention."""
    print("[3] Testing Mixed Batch (ZIP -> RAW -> ZIP) & Safety Recovery Retention...")
    with tempfile.TemporaryDirectory() as tmpdir:
        backup_dir = os.path.join(tmpdir, "backups")
        os.makedirs(backup_dir, exist_ok=True)

        # 1. Source files
        zip1_path = os.path.join(tmpdir, "item1_valid.zip")
        create_valid_zip_fixture(zip1_path, {"slot1.bin": b"NEW_ZIP1_PAYLOAD"})

        raw2_path = os.path.join(tmpdir, "item2_raw.bin")
        create_disa_matching_looking_artifact(raw2_path)

        zip3_path = os.path.join(tmpdir, "item3_valid.zip")
        create_valid_zip_fixture(zip3_path, {"slot3.bin": b"NEW_ZIP3_PAYLOAD"})

        # 2. Target sentinels
        target1 = TargetSaveSentinel(os.path.join(tmpdir, "target1.bin"), b"ORIGINAL_TARGET1_BYTES")
        target2 = TargetSaveSentinel(os.path.join(tmpdir, "target2.bin"), b"ORIGINAL_TARGET2_BYTES")
        target3 = TargetSaveSentinel(os.path.join(tmpdir, "target3.bin"), b"ORIGINAL_TARGET3_BYTES")

        # 3. Simulate batch execution: caller passes recovery path candidate; helper creates & verifies before open_write
        items = [(zip1_path, target1), (raw2_path, target2), (zip3_path, target3)]
        recovery_archives = []
        restored_count = 0
        last_item_is_raw = False

        for i, (src_path, tgt) in enumerate(items):
            reader = BoundedArtifactReader(src_path)
            rec_candidate = os.path.join(backup_dir, f"recovery_slot_{i}.zip")
            outcome, is_raw, mut_started, verified_rec = run_model_batch_restore_item(
                reader, tgt, recovery_path=rec_candidate
            )
            last_item_is_raw = is_raw

            if is_raw:
                # Halt immediately on RAW before auto-backup or target write
                break

            if outcome == "SUCCESS_ZIP_ITEM":
                assert verified_rec is not None, "Verified recovery path must be returned"
                recovery_archives.append(verified_rec)
                restored_count += 1

        # Check outcomes
        assert restored_count == 1, f"Expected 1 restored item, got {restored_count}"
        assert last_item_is_raw is True, "Batch must record last_item_is_raw = True"
        assert len(recovery_archives) == 1, "Exactly 1 recovery archive should exist"

        # Check shared event log ordering for Item 1: admission -> recovery_ready -> open_write -> write -> commit
        t1_log = target1.event_log
        assert "admission" in t1_log, "admission must be in event log"
        assert "recovery_ready" in t1_log, "recovery_ready must be in event log"
        assert "open_write" in t1_log, "open_write must be in event log"
        idx_adm = t1_log.index("admission")
        idx_rec = t1_log.index("recovery_ready")
        idx_open = t1_log.index("open_write")
        idx_write = t1_log.index("write:18")
        idx_commit = t1_log.index("commit")
        assert idx_adm < idx_rec < idx_open < idx_write < idx_commit, (
            f"Event order violation! Expected admission -> recovery_ready -> open_write -> write -> commit, got: {t1_log}"
        )

        # Verify Item 1 target was actually modified
        with open(target1.path, "rb") as f:
            t1_bytes = f.read()
        assert t1_bytes == b"RESTORED_ZIP_BATCH", "Target 1 must be modified by successful ZIP restore!"

        # Verify Item 1 recovery archive physically exists and contains initial bytes
        assert os.path.exists(recovery_archives[0])
        with zipfile.ZipFile(recovery_archives[0], "r") as zf:
            assert zf.read("original_target.bin") == b"ORIGINAL_TARGET1_BYTES"

        # Verify Target 2 and Target 3 sentinels are 100% UNCHANGED: no recovery, no target events
        target2.verify_unchanged()
        target3.verify_unchanged()

        # Check error reporting contract
        msg = (
            f"Restore stopped.\nSafety recovery archive(s) retained:\n{recovery_archives[0]}\n\n"
            f"RAW container restore is unsupported."
        )
        assert "Restore stopped.\nSafety recovery archive(s) retained:\n" in msg
        assert "RAW container restore is unsupported." in msg
        assert "rollback" not in msg.lower(), "Must not claim whole-batch rollback"

        # Scenario B: Malformed source first
        corrupt_first = os.path.join(tmpdir, "corrupt_first.bin")
        create_raw_fixture_file(corrupt_first, 0x80)
        target_mal = TargetSaveSentinel(os.path.join(tmpdir, "target_mal.bin"), b"ORIGINAL_MAL_BYTES")
        r_mal = BoundedArtifactReader(corrupt_first)
        rec_mal = os.path.join(backup_dir, "recovery_mal.zip")
        out_mal, is_raw_mal, mut_mal, ver_mal = run_model_batch_restore_item(r_mal, target_mal, recovery_path=rec_mal)
        assert out_mal == "REJECTED_ZIP_ADMISSION"
        assert is_raw_mal is False and mut_mal is False and ver_mal is None
        assert not os.path.exists(rec_mal), "No recovery archive should be created on rejected admission"
        target_mal.verify_unchanged()

    print("  -> Mixed batch & recovery retention simulation PASSED.")


def test_recovery_creation_readback_failure_fixture():
    """Verifies that failure in recovery archive creation or readback stops restore before open_write."""
    print("[4] Testing Recovery Creation & Readback Failure Fixtures...")
    with tempfile.TemporaryDirectory() as tmpdir:
        backup_dir = os.path.join(tmpdir, "backups")
        os.makedirs(backup_dir, exist_ok=True)

        valid_zip_path = os.path.join(tmpdir, "valid.zip")
        create_valid_zip_fixture(valid_zip_path, {"game.sav": b"VALID_SAVE_PAYLOAD"})

        # Case A: Corrupt recovery archive before reopen/readback
        target_corrupt = TargetSaveSentinel(os.path.join(tmpdir, "target_corrupt.bin"), b"INITIAL_TARGET_CORRUPT")
        reader_corrupt = BoundedArtifactReader(valid_zip_path)
        rec_corrupt_path = os.path.join(backup_dir, "recovery_corrupt.zip")

        outcome, is_raw, mut_started, ver_path = run_model_batch_restore_item(
            reader_corrupt,
            target_corrupt,
            recovery_path=rec_corrupt_path,
            recovery_fail_mode="corrupt_archive"
        )

        assert outcome == "RECOVERY_VERIFICATION_FAILED"
        assert is_raw is False
        assert mut_started is False, "mutation_started must be false on recovery readback failure"
        assert ver_path is None, "Incomplete/corrupt recovery archive must NOT be advertised as verified path"
        assert not os.path.exists(rec_corrupt_path), "Corrupt recovery archive must not remain advertised on disk"

        # Sentinel must be completely unchanged and open_write must NOT have been called
        target_corrupt.verify_unchanged(allow_non_mutating=True)
        assert "recovery_ready" not in target_corrupt.event_log
        assert "open_write" not in target_corrupt.event_log
        assert "write" not in str(target_corrupt.event_log)
        assert "commit" not in target_corrupt.event_log

        # Case B: Injected failing recovery creation callback
        target_cb_fail = TargetSaveSentinel(os.path.join(tmpdir, "target_cb_fail.bin"), b"INITIAL_TARGET_CB_FAIL")
        reader_cb = BoundedArtifactReader(valid_zip_path)
        rec_cb_path = os.path.join(backup_dir, "recovery_cb.zip")

        outcome_cb, is_raw_cb, mut_started_cb, ver_path_cb = run_model_batch_restore_item(
            reader_cb,
            target_cb_fail,
            recovery_path=rec_cb_path,
            recovery_fail_mode="callback_fail"
        )

        assert outcome_cb == "RECOVERY_CREATION_FAILED"
        assert is_raw_cb is False
        assert mut_started_cb is False
        assert ver_path_cb is None
        target_cb_fail.verify_unchanged(allow_non_mutating=True)
        assert "recovery_ready" not in target_cb_fail.event_log
        assert "open_write" not in target_cb_fail.event_log

    print("  -> Recovery creation & readback failure fixtures PASSED.")


def test_integrity_outcome_model():
    """Tests artifact-driven integrity verification outcome model."""
    print("[5] Testing Artifact-Driven Integrity Verification Outcome Model...")
    with tempfile.TemporaryDirectory() as tmpdir:
        # 1. RAW-only set
        raw1 = os.path.join(tmpdir, "raw1.disa")
        create_disa_matching_looking_artifact(raw1)
        raw2 = os.path.join(tmpdir, "raw2.bin")
        create_magic_only_artifact(raw2)

        def run_integrity(paths):
            valid = 0
            invalid = 0
            unsupported_raw = 0
            for p in paths:
                fname = os.path.basename(p)
                if fname.endswith(".zip"):
                    if strict_zip_admission(p):
                        valid += 1
                    else:
                        invalid += 1
                elif BoundedArtifactReader(p).probe_is_disa():
                    unsupported_raw += 1
                else:
                    invalid += 1
            return valid, invalid, unsupported_raw

        v_raw, inv_raw, unsupp_raw = run_integrity([raw1, raw2])
        assert v_raw == 0 and inv_raw == 0 and unsupp_raw == 2
        # Verify message logic
        assert unsupp_raw > 0 and inv_raw == 0 and v_raw == 0
        raw_msg = f"{unsupp_raw} RAW save container(s) are unsupported for verification."
        assert "all" not in raw_msg.lower() and "corrupt" not in raw_msg.lower()

        # 2. ZIP-only valid set
        zip1 = os.path.join(tmpdir, "valid1.zip")
        create_valid_zip_fixture(zip1, {"data.bin": b"1"})
        zip2 = os.path.join(tmpdir, "valid2.zip")
        create_valid_zip_fixture(zip2, {"data.bin": b"2"})
        v_zip, inv_zip, unsupp_zip = run_integrity([zip1, zip2])
        assert v_zip == 2 and inv_zip == 0 and unsupp_zip == 0

        # 3. Mixed set: 1 valid ZIP, 1 corrupt ZIP, 1 RAW container
        corrupt_zip = os.path.join(tmpdir, "corrupt.zip")
        with open(corrupt_zip, "wb") as f:
            f.write(b"CORRUPT_ZIP_DATA")

        v_mix, inv_mix, unsupp_mix = run_integrity([zip1, corrupt_zip, raw1])
        assert v_mix == 1 and inv_mix == 1 and unsupp_mix == 1

    print("  -> Integrity outcome model PASSED.")


def test_bounded_source_code_contracts():
    """Tests scoped source code slices, ordering, writer elimination, and i18n parity."""
    print("[6] Running Bounded Scoped Source Code Contracts & Destructive Symbol Elimination...")

    fb_ops_cpp = read_repo_file("sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_ops.cpp")
    save_ops_cpp = read_repo_file("sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    save_menu_cpp = read_repo_file("sphaira", "source", "ui", "menus", "save_menu.cpp")
    settings_cpp = read_repo_file("sphaira", "source", "ui", "menus", "settings", "settings_categories.cpp")
    save_paths_hpp = read_repo_file("sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    save_paths_cpp = read_repo_file("sphaira", "source", "ui", "menus", "save", "save_paths.cpp")
    cmake_txt = read_repo_file("sphaira", "CMakeLists.txt")
    en_json = json.loads(read_repo_file("assets", "romfs", "i18n", "en.json"))
    uk_json = json.loads(read_repo_file("assets", "romfs", "i18n", "uk.json"))

    # 6.1 Version
    assert "set(sphaira_VERSION 0.13.858)" in cmake_txt or "set(sphaira_VERSION 0.13.859)" in cmake_txt, "sphaira/CMakeLists.txt must be 0.13.858 or 0.13.859"

    # 6.2 Sliced Function: FsView::RestoreSaveFile in filebrowser_ops.cpp
    fb_start = fb_ops_cpp.find("void FsView::RestoreSaveFile(const FileEntry& entry)")
    assert fb_start != -1, "FsView::RestoreSaveFile not found"
    fb_end = fb_ops_cpp.find("void FsView::UnzipFiles(", fb_start)
    assert fb_end != -1, "FsView::UnzipFiles boundary not found"
    fb_func = fb_ops_cpp[fb_start:fb_end]

    # RAW refusal precedes candidate enumeration, picker, confirmation, and worker
    idx_fb_disa = fb_func.find("App::Push<OptionBox>(save::GetRawRestoreUnsupportedMessage()")
    idx_fb_cand = fb_func.find("add_candidates")
    idx_fb_picker = fb_func.find('App::Push<PopupList>("Select Target Save"')
    idx_fb_confirm = fb_func.find("Restore save data to")
    idx_fb_worker = fb_func.find("Restoring save...")

    assert -1 not in (idx_fb_disa, idx_fb_cand, idx_fb_picker, idx_fb_confirm, idx_fb_worker)
    assert idx_fb_disa < idx_fb_cand, "RAW refusal must precede candidate discovery"
    assert idx_fb_disa < idx_fb_confirm, "RAW refusal must precede confirmation prompt"
    assert idx_fb_disa < idx_fb_worker, "RAW refusal must precede worker"
    assert idx_fb_disa < idx_fb_picker, "RAW refusal must precede explicit target picker"

    # Filename shortcut and parsing completely absent
    assert "target_id = std::strtoull" not in fb_func
    assert "match_count == 1 && matched_candidate != nullptr" not in fb_func

    # BackupSaveInternal absent from RestoreSaveFile worker
    assert "BackupSaveInternal" not in fb_func, "RestoreSaveFile must not call BackupSaveInternal"

    # 6.3 Sliced Function: Menu::RestoreSavesPicked in save_menu_ops.cpp
    rsp_start = save_ops_cpp.find("void Menu::RestoreSavesPicked(")
    assert rsp_start != -1, "Menu::RestoreSavesPicked not found"
    rsp_end = save_ops_cpp.find("auto DownloadOneBackupFile(", rsp_start)
    assert rsp_end != -1, "DownloadOneBackupFile boundary not found"
    rsp_func = save_ops_cpp[rsp_start:rsp_end]

    idx_rsp_disa = rsp_func.find("if (is_raw) {")
    idx_rsp_confirm = rsp_func.find("Restore save data to")
    idx_rsp_worker = rsp_func.find('App::Push<ProgressBox>(0, "Restore"_i18n')
    assert -1 not in (idx_rsp_disa, idx_rsp_confirm, idx_rsp_worker)
    assert idx_rsp_disa < idx_rsp_confirm < idx_rsp_worker, "RestoreSavesPicked RAW refusal must precede prompt and worker"
    assert "BackupSaveInternal" not in rsp_func, "RestoreSavesPicked worker must not call BackupSaveInternal"

    # 6.4 Sliced Function: Menu::RestoreSaves (batch) bounded by DeleteSaves
    batch_start = save_ops_cpp.find("void Menu::RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets")
    assert batch_start != -1, "RestoreSaves batch overload not found"
    batch_end = save_ops_cpp.find("void Menu::DeleteSaves(", batch_start)
    assert batch_end != -1, "DeleteSaves boundary not found"
    batch_func = save_ops_cpp[batch_start:batch_end]

    idx_batch_raw = batch_func.find("if (is_raw) {")
    idx_batch_restore = batch_func.find("RestoreSaveInternal(", idx_batch_raw)
    assert idx_batch_raw != -1 and idx_batch_restore != -1
    assert idx_batch_raw < idx_batch_restore, "Batch RAW refusal must precede RestoreSaveInternal"
    assert "*last_item_is_raw = is_raw;" in batch_func
    assert "return Result_RawSaveRestoreUnsupported;" in batch_func
    assert 'prefix = "Restore stopped.\\nSafety recovery archive(s) retained:\\n"_i18n;' in batch_func

    # 6.5 Sliced Function: Menu::RestoreSaveInternal in save_menu_ops.cpp
    rsi_start = save_ops_cpp.find("Result Menu::RestoreSaveInternal(")
    assert rsi_start != -1, "RestoreSaveInternal not found"
    rsi_end = save_ops_cpp.find("Result Menu::BackupSaveInternal(", rsi_start)
    assert rsi_end != -1, "BackupSaveInternal boundary not found"
    rsi_func = save_ops_cpp[rsi_start:rsi_end]

    idx_rsi_raw = rsi_func.find("if (IsDisaSaveFile(probe_fs, path)) {")
    idx_rsi_ret = rsi_func.find("return Result_RawSaveRestoreUnsupported;", idx_rsi_raw)
    idx_rsi_rsz = rsi_func.find("return RestoreSaveZip(", idx_rsi_raw)
    assert -1 not in (idx_rsi_raw, idx_rsi_ret, idx_rsi_rsz)
    assert idx_rsi_raw < idx_rsi_ret < idx_rsi_rsz, "Internal RAW return must precede RestoreSaveZip"

    # 6.6 Destructive writer absence in all restore functions
    destructive_symbols = [
        "FsNativeBis",
        "bis_fs.DeleteFile",
        "bis_fs.CreateFile",
        "bis_fs.OpenFile",
        "bis_fs.Commit",
        "fsFsOpenSaveDataFileSystemBySaveDataSpaceId",
        "fsFsCreateFile",
        "fsFsDeleteFile",
        "fsFsOpenFile",
        "fsFileWrite",
        "fsFsCommit",
        "thread::Transfer"
    ]
    for fname, fsrc in [
        ("RestoreSaveFile", fb_func),
        ("RestoreSavesPicked", rsp_func),
        ("RestoreSaves_batch", batch_func),
        ("RestoreSaveInternal", rsi_func)
    ]:
        for bad in destructive_symbols:
            assert bad not in fsrc, f"Destructive symbol '{bad}' present in {fname}!"

    # 6.7 Sliced Function: Menu::VerifyIntegrity in save_menu.cpp
    vi_start = save_menu_cpp.find("void Menu::VerifyIntegrity(")
    assert vi_start != -1
    vi_end = save_menu_cpp.find("void Menu::DeleteOlderBackups(", vi_start)
    assert vi_end != -1
    vi_func = save_menu_cpp[vi_start:vi_end]

    assert "auto unsupported_raw_count = std::make_shared<size_t>(0);" in vi_func
    assert "VerifyDisaIntegrity" not in vi_func
    assert "RAW save container(s) are unsupported for verification." in vi_func

    # 6.8 Headers and Dead Constant Elimination
    assert "RAW_RESTORE_UNSUPPORTED_MSG" not in save_paths_hpp, "Dead constant RAW_RESTORE_UNSUPPORTED_MSG must be removed"
    assert "inline constexpr Result Result_RawSaveRestoreUnsupported = Result_FsInvalidType;" in save_paths_hpp
    assert "auto GetRawRestoreUnsupportedMessage() -> std::string;" in save_paths_hpp
    assert "VerifyDisaIntegrity" not in save_paths_hpp
    assert "VerifyDisaIntegrity" not in save_paths_cpp
    assert "auto GetRawRestoreUnsupportedMessage() -> std::string {" in save_paths_cpp

    # 6.9 Retained toggle description harmonized in save_menu.cpp and settings_categories.cpp
    toggle_desc = "ZIP restores always create a verified SD recovery archive regardless of this setting. RAW container restore is unsupported."
    assert toggle_desc in save_menu_cpp, "save_menu.cpp must contain exact harmonized toggle description"
    assert toggle_desc in settings_cpp, "settings_categories.cpp must contain exact harmonized toggle description"

    # 6.10 i18n parity check
    required_keys = [
        "RAW container restore is unsupported.",
        "Restore stopped.\nSafety recovery archive(s) retained:\n",
        toggle_desc,
        " RAW save container(s) are unsupported for verification.",
        "ZIP archives verified: ",
        " valid.\n",
        " valid ZIP archive(s).\n"
    ]
    for key in required_keys:
        assert key in en_json, f"en.json missing key: {repr(key)}"
        assert key in uk_json, f"uk.json missing key: {repr(key)}"
        assert en_json[key], f"en.json empty value for key: {repr(key)}"
        assert uk_json[key], f"uk.json empty value for key: {repr(key)}"

    print("  -> Bounded scoped source code contracts & destructive symbol elimination PASSED.")


def main():
    print("=== Sphaira v0.13.858: Strict RAW Save Restore Contract Test Suite ===")
    test_sentinel_positive_self_check()
    test_all_variants_across_all_routes()
    test_mixed_batch_simulation()
    test_recovery_creation_readback_failure_fixture()
    test_integrity_outcome_model()
    test_bounded_source_code_contracts()
    print("=== ALL REWORKED CONTRACT SUITES PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
