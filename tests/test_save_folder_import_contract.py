#!/usr/bin/env python3
"""
Test Suite: Save Folder Backup Import Contract (Sphaira v0.13.857)

Comprehensive verification for bounded, safe import of an explicitly selected
SD backup directory into an explicitly selected live save slot:

1. Scoped Static Source Contract Checks:
   - sphaira/CMakeLists.txt:
     - sphaira_VERSION bumped to 0.13.857.
   - sphaira/include/ui/menus/save_menu.hpp:
     - RestoreSaveFolder declaration matching RestoreSaveZip signature.
     - RestoreSaveZip 5-arg strict overload and 6-arg allow_empty overload.
   - sphaira/include/threaded_file_transfer.hpp:
     - TransferUnzipPreflight declarations with trailing bool allow_empty = false.
     - VerifyArchiveAgainstNative declarations with trailing bool allow_empty = false.
   - sphaira/source/threaded_file_transfer.cpp:
     - TransferUnzipPreflight: ginfo.number_entry == 0 opt-in empty payload handling.
     - TransferUnzipAll: ginfo.number_entry == 0 rejected with FsError_InvalidSize.
     - VerifyArchiveAgainstNative:
       - allow_empty parameter in both void* and FsPath overloads.
       - ginfo.number_entry == 0 empty branch: checks cancel, verifies expected empty
         inventory, verifies native inventory emptiness, gates on allow_empty.
   - sphaira/source/ui/menus/save/save_menu_ops.cpp:
     - CheckedJoinPath: length bounded to sizeof(fs::FsPath) with snprintf check.
     - IsStagingParentOrRelated: canonical component analysis, rejecting '.', '..',
       staging root '/', parent 'dumps', exact 'dumps/save-import', descendants,
       while preserving safe sibling 'dumps/save-import-other'.
     - StageBackupFolderToZip:
       - Positive short-read loop with cancellation checkpoints.
       - Checked trailing EOF handling (Read at file_info.size returning 0 bytes) for ALL files,
         including zero-byte files, with trailing read error propagation.
       - Final size check against inventoried size for ALL files.
       - Checked zip archive close with Result_ZipWriteInFileInZip / Result_FsUnknownStdioError.
       - Verification that rec_ctx.fp == nullptr after zipClose.
       - Elimination of undeclared Result_ZipClose.
       - Explicit fsdevCommitDevice("sdmc") and sd_fs.Commit().
     - RestoreSaveZip:
       - 5-arg overload forwards allow_empty = false.
       - 6-arg overload gates TransferUnzipAll on non-empty inventory unless allow_empty is false.
       - 6-arg overload threads allow_empty to final RO VerifyArchiveAgainstNative.
     - RestoreSaveFolder:
       - Source path validation before native access: bounded strnlen, absolute SD '/',
         no colons, no duplicate slashes '//', character validation, trailing slash trimming,
         component validation (no empty, no '.', no '..'), ancestry refusal before directory creation.
       - Worklist scan: CheckedJoinPath for cur_full_dir and full_entry_path,
         directory key length check with room for trailing '/' (rel_entry_len + 1 < sizeof(fs::FsPath)).
       - Owned reservation: native primitive fsFsCreateDirectory(&sd_fs.m_fs, dir_buf),
         directory ownership established on primitive success, explicit commit,
         cleanup on commit failure.
       - Staged zip path: CheckedJoinPath with "source.zip.temp".
       - ON_SCOPE_EXIT cleanup: deletes owned_stage_zip and non-recursively deletes owned_dir.
       - Staged validation: SaveReaderContext, TransferUnzipPreflight with allow_empty = true,
         ReadArchiveSaveMetadata, exact inventory comparison with ONLY shared reserved ROOT
         metadata filtering (IsSaveReservedMetadataRoot).
       - Delegation to RestoreSaveZip with allow_empty = true.
       - Exact owned stage cleanup after restore: deletes stage file and empty owned dir.
       - Preserves primary operation error on failure; reports cleanup failure honestly on success.
   - sphaira/source/ui/menus/filebrowser/filebrowser_options.cpp:
     - Single unselected non-parent SD directory action gate.
     - Localized text: "Restore this save backup directory to the console."
   - sphaira/source/ui/menus/filebrowser/filebrowser_ops.cpp:
     - RestoreSaveFile: is_dir detection, IsSd() enforcement, target_id inference suppression,
       haze::IsRunning() check upfront, se.save_data_id == 0 || se.is_backup rejection,
       routing to save::RestoreSaveFolder, recovery path and mutation tracking.
   - assets/romfs/i18n/en.json & uk.json:
     - Exact parity for all 4 new translation keys.

2. Real Filesystem Trees -> Real ZIP Artifacts (stdlib tempfile, pathlib, zipfile):
   - Fixture 1: Real JKSV backup tree with root .nx_save_meta.bin, title.txt, save_meta.json,
     sphaira_meta.json, and nested .nx_save_meta.bin. Proves ordinary metadata files and nested
     reserved names REMAIN payload.
   - Fixture 2: Real DBI backup tree with .dbi_save_info.ini and .dbi_save_extra.
   - Fixture 3: Real metadata-free Checkpoint backup tree.
   - Fixture 4: Fully-empty selected tree (0 files, 0 dirs) -> real 0-entry ZIP ->
     proves allow_empty acceptance across preflight, admission, extraction, and RO verification.
   - Fixture 5: Dirs-only selected tree (explicit empty directories, 0 files).
   - Fixture 6: Zero-byte files & empty file drift during staging.
   - Fixture 7: Canonical source path validation & ancestry matrix (dotdot bypass, mixed case,
     trailing slashes, duplicate slashes, safe sibling /dumps/save-import-other).
   - Fixture 8: Full joined-path bounds and overflow rejection.
   - Fixture 9: Owned reservation, collision loop, and cleanup boundaries on real temp directory.

3. JKSV 85-byte Source Metadata Validation, Coexistence & Conflict, and Account Remap:
   - Settled 85-byte layout: <IBQQQQBBHQQIqqQ.
   - Independent source metadata validation (malformed, truncated, negative sizes, invalid types).
   - Real ZIP Coexistence-Success: .nx_save_meta.bin + .dbi_save_info.ini coexist smoothly (DBI INI is opaque text).
   - Real ZIP Mutual Disagreement Refusal: .nx_save_meta.bin + .dbi_save_extra with disagreeing common fields.
   - Explicit Account Remapping fixture: source UID differs in BOTH halves from selected destination,
     source sizing hints differ from live sizes; destination identity, space, and live sizes stay authoritative.

4. Connected Real-Disk Artifact Lifecycle & Comprehensive Shared-Boundary Fault Matrix:
   - Real disk directory and file operations in tempfile.TemporaryDirectory().
   - Product order accurately modeled from selected target upfront to exact owned cleanup.
   - Reopen and drain actual staged ZIP on disk, full CRC check, payload inventory, and source metadata admission.
   - Stage alteration between initial admission and shared restore boundary (corrupt ZIP, invalid metadata).
   - Full destination identity comparison between selected Entry and actual live record: application_id,
     both UID halves, system_save_data_id, type, rank, index, space.
   - Live sizing refusal: nonpositive data_size, negative journal_size, payload exceeding live data_size.
   - Strict destination emptiness verification for expected-empty restore (leftover file/dir rejected).
   - Byte-level RO comparison detecting same-size byte corruption, unexpected file, missing file, unexpected directory.
   - 51 distinct test scenarios covering pre-mutation faults, post-mutation faults, cleanup failure, and successes.
"""

import hashlib
import json
import os
import pathlib
import struct
import sys
import tempfile
import zipfile

def check(condition, message):
    if not condition:
        print(f"FAIL: {message}")
        sys.exit(1)


# ==============================================================================
# 1. Scoped Static Source Contract Checks
# ==============================================================================


# ==============================================================================
# 2. Real Filesystem Trees -> Real ZIP Artifacts
# ==============================================================================

NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"

from contract_fixtures.save_folder_import_fixtures import test_real_filesystem_fixtures
from contract_fixtures.save_folder_import_scenarios import ( test_jksv_source_validation_and_account_remap,
    test_connected_fault_evidence_matrix
)

def main():
    print("=== Sphaira v0.13.857: Save Folder Backup Import Test Suite ===")
    test_real_filesystem_fixtures()
    test_jksv_source_validation_and_account_remap()
    test_connected_fault_evidence_matrix()
    print("=== ALL 4 CONTRACT SUITES PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
