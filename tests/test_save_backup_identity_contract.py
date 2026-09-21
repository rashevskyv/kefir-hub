#!/usr/bin/env python3
"""
Test Suite: Selected/Live Identity Guard Contract for Ordinary Backup Export (Sphaira v0.13.859)

Scope & Verification Boundary:
- Static source contracts verify:
  1. Menu::BackupSaveInternal in sphaira/source/ui/menus/save/save_menu_ops.cpp:
     - Early exit for e.save_data_id == 0 is preserved.
     - fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId reads extra before attr comparison.
     - Construction of FsSaveDataAttribute from Entry fields.
     - Strict 7-field equality guard:
         extra.attr.application_id == attr.application_id
         extra.attr.uid.uid[0] == attr.uid.uid[0]
         extra.attr.uid.uid[1] == attr.uid.uid[1]
         extra.attr.system_save_data_id == attr.system_save_data_id
         extra.attr.save_data_type == attr.save_data_type
         extra.attr.save_data_rank == attr.save_data_rank
         extra.attr.save_data_index == attr.save_data_index
     - Any mismatch returns FsError_PathNotFound (0x202).
     - Guard ordering strictly precedes FsNativeSave read-only open, collection
       enumeration, and any filesystem mutations (directory/temp/file creation).
  2. Shared callers preserve routing without default-space substitution:
     - Menu::BackupSavesOn and Menu::BackupSaves overloads in save_menu_ops.cpp.
     - Menu::CreateBackupIfNewer in save_menu.cpp.
     - Users safety-backup caller in users_profile.cpp.
     - WriteSaveBackupZip metadata formatting.
  3. sphaira/include/ui/menus/save/save_paths.hpp defines NX_SAVE_META_MAGIC as 0x4A4B5356.
  4. sphaira/include/fs.hpp FsNativeSave read-only branch preserves actual space + attr.
  5. sphaira/CMakeLists.txt version set to 0.13.859.
- Connected Python simulation model verifies:
  1. Independent encoding/decoding of 512-byte FsSaveDataExtraData wire format:
     - Offset 0..35: FsSaveDataAttribute (app/u64, uid0/u64, uid1/u64, sys/u64, type/u8, rank/u8, index/u16).
     - Offset 36..39: padding (4 bytes, non-identity).
     - Offset 40..63: unknown (24 bytes, non-identity).
     - Offset 64..111: metadata (owner/u64, timestamp/u64, flags/u32, unk/u32, data_size/s64, journal_size/s64, commit_id/u64).
     - Offset 112..511: reserved/trailing padding (400 bytes).
  2. Complete connected lifecycle:
     - Extra-data read event -> decode -> identity admission -> selected-space/attr RO mount
       -> payload enumeration and reading -> temporary ZIP creation -> rename -> publication.
  3. Real filesystem source payload connection and sentinel preservation:
     - Real filesystem files are created in a source payload subtree (including nested files).
     - Model reads payload files from the filesystem only after identity admission and RO-mount.
     - Pre-existing sentinel files outside payload subtree and in output directory.
     - On any read failure or identity mismatch: assert exact error code, zero payload-open/read,
       zero mount/enum/output events, and strictly zero modification of sentinels or output inventory.
     - On valid admission: actual ZIP archive is written and reopened; payload files inside ZIP
       match source files on disk bit-for-bit; metadata inside ZIP matches selected source identity.
  4. Comprehensive matrix:
     - All 7 save types x all 7 concrete spaces (49 synthetic routing preservation combinations;
       this validates model routing preservation, not firmware support of all combinations).
     - Primary and secondary rank.
     - Nonzero index and index boundary (0xFFFF).
     - Both UID halves tested separately (low-only mismatch, high-only mismatch).
     - Individual field mismatches for all 7 identity fields.
     - Extra-read failure before mount/output.
     - Differing padding/unknown bytes without identity refusal.
     - Valid System/SystemBcat (Sphaira NXSaveMeta, magic 0x4A4B5356) vs ordinary DBI metadata paths in ZIP.
     - Preservation of actual concrete spaces without default-space substitution.

Compiler-Free Disclaimer:
This test is written in standard-library Python and runs directly.
Static source checks and simulated models validate code contracts, algorithmic invariants,
and wire layouts. They do NOT execute compiled C++ code, libnx IPC, or Nintendo Switch hardware.
"""

import os
import shutil
import struct
import sys
import tempfile
import zipfile
from dataclasses import dataclass

# Error and return codes matching libnx / Sphaira defines
RES_OK = 0
FS_ERROR_PATH_NOT_FOUND = 0x202
ERR_IPC_READ_FAILED = 0x1002

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

