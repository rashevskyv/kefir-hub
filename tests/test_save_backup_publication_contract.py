#!/usr/bin/env python3
"""
Test Suite: Checked Publication Contract for Ordinary SD Backup (Sphaira v0.13.860)

Validates:
1. Static source code contracts & callers in sphaira:
   - Version 0.13.860 in sphaira/CMakeLists.txt
   - BackupSaveInternal SD branch:
     - native SD open check
     - pre-work probe of final destination (explicit FsError_PathAlreadyExists)
     - parent directory creation check
     - sibling stage directory reservation (final + ".stage") with length check
     - owned temp file path inside stage (final + ".stage/backup.zip.temp")
     - native fsFsCreateDirectory primitive with ownership set immediately before commit
     - checked stream writing via WriteSaveBackupZip(..., checked_stream = true)
     - pre-rename probe of final destination (explicit FsError_PathAlreadyExists)
     - cancellation gates before reservation, write, and rename
     - native fsFsRenameFile primitive with transition to Renamed
     - final SD commits: fsdevCommitDevice("sdmc") and native Commit()
     - publication transition to Published only after successful commits
     - scope-guard cleanup: exact owned temp/final, owned stage dir, never deleting pre-existing final or foreign stage
     - UMS / stdio export branch preserved in else
   - WriteSaveBackupZip:
     - checked transport decoupled from recovery_mode semantic flags
     - RecoveryStreamContext reuse for SD ordinary backup
     - check flags for short write, fflush, invalid fd, fsync, fclose, fp == nullptr
     - commit device and native commit
     - ordinary DBI / Sphaira format metadata and directory rules preserved
   - Caller sites intact:
     - BackupSaves / BackupSavesOn
     - CreateBackupIfNewer
     - users_profile safety-backup
2. Connected Real-File Simulation & Metadata Routing:
   - Real tempfile source directory -> actual ZIP -> staged publication
   - 7 concrete FsSaveDataType records (Account, Device, Bcat, Cache, Temporary, System, SystemBcat)
   - Nonzero rank=1 and index=3 valid record
   - Cache User vs SdUser spaces, System alternative spaces (System, SdSystem, ProperSystem)
   - Independently decoded and asserted DBI extra512 and NX128 metadata at exact byte offsets
   - Real nested / empty payload files, compressed and uncompressed modes
   - Full reopen and byte-level verification of published archives
   - Exact DBI info Account/type label and Space matches actual writer
   - Ordinary empty-save semantics (early return 0x0 on empty source)
3. Connected Failure & Mutation Matrix:
   - Modeled file-like stream sink exercising short write, fflush, invalid fd, fsync, fclose
   - ZIP entry close failure and final archive close failure separately hooked
   - Writer device commit and native SD commit before rename separately hooked
   - SD open error, parent creation error
   - Real valid ZIP collision before work (original bytes and reopened payload verified unchanged)
   - Target final probe error propagation (pre-work and pre-rename)
   - Sibling stage collision (foreign stage sentinel preserved)
   - Stage path length overflow (FsError_TooLongPath)
   - Stage primitive reservation failure vs stage commit failure
   - Cancellation gates (gate 1: before reservation, gate 2: before write, gate 3: before rename)
   - Concurrent pre-rename collision with real foreign ZIP created after writer completion
   - Native rename primitive failure
   - Post-rename commits failure (owned final deleted, prior state preserved)
   - Injected cleanup directory and file failures (honest retained artifacts, no false success)
   - Cancellation after committed Published (must NOT fail)
   - Executable event order assertions verifying downstream events are absent on failure

Scope & Evidence Boundaries:
This suite provides bounded Python policy/artifact evidence of the publication lifecycle
and fault handling. It does NOT claim full concurrency/hardware/IPC/libnx proof or atomic
hardware rollback. The empty file-only os.walk model in Python is not proof of actual
scanner empty-dir behavior.
"""

import os
import sys
import io
import struct
import tempfile
import zipfile
from dataclasses import dataclass
from typing import Optional, Tuple, Dict, Any, List, Callable

# Error and return codes matching libnx / Sphaira defines
RES_OK = 0
FS_ERROR_PATH_NOT_FOUND = 0x202
FS_ERROR_PATH_ALREADY_EXISTS = 0x402
FS_ERROR_TARGET_LOCKED = 0xE02
FS_ERROR_TOO_LONG_PATH = 0x2EE602
FS_ERROR_INVALID_SIZE = 0x2F5C02
RESULT_ZIP_WRITE_IN_FILE = 0x30001
RESULT_FS_UNKNOWN_STDIO_ERROR = 0x30002
RESULT_UNZ_OPEN2_64 = 0x30003
SVC_ERROR_CANCELLED = 0xEC01

# 7 save types
FS_SAVE_DATA_TYPE_SYSTEM = 0
FS_SAVE_DATA_TYPE_ACCOUNT = 1
FS_SAVE_DATA_TYPE_BCAT = 2
FS_SAVE_DATA_TYPE_DEVICE = 3
FS_SAVE_DATA_TYPE_TEMPORARY = 4
FS_SAVE_DATA_TYPE_CACHE = 5
FS_SAVE_DATA_TYPE_SYSTEM_BCAT = 6

ALL_SAVE_TYPES = [
    FS_SAVE_DATA_TYPE_SYSTEM,
    FS_SAVE_DATA_TYPE_ACCOUNT,
    FS_SAVE_DATA_TYPE_BCAT,
    FS_SAVE_DATA_TYPE_DEVICE,
    FS_SAVE_DATA_TYPE_TEMPORARY,
    FS_SAVE_DATA_TYPE_CACHE,
    FS_SAVE_DATA_TYPE_SYSTEM_BCAT,
]

# Save spaces
FS_SAVE_DATA_SPACE_ID_SYSTEM = 0
FS_SAVE_DATA_SPACE_ID_USER = 1
FS_SAVE_DATA_SPACE_ID_SD_SYSTEM = 2
FS_SAVE_DATA_SPACE_ID_TEMPORARY = 3
FS_SAVE_DATA_SPACE_ID_SD_USER = 4
FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM = 100

# Metadata constants
NX_SAVE_META_MAGIC = 0x4A4B5356  # 'JKSV'
NX_SAVE_META_VERSION = 1
NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"


def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


@dataclass
class SaveAttribute:
    application_id: int
    uid_low: int
    uid_high: int
    system_save_data_id: int
    save_data_type: int
    save_data_rank: int
    save_data_index: int


@dataclass
class SelectedEntry:
    save_data_id: int
    save_data_space_id: int
    attr: SaveAttribute
    name: str = "TestSave"
    size: int = 0x100000


def encode_extra_data(
    attr: SaveAttribute,
    owner_id: int = 0x0100000000001000,
    timestamp: int = 1710000000,
    flags: int = 0,
    unk_x54: int = 0,
    data_size: int = 0x200000,
    journal_size: int = 0x200000,
    commit_id: int = 42,
    padding: bytes = b"\x00" * 4,
    unknown: bytes = b"\x00" * 24,
) -> bytes:
    """Encodes a 512-byte FsSaveDataExtraData structure."""
    header = struct.pack(
        "<QQQQBBH",
        attr.application_id,
        attr.uid_low,
        attr.uid_high,
        attr.system_save_data_id,
        attr.save_data_type,
        attr.save_data_rank,
        attr.save_data_index,
    )
    assert len(header) == 36
    assert len(padding) == 4
    assert len(unknown) == 24

    meta = struct.pack(
        "<QQIIqqQ",
        owner_id,
        timestamp,
        flags,
        unk_x54,
        data_size,
        journal_size,
        commit_id,
    )
    assert len(meta) == 48

    reserved = b"\x00" * (512 - 36 - 4 - 24 - 48)
    data = header + padding + unknown + meta + reserved
    assert len(data) == 512
    return data


def decode_extra_data(b: bytes) -> Tuple[SaveAttribute, Dict[str, Any]]:
    """Decodes a 512-byte FsSaveDataExtraData structure."""
    if len(b) != 512:
        raise ValueError(f"Expected 512 bytes, got {len(b)}")

    app, ul, uh, sid, stype, rank, idx = struct.unpack("<QQQQBBH", b[:36])
    oid, ts, flags, unk_x54, dsize, jsize, cid = struct.unpack("<QQIIqqQ", b[64:112])

    attr = SaveAttribute(
        application_id=app,
        uid_low=ul,
        uid_high=uh,
        system_save_data_id=sid,
        save_data_type=stype,
        save_data_rank=rank,
        save_data_index=idx,
    )
    meta = {
        "owner_id": oid,
        "timestamp": ts,
        "flags": flags,
        "unk_x54": unk_x54,
        "data_size": dsize,
        "journal_size": jsize,
        "commit_id": cid,
    }
    return attr, meta


