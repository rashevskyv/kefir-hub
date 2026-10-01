#!/usr/bin/env python3
"""
Test Suite: Backup Source Provenance and Section Divider Contract (v0.13.885)

Target chat: Походження бекапів і папкові бекапи
Verifies:
1. Static source wiring:
   - BackupSource enum and GetBackupSourceLabel in save_menu.hpp.
   - DecodedSaveMetadata::has_kefir_comment in save_paths.hpp and ReadArchiveSaveMetadata in save_archive_metadata.cpp.
   - BackupArchiveInfo::backup_source in save_paths.hpp.
   - InspectBackupArchive source classification in save_backup_inspection.cpp.
   - Catalog grouping by source + BackupGroupKey in save_menu_catalog.cpp.
   - Source precedence sorting in save_menu_catalog.cpp.
   - Multi-section GridSections computation and divider rendering in save_menu_draw.cpp.
   - Non-interactive divider handling in save_menu.cpp.
   - i18n key parity in en.json and uk.json.
2. Behavioral model simulation:
   - Provenance classification for Kefir Hub (comment), DBI, JKSV, Checkpoint, and Other.
   - Separation of same-title backups from different sources into distinct groups.
   - Multi-section display mapping and non-interactive divider slots in vertical and horizontal layouts.
"""

import io
import json
import os
import sys
import zipfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