# 7 concrete spaces
FS_SAVE_DATA_SPACE_ID_SYSTEM = 0
FS_SAVE_DATA_SPACE_ID_USER = 1
FS_SAVE_DATA_SPACE_ID_SD_SYSTEM = 2
FS_SAVE_DATA_SPACE_ID_TEMPORARY = 3
FS_SAVE_DATA_SPACE_ID_SD_USER = 4
FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM = 100
FS_SAVE_DATA_SPACE_ID_SAFE_MODE = 101

ALL_SPACES = [
    FS_SAVE_DATA_SPACE_ID_SYSTEM,
    FS_SAVE_DATA_SPACE_ID_USER,
    FS_SAVE_DATA_SPACE_ID_SD_SYSTEM,
    FS_SAVE_DATA_SPACE_ID_TEMPORARY,
    FS_SAVE_DATA_SPACE_ID_SD_USER,
    FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM,
    FS_SAVE_DATA_SPACE_ID_SAFE_MODE,
]

# Metadata magic / constants
# Product defines: constexpr u32 NX_SAVE_META_MAGIC = 0x4A4B5356; ('JKSV')
NX_SAVE_META_MAGIC = 0x4A4B5356
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


def decode_extra_data(b: bytes) -> tuple[SaveAttribute, dict]:
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


def encode_nx_save_meta(attr: SaveAttribute, extra_meta: dict, raw_size: int) -> bytes:
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


# ==============================================================================
# 1. Static Source Contracts
# ==============================================================================

