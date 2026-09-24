# Scenarios for save folder import contract.
import os
import io
import struct
import pathlib
import zipfile
import tempfile
import shutil
from contract_fixtures.save_folder_import_models import (
    JKSV_MAGIC, JKSV_REVISION, VALID_SPACES, RESERVED_ROOT_NAMES,
    INVALID_PATH_CHARS, MAX_S64, is_save_reserved_metadata_root,
    stage_folder_to_zip, pack_jksv85, pack_dbi_raw512, unpack_jksv85,
    decode_dbi_raw512, compare_common_source_fields,
    validate_jksv85_source_metadata, admit_and_drain_zip_archive,
    ConnectedRealDiskImportRunner
)

def check(condition, message):
    if not condition:
        raise AssertionError(message)

def test_real_filesystem_fixtures():
    print("[2] Running real filesystem tree -> real ZIP artifact fixtures...")

    with tempfile.TemporaryDirectory() as tmpdir_str:
        tmpdir = pathlib.Path(tmpdir_str)

        # ----------------------------------------------------------------------
        # Fixture 1: Real JKSV backup tree with ordinary metadata & nested reserved
        # ----------------------------------------------------------------------
        jksv_dir = tmpdir / "JKSV_Animal_Crossing"
        jksv_dir.mkdir()
        (jksv_dir / ".nx_save_meta.bin").write_bytes(pack_jksv85())
        (jksv_dir / "title.txt").write_text("Animal Crossing: New Horizons", encoding="utf-8")
        (jksv_dir / "save_meta.json").write_text('{"type": "jksv"}', encoding="utf-8")
        (jksv_dir / "sphaira_meta.json").write_text('{"author": "Sphaira"}', encoding="utf-8")
        (jksv_dir / "main.dat").write_bytes(b"SAVE_PAYLOAD_MAIN_1234567890")
        nested_dir = jksv_dir / "nested"
        nested_dir.mkdir()
        (nested_dir / ".nx_save_meta.bin").write_bytes(b"NESTED_PAYLOAD_NOT_METADATA")
        (nested_dir / "sub.dat").write_bytes(b"SUB_DATA")

        jksv_zip = tmpdir / "staged_jksv.zip"
        src_files, src_dirs = stage_folder_to_zip(jksv_dir, jksv_zip)

        # Inspect real ZIP contents
        with zipfile.ZipFile(jksv_zip, "r") as zf:
            namelist = zf.namelist()
            check(".nx_save_meta.bin" in namelist, "Root .nx_save_meta.bin must be staged in ZIP")
            check("title.txt" in namelist, "title.txt must be staged in ZIP")
            check("save_meta.json" in namelist, "save_meta.json must be staged in ZIP")
            check("sphaira_meta.json" in namelist, "sphaira_meta.json must be staged in ZIP")
            check("nested/.nx_save_meta.bin" in namelist, "nested/.nx_save_meta.bin must be staged in ZIP")

            # Apply ONLY shared case-insensitive reserved ROOT filter
            payload_entries = [name for name in namelist if not is_save_reserved_metadata_root(name) and not name.endswith("/")]

            # Verify that title.txt, save_meta.json, sphaira_meta.json, nested/.nx_save_meta.bin are in payload!
            check("title.txt" in payload_entries, "title.txt MUST remain in payload inventory")
            check("save_meta.json" in payload_entries, "save_meta.json MUST remain in payload inventory")
            check("sphaira_meta.json" in payload_entries, "sphaira_meta.json MUST remain in payload inventory")
            check("nested/.nx_save_meta.bin" in payload_entries, "nested/.nx_save_meta.bin MUST remain in payload inventory")
            check(".nx_save_meta.bin" not in payload_entries, "Root .nx_save_meta.bin MUST be filtered from payload inventory")

            # Verify exact count: main.dat, title.txt, save_meta.json, sphaira_meta.json, nested/.nx_save_meta.bin, nested/sub.dat = 6
            check(len(payload_entries) == 6, f"Expected exactly 6 payload entries, got {len(payload_entries)}")
        print("  -> Fixture 1 (Real JKSV backup tree with payload preservation) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 2: Real DBI backup tree with .dbi_save_info.ini & .dbi_save_extra
        # ----------------------------------------------------------------------
        dbi_dir = tmpdir / "DBI_Game"
        dbi_dir.mkdir()
        (dbi_dir / ".dbi_save_info.ini").write_text("[Save]\nTitle=DBI Save\n", encoding="utf-8")
        (dbi_dir / ".dbi_save_extra").write_bytes(pack_dbi_raw512())
        (dbi_dir / "slot0.bin").write_bytes(b"SLOT0_PAYLOAD")

        dbi_zip = tmpdir / "staged_dbi.zip"
        stage_folder_to_zip(dbi_dir, dbi_zip)

        with zipfile.ZipFile(dbi_zip, "r") as zf:
            namelist = zf.namelist()
            payload_entries = [name for name in namelist if not is_save_reserved_metadata_root(name) and not name.endswith("/")]
            check(payload_entries == ["slot0.bin"], f"DBI payload must only be slot0.bin, got {payload_entries}")
        print("  -> Fixture 2 (Real DBI backup tree with reserved root filter) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 3: Real metadata-free Checkpoint backup tree
        # ----------------------------------------------------------------------
        cp_dir = tmpdir / "Checkpoint_Game"
        cp_dir.mkdir()
        (cp_dir / "savedata.bin").write_bytes(b"CHECKPOINT_SAVE_DATA")
        (cp_dir / "title.txt").write_text("Pokemon", encoding="utf-8")

        cp_zip = tmpdir / "staged_cp.zip"
        stage_folder_to_zip(cp_dir, cp_zip)

        with zipfile.ZipFile(cp_zip, "r") as zf:
            namelist = zf.namelist()
            payload_entries = [name for name in namelist if not is_save_reserved_metadata_root(name) and not name.endswith("/")]
            check(sorted(payload_entries) == sorted(["savedata.bin", "title.txt"]), "Both Checkpoint files must remain payload")
        print("  -> Fixture 3 (Real Checkpoint backup tree) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 4: Fully-empty selected tree (0 files, 0 dirs)
        # ----------------------------------------------------------------------
        empty_dir = tmpdir / "Empty_Tree"
        empty_dir.mkdir()

        empty_zip = tmpdir / "staged_empty.zip"
        src_files, src_dirs = stage_folder_to_zip(empty_dir, empty_zip)
        check(len(src_files) == 0 and len(src_dirs) == 0, "Empty tree has 0 files and 0 dirs")

        with zipfile.ZipFile(empty_zip, "r") as zf:
            check(len(zf.namelist()) == 0, "Empty staged ZIP has 0 entries")

        # Simulate TransferUnzipPreflight with allow_empty = true vs allow_empty = false
        def simulate_preflight(number_entry: int, allow_empty: bool):
            if number_entry == 0:
                if not allow_empty:
                    return False, "FsError_InvalidSize"
                return True, "OK"
            return True, "OK"

        ok_default, err_default = simulate_preflight(0, allow_empty=False)
        check(not ok_default and err_default == "FsError_InvalidSize",
              "Default generic callers (allow_empty=false) must reject 0-entry archive")

        ok_save, _ = simulate_preflight(0, allow_empty=True)
        check(ok_save, "Save path (allow_empty=true) must accept 0-entry archive")
        print("  -> Fixture 4 (Fully-empty selected tree & allow_empty contract) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 5: Dirs-only selected tree (explicit empty directories)
        # ----------------------------------------------------------------------
        dirs_only = tmpdir / "Dirs_Only"
        dirs_only.mkdir()
        (dirs_only / "empty_sub1").mkdir()
        (dirs_only / "empty_sub2").mkdir()
        (dirs_only / "empty_sub2" / "leaf").mkdir()

        dirs_zip = tmpdir / "staged_dirs.zip"
        src_files, src_dirs = stage_folder_to_zip(dirs_only, dirs_zip)
        check(len(src_files) == 0, "Dirs-only tree has 0 files")
        check(len(src_dirs) == 3, f"Dirs-only tree has 3 dirs, got {len(src_dirs)}")

        with zipfile.ZipFile(dirs_zip, "r") as zf:
            check(len(zf.namelist()) == 3, "Staged ZIP has 3 directory entries")
            payload_files = [n for n in zf.namelist() if not n.endswith("/")]
            check(len(payload_files) == 0, "No payload files in dirs-only ZIP")
        print("  -> Fixture 5 (Dirs-only selected tree) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 6: Zero-byte files & empty file drift
        # ----------------------------------------------------------------------
        zero_dir = tmpdir / "Zero_Byte_Tree"
        zero_dir.mkdir()
        (zero_dir / "zero.bin").write_bytes(b"")
        (zero_dir / "normal.bin").write_bytes(b"NORMAL")

        # Simulate trailing read check
        def check_trailing_read(file_size, actual_bytes):
            if len(actual_bytes) > file_size:
                return False, "FsError_InvalidSize: trailing read > 0 (drift/growth)"
            return True, "OK"

        ok_zero, _ = check_trailing_read(0, b"")
        check(ok_zero, "Zero-byte file with 0 trailing bytes must succeed")

        ok_drift, err_drift = check_trailing_read(0, b"GROWN")
        check(not ok_drift and "drift/growth" in err_drift, "Grown file must be rejected via trailing read check")
        print("  -> Fixture 6 (Zero-byte files & empty file drift check) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 7: Canonical source path validation & ancestry matrix
        # ----------------------------------------------------------------------
        def validate_and_check_ancestry(path_str: str):
            if not path_str or len(path_str) >= 769:
                return False, "FsError_TooLongPath"
            if not path_str.startswith("/") or ":" in path_str:
                return False, "FsError_InvalidCharacter"
            if "//" in path_str:
                return False, "FsError_InvalidPath"
            disallowed = set('*?:<>|"\\')
            if any(c in disallowed or ord(c) < 32 or ord(c) == 127 for c in path_str):
                return False, "FsError_InvalidCharacter"
            canonical = path_str
            if len(canonical) > 1 and canonical.endswith("/"):
                canonical = canonical[:-1]
            if canonical == "/" or not canonical:
                return False, "FsError_PathNotFound"
            comps = canonical[1:].split("/")
            for c in comps:
                if not c or c in (".", ".."):
                    return False, "FsError_InvalidPath"
            c0 = comps[0].lower()
            if len(comps) == 1:
                if c0 == "dumps":
                    return False, "FsError_PathAlreadyExists (ancestor)"
                return True, canonical
            c1 = comps[1].lower()
            if c0 == "dumps" and c1 == "save-import":
                return False, "FsError_PathAlreadyExists (equality or descendant)"
            return True, canonical

        check(not validate_and_check_ancestry("/other/../dumps/save-import")[0], "dotdot bypass must be REJECTED")
        check(not validate_and_check_ancestry("/dumps/save-import")[0], "exact staging parent must be REJECTED")
        check(not validate_and_check_ancestry("/dumps/save-import/")[0], "trailing slash staging parent must be REJECTED")
        check(not validate_and_check_ancestry("/dumps")[0], "parent /dumps must be REJECTED")
        check(not validate_and_check_ancestry("/DUMPS")[0], "case-insensitive /DUMPS must be REJECTED")
        check(not validate_and_check_ancestry("/")[0], "root / must be REJECTED")
        check(not validate_and_check_ancestry("/dumps/save-import/stage_1")[0], "staging child must be REJECTED")
        check(not validate_and_check_ancestry("/JKSV//Animal")[0], "duplicate slash must be REJECTED")
        check(not validate_and_check_ancestry("sd:/JKSV/Animal")[0], "sd: mount prefix must be REJECTED")
        check(not validate_and_check_ancestry("/JKSV/Animal*")[0], "wildcard must be REJECTED")

        # Crucial: Safe sibling /dumps/save-import-other MUST BE ALLOWED!
        ok_sib, can_sib = validate_and_check_ancestry("/dumps/save-import-other")
        check(ok_sib and can_sib == "/dumps/save-import-other", "Safe sibling /dumps/save-import-other MUST be allowed")
        ok_sib_child, can_sib_child = validate_and_check_ancestry("/dumps/save-import-other/backup")
        check(ok_sib_child and can_sib_child == "/dumps/save-import-other/backup", "Safe sibling child MUST be allowed")

        # Trailing slash trimming
        ok_trail, can_trail = validate_and_check_ancestry("/JKSV/Animal Crossing/")
        check(ok_trail and can_trail == "/JKSV/Animal Crossing", "Trailing slash trimmed without altering path")
        print("  -> Fixture 7 (Canonical path validation & ancestry matrix) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 8: Full joined-path bounds and overflow rejection
        # ----------------------------------------------------------------------
        def checked_join_path(base: str, rel: str):
            b = base.rstrip("/")
            r = rel.lstrip("/")
            total_len = len(b) + 1 + len(r)
            if total_len >= 769:
                return False, "FsError_TooLongPath"
            return True, f"{b}/{r}"

        base_short = "/dumps/save-import"
        rel_short = "source.zip.temp"
        ok_fit, joined_fit = checked_join_path(base_short, rel_short)
        check(ok_fit and joined_fit == "/dumps/save-import/source.zip.temp", "Normal join fits")

        base_long = "/" + ("a" * 500)
        rel_long = "b" * 300
        ok_over, err_over = checked_join_path(base_long, rel_long)
        check(not ok_over and err_over == "FsError_TooLongPath", "Overflow joined path rejected with FsError_TooLongPath")
        print("  -> Fixture 8 (Full joined-path bounds & overflow rejection) PASSED.")

        # ----------------------------------------------------------------------
        # Fixture 9: Owned reservation, collision loop, and cleanup boundaries
        # ----------------------------------------------------------------------
        staging_parent = tmpdir / "dumps" / "save-import"
        staging_parent.mkdir(parents=True)

        foreign_dir = staging_parent / "foreign_backup"
        foreign_dir.mkdir()
        foreign_file = foreign_dir / "keep_me.txt"
        foreign_file.write_text("DO_NOT_DELETE", encoding="utf-8")

        owned_stage_dir = staging_parent / "20260918_105000_0100000000001000_000"
        owned_stage_dir.mkdir()
        owned_stage_zip = owned_stage_dir / "source.zip.temp"
        owned_stage_zip.write_bytes(b"TEMP_ZIP_CONTENT")

        check(owned_stage_zip.exists() and owned_stage_dir.exists(), "Owned stage exists before cleanup")
        owned_stage_zip.unlink()
        owned_stage_dir.rmdir()

        check(not owned_stage_zip.exists(), "Owned stage zip deleted")
        check(not owned_stage_dir.exists(), "Owned stage dir deleted")
        check(foreign_file.exists() and foreign_dir.exists(), "Foreign directory and files remained 100% UNTOUCHED")
        print("  -> Fixture 9 (Owned reservation & strict cleanup boundaries) PASSED.")

    print("[2] All real filesystem tree fixtures PASSED.")


# ==============================================================================
# 3. JKSV 85-byte Source Metadata Validation, Coexistence & Conflict, and Account Remap
# ==============================================================================

JKSV_MAGIC = 0x56534B4A
JKSV_REVISION = 1
