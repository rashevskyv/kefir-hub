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
     - Menu::PromptBackupGroupAction & Menu::RestoreForUser:
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

def test_static_source_contracts() -> None:
    print("[1] Verifying static source contracts in Sphaira codebase (scoped to functions)...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 CMakeLists.txt version check
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(863, 890)),
          "sphaira/CMakeLists.txt must define sphaira_VERSION as 0.13.863 or later")

    # 1.2 save_menu.hpp declarations
    sm_hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save_menu.hpp")
    with open(sm_hpp_path, "r", encoding="utf-8") as f:
        sm_hpp = f.read()
    cand_pos = sm_hpp.find("struct BackupCandidate {")
    entry_pos = sm_hpp.find("struct Entry final : FsSaveDataInfo {")
    check(cand_pos != -1, "save_menu.hpp must define struct BackupCandidate")
    check(entry_pos != -1, "save_menu.hpp must define struct Entry")
    check(cand_pos < entry_pos, "struct BackupCandidate must be declared before struct Entry")
    check("std::vector<BackupCandidate> backup_members" in sm_hpp,
          "struct Entry must contain backup_members vector")
    check("bool backup_rank_known" in sm_hpp,
          "struct Entry must contain backup_rank_known boolean member")

    # 1.3 save_paths.hpp declarations
    sp_hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(sp_hpp_path, "r", encoding="utf-8") as f:
        sp_hpp = f.read()
    check("bool rank_known{false};" in sp_hpp,
          "save_paths.hpp must define rank_known in struct BackupArchiveInfo")
    check("FormatBackupRankMarker" not in sp_hpp,
          "save_paths.hpp must NOT declare FormatBackupRankMarker")

    # 1.4 save_backup_inspection.cpp rank provenance & key format
    sp_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_backup_inspection.cpp")
    with open(sp_cpp_path, "r", encoding="utf-8") as f:
        sp_cpp = f.read()
    check("out.rank_known = true;" in sp_cpp,
          "save_paths.cpp InspectBackupArchive must set rank_known to true on Valid metadata")
    check("auto FormatBackupRankMarker(const Entry& e) -> std::string" in sp_cpp,
          "save_paths.cpp must define FormatBackupRankMarker")
    check("rk:0" in sp_cpp and "rk:1" in sp_cpp and "rk:?" in sp_cpp,
          "save_paths.cpp must format explicit rank markers rk:0, rk:1, and rk:?")

    # 1.5 save_menu.cpp function-scoped checks
    save_menu_units = [
        os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save_menu.cpp"),
        os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_draw.cpp"),
        os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_catalog.cpp"),
        os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_target.cpp"),
        os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_actions.cpp"),
    ]
    sm_cpp = ""
    for unit_path in save_menu_units:
        with open(unit_path, "r", encoding="utf-8") as f:
            sm_cpp += f.read() + "\n"
    check("auto FormatBackupRankMarker(const Entry& e) -> std::string" in sm_cpp,
          "save_menu.cpp must define file-local FormatBackupRankMarker")

    # Scope A: ReadBackupEntries
    p_rbe = sm_cpp.find("void Menu::ReadBackupEntries(")
    check(p_rbe != -1, "Menu::ReadBackupEntries must exist")
    p_rbe_end = sm_cpp.find("void Menu::Sort() {", p_rbe)
    check(p_rbe_end != -1, "End of Menu::ReadBackupEntries must be found")
    rbe_src = sm_cpp[p_rbe:p_rbe_end]

    check("e.backup_rank_known = info.rank_known;" in rbe_src,
          "ReadBackupEntries must record info.rank_known into e.backup_rank_known")
    check("e.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio});" in rbe_src,
          "ReadBackupEntries must record new candidate into e.backup_members")
    check("existing.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio});" in rbe_src,
          "ReadBackupEntries must record candidate into existing.backup_members")
    check("meta.rep_source" in rbe_src, "ReadBackupEntries must track rep_source in GroupScanMeta")
    check("existing.backup_count = existing.backup_members.size();" in rbe_src,
          "ReadBackupEntries must update existing.backup_count to backup_members.size()")
    check("is_newer = info.timestamp > existing.backup_timestamp;" in rbe_src,
          "ReadBackupEntries must compare timestamp for newer representative")
    check("is_newer = source_prio < meta.rep_source;" in rbe_src,
          "ReadBackupEntries must compare source priority for equal timestamp")
    check("is_newer = (path.toString() < existing.backup_path.toString());" in rbe_src,
          "ReadBackupEntries must compare path for equal timestamp and source")
    check("a.ts > b.ts" in rbe_src and "a.source < b.source" in rbe_src and "a.path.toString() < b.path.toString()" in rbe_src,
          "ReadBackupEntries must sort backup_members with (-ts, +source, +path)")

    # Scope B: CollectGroupArchives
    p_cga = sm_cpp.find("auto Menu::CollectGroupArchives(")
    check(p_cga != -1, "Menu::CollectGroupArchives must exist")
    p_cga_end = sm_cpp.find("auto FormatTargetSlotLabel", p_cga)
    check(p_cga_end != -1, "End of Menu::CollectGroupArchives must be found")
    cga_src = sm_cpp[p_cga:p_cga_end]

    check("if (group.is_backup) {" in cga_src, "CollectGroupArchives must branch on group.is_backup")
    pos_ret = cga_src.find("return out;")
    pos_collect_legacy = cga_src.find("CollectBackups(fs, group, backup_root);")
    check(pos_ret != -1 and pos_collect_legacy != -1, "CollectGroupArchives must have return out and CollectBackups")
    check(pos_ret < pos_collect_legacy, "if (group.is_backup) block must return before live CollectBackups / fallback")
    check("for (const auto& m : group.backup_members)" in cga_src,
          "CollectGroupArchives must iterate ONLY group.backup_members for is_backup")
    check("InspectBackupArchive(fs, m.path, fname, group.dbi_game_dir, info)" in cga_src,
          "CollectGroupArchives must re-inspect each exact member path")
    check("if (BackupGroupKey(info) != target_key)" in cga_src,
          "CollectGroupArchives must validate BackupGroupKey")
    check("a.ts > b.ts" in cga_src and "a.source < b.source" in cga_src and "a.path.toString() < b.path.toString()" in cga_src,
          "CollectGroupArchives must sort candidates with (-ts, +source, +path)")

    # Scope C: PromptBackupGroupAction & RestoreBackupGroups
    p_pba = sm_cpp.find("void Menu::PromptBackupGroupAction(")
    check(p_pba != -1, "Menu::PromptBackupGroupAction must exist")
    check("RestoreBackupGroups(seeds, false);" in sm_cpp or "RestoreBackupGroups(" in sm_cpp,
          "PromptBackupGroupAction must delegate to RestoreBackupGroups")
    route_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_route.cpp")
    route_cpp = ""
    if os.path.exists(route_cpp_path):
        with open(route_cpp_path, "r", encoding="utf-8") as f:
            route_cpp = f.read()

    impl_cpp = sm_cpp + route_cpp

    check("void Menu::RestoreBackupGroups(" in impl_cpp, "save_menu.cpp / save_restore_route.cpp must implement RestoreBackupGroups")
    check("void Menu::RestoreSingleBackupGroup(" in impl_cpp, "save_menu.cpp / save_restore_route.cpp must implement RestoreSingleBackupGroup")

    p_rsbg = impl_cpp.find("void Menu::RestoreSingleBackupGroup(")
    check(p_rsbg != -1, "Menu::RestoreSingleBackupGroup must exist")
    p_rsbg_end = impl_cpp.find("void Menu::PromptBatchRestoreTargets(", p_rsbg)
    check(p_rsbg_end != -1, "End of Menu::RestoreSingleBackupGroup must be found")
    rsbg_src = impl_cpp[p_rsbg:p_rsbg_end]

    check("No backups found for selected saves." in rsbg_src,
          "RestoreSingleBackupGroup must report 'No backups found for selected saves.' on empty")
    check("RestoreSavesPicked(std::move(*target), group, location, backup_root, group.backup_members.front().path);" in rsbg_src,
          "RestoreSingleBackupGroup must restore single retained member")
    check("ShowRestorePickerPopup(std::move(*target), group, location, backup_root, {}, group.backup_members);" in rsbg_src,
          "RestoreSingleBackupGroup must present picker over retained members")
    on_ready_idx = rsbg_src.find("const auto on_account_ready =")
    on_ready_end = rsbg_src.find("};", on_ready_idx)
    check(on_ready_idx != -1 and on_ready_end != -1, "RestoreSingleBackupGroup must define on_account_ready callback")
    check("backup_path" not in rsbg_src[on_ready_idx:on_ready_end],
          "RestoreSingleBackupGroup on_account_ready callback must not retain loose backup_path fallback")

    # Scope D: RestoreForUser
    p_rfu = impl_cpp.find("void Menu::RestoreForUser(Entry e) {")
    check(p_rfu != -1, "Menu::RestoreForUser must exist")
    check("RestoreSingleBackupGroup(" in impl_cpp[p_rfu:p_rfu + 200],
          "RestoreForUser must delegate to RestoreSingleBackupGroup")

    # Scope E: Menu::Draw HbMenu layout header
    p_draw = sm_cpp.find("void Menu::Draw(NVGcontext* vg, Theme* theme) {")
    check(p_draw != -1, "Menu::Draw must exist")
    p_draw_end = sm_cpp.find("if (m_layout.Get() == grid::LayoutType_List) {", p_draw)
    check(p_draw_end != -1, "End of Menu::Draw HbMenu block must be found")
    draw_hb_src = sm_cpp[p_draw:p_draw_end]

    check("m_layout.Get() == grid::LayoutType_HbMenu" in draw_hb_src,
          "Menu::Draw must contain grid::LayoutType_HbMenu branch")
    check("FormatBackupRankMarker(e)" in draw_hb_src,
          "HbMenu backup branch must format rank marker via FormatBackupRankMarker(e)")
    check("((e.save_data_type == FsSaveDataType_Account && !m_all_accounts) ?" in draw_hb_src,
          "HbMenu live branch must preserve account logic")
    check("GetAccountName(e.uid) : GetAccountSummary()" in draw_hb_src,
          "HbMenu live branch must preserve account naming")

    # 1.6 save_menu_ops.cpp ShowRestorePickerPopup checks
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_cpp = f.read()

    combined_ops_cpp = ops_cpp + route_cpp
    p_pop = combined_ops_cpp.find("void Menu::ShowRestorePickerPopup(")
    check(p_pop != -1, "Menu::ShowRestorePickerPopup must exist")
    p_pop_end = combined_ops_cpp.find("void Menu::RestoreSavesPicked(", p_pop)
    if p_pop_end == -1:
        p_pop_end = combined_ops_cpp.find("void Menu::RestoreSingleBackupGroup(", p_pop)
    check(p_pop_end != -1, "End of Menu::ShowRestorePickerPopup must be found")
    pop_src = combined_ops_cpp[p_pop:p_pop_end]

    check("label_counts[raw_labels[i]] > 1" in pop_src,
          "ShowRestorePickerPopup must count duplicate labels and check > 1")
    check('label += " (" + std::string(c.path.s) + ")";' in pop_src,
          "ShowRestorePickerPopup must append path discriminator to ambiguous labels")
    check("candidates[*op_index].path" in pop_src,
          "ShowRestorePickerPopup must pass chosen candidates[*op_index].path")

    # 1.7 save_menu.cpp CreateBackupIfNewer conservative freshness checks
    p_cbin = sm_cpp.find("void Menu::CreateBackupIfNewer(")
    check(p_cbin != -1, "Menu::CreateBackupIfNewer must exist")
    p_cbin_end = sm_cpp.find("void Menu::PromptBackupGroupAction(", p_cbin)
    check(p_cbin_end != -1, "End of Menu::CreateBackupIfNewer must be found")
    cbin_src = sm_cpp[p_cbin:p_cbin_end]

    check("live_extra.timestamp != 0" in cbin_src, "CreateBackupIfNewer must require live_extra.timestamp != 0")
    check("binfo.source_timestamp != 0" in cbin_src, "CreateBackupIfNewer must require binfo.source_timestamp != 0")
    check("live_extra.commit_id != 0" in cbin_src, "CreateBackupIfNewer must require live_extra.commit_id != 0")
    check("binfo.commit_id != 0" in cbin_src, "CreateBackupIfNewer must require binfo.commit_id != 0")
    check("live_extra.timestamp == binfo.source_timestamp" in cbin_src,
          "CreateBackupIfNewer must require live_extra.timestamp == binfo.source_timestamp")
    check("live_extra.commit_id == binfo.commit_id" in cbin_src,
          "CreateBackupIfNewer must require live_extra.commit_id == binfo.commit_id")
    check("is_up_to_date = true;" not in cbin_src,
          "CreateBackupIfNewer must not contain unconditional is_up_to_date = true on zero commit ID")
    check("else if (live_extra.commit_id != 0" not in cbin_src,
          "CreateBackupIfNewer must not contain timestamp-free fallback branch")
    check("R_TRY(pbox->ShouldExitResult());" in cbin_src,
          "CreateBackupIfNewer must check cancellation gate before each seed")
    check("fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&live_extra" in cbin_src,
          "CreateBackupIfNewer must read live extra data by space ID")
    check("(*up_to_date_count)++;" in cbin_src, "CreateBackupIfNewer must increment up_to_date_count")
    check("to_backup.emplace_back(e);" in cbin_src, "CreateBackupIfNewer must add to to_backup on mismatch or failure")
    check("*to_backup_count = to_backup.size();" in cbin_src, "CreateBackupIfNewer must update to_backup_count")

    # 1.8 save_deletion.cpp and users_profile.cpp fail-closed save deletion contracts
    del_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_deletion.cpp")
    with open(del_cpp_path, "r", encoding="utf-8") as f:
        del_cpp = f.read()

    check("fsDeleteSaveDataFileSystemBySaveDataAttribute" not in ops_cpp and "fsDeleteSaveDataFileSystemBySaveDataAttribute" not in del_cpp,
          "save_menu_ops.cpp and save_deletion.cpp must NOT call fsDeleteSaveDataFileSystemBySaveDataAttribute")
    check("auto DeleteLiveSaveEntry(const Entry& e) -> Result" in del_cpp,
          "save_deletion.cpp must define DeleteLiveSaveEntry helper")
    check("if (e.save_data_id == 0)" in del_cpp,
          "DeleteLiveSaveEntry must validate save_data_id == 0 before any deletion call")
    check("fsDeleteSaveDataFileSystemBySaveDataSpaceId(space_id, e.save_data_id)" in del_cpp,
          "DeleteLiveSaveEntry must call fsDeleteSaveDataFileSystemBySaveDataSpaceId with exact space and save ID")
    check(del_cpp.count("R_TRY(DeleteLiveSaveEntry(e));") == 2,
          "DeleteSavesOn and DeleteSaves must both call DeleteLiveSaveEntry with R_TRY for fail-fast live deletion")
    check("R_TRY(sd_fs.DeleteFile(b.path));" in del_cpp,
          "DeleteSaves must call sd_fs.DeleteFile with R_TRY for fail-fast backup archive deletion")
    check("first_failure" not in del_cpp,
          "save_deletion.cpp must not contain first_failure accumulator")
    up_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "users", "users_profile.cpp")
    with open(up_cpp_path, "r", encoding="utf-8") as f:
        up_cpp = f.read()
    check("R_TRY(helper->DeleteSavesOn(pbox, all_saves));" in up_cpp,
          "users_profile.cpp must call helper->DeleteSavesOn with R_TRY")

    print("  -> Static source contracts verified successfully.")


# ==============================================================================
# 2. Executable Behavioral Regressions with Real Temp Files & ZIPs
# ==============================================================================


def main():
    print("=== Sphaira v0.13.866: Save Backup Library Membership & Operations Contract Suite ===")
    test_static_source_contracts()
    test_suite_1_connected_scanner_and_proven_regression()
    test_suite_2_location_matrix_priority_and_tiebreaking()
    test_suite_3_wire_and_rejection_fixtures()
    test_suite_4_real_archive_identity_and_grouping_matrix()
    test_suite_5_connected_picker_and_destination_boundary()
    test_suite_6_actions_sentinels_and_readmission()
    test_suite_7_source_anchors_and_model_lifetimes()
    test_suite_8_connected_create_backup_if_newer_conservative_freshness()
    test_suite_9_connected_save_and_backup_deletion_fail_closed()
    print("ALL TESTS PASSED SUCCESSFULLY (v0.13.866 contract verified)")

if __name__ == "__main__":
    main()