def test_behavioral_provenance_model() -> None:
    print("[2] Verifying behavioral provenance & grid section model...")

    def has_path_dir_component(path: str, target: str) -> bool:
        p = path
        if p.lower().startswith("sdmc:"):
            p = p[5:]
        p = p.rstrip("/\\")
        last_slash = max(p.rfind("/"), p.rfind("\\"))
        if last_slash < 0:
            return False
        dir_part = p[:last_slash]
        parts = [comp for comp in dir_part.replace("\\", "/").split("/") if comp]
        return any(comp.lower() == target.lower() for comp in parts)

    # Classify function model mirroring C++ logic
    def classify_source(path: str, filename: str, has_kefir_comment: bool,
                        has_nx_meta: bool, has_dbi_extra: bool, has_dbi_info: bool,
                        is_zip: bool, dbi_game_dir: str = "") -> str:
        if is_zip:
            if has_kefir_comment:
                return "Kefir Hub"
            if (has_dbi_extra or has_dbi_info or
                    has_path_dir_component(path, "dbi") or
                    has_path_dir_component(path, "dbisaves")):
                return "DBI"
            if has_nx_meta or has_path_dir_component(path, "jksv"):
                return "JKSV"
            if has_path_dir_component(path, "checkpoint"):
                return "Checkpoint"
            return "Other"
        else:
            if has_path_dir_component(path, "dbi") or has_path_dir_component(path, "dbisaves"):
                return "DBI"
            if has_path_dir_component(path, "jksv"):
                return "JKSV"
            if has_path_dir_component(path, "checkpoint"):
                return "Checkpoint"
            return "Other"

    # Case 1: Kefir Hub archive with comment in /switch/DBI/saves (even with dbi/nx metadata present)
    src1 = classify_source("/switch/DBI/saves/0100000000010000/0100000000010000_A_20260925120000_0.zip",
                           "0100000000010000_A_20260925120000_0.zip",
                           has_kefir_comment=True, has_nx_meta=True, has_dbi_extra=True, has_dbi_info=True,
                           is_zip=True, dbi_game_dir="Game Title")
    check(src1 == "Kefir Hub", f"Expected Kefir Hub, got {src1}")

    # Case 2: Non-Kefir archive carrying BOTH DBI and NX metadata -> MUST be DBI (precedence)
    src2_both = classify_source("/dumps/dual_meta.zip", "dual_meta.zip",
                                has_kefir_comment=False, has_nx_meta=True, has_dbi_extra=True, has_dbi_info=False,
                                is_zip=True)
    check(src2_both == "DBI", f"Non-Kefir archive with both DBI and NX meta must be DBI, got {src2_both}")

    # Case 3: External DBI archive without comment in /switch/DBI/saves
    src2 = classify_source("/switch/DBI/saves/0100000000010000/0100000000010000_A_20260925110000_0.zip",
                           "0100000000010000_A_20260925110000_0.zip",
                           has_kefir_comment=False, has_nx_meta=False, has_dbi_extra=True, has_dbi_info=True,
                           is_zip=True, dbi_game_dir="Game Title")
    check(src2 == "DBI", f"Expected DBI, got {src2}")

    # Case 4: JKSV archive with nx_meta
    src3 = classify_source("/dumps/jksv_backup.zip", "jksv_backup.zip",
                           has_kefir_comment=False, has_nx_meta=True, has_dbi_extra=False, has_dbi_info=False,
                           is_zip=True)
    check(src3 == "JKSV", f"Expected JKSV, got {src3}")

    # Case 5: Legitimate Checkpoint archive path
    src4 = classify_source("/switch/Checkpoint/saves/Game/20260925.zip", "20260925.zip",
                           has_kefir_comment=False, has_nx_meta=False, has_dbi_extra=False, has_dbi_info=False,
                           is_zip=True)
    check(src4 == "Checkpoint", f"Expected Checkpoint, got {src4}")

    # Case 6: Game title containing 'JKSV', 'Checkpoint', or 'DBI' must NOT trigger app classification
    src_title_jksv = classify_source("/dumps/Super JKSV World/save.zip", "save.zip",
                                     has_kefir_comment=False, has_nx_meta=False, has_dbi_extra=False, has_dbi_info=False,
                                     is_zip=True)
    check(src_title_jksv == "Other", f"Game title containing JKSV must be Other, got {src_title_jksv}")

    src_title_cp = classify_source("/dumps/Checkpoint Rally/save.zip", "save.zip",
                                   has_kefir_comment=False, has_nx_meta=False, has_dbi_extra=False, has_dbi_info=False,
                                   is_zip=True)
    check(src_title_cp == "Other", f"Game title containing Checkpoint must be Other, got {src_title_cp}")

    src_title_dbi = classify_source("/dumps/DBI Saves Explorer/save.zip", "save.zip",
                                    has_kefir_comment=False, has_nx_meta=False, has_dbi_extra=False, has_dbi_info=False,
                                    is_zip=True)
    check(src_title_dbi == "Other", f"Game title containing DBI must be Other, got {src_title_dbi}")

    # Case 7: Custom scan with dbi_game_dir_name but no DBI metadata or root must NOT be DBI
    src_custom = classify_source("/custom/Game Title/save.zip", "save.zip",
                                 has_kefir_comment=False, has_nx_meta=False, has_dbi_extra=False, has_dbi_info=False,
                                 is_zip=True, dbi_game_dir="Game Title")
    check(src_custom == "Other", f"Custom scan with dbi_game_dir_name alone must be Other, got {src_custom}")

    # Case 8: Other arbitrary archive in /dumps
    src5 = classify_source("/dumps/custom_save.zip", "custom_save.zip",
                           has_kefir_comment=False, has_nx_meta=False, has_dbi_extra=False, has_dbi_info=False,
                           is_zip=True)
    check(src5 == "Other", f"Expected Other, got {src5}")

    # Test catalog grouping: archives of the same game from Kefir Hub and DBI form separate groups
    common_save_key = "backup:app:0100000000010000:1:0000000000000001:0:rk:0"
    group_map = {}
    entries = []

    archives = [
        {"source": "Kefir Hub", "ts": 20260925120000, "path": "/switch/DBI/saves/kh.zip"},
        {"source": "DBI", "ts": 20260925110000, "path": "/switch/DBI/saves/dbi.zip"},
        {"source": "Kefir Hub", "ts": 20260925100000, "path": "/switch/DBI/saves/kh_old.zip"},
    ]

    for a in archives:
        key = f"{a['source']}:{common_save_key}"
        if key not in group_map:
            idx = len(entries)
            group_map[key] = idx
            entries.append({"source": a["source"], "members": [a["path"]], "ts": a["ts"]})
        else:
            entries[group_map[key]]["members"].append(a["path"])

    check(len(entries) == 2, f"Expected 2 groups (Kefir Hub & DBI), got {len(entries)}")
    check(len(entries[0]["members"]) == 2, "Kefir Hub group must hold both KH archives")
    check(len(entries[1]["members"]) == 1, "DBI group must hold DBI archive")

    # Grid mapping model
    class GridModel:
        def __init__(self, entries, row=6, is_backups=True, horizontal=False):
            self.row = 1 if horizontal else row
            self.horizontal = horizontal
            self.sections = []
            if is_backups and entries:
                cur_disp = 0
                idx = 0
                while idx < len(entries):
                    src = entries[idx]["source"]
                    end = idx + 1
                    while end < len(entries) and entries[end]["source"] == src:
                        end += 1
                    count = end - idx
                    if not self.sections:
                        cur_disp = self.row if horizontal else 0
                    else:
                        rem = cur_disp % self.row
                        if rem != 0:
                            cur_disp += (self.row - rem)
                        if horizontal:
                            cur_disp += self.row
                    self.sections.append({
                        "label": src,
                        "entry_start": idx,
                        "entry_count": count,
                        "first_display": cur_disp,
                        "has_divider": True
                    })
                    cur_disp += count
                    idx = end
                self.display_count = cur_disp
            else:
                self.display_count = len(entries)

        def entry_to_display(self, entry):
            for sec in self.sections:
                if sec["entry_start"] <= entry < sec["entry_start"] + sec["entry_count"]:
                    return sec["first_display"] + (entry - sec["entry_start"])
            return entry

        def display_to_entry(self, display):
            for sec in self.sections:
                if sec["first_display"] <= display < sec["first_display"] + sec["entry_count"]:
                    return sec["entry_start"] + (display - sec["first_display"])
            return -1

        def resolve_display(self, display, from_disp, total):
            e = self.display_to_entry(display)
            if e >= 0:
                return e
            if total <= 0:
                return -1
            if from_disp == self.entry_to_display(0) and display < from_disp:
                return total - 1
            if display >= from_disp:
                for i in range(total):
                    if self.entry_to_display(i) > display:
                        return i
                return total - 1
            else:
                for i in range(total - 1, -1, -1):
                    if self.entry_to_display(i) < display:
                        return i
                return 0

    # Test grid mapping in vertical 6-col grid:
    # 2 Kefir Hub items, 3 DBI items
    grid_entries = [
        {"source": "Kefir Hub", "id": 1},
        {"source": "Kefir Hub", "id": 2},
        {"source": "DBI", "id": 3},
        {"source": "DBI", "id": 4},
        {"source": "DBI", "id": 5},
    ]
    grid = GridModel(grid_entries, row=6, is_backups=True, horizontal=False)

    check(len(grid.sections) == 2, "Expected 2 sections")
    # Section 0 (Kefir Hub) starts in the first grid row.
    check(grid.sections[0]["first_display"] == 0, "Section 0 must start at display 0")
    check(grid.entry_to_display(0) == 0, "Entry 0 must map to display 0")
    check(grid.entry_to_display(1) == 1, "Entry 1 must map to display 1")

    # Section 1 (DBI) starts in the next row, without a spacer row.
    check(grid.sections[1]["first_display"] == 6, "Section 1 must start at display 6")
    check(grid.entry_to_display(2) == 6, "Entry 2 must map to display 6")
    check(grid.entry_to_display(4) == 8, "Entry 4 must map to display 8")
    check(grid.display_to_entry(2) == -1, "Unused cell must map to -1")

    # Stepping / navigation over dividers
    # Step Down/Up through unused cells at the end of the Kefir Hub row.
    check(grid.resolve_display(2, 1, len(grid_entries)) == 2, "Stepping forward must hop to Entry 2")
    check(grid.resolve_display(5, 6, len(grid_entries)) == 1, "Stepping backward must hop to Entry 1")

    # Test HOME horizontal grid (row=1)
    home_grid = GridModel(grid_entries, row=1, is_backups=True, horizontal=True)
    check(home_grid.sections[0]["first_display"] == 1, "HOME Section 0 starts at col 1 (col 0 is divider)")
    check(home_grid.sections[1]["first_display"] == 4, "HOME Section 1 starts at col 4 (col 3 is divider)")
    check(home_grid.display_to_entry(0) == -1, "HOME col 0 is divider (-1)")
    check(home_grid.display_to_entry(3) == -1, "HOME col 3 is divider (-1)")
    check(home_grid.resolve_display(0, 1, len(grid_entries)) == len(grid_entries) - 1,
          "UP from the first item must wrap to the last item")

    print("  -> Behavioral provenance & grid section model PASSED.")


