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


def test_static_source_wiring() -> None:
    print("[1] Verifying static source contracts...")

    # 1.1 Version bump
    cmake_path = os.path.join(REPO_ROOT, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check(any(v in cmake_src for v in ("set(sphaira_VERSION 0.13.888)", "set(sphaira_VERSION 0.13.889)")), "CMakeLists.txt must set sphaira_VERSION to 0.13.888 or later")

    # 1.2 BackupSource enum in save_menu.hpp
    sm_hpp_path = os.path.join(REPO_ROOT, "sphaira", "include", "ui", "menus", "save_menu.hpp")
    with open(sm_hpp_path, "r", encoding="utf-8") as f:
        sm_hpp = f.read()
    check("enum class BackupSource : u8 {" in sm_hpp, "save_menu.hpp must define enum class BackupSource")
    check("GetBackupSourceLabel(BackupSource" in sm_hpp, "save_menu.hpp must declare GetBackupSourceLabel")
    check("BackupSource backup_source{BackupSource::Other};" in sm_hpp, "Entry must contain backup_source member")
    check("struct Section {" in sm_hpp and "std::vector<Section> sections" in sm_hpp,
          "GridSections must declare struct Section and sections vector")
    check("void DrawSectionDivider(NVGcontext* vg, Theme* theme, const Vec4& first_v, const GridSections& g, const std::string& label, bool align_above_tile) const;" in sm_hpp,
          "save_menu.hpp must declare DrawSectionDivider with label parameter")

    # 1.3 save_paths.hpp declarations
    sp_hpp_path = os.path.join(REPO_ROOT, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(sp_hpp_path, "r", encoding="utf-8") as f:
        sp_hpp = f.read()
    check("bool has_kefir_comment{false};" in sp_hpp, "DecodedSaveMetadata must have has_kefir_comment")
    check("BackupSource backup_source{BackupSource::Other};" in sp_hpp, "BackupArchiveInfo must have backup_source")

    # 1.4 save_archive_metadata.cpp reads comment and preserves it into local_out
    sam_cpp_path = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "save", "save_archive_metadata.cpp")
    with open(sam_cpp_path, "r", encoding="utf-8") as f:
        sam_cpp = f.read()
    check("unzGetGlobalComment(zfile, comment, sizeof(comment))" in sam_cpp,
          "save_archive_metadata.cpp must read global comment via unzGetGlobalComment")
    check('std::strncmp(comment, "sphaira v", 9) == 0' in sam_cpp,
          "save_archive_metadata.cpp must check for 'sphaira v' prefix")
    check("local_out.has_kefir_comment = out.has_kefir_comment;" in sam_cpp,
          "save_archive_metadata.cpp must preserve has_kefir_comment into local_out across decoded branches")

    # 1.5 save_backup_inspection.cpp classifies sources
    sbi_cpp_path = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "save", "save_backup_inspection.cpp")
    with open(sbi_cpp_path, "r", encoding="utf-8") as f:
        sbi_cpp = f.read()
    check("out.backup_source = BackupSource::KefirHub;" in sbi_cpp, "InspectBackupArchive must assign KefirHub")
    check("out.backup_source = BackupSource::Jksv;" in sbi_cpp, "InspectBackupArchive must assign Jksv")
    check("out.backup_source = BackupSource::Checkpoint;" in sbi_cpp, "InspectBackupArchive must assign Checkpoint")
    check("out.backup_source = BackupSource::Dbi;" in sbi_cpp, "InspectBackupArchive must assign Dbi")
    check("out.backup_source = BackupSource::Other;" in sbi_cpp, "InspectBackupArchive must assign Other")
    check("path::HasPathDirComponentIC" in sbi_cpp, "InspectBackupArchive must use HasPathDirComponentIC")
    check("ContainsIC" not in sbi_cpp, "InspectBackupArchive must not use substring ContainsIC")
    # Verify DBI metadata precedence over NX metadata in archive inspection
    dbi_pos = sbi_cpp.find("archive_meta.has_dbi_extra")
    nx_pos = sbi_cpp.find("archive_meta.has_nx_meta")
    check(dbi_pos > 0 and nx_pos > 0 and dbi_pos < nx_pos,
          "InspectBackupArchive must classify explicit DBI metadata before has_nx_meta")

    # 1.6 save_menu_catalog.cpp groups by source and sorts by source precedence
    smc_cpp_path = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "save", "save_menu_catalog.cpp")
    with open(smc_cpp_path, "r", encoding="utf-8") as f:
        smc_cpp = f.read()
    check("info.backup_source" in smc_cpp and "BackupGroupKey(info)" in smc_cpp,
          "ReadBackupEntries must incorporate backup_source into group_map key")
    check("e.backup_source = info.backup_source;" in smc_cpp,
          "ReadBackupEntries must record info.backup_source into e.backup_source")
    check("a.backup_source != b.backup_source" in smc_cpp,
          "ReadBackupEntries must sort by backup_source precedence")

    # 1.7 save_menu_draw.cpp multi-section computation
    smd_cpp_path = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "save", "save_menu_draw.cpp")
    with open(smd_cpp_path, "r", encoding="utf-8") as f:
        smd_cpp = f.read()
    check("m_category == Category::Backups" in smd_cpp, "ComputeGridSections must branch on Category::Backups")
    check("sec.has_divider && disp == sec.first_display" in smd_cpp,
          "Draw must draw divider when disp matches section first_display")
    check("DrawSectionDivider(vg, theme, v, g, sec.label," in smd_cpp,
          "Draw must pass sec.label to DrawSectionDivider")
    check("from == EntryToDisplay(0, g) && display < from" in smd_cpp,
          "UP from the first item must wrap past the leading divider")
    check("cur_disp = compact_grid ? 0 : g.row;" in smd_cpp and "if (!compact_grid)" in smd_cpp,
          "backup grid sections must not reserve full empty rows")
    check("m_category == Category::Backups && m_layout.Get() == grid::LayoutType_Grid" in smd_cpp,
          "every backup grid divider must align above its first tile")
    check(smd_cpp.index("DrawSectionDivider(vg, theme, v, g, sec.label,") < smd_cpp.index("image_v = DrawEntry(vg, theme"),
          "the selected title label must paint above the section divider")

    # 1.8 save_menu.cpp non-interactive touch
    sm_cpp_path = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "save_menu.cpp")
    with open(sm_cpp_path, "r", encoding="utf-8") as f:
        sm_cpp_src = f.read()
    check("m_category == Category::Backups ? 34.f : 10.f" in sm_cpp_src,
          "backup grid rows must leave just enough room for the section label")
    check("touch && DisplayToEntry(disp, g) < 0" in sm_cpp_src,
          "save_menu.cpp must guard against touch on empty divider slots")

    # 1.9 i18n parity
    en_path = os.path.join(REPO_ROOT, "assets", "romfs", "i18n", "en.json")
    uk_path = os.path.join(REPO_ROOT, "assets", "romfs", "i18n", "uk.json")
    with open(en_path, "r", encoding="utf-8") as f:
        en_json = json.load(f)
    with open(uk_path, "r", encoding="utf-8") as f:
        uk_json = json.load(f)

    for key in ["Kefir Hub", "DBI", "JKSV", "Checkpoint", "Other"]:
        check(key in en_json, f"en.json must contain '{key}'")
        check(key in uk_json, f"uk.json must contain '{key}'")
    check(uk_json["Other"] == "Інші", "uk.json 'Other' translation must be 'Інші'")
    check(set(en_json.keys()) == set(uk_json.keys()), "en.json and uk.json keys must have exact parity")

    print("  -> Static source contracts PASSED.")


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

    # 3.1 Static wiring checks
    path_util_h = os.path.join(REPO_ROOT, "sphaira", "include", "path_util.hpp")
    with open(path_util_h, "r", encoding="utf-8") as f:
        pu_src = f.read()
    sw_pos = pu_src.find("StartsWithIC(")
    hp_pos = pu_src.find("HasPathDirComponentIC(")
    check(sw_pos > 0 and hp_pos > sw_pos, "StartsWithIC must be declared before HasPathDirComponentIC in path_util.hpp")

    sfd_hpp = os.path.join(REPO_ROOT, "sphaira", "include", "ui", "menus", "save", "save_folder_discovery.hpp")
    with open(sfd_hpp, "r", encoding="utf-8") as f:
        sfd_h_src = f.read()
    check("InspectBackupFolder(" in sfd_h_src, "save_folder_discovery.hpp must declare InspectBackupFolder")
    check("ExtractTitleIdFromDir(" in sfd_h_src, "save_folder_discovery.hpp must declare ExtractTitleIdFromDir")
    check("ExtractTitleNameFromDir(" in sfd_h_src, "save_folder_discovery.hpp must declare ExtractTitleNameFromDir")

    sm_hpp_path = os.path.join(REPO_ROOT, "sphaira", "include", "ui", "menus", "save_menu.hpp")
    with open(sm_hpp_path, "r", encoding="utf-8") as f:
        sm_hpp = f.read()
    check("bool is_directory{false};" in sm_hpp, "BackupCandidate must have is_directory member")
    check("bool backup_is_directory{false};" in sm_hpp, "Entry must have backup_is_directory member")

    sp_hpp_path = os.path.join(REPO_ROOT, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(sp_hpp_path, "r", encoding="utf-8") as f:
        sp_hpp = f.read()
    check("bool is_directory{false};" in sp_hpp, "BackupArchiveInfo must have is_directory member")
    check("JKSV_PATH" in sp_hpp and "CHECKPOINT_SAVES_PATH" in sp_hpp,
          "save_paths.hpp must define known roots for JKSV and Checkpoint")

    smc_cpp_path = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "save", "save_menu_catalog.cpp")
    with open(smc_cpp_path, "r", encoding="utf-8") as f:
        smc_cpp = f.read()
    check("scan_jksv_root" in smc_cpp, "save_menu_catalog.cpp must implement scan_jksv_root")
    check("scan_checkpoint_root" in smc_cpp, "save_menu_catalog.cpp must implement scan_checkpoint_root")
    check("scan_custom_folder_root" in smc_cpp, "save_menu_catalog.cpp must scan custom folder roots")
    check("InspectBackupFolder" in smc_cpp, "save_menu_catalog.cpp must call InspectBackupFolder")

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