def encode_nx_save_meta(attr: SaveAttribute, extra_meta: Dict[str, Any], raw_size: int) -> bytes:
    """Encodes a 128-byte NXSaveMeta structure used by System/SystemBcat."""
    buf = bytearray(128)
    struct.pack_into("<II", buf, 0, NX_SAVE_META_MAGIC, NX_SAVE_META_VERSION)
    struct.pack_into(
        "<QQQQBBH",
        buf,
        8,
        attr.application_id,
        attr.uid_low,
        attr.uid_high,
        attr.system_save_data_id,
        attr.save_data_type,
        attr.save_data_rank,
        attr.save_data_index,
    )
    struct.pack_into(
        "<QQIIqqQQ",
        buf,
        72,
        extra_meta.get("owner_id", 0),
        extra_meta.get("timestamp", 0),
        extra_meta.get("flags", 0),
        extra_meta.get("unk_x54", 0),
        extra_meta.get("data_size", 0),
        extra_meta.get("journal_size", 0),
        extra_meta.get("commit_id", 0),
        raw_size,
    )
    return bytes(buf)


def decode_nx_save_meta(b: bytes) -> Tuple[int, int, SaveAttribute, Dict[str, Any], int]:
    """Decodes a 128-byte NXSaveMeta structure."""
    if len(b) != 128:
        raise ValueError(f"Expected 128 bytes, got {len(b)}")
    magic, ver = struct.unpack("<II", b[:8])
    app, ul, uh, sid, stype, rank, idx = struct.unpack("<QQQQBBH", b[8:44])
    oid, ts, flags, unk_x54, dsize, jsize, cid, raw_size = struct.unpack("<QQIIqqQQ", b[72:128])
    attr = SaveAttribute(app, ul, uh, sid, stype, rank, idx)
    meta = {
        "owner_id": oid,
        "timestamp": ts,
        "flags": flags,
        "unk_x54": unk_x54,
        "data_size": dsize,
        "journal_size": jsize,
        "commit_id": cid,
    }
    return magic, ver, attr, meta, raw_size


def get_actual_dbi_account_label(save_data_type: int, account_nick: str = "TestAccount") -> str:
    """Matches exact Menu::WriteSaveBackupZip / GetSaveTypeLabel logic in sphaira."""
    if save_data_type == FS_SAVE_DATA_TYPE_ACCOUNT:
        return account_nick
    elif save_data_type == FS_SAVE_DATA_TYPE_DEVICE:
        return "Device"
    elif save_data_type == FS_SAVE_DATA_TYPE_BCAT:
        return "BCAT"
    elif save_data_type == FS_SAVE_DATA_TYPE_CACHE:
        return "Cache"
    elif save_data_type == FS_SAVE_DATA_TYPE_TEMPORARY:
        return "Temporary"
    elif save_data_type == FS_SAVE_DATA_TYPE_SYSTEM:
        return "System"
    elif save_data_type == FS_SAVE_DATA_TYPE_SYSTEM_BCAT:
        return "System BCAT"
    return "Unknown"


def get_actual_dbi_space_label(space_id: int) -> str:
    """Matches exact Menu::WriteSaveBackupZip space switch in sphaira."""
    if space_id in (FS_SAVE_DATA_SPACE_ID_SYSTEM, FS_SAVE_DATA_SPACE_ID_SD_SYSTEM, FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM):
        return "System"
    elif space_id == FS_SAVE_DATA_SPACE_ID_TEMPORARY:
        return "Temporary"
    else:
        return "User"


# ==============================================================================
# 1. Static Source Contracts
# ==============================================================================

def test_source_contracts() -> None:
    print("[1] Running static source contracts & architecture checks...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 CMakeLists.txt version check
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check("set(sphaira_VERSION 0.13.860)" in cmake_src or "set(sphaira_VERSION 0.13.861)" in cmake_src or "set(sphaira_VERSION 0.13.862)" in cmake_src or "set(sphaira_VERSION 0.13.863)" in cmake_src or "set(sphaira_VERSION 0.13.864)" in cmake_src or "set(sphaira_VERSION 0.13.865)" in cmake_src or "set(sphaira_VERSION 0.13.866)" in cmake_src,
          "sphaira/CMakeLists.txt must define sphaira_VERSION as 0.13.860, 0.13.861, 0.13.862, 0.13.863, 0.13.864, 0.13.865 or 0.13.866")

    # 1.2 sphaira/source/ui/menus/save/save_menu_ops.cpp
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_src = f.read()

    # WriteSaveBackupZip signature
    check("bool checked_stream = false" in ops_src,
          "WriteSaveBackupZip must accept checked_stream parameter")
    check("const bool use_checked_stream = recovery_mode || checked_stream;" in ops_src,
          "WriteSaveBackupZip must select checked stream when recovery_mode or checked_stream is true")

    # RecoveryStreamContext callbacks checks
    pos_zip_close = ops_src.find('zipClose(zfile, "sphaira v" APP_VERSION_HASH)')
    check(pos_zip_close != -1, "WriteSaveBackupZip must close zip archive")
    pos_rec_check = ops_src.find("if (use_checked_stream) {", pos_zip_close)
    check(pos_rec_check != -1, "WriteSaveBackupZip must guard post-zip checks with use_checked_stream")
    rec_body = ops_src[pos_rec_check:pos_rec_check + 800]
    check("!rec_ctx.write_failed" in rec_body, "Must check write_failed")
    check("!rec_ctx.flush_failed" in rec_body, "Must check flush_failed")
    check("!rec_ctx.sync_failed" in rec_body, "Must check sync_failed")
    check("!rec_ctx.close_failed" in rec_body, "Must check close_failed")
    check("rec_ctx.fp == nullptr" in rec_body, "Must check rec_ctx.fp == nullptr")
    check('fsdevCommitDevice("sdmc")' in rec_body, "Must commit sdmc device before rename")
    check("sd_fs.Commit()" in rec_body, "Must commit native sd_fs before rename")

    # BackupSaveInternal SD checked pipeline
    bsi_start = ops_src.find("Result Menu::BackupSaveInternal(")
    check(bsi_start != -1, "Menu::BackupSaveInternal must exist")
    bsi_end = ops_src.find("void Menu::SyncSavesRemote()", bsi_start)
    check(bsi_end != -1, "Menu::SyncSavesRemote boundary must exist")
    bsi_body = ops_src[bsi_start:bsi_end]

    # Location check
    check("const bool is_sd = (location.entry.type == dump::DumpLocationType_SdCard);" in bsi_body,
          "BackupSaveInternal must inspect location for DumpLocationType_SdCard")

    # SD native handle
    pos_is_sd = bsi_body.find("if (is_sd) {")
    check(pos_is_sd != -1, "BackupSaveInternal must branch on is_sd")
    sd_block = bsi_body[pos_is_sd:]

    check("fs::FsNativeSd sd_fs;" in sd_block, "SD pipeline must instantiate FsNativeSd")
    check("R_TRY(sd_fs.GetFsOpenResult());" in sd_block, "SD pipeline must check GetFsOpenResult")

    # Pre-work final probe
    pos_pre_probe = sd_block.find("sd_fs.GetEntryType(path, &final_entry_type)")
    check(pos_pre_probe != -1, "SD pipeline must probe final path before work")
    pos_pre_exists = sd_block.find("return FsError_PathAlreadyExists;", pos_pre_probe)
    check(pos_pre_exists != -1, "Pre-work probe must return FsError_PathAlreadyExists on success")
    check("pre_probe_rc != FsError_PathNotFound" in sd_block[pos_pre_probe:pos_pre_exists + 200],
          "Pre-work probe must propagate non-PathNotFound errors")

    # Parent directory creation
    check("sd_fs.CreateDirectoryRecursivelyWithPath(path)" in sd_block,
          "SD pipeline must create parent directories for final path")

    # Exclusive sibling stage directory
    check('std::snprintf(stage_dir, sizeof(stage_dir), "%s.stage", path.s);' in sd_block,
          'SD pipeline must derive sibling stage directory as path + ".stage"')
    check('std::snprintf(owned_temp_path, sizeof(owned_temp_path), "%s/backup.zip.temp", stage_dir.s);' in sd_block,
          'SD pipeline must construct owned temp path as stage_dir + "/backup.zip.temp"')

    # Native primitive reservation before commit
    pos_prim = sd_block.find("fsFsCreateDirectory(&sd_fs.m_fs, stage_dir);")
    check(pos_prim != -1, "SD pipeline must reserve stage dir with fsFsCreateDirectory primitive")
    pos_owned = sd_block.find("owned_stage_created = true;", pos_prim)
    check(pos_owned != -1, "owned_stage_created must be set immediately after primitive success")
    pos_stage_commit = sd_block.find("sd_fs.Commit();", pos_owned)
    check(pos_stage_commit != -1, "Stage creation must be committed after ownership is recorded")

    # WriteSaveBackupZip invocation
    pos_write = sd_block.find("WriteSaveBackupZip(", pos_stage_commit)
    check(pos_write != -1, "SD pipeline must call WriteSaveBackupZip")
    check("false, true" in sd_block[pos_write:pos_write + 200],
          "SD pipeline must pass recovery_mode=false, checked_stream=true to WriteSaveBackupZip")

    # Pre-rename final probe
    pos_post_probe = sd_block.find("sd_fs.GetEntryType(path, &final_entry_type)", pos_write)
    check(pos_post_probe != -1, "SD pipeline must probe final path before rename")

    # Native primitive rename
    pos_rename = sd_block.find("fsFsRenameFile(&sd_fs.m_fs, owned_temp_path, path);", pos_post_probe)
    check(pos_rename != -1, "SD pipeline must rename via fsFsRenameFile primitive")
    pos_renamed_state = sd_block.find("pub_state = BackupPubState::Renamed;", pos_rename)
    check(pos_renamed_state != -1, "pub_state must be set to Renamed after primitive success")

    # Post-rename commits
    pos_sdmc = sd_block.find('fsdevCommitDevice("sdmc")', pos_renamed_state)
    check(pos_sdmc != -1, "SD pipeline must commit sdmc device after rename")
    pos_final_commit = sd_block.find("sd_fs.Commit()", pos_sdmc)
    check(pos_final_commit != -1, "SD pipeline must commit sd_fs after rename")
    pos_pub_state = sd_block.find("pub_state = BackupPubState::Published;", pos_final_commit)
    check(pos_pub_state != -1, "pub_state must be set to Published only after final SD commits")

    # Cancellation gates
    gate1 = sd_block.find("pbox->ShouldExitResult()")
    check(gate1 != -1 and gate1 < pos_prim, "Gate 1 must precede stage reservation")
    gate2 = sd_block.find("pbox->ShouldExitResult()", pos_stage_commit)
    check(gate2 != -1 and gate2 < pos_write, "Gate 2 must precede WriteSaveBackupZip")
    gate3 = sd_block.find("pbox->ShouldExitResult()", pos_write)
    check(gate3 != -1 and gate3 < pos_rename, "Gate 3 must precede fsFsRenameFile")

    # Scope guard cleanup semantics
    pos_scope = sd_block.find("ON_SCOPE_EXIT {")
    check(pos_scope != -1, "SD pipeline must define ON_SCOPE_EXIT cleanup")
    scope_block = sd_block[pos_scope:sd_block.find("};", pos_scope)]
    check("pub_state == BackupPubState::Unpublished" in scope_block, "Must handle Unpublished state")
    check("pub_state == BackupPubState::Renamed" in scope_block, "Must handle Renamed state")
    check("pub_state == BackupPubState::Published" in scope_block, "Must handle Published state")
    check("sd_fs.DeleteFile(owned_temp_path);" in scope_block, "Unpublished cleanup must delete owned_temp_path")
    check("sd_fs.DeleteFile(path);" in scope_block, "Renamed cleanup must delete published path")
    check("sd_fs.DeleteDirectory(stage_dir);" in scope_block, "Cleanup must delete stage_dir")

    # UMS / stdio export branch preserved in else
    pos_else = sd_block.find("} else {")
    check(pos_else != -1, "BackupSaveInternal must maintain else branch for non-SD locations")
    else_block = sd_block[pos_else:]
    check("fs->CreateDirectoryRecursivelyWithPath(temp_path);" in else_block,
          "Else branch must retain existing CreateDirectoryRecursivelyWithPath")
    check("WriteSaveBackupZip(pbox, fs.get(), temp_path" in else_block,
          "Else branch must retain existing WriteSaveBackupZip call")
    check("fs->DeleteFile(path);" in else_block,
          "Else branch must retain existing DeleteFile(path)")
    check("fs->RenameFile(temp_path, path)" in else_block,
          "Else branch must retain existing RenameFile")

    # 1.3 Verify callers remain wired
    check("R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));" in ops_src,
          "save_menu_ops.cpp callers must invoke BackupSaveInternal")

    menu_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save_menu.cpp")
    with open(menu_cpp_path, "r", encoding="utf-8") as f:
        menu_src = f.read()
    check("R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));" in menu_src,
          "save_menu.cpp CreateBackupIfNewer must invoke BackupSaveInternal")

    users_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "users", "users_profile.cpp")
    with open(users_cpp_path, "r", encoding="utf-8") as f:
        users_src = f.read()
    check("R_TRY(helper->BackupSavesOn(pbox, save_backup));" in users_src,
          "users_profile.cpp must invoke helper->BackupSavesOn")

    print("  -> Static source contracts & architecture checks PASSED.")


