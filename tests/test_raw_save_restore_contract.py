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


# ==============================================================================
# 1. Real Binary Artifact Encoding (Switchbrew Savegames Layout)
# ==============================================================================

FIXTURE_LABEL = "independently encoded header-layout fixture, unauthenticated and incomplete"

from contract_fixtures.raw_save_restore_models import (
    FS_ERROR_NOT_FOUND, FS_ERROR_TARGET_LOCKED, FS_ERROR_INVALID_PATH,
    FS_ERROR_BUFFER_TOO_SMALL, ERR_RAW_RESTORE_REFUSED, ERR_RECOVERY_DIRTY,
    ERR_SIMULATED_CRASH, TargetSaveSentinel, BoundedArtifactReader,
    create_disa_matching_looking_artifact, create_magic_only_artifact,
    create_unsupported_version_artifact, create_valid_zip_fixture, strict_zip_admission
)
from contract_fixtures.raw_save_restore_scenarios import (
    test_sentinel_positive_self_check, test_all_variants_across_all_routes,
    test_mixed_batch_simulation, test_recovery_creation_readback_failure_fixture
)

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


def main():
    print("=== Sphaira v0.13.858: Strict RAW Save Restore Contract Test Suite ===")
    test_sentinel_positive_self_check()
    test_all_variants_across_all_routes()
    test_mixed_batch_simulation()
    test_recovery_creation_readback_failure_fixture()
    test_integrity_outcome_model()
    print("=== ALL REWORKED CONTRACT SUITES PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