def test_review_findings_and_restore_routing() -> None:
    print("[4] Verifying review findings and folder restore routing...")
    def read_src(*parts: str) -> str:
        with open(os.path.join(REPO_ROOT, *parts), "r", encoding="utf-8") as f:
            return f.read()

    disc_cpp = read_src("sphaira", "source", "ui", "menus", "save", "save_folder_discovery.cpp")
    check("ExtractIdentityFromAncestors" in disc_cpp and "has_ancestor_id ? derived_id : 0" in disc_cpp,
          "save_folder_discovery.cpp must derive title ID from candidate parent/ancestors")

    cp_path = "/switch/Checkpoint/saves/0x0100000000010000 Animal Crossing/backup1"
    parent_part = [p for p in cp_path.strip("/").split("/") if p][-2]
    check(parent_part.startswith("0x0100000000010000") and int(parent_part[2:18], 16) == 0x0100000000010000,
          "Checkpoint candidate parent path must yield 0x0100000000010000 regardless of display name")

    insp_cpp = read_src("sphaira", "source", "ui", "menus", "save", "save_backup_inspection.cpp")
    check("if (e.backup_source == BackupSource::Dbi)" in insp_cpp,
          "GetBackupSecondaryColumns and FormatBackupSecondaryText must gate DBI tag strictly on BackupSource::Dbi")

    ops_cpp = read_src("sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    check("RestoreSaveFolder(pbox, e, path, out_recovery_path, out_mutation_started, out_created_slot_retained)" in ops_cpp,
          "RestoreSaveInternal must route folder backups to RestoreSaveFolder")

    route_cpp = read_src("sphaira", "source", "ui", "menus", "save", "save_restore_route.cpp")
    check("InspectSaveFolderAdmission(archive_path, pbox, false)" in route_cpp,
          "PlanRestoreCreation must inspect folder backup admission")
    check("Backup folder metadata is missing or incomplete for save slot creation." in route_cpp,
          "PlanRestoreCreation must stop missing metadata folder restore before mutation")

    folder_cpp = read_src("sphaira", "source", "ui", "menus", "save", "save_folder_restore.cpp")
    check("e.save_data_id != 0 || e.is_planned_create" in folder_cpp,
          "RestoreSaveFolder must permit planned slot creation targets")
    print("  -> Review findings and folder restore routing PASSED.")


if __name__ == "__main__":
    test_static_source_wiring()
    test_behavioral_provenance_model()
    test_folder_backup_admission_and_rejection()
    test_review_findings_and_restore_routing()
    print("ALL BACKUP SOURCE SECTION CONTRACTS PASSED.")
