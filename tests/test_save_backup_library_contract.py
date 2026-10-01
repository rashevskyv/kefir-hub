#!/usr/bin/env python3
"""
Test Suite: Save Backup Library Group Membership and Action Contract (Sphaira v0.13.863)

Target chat: Аудит і виправлення системи сейвів
Scope & Verification Boundary:
1. Static Source Contracts (Scoped to function bodies):
   - sphaira/CMakeLists.txt: set(sphaira_VERSION 0.13.863).
   - sphaira/include/ui/menus/save_menu.hpp:
     - struct BackupCandidate declared before struct Entry.
     - struct Entry contains std::vector<BackupCandidate> backup_members.
   - sphaira/source/ui/menus/save_menu.cpp:
     - Menu::ReadBackupEntries:
       - Records every admitted candidate into e.backup_members / existing.backup_members.
       - Tracks rep_source in GroupScanMeta.
       - Updates backup_count = backup_members.size().
       - Updates representative (backup_timestamp, backup_path, source_timestamp, commit_id) on newer ts,
         lower source prio, or smaller path string.
       - Sorts each group's backup_members (-ts, +source, +path).
     - Menu::CollectGroupArchives:
       - Evaluates if (group.is_backup) and returns before CollectBackups / fallback.
       - Iterates ONLY group.backup_members.
       - Re-inspects each exact path via InspectBackupArchive.
       - Validates BackupGroupKey.
       - Omits missing, invalid, or changed-group members.
       - Deduplicates exact paths.
       - Sorts candidates (-ts, +source, +path).
       - Returns empty list on no matches without fallback.
     - Menu::PromptBackupGroupAction & Menu::RestoreSingleBackupGroup:
       - } else if (e.is_backup) { precedes old fallback.
       - Displays "No backups found for selected saves." without stale backup_path fallback.
     - Menu::CreateBackupIfNewer:
       - Enforces strict dual nonzero equality freshness predicate:
         live_extra.timestamp != 0 && binfo.source_timestamp != 0 &&
         live_extra.commit_id != 0 && binfo.commit_id != 0 &&
         live_extra.timestamp == binfo.source_timestamp &&
         live_extra.commit_id == binfo.commit_id.
       - Rejects zero timestamps, zero commit IDs, mismatches, or missing metadata.
   - sphaira/source/ui/menus/save/save_menu_ops.cpp:
     - Menu::ShowRestorePickerPopup:
       - Counts label duplicates via label_counts.
       - Appends (" + path + ") only to ambiguous labels (count > 1).
       - Passes chosen candidates[*op_index].path to RestoreSavesPicked.

2. Executable Behavioral Regressions (Real Temp Files, Real ZIPs, Wire Decoder):
   - Suite 1: Connected Scanner & Proven Regression:
     - Real filesystem walk (read_backup_entries_model) creates EntryModel from two arbitrary-named ZIPs in /dumps.
     - Old narrow discovery (CollectBackups) returns empty and falls back to only 1 path, losing the 2nd artifact.
     - New library CollectGroupArchives retains both, exactly matching backup_count == 2.
     - Representative attributes match backup_members[0].
   - Suite 2: Location Matrix, Priorities, & Tie-breaking:
     - All scan depths: DBI direct, DBI date folders, /dumps L1..L3, custom roots L1..L3.
     - Exact tie-break ordering (-ts, +source, +path).
     - Path deduplication across overlapping configured roots.
   - Suite 3: Wire & Rejection Fixtures:
     - Genuine POSIX seconds converted via posix_to_timestamp.
     - Filename timestamp overrides metadata fallback; hand-renamed files fall back to metadata calendar.
     - Real ZIP rejection: duplicate metadata, parent directory conflict, conflicting NX/DBI,
       ambiguous 86 layout, malformed/invalid source fields. All fail closed and never enter scan groups.
   - Suite 4: Real Archive Identity & Grouping Matrix:
     - Real ZIPs: rank 0 vs 1 (same group), index 0 vs 1 (separate groups), Cache index (separate groups),
       Account vs Device (separate groups), System IDs (separate groups), UID-high (separate groups),
       source-space facts (same group), System vs SystemBcat (separate groups), mixed wire versions (same group),
       metadata-free DBI legacy (parsed by filename).
     - Exact artifact membership mapping verified for all 16 scan-produced groups via collect_group_archives_model.
   - Suite 5: Connected Picker Callback & Destination Object Boundary:
     - Modeled picker callback passes chosen candidate path and explicit live target to restore boundary.
     - Payload read back from chosen ZIP to prove exact artifact read.
     - Explicit recording boundary assertions: recorded target app/UID/save ID equal explicit live target,
       and recorded UID differs from source candidate UID (Account user remap boundary).
     - All 9 live target fields captured before callback and asserted unchanged after.
     - Picker label disambiguation for duplicate dates and hand-renamed identical basenames.
   - Suite 6: Actions, Sentinels, & Readmission:
     - Scan-produced groups used for Verify, Prune, Delete.
     - Post-scan added ZIP is not touched or rediscovered; bytes identical.
     - Foreign injected path dropped by CollectGroupArchives; bytes identical.
     - Deleted member omitted; all deleted returns empty without stale fallback.
     - Timestamp modification picked up by fresh inspection and re-sorted.
     - Prune deletes older exact paths, preserves newest and foreign sentinels; byte-for-byte preservation on newest.
     - Fresh timestamp reinspection & re-sorting on arbitrary-named ZIPs without mutating group.backup_members scan inventory.
     - Post-scan admission changes on actual scan-produced groups (corrupt member, changed-group member, valid member,
       and empty collection on removal without stale fallback).
     - Delete deletes all group paths, preserves sentinels, cleans parent nonrecursively.
   - Suite 7: Scoped Source Anchors & Model-Level Lifetimes:
     - Scoped checks on extracted function blocks.
     - Model-level container append, copy, move, and lambda capture invariants.
   - Suite 8: Connected CreateBackupIfNewer Conservative Freshness Contract:
     - Complete 18-case matrix executed against real temporary ZIP archives and live extra data models.
     - Defect reproductions on zero timestamps and zero commit IDs proving old predicate flaw and new fix.
     - Newest archive inspection priority (older matching archives do not suppress backups).
     - Cancellation early exit and independent multi-seed counters.

NO C++ COMPILATION, NO BINARIES, NO NRO, NO WSL REQUIRED. Pure Python stdlib.
"""