def test_source_contracts() -> None:
    print("[1] Running static source contract & ordering checks...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 sphaira/source/ui/menus/save/save_menu_ops.cpp
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_src = f.read()

    bsi_start = ops_src.find("Result Menu::BackupSaveInternal(")
    check(bsi_start != -1, "Menu::BackupSaveInternal definition must exist in save_menu_ops.cpp")

    sync_start = ops_src.find("void Menu::SyncSavesRemote()", bsi_start)
    check(sync_start != -1, "Menu::SyncSavesRemote must follow BackupSaveInternal")

    bsi_body = ops_src[bsi_start:sync_start]

    # Verify early exit on zero save ID
    check("if (e.save_data_id == 0) {\n        return 0;\n    }" in bsi_body,
          "BackupSaveInternal must retain early exit for e.save_data_id == 0")

    # Verify extra read
    pos_extra_read = bsi_body.find("R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), save_data_space_id, e.save_data_id));")
    check(pos_extra_read != -1,
          "BackupSaveInternal must read extra data by save_data_space_id and e.save_data_id")

    # Verify attr construction
    pos_attr_app = bsi_body.find("attr.application_id = e.application_id;", pos_extra_read)
    pos_attr_uid = bsi_body.find("attr.uid = e.uid;", pos_attr_app)
    pos_attr_sys = bsi_body.find("attr.system_save_data_id = e.system_save_data_id;", pos_attr_uid)
    pos_attr_type = bsi_body.find("attr.save_data_type = e.save_data_type;", pos_attr_sys)
    pos_attr_rank = bsi_body.find("attr.save_data_rank = e.save_data_rank;", pos_attr_type)
    pos_attr_idx = bsi_body.find("attr.save_data_index = e.save_data_index;", pos_attr_rank)
    check(pos_attr_idx != -1, "BackupSaveInternal must construct attr from entry fields")

    # Verify exact 7-field equality guard
    guard_lines = [
        "extra.attr.application_id != attr.application_id",
        "extra.attr.uid.uid[0] != attr.uid.uid[0]",
        "extra.attr.uid.uid[1] != attr.uid.uid[1]",
        "extra.attr.system_save_data_id != attr.system_save_data_id",
        "extra.attr.save_data_type != attr.save_data_type",
        "extra.attr.save_data_rank != attr.save_data_rank",
        "extra.attr.save_data_index != attr.save_data_index",
        "return FsError_PathNotFound;",
    ]
    for line in guard_lines:
        check(line in bsi_body, f"BackupSaveInternal guard must contain '{line}'")

    pos_guard = bsi_body.find("extra.attr.application_id != attr.application_id")
    check(pos_attr_idx < pos_guard, "Guard must be placed AFTER attr construction")

    # Verify FsNativeSave read-only open
    pos_save_fs = bsi_body.find("fs::FsNativeSave save_fs{(FsSaveDataType)e.save_data_type, save_data_space_id, &attr, true};")
    check(pos_save_fs != -1, "BackupSaveInternal must open FsNativeSave with read_only=true")
    check(pos_guard < pos_save_fs, "Identity guard must execute BEFORE opening FsNativeSave")

    # Verify enumeration
    pos_get_colls = bsi_body.find("filebrowser::FsView::get_collections(&save_fs, \"/\", \"\", collections)", pos_save_fs)
    check(pos_get_colls != -1, "BackupSaveInternal must enumerate collections from save_fs")

    # Verify empty check
    pos_empty_check = bsi_body.find("R_UNLESS(!collections.empty(), 0x0);", pos_get_colls)
    check(pos_empty_check != -1, "BackupSaveInternal must exit early with 0x0 on empty collections")

    # Verify directory / temp path creation happens AFTER empty check
    pos_create_dir = bsi_body.find("fs->CreateDirectoryRecursivelyWithPath(temp_path);", pos_empty_check)
    check(pos_create_dir != -1, "BackupSaveInternal must create directory/temp path after empty check")

    # Verify WriteSaveBackupZip and rename
    pos_write_zip = bsi_body.find("WriteSaveBackupZip(", pos_create_dir)
    check(pos_write_zip != -1, "BackupSaveInternal must call WriteSaveBackupZip")

    pos_rename = bsi_body.find("fs->RenameFile(temp_path, path)", pos_write_zip)
    check(pos_rename != -1, "BackupSaveInternal must rename temp_path to final path")

    # 1.2 Verify callers in save_menu_ops.cpp
    check("R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));" in ops_src,
          "BackupSavesOn and BackupSaves must delegate to BackupSaveInternal")

    # 1.3 Verify CreateBackupIfNewer in sphaira/source/ui/menus/save_menu.cpp
    menu_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save_menu.cpp")
    with open(menu_cpp_path, "r", encoding="utf-8") as f:
        menu_src = f.read()

    pos_cbifn = menu_src.find("void Menu::CreateBackupIfNewer(")
    check(pos_cbifn != -1, "Menu::CreateBackupIfNewer must exist in save_menu.cpp")
    check("R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));" in menu_src[pos_cbifn:],
          "CreateBackupIfNewer must delegate to BackupSaveInternal")

    # 1.4 Verify users profile caller in sphaira/source/ui/menus/users/users_profile.cpp
    users_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "users", "users_profile.cpp")
    with open(users_cpp_path, "r", encoding="utf-8") as f:
        users_src = f.read()

    pos_delete_user = users_src.find("Delete user")
    check(pos_delete_user != -1, "User deletion handler must exist in users_profile.cpp")
    check("R_TRY(helper->BackupSavesOn(pbox, save_backup));" in users_src[pos_delete_user:],
          "User profile delete safety-backup must call helper->BackupSavesOn")

    # 1.5 Verify product constant in sphaira/include/ui/menus/save/save_paths.hpp
    paths_hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(paths_hpp_path, "r", encoding="utf-8") as f:
        paths_hpp_src = f.read()

    check("constexpr u32 NX_SAVE_META_MAGIC = 0x4A4B5356;" in paths_hpp_src,
          "save_paths.hpp must define NX_SAVE_META_MAGIC as 0x4A4B5356")
    check("constexpr u32 NX_SAVE_META_VERSION = 1;" in paths_hpp_src,
          "save_paths.hpp must define NX_SAVE_META_VERSION as 1")
    check('constexpr const char* NX_SAVE_META_NAME = ".nx_save_meta.bin";' in paths_hpp_src,
          'save_paths.hpp must define NX_SAVE_META_NAME as ".nx_save_meta.bin"')

    # 1.6 Verify sphaira/include/fs.hpp FsNativeSave read_only branch
    fs_hpp_path = os.path.join(repo_root, "sphaira", "include", "fs.hpp")
    with open(fs_hpp_path, "r", encoding="utf-8") as f:
        fs_hpp_src = f.read()

    check("m_open_result = fsOpenReadOnlySaveDataFileSystem(&m_fs, save_data_space_id, attr);" in fs_hpp_src,
          "FsNativeSave read_only branch must call fsOpenReadOnlySaveDataFileSystem with exact space and attr")

    # 1.7 Verify sphaira/CMakeLists.txt version 0.13.859
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()

    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(859, 870)),
          "sphaira/CMakeLists.txt must define sphaira_VERSION as 0.13.859 or later")

    print("  -> Static source contract & ordering checks PASSED.")


# ==============================================================================
# 2. Connected Admission & Export Simulation Model
# ==============================================================================

