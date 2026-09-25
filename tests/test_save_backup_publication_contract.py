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


from contract_fixtures.save_backup_publication_scenarios import (
    test_seven_save_types_connected_pipeline, test_failure_matrix_connected
)

def test_source_contracts() -> None:
    print("[1] Running static source contracts & architecture checks...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 CMakeLists.txt version check
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(860, 880)),
          "sphaira/CMakeLists.txt must define sphaira_VERSION as 0.13.860 or later")

    # 1.2 sphaira/source/ui/menus/save/save_backup_writer.cpp & save_backup_pub.cpp
    writer_hpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_backup_writer.hpp")
    with open(writer_hpp_path, "r", encoding="utf-8") as f:
        writer_hpp_src = f.read()

    writer_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_backup_writer.cpp")
    with open(writer_cpp_path, "r", encoding="utf-8") as f:
        writer_src = f.read()

    pub_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_backup_pub.cpp")
    with open(pub_cpp_path, "r", encoding="utf-8") as f:
        ops_src = f.read()

    # WriteSaveBackupZip signature
    check("bool checked_stream = false" in writer_hpp_src or "bool checked_stream = false" in writer_src,
          "WriteSaveBackupZip must accept checked_stream parameter")
    check("const bool use_checked_stream = recovery_mode || checked_stream;" in writer_src,
          "WriteSaveBackupZip must select checked stream when recovery_mode or checked_stream is true")

    # RecoveryStreamContext callbacks checks
    pos_zip_close = writer_src.find('zipClose(zfile, "sphaira v" APP_VERSION_HASH)')
    check(pos_zip_close != -1, "WriteSaveBackupZip must close zip archive")
    pos_rec_check = writer_src.find("if (use_checked_stream) {", pos_zip_close)
    check(pos_rec_check != -1, "WriteSaveBackupZip must guard post-zip checks with use_checked_stream")
    rec_body = writer_src[pos_rec_check:pos_rec_check + 800]
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
    bsi_end = ops_src.find("} // namespace sphaira::ui::menu::save", bsi_start)
    check(bsi_end != -1, "namespace end boundary must exist")
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
    pos_scope = sd_block.find("ON_SCOPE_EXIT({")
    if pos_scope == -1:
        pos_scope = sd_block.find("ON_SCOPE_EXIT {")
    check(pos_scope != -1, "SD pipeline must define ON_SCOPE_EXIT cleanup")
    end_scope = sd_block.find("});", pos_scope)
    if end_scope == -1:
        end_scope = sd_block.find("};", pos_scope)
    scope_block = sd_block[pos_scope:end_scope]
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

    menu_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_actions.cpp")
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


def main():
    print("=== Sphaira v0.13.860: Checked Backup Publication Contract Suite ===")
    test_source_contracts()
    test_seven_save_types_connected_pipeline()
    test_failure_matrix_connected()
    print("=== ALL PUBLICATION CONTRACT AND BEHAVIORAL CHECKS PASSED ===")

if __name__ == "__main__":
    main()