import io
import os
import re
import shutil
import struct
import sys
import tempfile
import time
import warnings
import zipfile
warnings.filterwarnings("ignore", category=UserWarning)
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Set, Tuple
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

# Reuse bounded reference model decoder and wire constants from test_save_metadata_wire_contract
from tests.test_save_metadata_wire_contract import (
    ReferenceSaveMetadataDecoder,
    pack_jksv85,
    pack_jksv_tail86,
    pack_jksv_middle86,
    pack_sphaira128,
    pack_dbi_raw512,
    make_zip,
    JKSV_MAGIC,
    JKSV_REVISION,
    SPHAIRA_MAGIC,
    SPHAIRA_VERSION,
    VALID_SPACES,
)


def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ==============================================================================
# Wire Constants & Pure-Python Data Structures
# ==============================================================================

NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"

SAVE_TYPE_SYSTEM = 0
SAVE_TYPE_ACCOUNT = 1
SAVE_TYPE_BCAT = 2
SAVE_TYPE_DEVICE = 3
SAVE_TYPE_TEMPORARY = 4
SAVE_TYPE_CACHE = 5
SAVE_TYPE_SYSTEM_BCAT = 6
from contract_fixtures.save_backup_library_models import (
    RES_OK, FS_ERROR_PATH_NOT_FOUND, FS_ERROR_TARGET_LOCKED,
    FS_SAVE_DATA_SPACE_ID_SYSTEM, FS_SAVE_DATA_SPACE_ID_USER,
    FS_SAVE_DATA_SPACE_ID_SD_USER, FS_SAVE_DATA_TYPE_SYSTEM,
    FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_TYPE_DEVICE,
    FS_SAVE_DATA_TYPE_SYSTEM_BCAT, NX_SAVE_META_MAGIC, NX_SAVE_META_NAME,
    DBI_SAVE_INFO_NAME, DBI_SAVE_EXTRA_NAME, is_system_like, make_zip_file,
    read_zip_entry, BackupCandidateModel, BackupArchiveInfoModel, EntryModel,
    backup_group_key, posix_to_timestamp, parse_backup_name_timestamp,
    parse_dbi_type_letter, parse_dbi_backup_app_id, parse_dbi_backup_index,
    inspect_backup_archive_model
)
from contract_fixtures.save_backup_library_scanner import (
    read_backup_entries_model, collect_group_archives_model,
    collect_group_archives_legacy_model, FsSaveDataExtraDataModel,
    create_backup_if_newer_model, disambiguate_picker_labels
)
from contract_fixtures.save_backup_library_suites_1_to_4 import (
    test_suite_1_connected_scanner_and_proven_regression,
    test_suite_2_location_matrix_priority_and_tiebreaking,
    test_suite_3_wire_and_rejection_fixtures,
    test_suite_4_real_archive_identity_and_grouping_matrix
)
from contract_fixtures.save_backup_library_suites_5_to_6 import (
    test_suite_5_connected_picker_and_destination_boundary,
    test_suite_6_actions_sentinels_and_readmission
)
from contract_fixtures.save_backup_library_suites_7_to_8 import (
    test_suite_7_source_anchors_and_model_lifetimes,
    test_suite_8_connected_create_backup_if_newer_conservative_freshness
)
from contract_fixtures.save_backup_library_suite_9 import (
    test_suite_9_connected_save_and_backup_deletion_fail_closed
)
from contract_fixtures.save_backup_library_suite_10 import (
    test_suite_10_game_grouping_and_batch_admission
)


# ==============================================================================
# 2. Executable Behavioral Regressions with Real Temp Files & ZIPs
# ==============================================================================


def main():
    print("=== Sphaira v0.13.866: Save Backup Library Membership & Operations Contract Suite ===")
    test_suite_1_connected_scanner_and_proven_regression()
    test_suite_2_location_matrix_priority_and_tiebreaking()
    test_suite_3_wire_and_rejection_fixtures()
    test_suite_4_real_archive_identity_and_grouping_matrix()
    test_suite_5_connected_picker_and_destination_boundary()
    test_suite_6_actions_sentinels_and_readmission()
    test_suite_7_source_anchors_and_model_lifetimes()
    test_suite_8_connected_create_backup_if_newer_conservative_freshness()
    test_suite_9_connected_save_and_backup_deletion_fail_closed()
    test_suite_10_game_grouping_and_batch_admission()
    print("ALL TESTS PASSED SUCCESSFULLY (v0.13.894 contract verified)")

if __name__ == "__main__":
    main()