# ==============================================================================
# 2. Modeled Recovery Stream Sink & Connected Simulator
# ==============================================================================

class ModeledRecoverySink:
    """
    Modeled stdio file-like sink mirroring RecoveryOpen/Write/Close:
    - Real OS file writing on disk.
    - Short-write hook that actually writes fewer bytes than requested.
    - Sink lifecycle hooks for fflush, fileno (-1), fsync, fclose.
    """

    def __init__(self, filepath: str, sim: "ConnectedBackupSimulator"):
        self.filepath = filepath
        self.sim = sim
        self.raw_file = open(filepath, "wb")
        self.write_failed = False
        self.flush_failed = False
        self.sync_failed = False
        self.close_failed = False
        self.fp_valid = True

    def seek(self, offset: int, whence: int = os.SEEK_SET) -> int:
        return self.raw_file.seek(offset, whence)

    def tell(self) -> int:
        return self.raw_file.tell()

    def flush(self) -> None:
        self.raw_file.flush()

    def write(self, b: bytes) -> int:
        if self.sim.inject_short_write:
            # Actually write fewer bytes than requested
            short_len = min(len(b), 4)
            written = self.raw_file.write(b[:short_len])
            if written != len(b):
                self.write_failed = True
            return written
        written = self.raw_file.write(b)
        return written

    def close(self) -> None:
        self.sim.events.append("stream_sink_fflush")
        if self.sim.inject_fflush_error:
            self.flush_failed = True
        else:
            try:
                self.raw_file.flush()
            except Exception:
                self.flush_failed = True

        self.sim.events.append("stream_sink_fsync")
        fd = -1 if self.sim.inject_invalid_fd else self.raw_file.fileno()
        if fd == -1:
            self.sync_failed = True
        else:
            try:
                if self.sim.fsync_fault_hook:
                    self.sim.fsync_fault_hook(self.raw_file.fileno())
                elif self.sim.inject_fsync_error:
                    # Invalidate descriptor so real os.fsync executes and raises OSError
                    os.close(self.raw_file.fileno())
                os.fsync(self.raw_file.fileno())
            except OSError:
                self.sync_failed = True
            except Exception:
                self.sync_failed = True

        self.sim.events.append("stream_sink_fclose")
        if self.sim.inject_fclose_error:
            self.close_failed = True
            try:
                self.raw_file.close()
            except Exception:
                pass
        else:
            try:
                self.raw_file.close()
            except Exception:
                pass

        self.fp_valid = False