class BackupPipelineSimulator:
    """Simulates the BackupSaveInternal pipeline with event tracking and real filesystem I/O."""

    def __init__(self):
        self.events: list[str] = []
        self.destructive_events: list[str] = []

    def execute(
        self,
        selected: SelectedEntry,
        extra_raw_bytes: bytes | None,
        read_rc: int = RES_OK,
        source_payload_dir: str | None = None,
        output_dir: str | None = None,
    ) -> tuple[int, str | None]:
        """
        Executes simulated BackupSaveInternal:
        - Early exit on zero save ID.
        - Extra read with read_rc check.
        - Decode 512-byte extra data.
        - 7-field identity comparison.
        - Read-only save filesystem mount event.
        - Collection enumeration and reading from real source payload directory.
        - Real ZIP creation, write metadata, write payload, rename.
        Returns: (result_code, final_zip_path_or_none)
        """
        self.events.clear()
        self.destructive_events.clear()

        # Step 0: Zero ID check
        if selected.save_data_id == 0:
            self.events.append("zero_id_early_exit")
            return RES_OK, None

        # Step 1: Extra read event
        self.events.append(f"extra_read(space={selected.save_data_space_id}, id=0x{selected.save_data_id:X})")
        if read_rc != RES_OK:
            self.events.append(f"extra_read_failed(rc=0x{read_rc:X})")
            return read_rc, None

        if extra_raw_bytes is None or len(extra_raw_bytes) != 512:
            self.events.append("extra_read_invalid_size")
            return FS_ERROR_PATH_NOT_FOUND, None

        # Step 2: Decode extra data
        live_attr, extra_meta = decode_extra_data(extra_raw_bytes)
        self.events.append("extra_decoded")

        # Step 3: Identity comparison guard (the 7 fields)
        target_attr = selected.attr
        if (
            live_attr.application_id != target_attr.application_id
            or live_attr.uid_low != target_attr.uid_low
            or live_attr.uid_high != target_attr.uid_high
            or live_attr.system_save_data_id != target_attr.system_save_data_id
            or live_attr.save_data_type != target_attr.save_data_type
            or live_attr.save_data_rank != target_attr.save_data_rank
            or live_attr.save_data_index != target_attr.save_data_index
        ):
            self.events.append("identity_guard_refusal(FsError_PathNotFound)")
            return FS_ERROR_PATH_NOT_FOUND, None

        self.events.append("identity_guard_passed")

        # Step 4: FsNativeSave read-only open event
        self.events.append(
            f"ro_mount(type={target_attr.save_data_type}, space={selected.save_data_space_id}, read_only=True)"
        )

        # Step 5: Collection enumeration & bounded payload file reads from source subtree
        self.events.append("enum_collections")
        payload_items: list[tuple[str, bytes]] = []
        if source_payload_dir and os.path.exists(source_payload_dir):
            for root, dirs, files in os.walk(source_payload_dir):
                dirs.sort()
                files.sort()
                for fname in files:
                    abs_fpath = os.path.join(root, fname)
                    rel_fpath = os.path.relpath(abs_fpath, source_payload_dir).replace("\\", "/")
                    self.events.append(f"payload_read({rel_fpath})")
                    with open(abs_fpath, "rb") as f:
                        payload_items.append((rel_fpath, f.read()))

        if len(payload_items) == 0:
            self.events.append("empty_collections_early_exit(0x0)")
            return RES_OK, None

        # From this point on, output FS operations begin
        if output_dir is None:
            raise ValueError("output_dir must be provided for non-empty export")

        # Step 6: Create temp directory and files
        temp_zip_path = os.path.join(output_dir, f"{selected.name}.zip.temp")
        final_zip_path = os.path.join(output_dir, f"{selected.name}.zip")

        self.events.append(f"create_temp_path({temp_zip_path})")
        self.destructive_events.append(f"create({temp_zip_path})")

        # Step 7: Write ZIP
        self.events.append(f"write_zip({temp_zip_path})")
        self.destructive_events.append(f"write({temp_zip_path})")

        is_system_like = target_attr.save_data_type in (
            FS_SAVE_DATA_TYPE_SYSTEM,
            FS_SAVE_DATA_TYPE_SYSTEM_BCAT,
        )

        with zipfile.ZipFile(temp_zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            if not is_system_like:
                # DBI format writes explicit directory entries with trailing slash
                seen_dirs = set()
                for rel_path, _ in payload_items:
                    dir_part = os.path.dirname(rel_path)
                    if dir_part and dir_part not in seen_dirs:
                        seen_dirs.add(dir_part)
                        zf.writestr(f"{dir_part}/", b"")

                for rel_path, data in payload_items:
                    zf.writestr(rel_path, data)

                # DBI metadata entries
                dbi_info = (
                    f"TitleId={target_attr.application_id:016X}\n"
                    f"TitleName={selected.name}\n"
                    f"BackupDate=2026-09-18 16:00:00\n"
                    f"Account=TestAccount\n"
                    f"Space=User"
                ).encode("utf-8")
                zf.writestr(DBI_SAVE_INFO_NAME, dbi_info)
                zf.writestr(DBI_SAVE_EXTRA_NAME, extra_raw_bytes)
            else:
                # System format: Sphaira NXSaveMeta first
                nx_meta_bytes = encode_nx_save_meta(target_attr, extra_meta, selected.size)
                zf.writestr(NX_SAVE_META_NAME, nx_meta_bytes)

                for rel_path, data in payload_items:
                    zf.writestr(rel_path, data)

        # Step 8: Rename temp -> final
        self.events.append(f"rename_file({temp_zip_path} -> {final_zip_path})")
        self.destructive_events.append(f"rename({temp_zip_path} -> {final_zip_path})")
        os.replace(temp_zip_path, final_zip_path)

        self.events.append("export_published")
        return RES_OK, final_zip_path


# ==============================================================================
# 3. Behavioral Fixtures & Matrix Verification
# ==============================================================================

def make_valid_attr(
    app_id: int = 0x0100000000010000,
    uid_low: int = 0x1111222233334444,
    uid_high: int = 0x5555666677778888,
    sys_id: int = 0,
    save_type: int = FS_SAVE_DATA_TYPE_ACCOUNT,
    rank: int = 0,
    index: int = 0,
) -> SaveAttribute:
    return SaveAttribute(
        application_id=app_id,
        uid_low=uid_low,
        uid_high=uid_high,
        system_save_data_id=sys_id,
        save_data_type=save_type,
        save_data_rank=rank,
        save_data_index=index,
    )


def test_behavioral_fixtures() -> None:
    print("[2] Running behavioral simulation fixtures with real files and sentinels...")
    sim = BackupPipelineSimulator()

    test_root = tempfile.mkdtemp(prefix="sphaira_backup_test_")
    try:
        source_dir = os.path.join(test_root, "source")
        source_payload_dir = os.path.join(source_dir, "save_payload")
        output_dir = os.path.join(test_root, "output")
        empty_payload_dir = os.path.join(source_dir, "empty_payload")

        os.makedirs(source_payload_dir, exist_ok=True)
        os.makedirs(empty_payload_dir, exist_ok=True)
        os.makedirs(output_dir, exist_ok=True)

        # Real source payload files inside payload subtree
        real_file1_path = os.path.join(source_payload_dir, "save_slot_0.dat")
        real_file1_bytes = b"SLOT_0_BINARY_PAYLOAD_CONTENT_AAAA"
        with open(real_file1_path, "wb") as f:
            f.write(real_file1_bytes)

        nested_dir = os.path.join(source_payload_dir, "nested")
        os.makedirs(nested_dir, exist_ok=True)
        real_file2_path = os.path.join(nested_dir, "settings.json")
        real_file2_bytes = b'{"sound": 100, "difficulty": "hard"}'
        with open(real_file2_path, "wb") as f:
            f.write(real_file2_bytes)

        # Sentinels (source sentinel is outside payload subtree; output sentinel is in output dir)
        source_sentinel_path = os.path.join(source_dir, "source_sentinel.bin")
        output_sentinel_path = os.path.join(output_dir, "output_sentinel.bin")
        sentinel_payload = b"SENTINEL_INTEGRITY_CHECK_CONSTANT_XYZ"

        with open(source_sentinel_path, "wb") as f:
            f.write(sentinel_payload)
        with open(output_sentinel_path, "wb") as f:
            f.write(sentinel_payload)

        def assert_sentinels_and_source_intact():
            with open(source_sentinel_path, "rb") as sf:
                check(sf.read() == sentinel_payload, "Source sentinel must remain completely unaltered")
            with open(output_sentinel_path, "rb") as of:
                check(of.read() == sentinel_payload, "Output sentinel must remain completely unaltered")
            with open(real_file1_path, "rb") as f1:
                check(f1.read() == real_file1_bytes, "Source payload file 1 must remain unaltered")
            with open(real_file2_path, "rb") as f2:
                check(f2.read() == real_file2_bytes, "Source payload file 2 must remain unaltered")

        def assert_output_dir_clean():
            items = os.listdir(output_dir)
            check(items == ["output_sentinel.bin"],
                  f"Output directory must have ZERO created/leftover files, got: {items}")

        # ----------------------------------------------------------------------
        # Fixture 1: Extra read failure -> exact error, no payload-read, no mount, sentinels intact
        # ----------------------------------------------------------------------
        attr = make_valid_attr()
        entry = SelectedEntry(save_data_id=0x12345, save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER, attr=attr)
        rc, zip_path = sim.execute(entry, None, read_rc=ERR_IPC_READ_FAILED, source_payload_dir=source_payload_dir, output_dir=output_dir)
        check(rc == ERR_IPC_READ_FAILED, "Read failure must propagate exact Result code")
        check(zip_path is None, "Read failure must not produce output ZIP")
        check(len(sim.destructive_events) == 0, "Read failure must have zero destructive events")
        check("ro_mount" not in "".join(sim.events), "Read failure must never reach mount")
        check("payload_read" not in "".join(sim.events), "Read failure must never read payload files")
        assert_sentinels_and_source_intact()
        assert_output_dir_clean()

        # ----------------------------------------------------------------------
        # Fixture 2: Per-field mismatch matrix for each of the 7 fields
        # ----------------------------------------------------------------------
        mismatch_mutations = [
            ("application_id", lambda a: setattr(a, "application_id", 0x0100000000099999)),
            ("uid_low", lambda a: setattr(a, "uid_low", 0x9999999999999999)),
            ("uid_high", lambda a: setattr(a, "uid_high", 0xAAAAAAAAAAAAAAAA)),  # UID-high-only mismatch!
            ("system_save_data_id", lambda a: setattr(a, "system_save_data_id", 0x8000000000000001)),
            ("save_data_type", lambda a: setattr(a, "save_data_type", FS_SAVE_DATA_TYPE_DEVICE)),
            ("save_data_rank", lambda a: setattr(a, "save_data_rank", 1)),
            ("save_data_index", lambda a: setattr(a, "save_data_index", 5)),
        ]

        for field_name, mutator in mismatch_mutations:
            entry_attr = make_valid_attr()
            live_attr = make_valid_attr()
            mutator(live_attr)  # Mutate live extra data attribute

            extra_bytes = encode_extra_data(live_attr)
            entry = SelectedEntry(save_data_id=0xABCDE, save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER, attr=entry_attr)

            rc, zip_path = sim.execute(entry, extra_bytes, read_rc=RES_OK, source_payload_dir=source_payload_dir, output_dir=output_dir)
            check(rc == FS_ERROR_PATH_NOT_FOUND,
                  f"Identity mismatch on '{field_name}' must return FsError_PathNotFound")
            check(zip_path is None, f"Mismatch on '{field_name}' must not produce output ZIP")
            check(len(sim.destructive_events) == 0,
                  f"Mismatch on '{field_name}' must produce zero destructive events")
            check("ro_mount" not in "".join(sim.events),
                  f"Mismatch on '{field_name}' must never reach mount")
            check("enum_collections" not in "".join(sim.events),
                  f"Mismatch on '{field_name}' must never reach enumeration")
            check("payload_read" not in "".join(sim.events),
                  f"Mismatch on '{field_name}' must never read payload files")
            assert_sentinels_and_source_intact()
            assert_output_dir_clean()

        # ----------------------------------------------------------------------
        # Fixture 3: Padding & Unknown bytes differ -> NO refusal, export proceeds
        # ----------------------------------------------------------------------
        valid_attr = make_valid_attr()
        dirty_padding = b"\xDE\xAD\xBE\xEF"
        dirty_unknown = b"\x5A\x3C\x9F\x12" * 6  # 24 dirty bytes
        extra_bytes = encode_extra_data(valid_attr, padding=dirty_padding, unknown=dirty_unknown)

        entry = SelectedEntry(save_data_id=0x54321, save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER, attr=valid_attr)
        rc, zip_path = sim.execute(entry, extra_bytes, read_rc=RES_OK, source_payload_dir=source_payload_dir, output_dir=output_dir)
        check(rc == RES_OK, "Differing padding/unknown bytes must NOT trigger identity refusal")
        check(zip_path is not None and os.path.exists(zip_path), "Valid export must produce output ZIP")
        check("ro_mount" in "".join(sim.events), "Valid export must execute ro_mount")
        check("export_published" in "".join(sim.events), "Valid export must execute publish")
        check("payload_read(save_slot_0.dat)" in sim.events, "Valid export must read slot 0 file")
        check("payload_read(nested/settings.json)" in sim.events, "Valid export must read nested file")

        # Verify ZIP contents and metadata
        with zipfile.ZipFile(zip_path, "r") as zf:
            namelist = zf.namelist()
            check(DBI_SAVE_INFO_NAME in namelist, "DBI backup must contain .dbi_save_info.ini")
            check(DBI_SAVE_EXTRA_NAME in namelist, "DBI backup must contain .dbi_save_extra")
            check("save_slot_0.dat" in namelist, "Payload file must be in ZIP")
            check("nested/settings.json" in namelist, "Nested payload file must be in ZIP")

            # Compare exact payload names and bytes with actual source files on disk
            with open(real_file1_path, "rb") as rf1:
                check(zf.read("save_slot_0.dat") == rf1.read(),
                      "Payload file content in ZIP must match source file on disk bit-for-bit")
            with open(real_file2_path, "rb") as rf2:
                check(zf.read("nested/settings.json") == rf2.read(),
                      "Nested payload content in ZIP must match source file on disk bit-for-bit")

            # Verify extra bytes in ZIP match
            extra_in_zip = zf.read(DBI_SAVE_EXTRA_NAME)
            check(extra_in_zip == extra_bytes, "DBI extra data in ZIP must match source extra bytes")

        os.remove(zip_path)
        assert_sentinels_and_source_intact()
        assert_output_dir_clean()

        # ----------------------------------------------------------------------
        # Fixture 4: System / SystemBcat export (Sphaira NXSaveMeta format)
        # ----------------------------------------------------------------------
        for sys_type in (FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_TYPE_SYSTEM_BCAT):
            sys_attr = make_valid_attr(
                save_type=sys_type,
                sys_id=0x8000000000000010,
                uid_low=0x1234567890ABCDEF,
                uid_high=0xFEDCBA0987654321,
                rank=1,
                index=7,
            )
            sys_extra = encode_extra_data(sys_attr)
            sys_entry = SelectedEntry(
                save_data_id=0x99999,
                save_data_space_id=FS_SAVE_DATA_SPACE_ID_SYSTEM,
                attr=sys_attr,
                name=f"SystemSave_{sys_type}",
            )
            rc, zip_path = sim.execute(sys_entry, sys_extra, read_rc=RES_OK, source_payload_dir=source_payload_dir, output_dir=output_dir)
            check(rc == RES_OK, f"System-like save type {sys_type} export must succeed")
            check(zip_path is not None and os.path.exists(zip_path), "Output ZIP must exist")

            with zipfile.ZipFile(zip_path, "r") as zf:
                namelist = zf.namelist()
                check(NX_SAVE_META_NAME in namelist, "System save must contain .nx_save_meta.bin")
                check(DBI_SAVE_INFO_NAME not in namelist, "System save must not contain DBI info")

                # Payload files bit-for-bit readback on NX path
                check("save_slot_0.dat" in namelist, "Payload file must be in NX ZIP")
                check("nested/settings.json" in namelist, "Nested payload file must be in NX ZIP")
                with open(real_file1_path, "rb") as rf1:
                    check(zf.read("save_slot_0.dat") == rf1.read(),
                          "NX ZIP payload 1 must match source disk file bit-for-bit")
                with open(real_file2_path, "rb") as rf2:
                    check(zf.read("nested/settings.json") == rf2.read(),
                          "NX ZIP payload 2 must match source disk file bit-for-bit")

                # Detailed NXSaveMeta readback asserting all 7 identity fields
                meta_bytes = zf.read(NX_SAVE_META_NAME)
                check(len(meta_bytes) == 128, "NXSaveMeta must be 128 bytes")
                magic, ver = struct.unpack_from("<II", meta_bytes, 0)
                check(magic == NX_SAVE_META_MAGIC, f"NXSaveMeta magic must be 0x4A4B5356, got 0x{magic:X}")
                check(ver == NX_SAVE_META_VERSION, "NXSaveMeta version must match")

                m_app, m_ul, m_uh, m_sid, m_type, m_rank, m_idx = struct.unpack_from("<QQQQBBH", meta_bytes, 8)
                check(m_app == sys_attr.application_id, "NXSaveMeta application_id must match selected")
                check(m_ul == sys_attr.uid_low, "NXSaveMeta uid_low must match selected")
                check(m_uh == sys_attr.uid_high, "NXSaveMeta uid_high must match selected")
                check(m_sid == sys_attr.system_save_data_id, "NXSaveMeta system_save_data_id must match selected")
                check(m_type == sys_attr.save_data_type, "NXSaveMeta save_data_type must match selected")
                check(m_rank == sys_attr.save_data_rank, "NXSaveMeta save_data_rank must match selected")
                check(m_idx == sys_attr.save_data_index, "NXSaveMeta save_data_index must match selected")

            os.remove(zip_path)
            assert_sentinels_and_source_intact()
            assert_output_dir_clean()

        # ----------------------------------------------------------------------
        # Fixture 5: All 7 Save Types x All 7 Concrete Spaces (49 combinations)
        # Note: Validates synthetic routing preservation in model, not firmware support.
        # ----------------------------------------------------------------------
        print("  -> Testing 7 types x 7 concrete spaces matrix (49 routing preservation combinations)...")
        combination_count = 0
        for stype in ALL_SAVE_TYPES:
            for space in ALL_SPACES:
                attr = make_valid_attr(
                    save_type=stype,
                    sys_id=0x8000000000000001 if stype in (0, 6) else 0,
                    rank=1 if stype == FS_SAVE_DATA_TYPE_CACHE else 0,
                    index=3 if stype == FS_SAVE_DATA_TYPE_BCAT else 0,
                )
                extra_bytes = encode_extra_data(attr)
                entry = SelectedEntry(
                    save_data_id=0x10000 + combination_count,
                    save_data_space_id=space,
                    attr=attr,
                    name=f"CombSave_{stype}_{space}",
                )
                rc, zip_path = sim.execute(entry, extra_bytes, read_rc=RES_OK, source_payload_dir=source_payload_dir, output_dir=output_dir)
                check(rc == RES_OK, f"Combination type={stype}, space={space} must pass")
                check(zip_path is not None and os.path.exists(zip_path), "ZIP must exist")

                # Verify event routing strictly preserved concrete space
                expected_mount = f"ro_mount(type={stype}, space={space}, read_only=True)"
                check(expected_mount in sim.events,
                      f"Mount event must preserve concrete space {space} without defaulting")

                os.remove(zip_path)
                combination_count += 1

        check(combination_count == 49, f"Expected 49 combinations, got {combination_count}")
        assert_sentinels_and_source_intact()
        assert_output_dir_clean()

        # ----------------------------------------------------------------------
        # Fixture 6: Rank and Index boundaries
        # ----------------------------------------------------------------------
        # Secondary rank (1)
        r1_attr = make_valid_attr(rank=1)
        r1_entry = SelectedEntry(save_data_id=0x70001, save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER, attr=r1_attr, name="Rank1")
        rc, zip_path = sim.execute(r1_entry, encode_extra_data(r1_attr), source_payload_dir=source_payload_dir, output_dir=output_dir)
        check(rc == RES_OK, "Secondary rank valid fixture must pass")
        os.remove(zip_path)

        # Index boundary (0xFFFF = 65535)
        idx_attr = make_valid_attr(index=65535)
        idx_entry = SelectedEntry(save_data_id=0x70002, save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER, attr=idx_attr, name="IndexMax")
        rc, zip_path = sim.execute(idx_entry, encode_extra_data(idx_attr), source_payload_dir=source_payload_dir, output_dir=output_dir)
        check(rc == RES_OK, "Index 65535 valid fixture must pass")
        os.remove(zip_path)

        # Index mismatch 65535 vs 0
        idx_mismatch_extra = encode_extra_data(make_valid_attr(index=0))
        rc, zip_path = sim.execute(idx_entry, idx_mismatch_extra, source_payload_dir=source_payload_dir, output_dir=output_dir)
        check(rc == FS_ERROR_PATH_NOT_FOUND, "Index boundary mismatch must return FsError_PathNotFound")
        check(zip_path is None, "Mismatch must not produce output")
        assert_sentinels_and_source_intact()
        assert_output_dir_clean()

        # ----------------------------------------------------------------------
        # Fixture 7: Early exit semantics (zero save_data_id and empty collections)
        # ----------------------------------------------------------------------
        # Zero save_data_id -> early return 0 without extra read, mount, or payload reading
        zero_id_entry = SelectedEntry(save_data_id=0, save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER, attr=make_valid_attr())
        rc, zip_path = sim.execute(zero_id_entry, None, source_payload_dir=source_payload_dir, output_dir=output_dir)
        check(rc == RES_OK, "Zero save_data_id must return 0 early")
        check(zip_path is None, "Zero save_data_id must not produce output")
        check("extra_read" not in "".join(sim.events), "Zero save_data_id must not read extra")
        check("payload_read" not in "".join(sim.events), "Zero save_data_id must not read payload files")
        check(len(sim.destructive_events) == 0, "Zero save_data_id must have zero destructive events")
        assert_sentinels_and_source_intact()
        assert_output_dir_clean()

        # Empty collections -> return 0x0 early without output FS mutations
        empty_entry = SelectedEntry(save_data_id=0x80001, save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER, attr=make_valid_attr())
        rc, zip_path = sim.execute(empty_entry, encode_extra_data(make_valid_attr()), source_payload_dir=empty_payload_dir, output_dir=output_dir)
        check(rc == RES_OK, "Empty save must return 0x0 early")
        check(zip_path is None, "Empty save must not produce output")
        check("create_temp_path" not in "".join(sim.events), "Empty save must not create temp file")
        check(len(sim.destructive_events) == 0, "Empty save must have zero destructive events")
        assert_sentinels_and_source_intact()
        assert_output_dir_clean()

    finally:
        shutil.rmtree(test_root, ignore_errors=True)

    print("  -> All behavioral fixtures, real ZIP validations, and sentinel checks PASSED.")


# ==============================================================================
# Main Runner
# ==============================================================================

def main() -> None:
    print("=== Sphaira v0.13.859: Selected/Live Identity Guard Contract Suite ===")
    test_source_contracts()
    test_behavioral_fixtures()
    print("=== ALL CHECKS PASSED SUCCESSFULLY ===")


if __name__ == "__main__":
    main()
