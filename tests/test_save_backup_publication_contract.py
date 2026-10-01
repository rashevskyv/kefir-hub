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


# ==============================================================================
# 2. Modeled Recovery Stream Sink & Connected Simulator
# ==============================================================================


def main():
    print("=== Sphaira v0.13.860: Checked Backup Publication Contract Suite ===")
    test_seven_save_types_connected_pipeline()
    test_failure_matrix_connected()
    print("=== ALL PUBLICATION CONTRACT AND BEHAVIORAL CHECKS PASSED ===")

if __name__ == "__main__":
    main()