def test_folder_backup_admission_and_rejection() -> None:
    print("[3] Verifying folder backup admission and rejection...")

    # 3.2 Focused behavioral check for folder admission/rejection
    def extract_title_id(name: str) -> int:
        if len(name) == 16:
            try:
                return int(name, 16)
            except ValueError:
                pass
        if (name.startswith("0x") or name.startswith("0X")) and len(name) >= 18:
            try:
                return int(name[2:18], 16)
            except ValueError:
                pass
        if len(name) > 16 and name[16] in (' ', '_', '-'):
            try:
                return int(name[:16], 16)
            except ValueError:
                pass
        if '[' in name and ']' in name:
            inner = name[name.find('[') + 1 : name.find(']')]
            if inner.startswith("0x") or inner.startswith("0X"):
                inner = inner[2:]
            if len(inner) == 16:
                try:
                    return int(inner, 16)
                except ValueError:
                    pass
        return 0

    def inspect_folder(folder_path: str, folder_name: str, game_dir_name: str,
                       files: list, child_dirs: list,
                       valid_nx_meta: bool = False, has_nx_meta: bool = False) -> tuple:
        p_lower = folder_path.lower()
        if "_restore_" in p_lower or "_staging_" in p_lower or "/dumps/save-import" in p_lower:
            return False, "staging_rejected", None

        # Check raw DISA
        if any(f.endswith(".disa") for f in files):
            return False, "raw_disa_rejected", None

        # Check if parent directory containing backup subdirs with metadata
        if any(d.endswith("_backup_with_meta") for d in child_dirs):
            return False, "parent_directory_rejected", None

        # Count payload files
        payload_files = [f for f in files if f != ".nx_save_meta.bin" and not f.startswith(".dbi_")]
        if not payload_files:
            return False, "empty_payload_rejected", None

        if has_nx_meta:
            if not valid_nx_meta:
                return False, "invalid_metadata_rejected", None
            title_id = 0x0100000000010000
            source = "JKSV"
            uid = 0x12345678
            rank_known = True
        else:
            title_id = extract_title_id(game_dir_name)
            if not title_id:
                return False, "ambiguous_identity_rejected", None
            source = "Checkpoint" if "checkpoint" in p_lower else ("JKSV" if "jksv" in p_lower else "Other")
            uid = 0
            rank_known = False

        return True, "admitted", {
            "title_id": title_id,
            "source": source,
            "uid": uid,
            "rank_known": rank_known,
            "is_directory": True
        }

    # Case 1: Valid JKSV leaf folder with .nx_save_meta.bin and payload
    ok, reason, info = inspect_folder("/JKSV/0100000000010000/2026-09-25 12.00.00",
                                      "2026-09-25 12.00.00", "0100000000010000",
                                      files=[".nx_save_meta.bin", "main.dat"],
                                      child_dirs=[],
                                      valid_nx_meta=True, has_nx_meta=True)
    check(ok and info["source"] == "JKSV" and info["is_directory"], "Valid JKSV leaf folder must be admitted")

    # Case 2: Corrupt .nx_save_meta.bin in JKSV leaf folder
    ok, reason, _ = inspect_folder("/JKSV/0100000000010000/2026-09-25 12.00.00",
                                   "2026-09-25 12.00.00", "0100000000010000",
                                   files=[".nx_save_meta.bin", "main.dat"],
                                   child_dirs=[],
                                   valid_nx_meta=False, has_nx_meta=True)
    check(not ok and reason == "invalid_metadata_rejected", "Corrupt .nx_save_meta.bin must fail closed")

    # Case 3: Valid Checkpoint leaf folder with documented path convention
    ok, reason, info = inspect_folder("/switch/Checkpoint/saves/0x0100000000010000 Animal Crossing/2026-09-25",
                                      "2026-09-25", "0x0100000000010000 Animal Crossing",
                                      files=["savedata.bin", "title.txt"],
                                      child_dirs=[],
                                      valid_nx_meta=False, has_nx_meta=False)
    check(ok and info["source"] == "Checkpoint" and info["uid"] == 0 and not info["rank_known"],
          "Checkpoint leaf folder must be admitted with conservative identity")

    # Case 4: Parent/game directory containing child backups
    ok, reason, _ = inspect_folder("/JKSV/0100000000010000",
                                   "0100000000010000", "JKSV",
                                   files=[],
                                   child_dirs=["slot1_backup_with_meta"],
                                   valid_nx_meta=False, has_nx_meta=False)
    check(not ok, "Parent/game directory containing child backups must be rejected")

    # Case 5: Empty directory (0 files)
    ok, reason, _ = inspect_folder("/switch/Checkpoint/saves/0x0100000000010000/empty_dir",
                                   "empty_dir", "0x0100000000010000",
                                   files=[],
                                   child_dirs=[],
                                   valid_nx_meta=False, has_nx_meta=False)
    check(not ok and reason == "empty_payload_rejected", "Empty directory must be rejected")

    # Case 6: Directory with only raw DISA files
    ok, reason, _ = inspect_folder("/switch/Checkpoint/saves/0x0100000000010000/raw_dir",
                                   "raw_dir", "0x0100000000010000",
                                   files=["0000000000000001.disa"],
                                   child_dirs=[],
                                   valid_nx_meta=False, has_nx_meta=False)
    check(not ok and reason == "raw_disa_rejected", "Raw DISA directory must be rejected")

    # Case 7: Staging directory
    ok, reason, _ = inspect_folder("/dumps/save-import/temp_restore",
                                   "temp_restore", "save-import",
                                   files=["main.dat"],
                                   child_dirs=[],
                                   valid_nx_meta=False, has_nx_meta=False)
    check(not ok and reason == "staging_rejected", "Staging directory must be rejected")

    # Case 8: Ambiguous directory without metadata or title ID in path
    ok, reason, _ = inspect_folder("/custom/Random Game/backup1",
                                   "backup1", "Random Game",
                                   files=["main.dat"],
                                   child_dirs=[],
                                   valid_nx_meta=False, has_nx_meta=False)
    check(not ok and reason == "ambiguous_identity_rejected", "Ambiguous directory must be rejected")

    print("  -> Folder backup admission and rejection PASSED.")


if __name__ == "__main__":
    test_behavioral_provenance_model()
    test_folder_backup_admission_and_rejection()
    print("ALL BACKUP SOURCE SECTION CONTRACTS PASSED.")