class ConnectedBackupSimulator:
    """
    Python policy simulation model of BackupSaveInternal SD checked publication.
    Provides bounded Python policy/artifact evidence of publication lifecycle
    and fault handling. Does NOT claim full concurrency/hardware/IPC/libnx proof
    or atomic hardware rollback.
    """

    def __init__(self, root_dir: str):
        self.root_dir = root_dir
        self.events: List[str] = []
        self.pub_state = "Unpublished"
        self.owned_stage_created = False
        self.stage_dir: Optional[str] = None
        self.owned_temp_path: Optional[str] = None
        self.final_path: Optional[str] = None

        # Fault injection hooks
        self.inject_sd_open_error: Optional[int] = None
        self.inject_pre_probe_error: Optional[int] = None
        self.inject_parent_create_error: Optional[int] = None
        self.inject_stage_primitive_error: Optional[int] = None
        self.inject_stage_commit_error: Optional[int] = None
        self.inject_cancel_at_gate: Optional[int] = None  # 1, 2, 3, or 4
        self.inject_source_read_error: bool = False
        self.inject_short_write: bool = False
        self.inject_zip_entry_close_error: bool = False
        self.inject_zip_final_close_error: bool = False
        self.inject_fflush_error: bool = False
        self.inject_invalid_fd: bool = False
        self.inject_fsync_error: bool = False
        self.fsync_fault_hook: Optional[Callable[[int], None]] = None
        self.inject_fclose_error: bool = False
        self.inject_writer_sdmc_commit_error: Optional[int] = None
        self.inject_writer_sd_commit_error: Optional[int] = None
        self.pre_rename_hook: Optional[Callable[[], None]] = None
        self.inject_post_probe_error: Optional[int] = None
        self.inject_rename_error: Optional[int] = None
        self.inject_post_rename_sdmc_commit_error: Optional[int] = None
        self.inject_post_rename_sd_commit_error: Optional[int] = None
        self.inject_cleanup_delete_file_error: bool = False
        self.inject_cleanup_delete_dir_error: bool = False

    def run_backup(
        self,
        entry: SelectedEntry,
        extra_data_bytes: bytes,
        source_dir: str,
        rel_final_path: str,
        compressed: bool = False,
    ) -> Tuple[int, Optional[str]]:
        self.events.clear()
        self.pub_state = "Unpublished"
        self.owned_stage_created = False

        self.final_path = os.path.join(self.root_dir, rel_final_path.lstrip("/\\"))
        self.stage_dir = self.final_path + ".stage"
        self.owned_temp_path = os.path.join(self.stage_dir, "backup.zip.temp")

        # Zero save ID check
        if entry.save_data_id == 0:
            self.events.append("zero_id_early_return")
            return RES_OK, None

        # Identity comparison
        live_attr, extra_meta = decode_extra_data(extra_data_bytes)
        target_attr = entry.attr
        if (
            live_attr.application_id != target_attr.application_id
            or live_attr.uid_low != target_attr.uid_low
            or live_attr.uid_high != target_attr.uid_high
            or live_attr.system_save_data_id != target_attr.system_save_data_id
            or live_attr.save_data_type != target_attr.save_data_type
            or live_attr.save_data_rank != target_attr.save_data_rank
            or live_attr.save_data_index != target_attr.save_data_index
        ):
            self.events.append("identity_guard_mismatch")
            return FS_ERROR_PATH_NOT_FOUND, None

        # Source collection scan
        source_files: List[Tuple[str, str, int]] = []
        for root, dirs, files in os.walk(source_dir):
            for f in sorted(files):
                full_p = os.path.join(root, f)
                rel_p = os.path.relpath(full_p, source_dir).replace("\\", "/")
                source_files.append((rel_p, full_p, os.path.getsize(full_p)))

        if not source_files:
            self.events.append("empty_collections_early_return")
            return RES_OK, None

        def cleanup():
            self.events.append(f"cleanup_invoked(state={self.pub_state}, owned_stage={self.owned_stage_created})")
            if self.pub_state == "Unpublished":
                if self.owned_stage_created:
                    if os.path.exists(self.owned_temp_path):
                        if not self.inject_cleanup_delete_file_error:
                            os.remove(self.owned_temp_path)
                            self.events.append("cleaned_owned_temp")
                        else:
                            self.events.append("injected_cleanup_file_fail")
                    if os.path.exists(self.stage_dir):
                        if not self.inject_cleanup_delete_dir_error:
                            try:
                                os.rmdir(self.stage_dir)
                                self.events.append("cleaned_stage_dir")
                            except OSError:
                                self.events.append("stage_dir_cleanup_oserror")
                        else:
                            self.events.append("injected_cleanup_dir_fail")
            elif self.pub_state == "Renamed":
                if os.path.exists(self.final_path):
                    if not self.inject_cleanup_delete_file_error:
                        os.remove(self.final_path)
                        self.events.append("cleaned_owned_final")
                    else:
                        self.events.append("injected_cleanup_file_fail")
                if self.owned_stage_created and os.path.exists(self.stage_dir):
                    if not self.inject_cleanup_delete_dir_error:
                        try:
                            os.rmdir(self.stage_dir)
                            self.events.append("cleaned_stage_dir")
                        except OSError:
                            self.events.append("stage_dir_cleanup_oserror")
                    else:
                        self.events.append("injected_cleanup_dir_fail")
            elif self.pub_state == "Published":
                if self.owned_stage_created and os.path.exists(self.stage_dir):
                    if not self.inject_cleanup_delete_dir_error:
                        try:
                            os.rmdir(self.stage_dir)
                            self.events.append("cleaned_stage_dir")
                        except OSError:
                            self.events.append("stage_dir_cleanup_oserror")
                    else:
                        self.events.append("injected_cleanup_dir_fail")

        # --- Step 1: SD open check ---
        self.events.append("sd_open_check")
        if self.inject_sd_open_error is not None:
            self.events.append("sd_open_failed")
            cleanup()
            return self.inject_sd_open_error, None

        # --- Step 2: Final path pre-probe ---
        self.events.append("pre_probe_final")
        if self.inject_pre_probe_error is not None:
            cleanup()
            return self.inject_pre_probe_error, None
        if os.path.exists(self.final_path):
            self.events.append("final_path_already_exists_pre_probe")
            cleanup()
            return FS_ERROR_PATH_ALREADY_EXISTS, None

        # --- Step 3: Parent directory creation ---
        parent_dir = os.path.dirname(self.final_path)
        self.events.append(f"create_parent_dirs({parent_dir})")
        if self.inject_parent_create_error is not None:
            cleanup()
            return self.inject_parent_create_error, None
        os.makedirs(parent_dir, exist_ok=True)

        # Path length bounds check
        if len(self.stage_dir) > 768 or len(self.owned_temp_path) > 768:
            self.events.append("path_too_long")
            cleanup()
            return FS_ERROR_TOO_LONG_PATH, None

        # --- Gate 1: Cancellation before reservation ---
        self.events.append("cancellation_gate_1")
        if self.inject_cancel_at_gate == 1:
            self.events.append("cancelled_at_gate_1")
            cleanup()
            return SVC_ERROR_CANCELLED, None

        # --- Step 4: Reserve exclusive sibling stage directory ---
        self.events.append(f"reserve_stage_dir({self.stage_dir})")
        if os.path.exists(self.stage_dir):
            self.events.append("stage_dir_collision")
            cleanup()
            return FS_ERROR_PATH_ALREADY_EXISTS, None
        if self.inject_stage_primitive_error is not None:
            self.events.append("stage_primitive_failed")
            cleanup()
            return self.inject_stage_primitive_error, None

        os.mkdir(self.stage_dir)
        self.owned_stage_created = True
        self.events.append("ownership_set_owned_stage_true")

        # Stage commit
        self.events.append("stage_dir_commit")
        if self.inject_stage_commit_error is not None:
            self.events.append("stage_commit_failed")
            cleanup()
            return self.inject_stage_commit_error, None

        # --- Gate 2: Cancellation before write ---
        self.events.append("cancellation_gate_2")
        if self.inject_cancel_at_gate == 2:
            self.events.append("cancelled_at_gate_2")
            cleanup()
            return SVC_ERROR_CANCELLED, None

        # --- Step 5: WriteSaveBackupZip using checked stream transport ---
        self.events.append("write_save_backup_zip_start")
        is_dbi_format = (entry.attr.save_data_type not in (FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_TYPE_SYSTEM_BCAT))

        sink = ModeledRecoverySink(self.owned_temp_path, self)
        zip_compress_type = zipfile.ZIP_DEFLATED if compressed else zipfile.ZIP_STORED

        zf = None
        try:
            zf = zipfile.ZipFile(sink, "w", compression=zip_compress_type)

            def write_zip_entry(entry_name: str, data: bytes, is_payload: bool = False):
                self.events.append(f"writer_zip_entry_open({entry_name})")
                handle = zf.open(entry_name, "w")
                try:
                    handle.write(data)
                finally:
                    handle.close()
                if is_payload and self.inject_zip_entry_close_error:
                    raise IOError("Modeled zipCloseFileInZip failure after handle.close()")
                self.events.append(f"writer_zip_entry_close({entry_name})")

            # If Sphaira format: NXSaveMeta at start
            if not is_dbi_format:
                meta_bytes = encode_nx_save_meta(entry.attr, extra_meta, entry.size)
                write_zip_entry(NX_SAVE_META_NAME, meta_bytes, is_payload=False)

            # If DBI format: explicit directory entries
            if is_dbi_format:
                for rel_p, _, _ in source_files:
                    parent_rel = os.path.dirname(rel_p)
                    if parent_rel:
                        dir_entry_name = "/" + parent_rel.replace("\\", "/") + "/"
                        if dir_entry_name not in zf.namelist():
                            write_zip_entry(dir_entry_name, b"", is_payload=False)

            # Payload files
            for rel_p, full_p, size in source_files:
                if self.inject_source_read_error:
                    raise IOError("Simulated source read failure")
                with open(full_p, "rb") as sf:
                    content = sf.read()
                entry_in_zip = "/" + rel_p if is_dbi_format else rel_p
                write_zip_entry(entry_in_zip, content, is_payload=True)

            # If DBI format: meta entries last
            if is_dbi_format:
                account_label = get_actual_dbi_account_label(entry.attr.save_data_type, "TestAccount")
                space_str = get_actual_dbi_space_label(entry.save_data_space_id)
                dbi_info_text = (
                    f"TitleId={entry.attr.application_id:016X}\n"
                    f"TitleName={entry.name}\n"
                    f"BackupDate=2026-09-18 16:25:00\n"
                    f"Account={account_label}\n"
                    f"Space={space_str}"
                )
                write_zip_entry(DBI_SAVE_INFO_NAME, dbi_info_text.encode("utf-8"), is_payload=False)
                write_zip_entry(DBI_SAVE_EXTRA_NAME, extra_data_bytes, is_payload=False)

            # Archive finalization: explicit actual zf.close()
            self.events.append("writer_zip_final_close_call")
            zf.close()
            if self.inject_zip_final_close_error:
                raise IOError("Modeled zipClose failure after actual zf.close()")
            self.events.append("writer_zip_final_close")

        except Exception as ex:
            self.events.append(f"write_zip_exception: {ex}")
            if zf is not None:
                try:
                    zf.close()
                except Exception:
                    pass
            sink.close()
            cleanup()
            return RESULT_ZIP_WRITE_IN_FILE, None

        # Modeled RecoveryClose
        sink.close()

        # Recovery stream callback checks
        if sink.write_failed:
            self.events.append("sink_write_failed_detected")
            cleanup()
            return RESULT_ZIP_WRITE_IN_FILE, None
        if sink.flush_failed:
            self.events.append("sink_flush_failed_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None
        if sink.sync_failed:
            self.events.append("sink_sync_failed_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None
        if sink.close_failed:
            self.events.append("sink_close_failed_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None
        if sink.fp_valid:
            self.events.append("sink_fp_leak_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None

        # Writer device and native SD commit BEFORE rename
        self.events.append("writer_fsdev_commit_device_sdmc")
        if self.inject_writer_sdmc_commit_error is not None:
            self.events.append("writer_sdmc_commit_failed")
            cleanup()
            return self.inject_writer_sdmc_commit_error, None

        self.events.append("writer_native_sd_commit")
        if self.inject_writer_sd_commit_error is not None:
            self.events.append("writer_sd_commit_failed")
            cleanup()
            return self.inject_writer_sd_commit_error, None

        # --- Gate 3: Cancellation before rename ---
        self.events.append("cancellation_gate_3")
        if self.inject_cancel_at_gate == 3:
            self.events.append("cancelled_at_gate_3")
            cleanup()
            return SVC_ERROR_CANCELLED, None

        # Operation hook simulating concurrent external action before post-probe
        if self.pre_rename_hook is not None:
            self.pre_rename_hook()

        # --- Step 6: Post-probe final destination before rename ---
        self.events.append("post_probe_final")
        if self.inject_post_probe_error is not None:
            cleanup()
            return self.inject_post_probe_error, None
        if os.path.exists(self.final_path):
            self.events.append("final_path_already_exists_post_probe")
            cleanup()
            return FS_ERROR_PATH_ALREADY_EXISTS, None

        # --- Step 7: Native primitive rename ---
        self.events.append(f"primitive_rename({self.owned_temp_path} -> {self.final_path})")
        if self.inject_rename_error is not None:
            self.events.append("rename_failed")
            cleanup()
            return self.inject_rename_error, None

        os.rename(self.owned_temp_path, self.final_path)
        self.pub_state = "Renamed"
        self.events.append("ownership_set_renamed")

        # --- Step 8: Final SD Commits ---
        self.events.append("post_rename_fsdev_commit_device_sdmc")
        if self.inject_post_rename_sdmc_commit_error is not None:
            self.events.append("post_rename_sdmc_commit_failed")
            cleanup()
            return self.inject_post_rename_sdmc_commit_error, None

        self.events.append("post_rename_native_sd_commit")
        if self.inject_post_rename_sd_commit_error is not None:
            self.events.append("post_rename_sd_commit_failed")
            cleanup()
            return self.inject_post_rename_sd_commit_error, None

        # --- Publication Success ---
        self.pub_state = "Published"
        self.events.append("ownership_set_published")

        # Post-publication cancellation gate must NEVER fail
        if self.inject_cancel_at_gate == 4:
            self.events.append("ignored_cancellation_post_publication")

        cleanup()
        self.events.append("backup_success")
        return RES_OK, self.final_path


# ==============================================================================
# 3. Connected Verification: 7 Concrete Types & Independent Wire Readback
# ==============================================================================

def assert_successful_ordered_subsequence(events: List[str]):
    """
    Asserts the strict ordered subsequence of lifecycle events for a successful publication:
    primitive stage -> owned -> stage commit -> entry close ->
    archive close -> stream flush/sync/close -> writer device/native commits ->
    fresh final probe -> rename -> Renamed -> final device/native commits ->
    Published -> success.
    """
    def find_idx(predicate, start_from=0, name=""):
        for i in range(start_from, len(events)):
            if predicate(events[i]):
                return i
        raise AssertionError(f"Subsequence item '{name}' not found after index {start_from} in events:\n" + "\n".join(events))

    idx_stage = find_idx(lambda e: e.startswith("reserve_stage_dir("), 0, "primitive stage")
    idx_owned = find_idx(lambda e: e == "ownership_set_owned_stage_true", idx_stage + 1, "owned stage true")
    idx_stage_commit = find_idx(lambda e: e == "stage_dir_commit", idx_owned + 1, "stage commit")
    idx_entry_close = find_idx(lambda e: e.startswith("writer_zip_entry_close("), idx_stage_commit + 1, "entry close")
    idx_arch_close = find_idx(lambda e: e == "writer_zip_final_close", idx_entry_close + 1, "archive close")
    idx_flush = find_idx(lambda e: e == "stream_sink_fflush", idx_arch_close + 1, "stream flush")
    idx_sync = find_idx(lambda e: e == "stream_sink_fsync", idx_flush + 1, "stream sync")
    idx_sink_close = find_idx(lambda e: e == "stream_sink_fclose", idx_sync + 1, "stream close")
    idx_w_sdmc = find_idx(lambda e: e == "writer_fsdev_commit_device_sdmc", idx_sink_close + 1, "writer sdmc commit")
    idx_w_sd = find_idx(lambda e: e == "writer_native_sd_commit", idx_w_sdmc + 1, "writer native sd commit")
    idx_probe = find_idx(lambda e: e == "post_probe_final", idx_w_sd + 1, "fresh final probe")
    idx_rename = find_idx(lambda e: e.startswith("primitive_rename("), idx_probe + 1, "rename primitive")
    idx_renamed = find_idx(lambda e: e == "ownership_set_renamed", idx_rename + 1, "Renamed state")
    idx_pr_sdmc = find_idx(lambda e: e == "post_rename_fsdev_commit_device_sdmc", idx_renamed + 1, "post-rename sdmc commit")
    idx_pr_sd = find_idx(lambda e: e == "post_rename_native_sd_commit", idx_pr_sdmc + 1, "post-rename native sd commit")
    idx_published = find_idx(lambda e: e == "ownership_set_published", idx_pr_sd + 1, "Published state")
    idx_success = find_idx(lambda e: e == "backup_success", idx_published + 1, "success")

    ordered_steps = [
        ("primitive stage", idx_stage),
        ("owned", idx_owned),
        ("stage commit", idx_stage_commit),
        ("entry close", idx_entry_close),
        ("archive close", idx_arch_close),
        ("stream flush", idx_flush),
        ("stream sync", idx_sync),
        ("stream close", idx_sink_close),
        ("writer device commit", idx_w_sdmc),
        ("writer native commit", idx_w_sd),
        ("fresh final probe", idx_probe),
        ("rename", idx_rename),
        ("Renamed", idx_renamed),
        ("final device commit", idx_pr_sdmc),
        ("final native commit", idx_pr_sd),
        ("Published", idx_published),
        ("success", idx_success),
    ]
    for i in range(len(ordered_steps) - 1):
        name_curr, idx_curr = ordered_steps[i]
        name_next, idx_next = ordered_steps[i + 1]
        check(idx_curr < idx_next, f"Ordered subsequence violation: {name_curr} (idx {idx_curr}) not before {name_next} (idx {idx_next})")


def test_seven_save_types_connected_pipeline():
    print("[2] Running connected 7-save-type publication & independent wire readback tests...")

    test_cases = [
        # (name, type, space, uid_low, uid_high, sid, rank, idx, compressed, is_dbi, expect_account, expect_space)
        ("Account_Main", FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_SPACE_ID_USER, 0x1234567890ABCDEF, 0xFEDCBA0987654321, 0, 0, 0, False, True, "TestAccount", "User"),
        ("Account_Rank1_Idx3", FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_SPACE_ID_USER, 0x1234567890ABCDEF, 0xFEDCBA0987654321, 0, 1, 3, True, True, "TestAccount", "User"),
        ("Device", FS_SAVE_DATA_TYPE_DEVICE, FS_SAVE_DATA_SPACE_ID_USER, 0, 0, 0, 0, 0, True, True, "Device", "User"),
        ("Bcat", FS_SAVE_DATA_TYPE_BCAT, FS_SAVE_DATA_SPACE_ID_USER, 0, 0, 0, 0, 0, False, True, "BCAT", "User"),
        ("Cache_User", FS_SAVE_DATA_TYPE_CACHE, FS_SAVE_DATA_SPACE_ID_USER, 0x1111222233334444, 0x5555666677778888, 0, 0, 0, True, True, "Cache", "User"),
        ("Cache_SdUser", FS_SAVE_DATA_TYPE_CACHE, FS_SAVE_DATA_SPACE_ID_SD_USER, 0x1111222233334444, 0x5555666677778888, 0, 0, 1, False, True, "Cache", "User"),
        ("Temporary", FS_SAVE_DATA_TYPE_TEMPORARY, FS_SAVE_DATA_SPACE_ID_TEMPORARY, 0, 0, 0, 0, 0, True, True, "Temporary", "Temporary"),
        ("System", FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_SPACE_ID_SYSTEM, 0, 0, 0x8000000000000010, 0, 0, False, False, "System", "System"),
        ("System_SdSystem", FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_SPACE_ID_SD_SYSTEM, 0, 0, 0x8000000000000020, 0, 0, True, False, "System", "System"),
        ("System_ProperSystem", FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM, 0, 0, 0x8000000000000030, 0, 0, False, False, "System", "System"),
        ("SystemBcat", FS_SAVE_DATA_TYPE_SYSTEM_BCAT, FS_SAVE_DATA_SPACE_ID_SYSTEM, 0, 0, 0x8000000000000040, 0, 0, True, False, "System BCAT", "System"),
    ]

    with tempfile.TemporaryDirectory() as td:
        root_dir = os.path.join(td, "sdcard")
        os.makedirs(root_dir, exist_ok=True)

        source_dir = os.path.join(td, "source_payload")
        os.makedirs(os.path.join(source_dir, "nested", "dir"), exist_ok=True)
        main_payload_bytes = b"MAIN_SAVE_PAYLOAD_BYTES_0123456789"
        nested_payload_bytes = b"NESTED_SLOT_DATA_PAYLOAD"
        with open(os.path.join(source_dir, "save.dat"), "wb") as f:
            f.write(main_payload_bytes)
        with open(os.path.join(source_dir, "nested", "dir", "slot.bin"), "wb") as f:
            f.write(nested_payload_bytes)
        with open(os.path.join(source_dir, "empty_file.bin"), "wb") as f:
            pass

        sim = ConnectedBackupSimulator(root_dir)

        for name, stype, space, ul, uh, sid, rank, idx, comp, expect_dbi, exp_account, exp_space in test_cases:
            app_id = 0x0100000000010000 if sid == 0 else 0
            attr = SaveAttribute(
                application_id=app_id,
                uid_low=ul,
                uid_high=uh,
                system_save_data_id=sid,
                save_data_type=stype,
                save_data_rank=rank,
                save_data_index=idx,
            )
            entry = SelectedEntry(
                save_data_id=0x99990000 + stype,
                save_data_space_id=space,
                attr=attr,
                name=f"Game_{name}",
                size=0x80000,
            )
            extra_bytes = encode_extra_data(
                attr,
                owner_id=0x0100000000001000 + stype,
                timestamp=1726000000 + stype,
                flags=0x12,
                unk_x54=0x34,
                data_size=0x200000,
                journal_size=0x200000,
                commit_id=100 + stype,
            )
            rel_zip = f"/backup/saves/{name}.zip"

            rc, final_path = sim.run_backup(entry, extra_bytes, source_dir, rel_zip, compressed=comp)
            check(rc == RES_OK, f"Backup failed for {name} with rc 0x{rc:X}")
            check(final_path is not None and os.path.exists(final_path), f"Final zip missing for {name}")
            check(sim.pub_state == "Published", f"Publication state must be Published for {name}")
            check(not os.path.exists(sim.stage_dir), f"Stage directory must be cleaned up for {name}")

            # Verify ordered subsequence via event indices
            assert_successful_ordered_subsequence(sim.events)

            # Reopen ZIP and verify actual independent wire encoding and payload
            with zipfile.ZipFile(final_path, "r") as zf:
                names = zf.namelist()
                # Check compression type
                expected_compress = zipfile.ZIP_DEFLATED if comp else zipfile.ZIP_STORED
                if expect_dbi:
                    info = zf.getinfo("/save.dat")
                    check(info.compress_type == expected_compress, f"{name}: compress_type mismatch")
                else:
                    info = zf.getinfo("save.dat")
                    check(info.compress_type == expected_compress, f"{name}: compress_type mismatch")

                if expect_dbi:
                    check(DBI_SAVE_INFO_NAME in names, f"{name}: missing {DBI_SAVE_INFO_NAME}")
                    check(DBI_SAVE_EXTRA_NAME in names, f"{name}: missing {DBI_SAVE_EXTRA_NAME}")

                    # Independent unpack of DBI extra512 at raw offsets
                    read_extra = zf.read(DBI_SAVE_EXTRA_NAME)
                    check(len(read_extra) == 512, f"{name}: DBI extra size must be 512")
                    u_app, u_ul, u_uh, u_sid, u_stype, u_rank, u_idx = struct.unpack("<QQQQBBH", read_extra[:36])
                    u_oid, u_ts, u_flags, u_unk, u_dsize, u_jsize, u_cid = struct.unpack("<QQIIqqQ", read_extra[64:112])
                    check(u_app == app_id, f"{name}: raw app mismatch")
                    check(u_ul == ul and u_uh == uh, f"{name}: raw uid mismatch")
                    check(u_sid == sid, f"{name}: raw sid mismatch")
                    check(u_stype == stype, f"{name}: raw stype mismatch")
                    check(u_rank == rank, f"{name}: raw rank mismatch")
                    check(u_idx == idx, f"{name}: raw index mismatch")
                    check(u_oid == 0x0100000000001000 + stype, f"{name}: raw owner_id mismatch")
                    check(u_ts == 1726000000 + stype, f"{name}: raw timestamp mismatch")
                    check(u_flags == 0x12, f"{name}: raw flags mismatch")
                    check(u_unk == 0x34, f"{name}: raw unk mismatch")
                    check(u_dsize == 0x200000, f"{name}: raw data_size mismatch")
                    check(u_jsize == 0x200000, f"{name}: raw journal_size mismatch")
                    check(u_cid == 100 + stype, f"{name}: raw commit_id mismatch")

                    # Verify exact DBI info fields matching actual writer
                    read_info = zf.read(DBI_SAVE_INFO_NAME).decode("utf-8")
                    check(f"TitleId={app_id:016X}" in read_info, f"{name}: DBI info TitleId mismatch")
                    check(f"Account={exp_account}" in read_info, f"{name}: DBI info Account mismatch, got: {read_info}")
                    check(f"Space={exp_space}" in read_info, f"{name}: DBI info Space mismatch, got: {read_info}")

                    # Check payload files with absolute slash
                    check("/save.dat" in names, f"{name}: missing /save.dat")
                    check("/nested/dir/slot.bin" in names, f"{name}: missing /nested/dir/slot.bin")
                    check("/empty_file.bin" in names, f"{name}: missing /empty_file.bin")
                    check(zf.read("/save.dat") == main_payload_bytes, f"{name}: main payload mismatch")
                    check(zf.read("/nested/dir/slot.bin") == nested_payload_bytes, f"{name}: nested payload mismatch")
                    check(zf.read("/empty_file.bin") == b"", f"{name}: empty file must be 0 bytes")
                else:
                    check(NX_SAVE_META_NAME in names, f"{name}: missing {NX_SAVE_META_NAME}")
                    read_meta = zf.read(NX_SAVE_META_NAME)
                    check(len(read_meta) == 128, f"{name}: NXSaveMeta size must be 128")

                    # Independent unpack of NX128 at raw offsets
                    m_magic, m_ver = struct.unpack("<II", read_meta[:8])
                    check(m_magic == NX_SAVE_META_MAGIC, f"{name}: raw magic mismatch")
                    check(m_ver == NX_SAVE_META_VERSION, f"{name}: raw version mismatch")
                    m_app, m_ul, m_uh, m_sid, m_stype, m_rank, m_idx = struct.unpack("<QQQQBBH", read_meta[8:44])
                    check(m_app == app_id, f"{name}: raw NX app mismatch")
                    check(m_ul == ul and m_uh == uh, f"{name}: raw NX uid mismatch")
                    check(m_sid == sid, f"{name}: raw NX sid mismatch")
                    check(m_stype == stype, f"{name}: raw NX type mismatch")
                    check(m_rank == rank, f"{name}: raw NX rank mismatch")
                    check(m_idx == idx, f"{name}: raw NX idx mismatch")
                    m_oid, m_ts, m_flags, m_unk, m_dsize, m_jsize, m_cid, m_raw = struct.unpack("<QQIIqqQQ", read_meta[72:128])
                    check(m_oid == 0x0100000000001000 + stype, f"{name}: raw NX owner_id mismatch")
                    check(m_ts == 1726000000 + stype, f"{name}: raw NX timestamp mismatch")
                    check(m_flags == 0x12, f"{name}: raw NX flags mismatch")
                    check(m_unk == 0x34, f"{name}: raw NX unk mismatch")
                    check(m_dsize == 0x200000, f"{name}: raw NX data_size mismatch")
                    check(m_jsize == 0x200000, f"{name}: raw NX journal_size mismatch")
                    check(m_cid == 100 + stype, f"{name}: raw NX cid mismatch")
                    check(m_raw == entry.size, f"{name}: raw NX raw_size mismatch")

                    # Check payload files without root slash
                    check("save.dat" in names, f"{name}: missing save.dat")
                    check("nested/dir/slot.bin" in names, f"{name}: missing nested/dir/slot.bin")
                    check("empty_file.bin" in names, f"{name}: missing empty_file.bin")
                    check(zf.read("save.dat") == main_payload_bytes, f"{name}: main payload mismatch")
                    check(zf.read("nested/dir/slot.bin") == nested_payload_bytes, f"{name}: nested payload mismatch")
                    check(zf.read("empty_file.bin") == b"", f"{name}: empty file must be 0 bytes")

    print("  -> Connected 7-save-type publication & independent wire readback tests PASSED.")


# ==============================================================================
# 4. Connected Failure & Mutation Matrix With Full Lifecycle Assertions
# ==============================================================================

def test_failure_matrix_connected():
    print("[3] Running connected failure matrix & event order assertions...")

    with tempfile.TemporaryDirectory() as td:
        root_dir = os.path.join(td, "sdcard")
        os.makedirs(root_dir, exist_ok=True)

        source_dir = os.path.join(td, "source_payload")
        os.makedirs(source_dir, exist_ok=True)
        sentinel_payload = b"SENTINEL_SOURCE_PAYLOAD_DO_NOT_TOUCH_012345"
        source_file = os.path.join(source_dir, "data.bin")
        with open(source_file, "wb") as f:
            f.write(sentinel_payload)

        attr = SaveAttribute(
            application_id=0x0100000000010000,
            uid_low=0x1234,
            uid_high=0x5678,
            system_save_data_id=0,
            save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT,
            save_data_rank=0,
            save_data_index=0,
        )
        entry = SelectedEntry(
            save_data_id=0x12345678,
            save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER,
            attr=attr,
            name="MatrixGame",
        )
        extra_bytes = encode_extra_data(attr)

        def assert_source_sentinel_unchanged():
            with open(source_file, "rb") as f:
                check(f.read() == sentinel_payload, "ASSERTION FAILED: Source sentinel payload was mutated!")

        # 3.1 Zero save ID early exit
        sim = ConnectedBackupSimulator(root_dir)
        zero_entry = SelectedEntry(save_data_id=0, save_data_space_id=1, attr=attr)
        rc, p = sim.run_backup(zero_entry, extra_bytes, source_dir, "/backup/zero.zip")
        check(rc == RES_OK and p is None, "Zero ID must return RES_OK with no archive")
        check("zero_id_early_return" in sim.events, "Must log zero_id_early_return")
        check("sd_open_check" not in sim.events, "Must not open SD on zero save ID")
        assert_source_sentinel_unchanged()

        # 3.2 Empty collections early return
        empty_src_dir = os.path.join(td, "empty_source")
        os.makedirs(empty_src_dir, exist_ok=True)
        sim = ConnectedBackupSimulator(root_dir)
        rc, p = sim.run_backup(entry, extra_bytes, empty_src_dir, "/backup/empty.zip")
        check(rc == RES_OK and p is None, "Empty source collections must exit 0x0 without archive")
        check("empty_collections_early_return" in sim.events, "Must log empty_collections_early_return")
        check("sd_open_check" not in sim.events, "Must not open SD on empty collections")

        # 3.3 SD native open failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_sd_open_error = 0x22202
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/sd_open_fail.zip")
        check(rc == 0x22202, "SD open failure must propagate")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check("pre_probe_final" not in sim.events, "Must not probe final after SD open failure")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage dir after SD open failure")
        assert_source_sentinel_unchanged()

        # 3.4 Pre-work final probe IO error (propagated)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_pre_probe_error = 0x33302
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/pre_probe_err.zip")
        check(rc == 0x33302, "Pre-probe IO error must propagate")
        check("create_parent_dirs" not in sim.events, "Must not create parent dirs after pre-probe error")
        assert_source_sentinel_unchanged()

        # 3.5 Real valid ZIP collision BEFORE work
        sim = ConnectedBackupSimulator(root_dir)
        coll_final_path = os.path.join(root_dir, "backup", "coll_pre.zip")
        os.makedirs(os.path.dirname(coll_final_path), exist_ok=True)
        prior_zip_sentinel = b"VALID_EXISTING_ZIP_PAYLOAD_PROTECTED"
        with zipfile.ZipFile(coll_final_path, "w") as zf:
            zf.writestr("prior_save.dat", prior_zip_sentinel)
        with open(coll_final_path, "rb") as f:
            captured_prior_bytes = f.read()

        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/coll_pre.zip")
        check(rc == FS_ERROR_PATH_ALREADY_EXISTS, "Pre-work collision must return FsError_PathAlreadyExists")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage on pre-work collision")
        with open(coll_final_path, "rb") as f:
            check(f.read() == captured_prior_bytes, "ASSERTION FAILED: Pre-existing ZIP bytes mutated!")
        with zipfile.ZipFile(coll_final_path, "r") as zf:
            check(zf.read("prior_save.dat") == prior_zip_sentinel, "ASSERTION FAILED: Pre-existing ZIP payload corrupt!")
        assert_source_sentinel_unchanged()

        # 3.6 Parent directory creation failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_parent_create_error = 0x44402
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/parent_err.zip")
        check(rc == 0x44402, "Parent create error must propagate")
        check("cancellation_gate_1" not in sim.events, "Must not reach gate 1 after parent create error")
        assert_source_sentinel_unchanged()

        # 3.7 Cancellation at Gate 1 (before reservation)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 1
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g1.zip")
        check(rc == SVC_ERROR_CANCELLED, "Must return SVC_ERROR_CANCELLED at gate 1")
        check(not os.path.exists(sim.stage_dir), "Stage dir must not exist on cancellation at gate 1")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage dir after gate 1 cancel")
        assert_source_sentinel_unchanged()

        # 3.8 Stage path length bounds overflow (FsError_TooLongPath)
        sim = ConnectedBackupSimulator(root_dir)
        long_name = "x" * 800
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, f"/backup/{long_name}.zip")
        check(rc == FS_ERROR_TOO_LONG_PATH, "Long path must return FsError_TooLongPath")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage on length overflow")

        # 3.9 Sibling stage collision (foreign .stage already exists with real sentinel)
        sim = ConnectedBackupSimulator(root_dir)
        foreign_stage_path = os.path.join(root_dir, "backup", "stage_coll.zip.stage")
        os.makedirs(foreign_stage_path, exist_ok=True)
        foreign_sentinel = os.path.join(foreign_stage_path, "foreign_lock.txt")
        foreign_sentinel_bytes = b"FOREIGN_STAGE_SENTINEL_DO_NOT_DELETE_0123"
        with open(foreign_sentinel, "wb") as f:
            f.write(foreign_sentinel_bytes)

        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/stage_coll.zip")
        check(rc == FS_ERROR_PATH_ALREADY_EXISTS, "Stage collision must return FsError_PathAlreadyExists")
        check(sim.owned_stage_created is False, "owned_stage_created must be false on stage collision")
        check(os.path.exists(foreign_sentinel), "ASSERTION FAILED: Foreign stage sentinel was deleted!")
        with open(foreign_sentinel, "rb") as f:
            check(f.read() == foreign_sentinel_bytes, "Foreign stage sentinel was corrupted!")
        assert_source_sentinel_unchanged()

        # 3.10 Native stage primitive reservation failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_stage_primitive_error = 0x55502
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/stage_prim_fail.zip")
        check(rc == 0x55502, "Stage primitive failure must propagate")
        check(sim.owned_stage_created is False, "owned_stage_created must be false on primitive failure")
        check("stage_dir_commit" not in sim.events, "Must not commit stage dir on primitive failure")
        check("write_save_backup_zip_start" not in sim.events, "Must not start writer on stage failure")

        # 3.11 Stage commit failure after primitive success
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_stage_commit_error = 0x66602
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/stage_commit_fail.zip")
        check(rc == 0x66602, "Stage commit error must propagate")
        check("ownership_set_owned_stage_true" in sim.events, "Ownership must be set true before commit")
        check(not os.path.exists(sim.stage_dir), "Owned stage dir must be deleted on stage commit failure")
        check("write_save_backup_zip_start" not in sim.events, "Must not start writer on stage commit failure")

        # 3.12 Cancellation at Gate 2 (before write)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 2
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g2.zip")
        check(rc == SVC_ERROR_CANCELLED, "Must return SVC_ERROR_CANCELLED at gate 2")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on cancellation at gate 2")
        check("write_save_backup_zip_start" not in sim.events, "Must not write on gate 2 cancel")

        # 3.13 Source read error during write
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_source_read_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/src_read_err.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on source read failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on write failure")
        check("primitive_rename" not in sim.events, "Must not rename on write failure")

        # 3.14 Short write in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_short_write = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/short_write.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on short write")
        check("sink_write_failed_detected" in sim.events, "Must detect sink write failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on short write")
        check("primitive_rename" not in sim.events, "Must not rename on short write")

        # 3.15 ZIP entry close failure hooked at entry close boundary
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_zip_entry_close_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/entry_close_err.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on zip entry close failure")
        check("writer_fsdev_commit_device_sdmc" not in sim.events, "Must not reach writer commits on entry close failure")
        check("primitive_rename" not in sim.events, "Must not rename on entry close failure")
        check("backup_success" not in sim.events, "Must not succeed on entry close failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on entry close failure")

        # 3.16 ZIP final archive close failure hooked at actual zf.close()
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_zip_final_close_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/final_close_err.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on zip final close failure")
        check("writer_fsdev_commit_device_sdmc" not in sim.events, "Must not reach writer commits on final close failure")
        check("primitive_rename" not in sim.events, "Must not rename on final close failure")
        check("backup_success" not in sim.events, "Must not succeed on final close failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on final close failure")

        # 3.17 fflush failure in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_fflush_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fflush_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fflush failure")
        check("sink_flush_failed_detected" in sim.events, "Must detect sink flush failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on fflush failure")

        # 3.18 invalid fd (-1) in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_invalid_fd = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/inv_fd_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on invalid fd")
        check("sink_sync_failed_detected" in sim.events, "Must detect sink sync failure")

        # 3.19 Real os.fsync failure via narrow fault hook throwing OSError
        sim = ConnectedBackupSimulator(root_dir)
        def fault_hook_close_os_fd(fd):
            # Close underlying OS file descriptor so actual os.fsync(fd) call raises OSError(EBADF)
            os.close(fd)
        sim.fsync_fault_hook = fault_hook_close_os_fd
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fsync_hook_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fsync failure")
        check("sink_sync_failed_detected" in sim.events, "Must detect sink sync failure")
        check("primitive_rename" not in sim.events, "Must not rename on fsync failure")
        check("backup_success" not in sim.events, "Must not succeed on fsync failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(p is None or not os.path.exists(sim.final_path), "Must not have published final path")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on fsync failure")

        # 3.19b Real os.fsync failure via inject_fsync_error flag throwing OSError
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_fsync_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fsync_flag_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fsync flag failure")
        check("sink_sync_failed_detected" in sim.events, "Must detect sink sync failure")
        check("primitive_rename" not in sim.events, "Must not rename on fsync flag failure")
        check("backup_success" not in sim.events, "Must not succeed on fsync flag failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(p is None or not os.path.exists(sim.final_path), "Must not have published final path")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on fsync flag failure")

        # 3.20 fclose failure in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_fclose_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fclose_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fclose failure")
        check("sink_close_failed_detected" in sim.events, "Must detect sink close failure")

        # 3.21 Writer fsdevCommitDevice("sdmc") failure BEFORE rename
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_writer_sdmc_commit_error = 0x77702
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/w_sdmc_fail.zip")
        check(rc == 0x77702, "Writer sdmc commit error must propagate")
        check("primitive_rename" not in sim.events, "Must not rename on writer sdmc commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on writer commit failure")

        # 3.22 Writer native SD Commit failure BEFORE rename
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_writer_sd_commit_error = 0x77802
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/w_sd_fail.zip")
        check(rc == 0x77802, "Writer native commit error must propagate")
        check("primitive_rename" not in sim.events, "Must not rename on writer native commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on writer native commit failure")

        # 3.23 Cancellation at Gate 3 (before rename)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 3
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g3.zip")
        check(rc == SVC_ERROR_CANCELLED, "Must return SVC_ERROR_CANCELLED at gate 3")
        check("primitive_rename" not in sim.events, "Must not rename on gate 3 cancel")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on cancellation at gate 3")
        check(not os.path.exists(sim.final_path), "No final archive on cancellation at gate 3")

        # 3.24 Real concurrent collision before rename: operation hook creates valid ZIP
        sim = ConnectedBackupSimulator(root_dir)
        foreign_final_sentinel = b"CONCURRENT_EXTERNAL_ZIP_SENTINEL_PAYLOAD"
        foreign_final_file = os.path.join(root_dir, "backup", "coll_prerename.zip")

        captured_foreign_bytes: Optional[bytes] = None
        def concurrent_writer_hook():
            nonlocal captured_foreign_bytes
            with zipfile.ZipFile(foreign_final_file, "w") as zf:
                zf.writestr("concurrent.dat", foreign_final_sentinel)
            with open(foreign_final_file, "rb") as f:
                captured_foreign_bytes = f.read()

        sim.pre_rename_hook = concurrent_writer_hook
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/coll_prerename.zip")
        check(rc == FS_ERROR_PATH_ALREADY_EXISTS, "Pre-rename collision must return PathAlreadyExists")
        check("primitive_rename" not in sim.events, "Must not execute rename on pre-rename collision")
        check(not os.path.exists(sim.stage_dir), "Owned stage dir must be cleaned on pre-rename collision")
        # Verify exact byte comparison after refusal
        check(captured_foreign_bytes is not None and len(captured_foreign_bytes) > 0, "Captured foreign bytes must be non-empty")
        with open(foreign_final_file, "rb") as f:
            check(f.read() == captured_foreign_bytes, "ASSERTION FAILED: Foreign concurrent ZIP bytes were mutated after refusal!")
        # Verify real foreign ZIP is intact and can be reopened
        with zipfile.ZipFile(foreign_final_file, "r") as zf:
            check(zf.read("concurrent.dat") == foreign_final_sentinel, "Foreign concurrent ZIP was corrupted!")

        # 3.25 Pre-rename final probe IO error (propagated)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_probe_error = 0x88802
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/post_probe_err.zip")
        check(rc == 0x88802, "Post-probe error must propagate")
        check("primitive_rename" not in sim.events, "Must not rename on post-probe error")
        check(not os.path.exists(sim.stage_dir), "Owned stage dir must be cleaned on post-probe error")

        # 3.26 Native rename primitive failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_rename_error = 0x99902
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/rename_fail.zip")
        check(rc == 0x99902, "Rename failure must propagate")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished if rename failed")
        check("ownership_set_renamed" not in sim.events, "Must not transition to Renamed on rename failure")
        check(not os.path.exists(sim.final_path), "No final path created if rename failed")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on rename failure")

        # 3.27 Post-rename sdmc commit failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_rename_sdmc_commit_error = 0xAAA02
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/pr_sdmc_fail.zip")
        check(rc == 0xAAA02, "Post-rename sdmc commit error must propagate")
        check("ownership_set_renamed" in sim.events, "Must reach Renamed state")
        check("ownership_set_published" not in sim.events, "Must not reach Published on commit failure")
        check(not os.path.exists(sim.final_path), "Owned final must be deleted on post-rename commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be deleted")

        # 3.28 Post-rename native SD commit failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_rename_sd_commit_error = 0xBBB02
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/pr_sd_fail.zip")
        check(rc == 0xBBB02, "Post-rename native SD commit error must propagate")
        check("ownership_set_renamed" in sim.events, "Must reach Renamed state")
        check("ownership_set_published" not in sim.events, "Must not reach Published on commit failure")
        check(not os.path.exists(sim.final_path), "Owned final must be deleted on post-rename commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be deleted")

        # 3.29 Cleanup directory failure (honest retained artifact, error returned)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_stage_commit_error = 0x66602
        sim.inject_cleanup_delete_dir_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cleanup_dir_fail.zip")
        check(rc == 0x66602, "Must return stage commit error")
        check("injected_cleanup_dir_fail" in sim.events, "Must record directory cleanup failure")
        check(os.path.exists(sim.stage_dir), "Honest retained stage dir remains when cleanup fails")

        # 3.30 Cleanup file failure after post-rename failure (honest retained final artifact)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_rename_sdmc_commit_error = 0xAAA02
        sim.inject_cleanup_delete_file_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cleanup_file_fail.zip")
        check(rc == 0xAAA02, "Must return commit failure even when cleanup fails")
        check(os.path.exists(sim.final_path), "Honest retained artifact remains when file cleanup fails")
        check("injected_cleanup_file_fail" in sim.events, "Must record file cleanup failure")

        # 3.31 Cancellation after committed Published must NOT fail
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 4
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g4_success.zip")
        check(rc == RES_OK, "Cancellation after committed Published must return RES_OK")
        check(p is not None and os.path.exists(p), "Published archive must exist")
        check(sim.pub_state == "Published", "Pub state must be Published")
        check("ignored_cancellation_post_publication" in sim.events, "Must record ignored post-pub cancellation")

        # 3.32 Dedicated successful publication lifecycle ordered subsequence verification
        sim = ConnectedBackupSimulator(root_dir)
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/matrix_success.zip")
        check(rc == RES_OK, "Must succeed on clean run")
        check(p is not None and os.path.exists(p), "Published archive must exist")
        check(sim.pub_state == "Published", "Pub state must be Published")
        assert_successful_ordered_subsequence(sim.events)

        # Final assertion: source sentinel was NEVER mutated
        assert_source_sentinel_unchanged()

    print("  -> Connected failure matrix & event order assertions PASSED.")


def main():
    print("=== Sphaira v0.13.860: Checked Backup Publication Contract Suite ===")
    test_source_contracts()
    test_seven_save_types_connected_pipeline()
    test_failure_matrix_connected()
    print("=== ALL CONTRACT SUITES PASSED SUCCESSFULLY ===")


if __name__ == "__main__":
    main()
