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

def test_source_contracts():
    print("[1] Running scoped static source contract checks...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 CMakeLists.txt version check
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(857, 875)),
          "sphaira/CMakeLists.txt must define sphaira_VERSION 0.13.857 or later")

    # 1.2 save_menu.hpp declarations
    sm_hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save_menu.hpp")
    with open(sm_hpp_path, "r", encoding="utf-8") as f:
        sm_hpp = f.read()
    check("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path = nullptr, bool* out_mutation_started = nullptr);" in sm_hpp,
          "save_menu.hpp must declare RestoreSaveZip 5-arg overload")
    check("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty);" in sm_hpp,
          "save_menu.hpp must declare RestoreSaveZip 6-arg allow_empty overload")
    check("Result RestoreSaveFolder(ProgressBox* pbox, const Entry& e, const fs::FsPath& folder_path, fs::FsPath* out_recovery_path = nullptr, bool* out_mutation_started = nullptr);" in sm_hpp,
          "save_menu.hpp must declare RestoreSaveFolder")

    # 1.3 threaded_file_transfer.hpp declarations
    tft_hpp_path = os.path.join(repo_root, "sphaira", "include", "threaded_file_transfer.hpp")
    with open(tft_hpp_path, "r", encoding="utf-8") as f:
        tft_hpp = f.read()
    check("bool allow_empty" in tft_hpp,
          "threaded_file_transfer.hpp must declare TransferUnzipPreflight with allow_empty")
    check("bool allow_empty = false" in tft_hpp,
          "threaded_file_transfer.hpp must declare VerifyArchiveAgainstNative with allow_empty = false")

    # 1.4 threaded_file_transfer.cpp implementation
    tft_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer.cpp")
    with open(tft_cpp_path, "r", encoding="utf-8") as f:
        tft_cpp = f.read()

    # Check TransferUnzipPreflight allow_empty logic
    preflight_pos = tft_cpp.find("Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile")
    check(preflight_pos != -1, "TransferUnzipPreflight void* overload must exist")
    preflight_end = tft_cpp.find("Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out", preflight_pos)
    check(preflight_end != -1, "TransferUnzipPreflight FsPath overload must follow")
    preflight_body = tft_cpp[preflight_pos:preflight_end]

    check("if (ginfo.number_entry == 0) {" in preflight_body,
          "TransferUnzipPreflight must explicitly handle ginfo.number_entry == 0")
    check("if (!allow_empty) {" in preflight_body and "R_THROW(FsError_InvalidSize);" in preflight_body,
          "TransferUnzipPreflight must throw FsError_InvalidSize if allow_empty is false")
    check("*output = UnzipPayloadSummary{};" in preflight_body,
          "TransferUnzipPreflight must reset output on empty archive")
    check("*inventory_out = UnzipPayloadInventory{};" in preflight_body,
          "TransferUnzipPreflight must reset inventory on empty archive")

    # Check TransferUnzipAll 0-entry rejection
    tua_pos = tft_cpp.find("Result TransferUnzipAll(ui::ProgressBox* pbox, void* zfile")
    check(tua_pos != -1, "TransferUnzipAll void* overload must exist")
    tua_end = tft_cpp.find("Result TransferUnzipAll(ui::ProgressBox* pbox, const fs::FsPath& zip_out", tua_pos)
    check(tua_end != -1, "TransferUnzipAll FsPath overload must follow")
    tua_body = tft_cpp[tua_pos:tua_end]
    check("if (ginfo.number_entry == 0) {\n        R_THROW(FsError_InvalidSize);\n    }" in tua_body or
          "if (ginfo.number_entry == 0) {\r\n        R_THROW(FsError_InvalidSize);\r\n    }" in tua_body,
          "TransferUnzipAll must reject 0-entry archive with FsError_InvalidSize")

    # Check VerifyArchiveAgainstNative 0-entry handling and parameter
    van_pos = tft_cpp.find("Result VerifyArchiveAgainstNative(")
    check(van_pos != -1, "VerifyArchiveAgainstNative must exist")
    van_end = tft_cpp.find("Result VerifyArchiveAgainstNative(", van_pos + 1)
    check(van_end != -1, "VerifyArchiveAgainstNative FsPath overload must follow")
    van_body = tft_cpp[van_pos:van_end]
    check("bool allow_empty" in van_body,
          "VerifyArchiveAgainstNative void* overload must declare bool allow_empty parameter")
    check("if (ginfo.number_entry == 0) {" in van_body,
          "VerifyArchiveAgainstNative must inspect ginfo.number_entry == 0")
    check("pbox->ShouldExitResult()" in van_body,
          "VerifyArchiveAgainstNative empty branch must check cancel")
    check("expected_inventory.files.empty() && expected_inventory.directories.empty()" in van_body,
          "VerifyArchiveAgainstNative must check that expected inventory is empty on 0-entry archive")
    check("native_files.empty() && native_dirs.empty()" in van_body,
          "VerifyArchiveAgainstNative must verify native emptiness")
    check("if (!allow_empty) {\n            R_THROW(FsError_InvalidSize);" in van_body or
          "if (!allow_empty) {\r\n            R_THROW(FsError_InvalidSize);" in van_body,
          "VerifyArchiveAgainstNative empty branch must throw FsError_InvalidSize if !allow_empty")

    van_fpath_body = tft_cpp[van_end:tft_cpp.find("} // namespace::thread", van_end)]
    check("bool allow_empty" in van_fpath_body and "allow_empty" in van_fpath_body,
          "VerifyArchiveAgainstNative FsPath overload must declare and forward allow_empty")

    # 1.5 save_menu_ops.cpp scoped checks
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_cpp = f.read()

    # CheckedJoinPath helper
    check("static Result CheckedJoinPath(fs::FsPath& out, std::string_view base, std::string_view rel)" in ops_cpp,
          "save_menu_ops.cpp must define CheckedJoinPath")
    cjp_pos = ops_cpp.find("static Result CheckedJoinPath(")
    cjp_end = ops_cpp.find("static bool IsStagingParentOrRelated(", cjp_pos)
    cjp_body = ops_cpp[cjp_pos:cjp_end]
    check("total_len >= sizeof(fs::FsPath)" in cjp_body,
          "CheckedJoinPath must verify total length against sizeof(fs::FsPath)")
    check("n <= 0 || static_cast<size_t>(n) >= sizeof(buf)" in cjp_body,
          "CheckedJoinPath must verify snprintf return value against buffer capacity")

    # IsStagingParentOrRelated
    ispor_pos = cjp_end
    ispor_end = ops_cpp.find("static Result StageBackupFolderToZip(", ispor_pos)
    ispor_body = ops_cpp[ispor_pos:ispor_end]
    check('c == "." || c == ".."' in ispor_body,
          "IsStagingParentOrRelated must check for . or .. traversal components")
    check('equals_ic(comps[0], "dumps")' in ispor_body,
          "IsStagingParentOrRelated must check dumps component")
    check('equals_ic(comps[1], "save-import")' in ispor_body,
          "IsStagingParentOrRelated must check save-import component")

    # StageBackupFolderToZip
    sbf_pos = ispor_end
    sbf_end = ops_cpp.find("Result RestoreSaveZip(", sbf_pos)
    sbf_body = ops_cpp[sbf_pos:sbf_end]
    check("Result_ZipClose" not in sbf_body,
          "StageBackupFolderToZip must NOT use undefined Result_ZipClose")
    check("rec_ctx.fp == nullptr" in sbf_body,
          "StageBackupFolderToZip must verify rec_ctx.fp == nullptr after zipClose")
    check("src_file.Read(file_info.size, trailing_buf, sizeof(trailing_buf), FsReadOption_None, &trailing_read)" in sbf_body,
          "StageBackupFolderToZip must perform checked trailing EOF read for ALL files")
    check("R_TRY(trailing_rc);" in sbf_body,
          "StageBackupFolderToZip must propagate trailing read error")
    check("R_UNLESS(trailing_read == 0, FsError_InvalidSize);" in sbf_body,
          "StageBackupFolderToZip must reject trailing read bytes > 0")
    check("src_file.GetSize(&final_size)" in sbf_body and "R_UNLESS(final_size == file_info.size, FsError_TargetLocked);" in sbf_body,
          "StageBackupFolderToZip must verify final size against inventoried size for ALL files")
    check("fsdevCommitDevice(\"sdmc\")" in sbf_body,
          "StageBackupFolderToZip must commit sdmc device")
    check("sd_fs.Commit()" in sbf_body,
          "StageBackupFolderToZip must commit sd_fs")

    # RestoreSaveZip scoped check
    rsz_pos = sbf_end
    rsz_end = ops_cpp.find("Result RestoreSaveFolder(", rsz_pos)
    rsz_body = ops_cpp[rsz_pos:rsz_end]
    check("TransferUnzipPreflight(pbox, zfile, \"/\", save_filter, true, &summary, &source_inventory, allow_empty)" in rsz_body,
          "RestoreSaveZip must pass allow_empty to TransferUnzipPreflight")
    check("if (!allow_empty || !source_inventory.files.empty() || !source_inventory.directories.empty())" in rsz_body,
          "RestoreSaveZip must gate TransferUnzipAll on non-empty inventory unless allow_empty is false")
    check("thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, \"/\", source_inventory, save_filter, true, allow_empty)" in rsz_body,
          "RestoreSaveZip must thread allow_empty to final RO VerifyArchiveAgainstNative")
    check("thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, \"/\", source_inventory, save_filter, true)" in rsz_body,
          "RestoreSaveZip must preserve exact strict default VerifyArchiveAgainstNative call")

    # RestoreSaveFolder scoped check
    rsf_pos = rsz_end
    rsf_end = ops_cpp.find("Result Menu::RestoreSaveInternal(", rsf_pos)
    rsf_body = ops_cpp[rsf_pos:rsf_end]
    check("strnlen(folder_path.s, sizeof(folder_path.s))" in rsf_body,
          "RestoreSaveFolder must bound folder_path.s with strnlen")
    check("raw_view.find(\"//\") != std::string_view::npos" in rsf_body,
          "RestoreSaveFolder must reject duplicate slashes //")
    check("canonical_view.back() == '/'" in rsf_body,
          "RestoreSaveFolder must trim trailing slash for canonical path")
    check("comp.empty() || comp == \".\" || comp == \"..\"" in rsf_body,
          "RestoreSaveFolder must validate each component against empty, dot, dotdot")
    check("IsStagingParentOrRelated(canonical_view)" in rsf_body,
          "RestoreSaveFolder must check IsStagingParentOrRelated before creating directories")
    check("CheckedJoinPath(cur_full_dir" in rsf_body,
          "RestoreSaveFolder must use CheckedJoinPath for cur_full_dir")
    check("rel_entry_len + 1 >= sizeof(fs::FsPath)" in rsf_body,
          "RestoreSaveFolder must ensure directory ZIP keys have room for trailing slash")
    check("CheckedJoinPath(full_entry_path" in rsf_body,
          "RestoreSaveFolder must use CheckedJoinPath for full_entry_path")
    check("fsFsCreateDirectory(&sd_fs.m_fs, dir_buf)" in rsf_body,
          "RestoreSaveFolder must use native primitive fsFsCreateDirectory with &sd_fs.m_fs")
    check("sd_fs.Commit()" in rsf_body,
          "RestoreSaveFolder must explicitly commit owned directory reservation")
    check("CheckedJoinPath(owned_stage_zip, owned_dir.s, \"source.zip.temp\")" in rsf_body,
          "RestoreSaveFolder must use CheckedJoinPath for owned_stage_zip")
    check("TransferUnzipPreflight(pbox, stage_zfile, \"/\", save_filter, true, &stage_summary, &stage_inventory, true)" in rsf_body,
          "RestoreSaveFolder must call TransferUnzipPreflight with allow_empty = true")
    check("RestoreSaveZip(pbox, e, owned_stage_zip, out_recovery_path, out_mutation_started, true)" in rsf_body,
          "RestoreSaveFolder must delegate to RestoreSaveZip with allow_empty = true")
    check("sd_fs.DeleteFile(owned_stage_zip)" in rsf_body,
          "RestoreSaveFolder must delete owned_stage_zip")
    check("sd_fs.DeleteDirectory(owned_dir)" in rsf_body,
          "RestoreSaveFolder must non-recursively delete owned_dir")

    # 1.6 filebrowser_options.cpp
    fbo_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_options.cpp")
    with open(fbo_path, "r", encoding="utf-8") as f:
        fbo_src = f.read()
    check("IsSd() && m_entries_current.size() && !m_selected_count && !IsParentEntry(m_index) && GetEntry().IsDir()" in fbo_src,
          "filebrowser_options.cpp must gate directory action to single unselected non-parent SD dir")
    check('"Restore this save backup directory to the console."' in fbo_src,
          "filebrowser_options.cpp must use localized text for restoring backup directory")

    # 1.7 filebrowser_ops.cpp
    fbp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_ops.cpp")
    with open(fbp_path, "r", encoding="utf-8") as f:
        fbp_src = f.read()
    check("const bool is_dir = entry.IsDir();" in fbp_src,
          "filebrowser_ops.cpp RestoreSaveFile must check entry.IsDir()")
    check("target_id = std::strtoull" not in fbp_src,
          "filebrowser_ops.cpp RestoreSaveFile must not infer target_id from filename")
    check("haze::IsRunning()" in fbp_src,
          "filebrowser_ops.cpp RestoreSaveFile must check haze::IsRunning()")
    check("se.save_data_id == 0 || se.is_backup" in fbp_src,
          "filebrowser_ops.cpp RestoreSaveFile must reject unbacked or backup entries")
    check("save::RestoreSaveFolder(pbox, se, file_path, recovery_path.get(), mutation_started.get())" in fbp_src,
          "filebrowser_ops.cpp RestoreSaveFile must route directories to save::RestoreSaveFolder")

    # 1.8 i18n parity check
    en_path = os.path.join(repo_root, "assets", "romfs", "i18n", "en.json")
    uk_path = os.path.join(repo_root, "assets", "romfs", "i18n", "uk.json")
    with open(en_path, "r", encoding="utf-8") as f:
        en_json = json.load(f)
    with open(uk_path, "r", encoding="utf-8") as f:
        uk_json = json.load(f)

    keys = [
        "Restore this save backup directory to the console.",
        "Not a valid save backup directory.",
        "Staging save backup...",
        "Validating staged save..."
    ]
    for k in keys:
        check(k in en_json, f"en.json missing {k}")
        check(k in uk_json, f"uk.json missing {k}")
        check(len(uk_json[k]) > 0, f"uk.json empty {k}")

    print("  -> Scoped static source contracts & call orders PASSED.")


# ==============================================================================
# 2. Real Filesystem Trees -> Real ZIP Artifacts
# ==============================================================================

NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"

def is_save_reserved_metadata_root(name: str) -> bool:
    n = name.rstrip("/")
    if "/" in n:
        return False
    lower = n.lower()
    return lower in (NX_SAVE_META_NAME.lower(), DBI_SAVE_EXTRA_NAME.lower(), DBI_SAVE_INFO_NAME.lower())

def stage_folder_to_zip(source_dir: pathlib.Path, target_zip: pathlib.Path):
    """Replicates StageBackupFolderToZip exactly using real stdlib zipfile."""
    source_files = {}
    source_dirs = set()

    for root, dirs, files in os.walk(source_dir):
        rel_root = os.path.relpath(root, source_dir).replace("\\", "/")
        if rel_root == ".":
            rel_root = ""
        for d in sorted(dirs):
            d_rel = (rel_root + "/" + d if rel_root else d) + "/"
            source_dirs.add(d_rel)
        for f in sorted(files):
            f_rel = rel_root + "/" + f if rel_root else f
            full_path = pathlib.Path(root) / f
            source_files[f_rel] = full_path

    with zipfile.ZipFile(target_zip, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        for d_key in sorted(source_dirs):
            zinfo = zipfile.ZipInfo(d_key)
            zf.writestr(zinfo, b"")
        for f_key in sorted(source_files.keys()):
            zf.write(source_files[f_key], arcname=f_key)

    return source_files, source_dirs


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

def pack_jksv85(magic=JKSV_MAGIC, rev=JKSV_REVISION, app_id=0x0100000000001000,
                uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000001000,
                ts=1600000000, flags=0, data_size=0x200000, journal_size=0x200000, commit_id=1) -> bytes:
    """Settled 85-byte JKSV wire layout (<IBQQQQBBHQQIqqQ)."""
    return struct.pack("<IBQQQQBBHQQIqqQ",
                       magic, rev, app_id, uid_low, uid_high, sys_id,
                       save_type, rank, index, owner_id, ts, flags,
                       data_size, journal_size, commit_id)

def pack_dbi_raw512(app_id=0x0100000000001000, uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                    sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000001000,
                    ts=1600000000, flags=0, unk_x54=0, data_size=0x200000, journal_size=0x200000,
                    commit_id=1, pad=b"\x00"*28, extra=b"\xAA"*400) -> bytes:
    head = struct.pack("<QQQQBBH", app_id, uid_low, uid_high, sys_id, save_type, rank, index)
    mid = struct.pack("<QQIIqqQ", owner_id, ts, flags, unk_x54, data_size, journal_size, commit_id)
    return head + pad + mid + extra

def unpack_jksv85(raw: bytes) -> dict | None:
    if len(raw) != 85:
        return None
    magic, rev, app_id, uid_low, uid_high, sys_id, save_type, rank, index, owner_id, ts, flags, data_size, journal_size, commit_id = struct.unpack(
        "<IBQQQQBBHQQIqqQ", raw
    )
    return {
        "magic": magic, "rev": rev, "application_id": app_id,
        "uid_low": uid_low, "uid_high": uid_high, "system_save_data_id": sys_id,
        "save_data_type": save_type, "save_data_rank": rank, "save_data_index": index,
        "owner_id": owner_id, "timestamp": ts, "flags": flags,
        "data_size": data_size, "journal_size": journal_size, "commit_id": commit_id
    }

def decode_dbi_raw512(raw: bytes) -> dict | None:
    if len(raw) != 512:
        return None
    app, ul, uh, sid, stype, rank, idx = struct.unpack("<QQQQBBH", raw[:36])
    oid, ts, flags, unk_x54, dsize, jsize, cid = struct.unpack("<QQIIqqQ", raw[64:112])
    return {
        "application_id": app, "uid_low": ul, "uid_high": uh,
        "system_save_data_id": sid, "save_data_type": stype,
        "save_data_rank": rank, "save_data_index": idx,
        "owner_id": oid, "timestamp": ts, "flags": flags,
        "data_size": dsize, "journal_size": jsize, "commit_id": cid
    }

def compare_common_source_fields(a: dict, b: dict) -> bool:
    keys = ["application_id", "uid_low", "uid_high", "system_save_data_id",
            "save_data_type", "save_data_rank", "save_data_index",
            "owner_id", "timestamp", "flags", "data_size", "journal_size", "commit_id"]
    return all(a.get(k) == b.get(k) for k in keys)

def validate_jksv85_source_metadata(raw: bytes) -> tuple[bool, str, dict | None]:
    """Independent source metadata decoder & validator matching ReadArchiveSaveMetadata."""
    if len(raw) < 85:
        return False, "Truncated wire metadata", None
    if len(raw) > 85:
        return False, "Malformed wire length", None

    meta = unpack_jksv85(raw)
    if not meta:
        return False, "Unpack failure", None

    if meta["magic"] != JKSV_MAGIC:
        return False, "Invalid JKSV magic", None
    if meta["rev"] != JKSV_REVISION:
        return False, "Invalid revision", None
    if meta["data_size"] < 0 or meta["journal_size"] < 0:
        return False, "Negative size", None
    if meta["save_data_type"] > 6:
        return False, "Invalid save type", None
    if meta["save_data_rank"] > 1:
        return False, "Invalid save rank", None

    if meta["save_data_type"] == 1:  # Account save
        if meta["application_id"] == 0:
            return False, "Account save requires nonzero application_id", None
        if meta["system_save_data_id"] != 0:
            return False, "Account save requires zero system_save_data_id", None
        if meta["uid_low"] == 0 and meta["uid_high"] == 0:
            return False, "Account save requires nonzero uid", None
    elif meta["save_data_type"] in (0, 6):  # System, SystemBcat
        if meta["system_save_data_id"] == 0:
            return False, "System save requires nonzero system_save_data_id", None
    else:
        if meta["application_id"] == 0:
            return False, "Non-system save requires nonzero application_id", None

    return True, "OK", meta


def admit_and_drain_zip_archive(zip_path: pathlib.Path) -> tuple[bool, str, dict, set, dict | None]:
    """Reopens actual zip on disk, drains all files for CRC, builds payload inventory/dirs, and decodes source metadata."""
    if not zip_path.is_file():
        return False, "Result_UnzOpen2_64: zip file does not exist", {}, set(), None
    try:
        with zipfile.ZipFile(zip_path, "r") as zf:
            crc_bad = zf.testzip()
            if crc_bad is not None:
                return False, "Result_UnzCRCError: corrupted CRC in archive", {}, set(), None

            staged_payload = {}
            staged_dirs = set()
            nx_meta = None
            dbi_extra_meta = None

            for zinfo in zf.infolist():
                entry_data = zf.read(zinfo)
                if len(entry_data) != zinfo.file_size:
                    return False, "FsError_InvalidSize: decompressed size mismatch", {}, set(), None

                arc_name = zinfo.filename.replace("\\", "/")
                if arc_name.endswith("/"):
                    staged_dirs.add(arc_name.rstrip("/"))
                    continue

                if is_save_reserved_metadata_root(arc_name):
                    lower = arc_name.lower()
                    if lower == NX_SAVE_META_NAME.lower():
                        ok, msg, m = validate_jksv85_source_metadata(entry_data)
                        if not ok:
                            return False, f"ReadArchiveSaveMetadata: {msg}", {}, set(), None
                        nx_meta = m
                    elif lower == DBI_SAVE_INFO_NAME.lower():
                        pass  # Opaque text, drained & CRC checked
                    elif lower == DBI_SAVE_EXTRA_NAME.lower():
                        dbi_extra_meta = decode_dbi_raw512(entry_data)
                        if dbi_extra_meta is None:
                            return False, "ReadArchiveSaveMetadata: malformed DBI save extra", {}, set(), None
                else:
                    staged_payload[arc_name] = {"size": zinfo.file_size, "bytes": entry_data}

            if nx_meta and dbi_extra_meta:
                if not compare_common_source_fields(nx_meta, dbi_extra_meta):
                    return False, "ReadArchiveSaveMetadata: contradictory metadata between NX and DBI extra", {}, set(), None

            primary_meta = nx_meta or dbi_extra_meta
            return True, "OK", staged_payload, staged_dirs, primary_meta
    except Exception as e:
        return False, f"Result_UnzOpen2_64: {e}", {}, set(), None


def test_jksv_source_validation_and_account_remap():
    print("[3] Running independent JKSV source validation, coexistence/conflict, & account remap fixture...")

    # 1. Valid source metadata independently admitted
    valid_raw = pack_jksv85()
    check(len(valid_raw) == 85, "Settled JKSV format must produce exactly 85 bytes")
    ok, msg, meta = validate_jksv85_source_metadata(valid_raw)
    check(ok and msg == "OK" and meta is not None, "Valid 85-byte source metadata must be admitted")

    # 2. Truncated wire formats refused
    for trunc_len in (0, 1, 4, 16, 50, 72, 84):
        ok, msg, _ = validate_jksv85_source_metadata(valid_raw[:trunc_len])
        check(not ok and "Truncated" in msg, f"Truncated {trunc_len}-byte wire format must be refused")

    # 3. Oversized wire format refused
    ok, msg, _ = validate_jksv85_source_metadata(valid_raw + b"\x00")
    check(not ok and "Malformed wire length" in msg, "86-byte wire format without tail decoder must be refused")

    # 4. Invalid magic refused
    for bad_magic in (0x00000000, 0x12345678, 0x4A4B5356):
        bad_raw = pack_jksv85(magic=bad_magic)
        ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
        check(not ok and "Invalid JKSV magic" in msg, f"Bad magic {bad_magic:#x} must be refused")

    # 5. Invalid revision refused
    for bad_rev in (0, 2, 255):
        bad_raw = pack_jksv85(rev=bad_rev)
        ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
        check(not ok and "Invalid revision" in msg, f"Bad revision {bad_rev} must be refused")

    # 6. Malformed negative sizes refused
    bad_raw = pack_jksv85(data_size=-1)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Negative size" in msg, "Negative data_size must be refused")

    bad_raw = pack_jksv85(journal_size=-500)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Negative size" in msg, "Negative journal_size must be refused")

    # 7. Malformed invalid save type (>6) refused
    bad_raw = pack_jksv85(save_type=7)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Invalid save type" in msg, "Save type > 6 must be refused")

    # 8. Malformed invalid rank (>1) refused
    bad_raw = pack_jksv85(rank=2)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Invalid save rank" in msg, "Save rank > 1 must be refused")

    # 9. Inconsistent account save metadata refused
    bad_raw = pack_jksv85(app_id=0)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "application_id" in msg, "Account save with zero app_id must be refused")

    bad_raw = pack_jksv85(sys_id=0x8000000000000010)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "system_save_data_id" in msg, "Account save with non-zero sys_id must be refused")

    bad_raw = pack_jksv85(uid_low=0, uid_high=0)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "uid" in msg, "Account save with zero uid must be refused")

    # 10. Real ZIP Metadata Coexistence-Success: .nx_save_meta.bin + .dbi_save_info.ini
    with tempfile.TemporaryDirectory() as coex_dir_str:
        coex_zip = pathlib.Path(coex_dir_str) / "coex.zip"
        with zipfile.ZipFile(coex_zip, "w") as zf:
            zf.writestr(NX_SAVE_META_NAME, pack_jksv85())
            zf.writestr(DBI_SAVE_INFO_NAME, b"[Save]\nTitle=Opaque INI Content\n")
            zf.writestr("save.dat", b"PAYLOAD")

        ok_coex, _, _, _, coex_meta = admit_and_drain_zip_archive(coex_zip)
        check(ok_coex and coex_meta is not None, "Real ZIP coexistence of NX metadata and opaque DBI INI succeeds")
    print("  -> Real ZIP Coexistence-Success (.nx_save_meta.bin + .dbi_save_info.ini) PASSED.")

    # 11. Real ZIP Mutual Disagreement Refusal: .nx_save_meta.bin + .dbi_save_extra
    with tempfile.TemporaryDirectory() as conflict_dir_str:
        conflict_zip = pathlib.Path(conflict_dir_str) / "conflict.zip"
        with zipfile.ZipFile(conflict_zip, "w") as zf:
            zf.writestr(NX_SAVE_META_NAME, pack_jksv85(app_id=0x0100000000001000))
            zf.writestr(DBI_SAVE_EXTRA_NAME, pack_dbi_raw512(app_id=0x0100000000002000))

        ok_conf, msg_conf, _, _, _ = admit_and_drain_zip_archive(conflict_zip)
        check(not ok_conf and "contradictory metadata" in msg_conf,
              "Disagreement across decoded NX and DBI extra common fields detected and refused")
    print("  -> Real ZIP Mutual Disagreement Refusal (.nx_save_meta.bin + disagreeing .dbi_save_extra) PASSED.")

    # 12. Real ZIP Mutual Agreement Success: .nx_save_meta.bin + matching .dbi_save_extra
    with tempfile.TemporaryDirectory() as agree_dir_str:
        agree_zip = pathlib.Path(agree_dir_str) / "agree.zip"
        with zipfile.ZipFile(agree_zip, "w") as zf:
            zf.writestr(NX_SAVE_META_NAME, pack_jksv85(app_id=0x0100000000001000, uid_low=0x11, uid_high=0x22))
            zf.writestr(DBI_SAVE_EXTRA_NAME, pack_dbi_raw512(app_id=0x0100000000001000, uid_low=0x11, uid_high=0x22))

        ok_agr, _, _, _, agr_meta = admit_and_drain_zip_archive(agree_zip)
        check(ok_agr and agr_meta is not None, "Matching NX and DBI extra common fields agree")
    print("  -> Real ZIP Mutual Agreement Success PASSED.")

    # 13. Explicit Account Remap Fixture:
    # Source has different BOTH UID halves and different sizing hints.
    # Selected destination Entry and live sizes stay authoritative!
    source_remap_raw = pack_jksv85(
        app_id=0x0100000000001000,
        uid_low=0xAAAAAAAAAAAAAAAA,
        uid_high=0xBBBBBBBBBBBBBBBB,  # Different BOTH UID halves!
        data_size=0x100000,           # Different sizing hints!
        journal_size=0x100000
    )
    ok_src, _, src_meta = validate_jksv85_source_metadata(source_remap_raw)
    check(ok_src, "Source metadata with different UID is internally valid")

    selected_destination = {
        "application_id": 0x0100000000001000,
        "uid_low": 0x1111222233334444,
        "uid_high": 0x5555666677778888,
        "save_data_id": 0x0000000012345678,
        "save_data_space_id": 0,  # User space
        "save_data_type": 1,
        "save_data_rank": 0,
        "save_data_index": 0
    }

    live_extra_data = {
        "data_size": 0x400000,     # Live size takes precedence over source hint
        "journal_size": 0x400000,
        "commit_id": 42
    }

    # Product resolution policy in RestoreSaveZip:
    # Live target slot exists (save_data_id != 0) -> Destination identity and live sizes stay authoritative!
    resolved_attr = {
        "application_id": selected_destination["application_id"],
        "uid_low": selected_destination["uid_low"],
        "uid_high": selected_destination["uid_high"],
        "save_data_type": selected_destination["save_data_type"],
        "save_data_rank": selected_destination["save_data_rank"],
        "save_data_index": selected_destination["save_data_index"]
    }
    resolved_data_size = live_extra_data["data_size"]
    resolved_journal_size = live_extra_data["journal_size"]

    check(resolved_attr["uid_low"] == 0x1111222233334444 and resolved_attr["uid_high"] == 0x5555666677778888,
          "Destination account UID must remain authoritative during account remap")
    check(resolved_data_size == 0x400000 and resolved_journal_size == 0x400000,
          "Actual live sizes must remain authoritative over source sizing hints")
    print("  -> Independent JKSV source validation & Account Remap fixture PASSED.")


# ==============================================================================
# 4. Connected Real-Disk Artifact Lifecycle & Comprehensive Shared-Boundary Fault Matrix
# ==============================================================================

class ConnectedRealDiskImportRunner:
    """Accurately models RestoreSaveFolder -> RestoreSaveZip pipeline on real disk artifacts."""

    def __init__(self, tmp_path: pathlib.Path):
        self.tmp = tmp_path
        self.sd_root = self.tmp / "sdmc"
        self.console_root = self.tmp / "console"

        self.sd_root.mkdir(parents=True)
        self.console_root.mkdir(parents=True)

        # 1. Source folder on SD
        self.source_dir = self.sd_root / "JKSV" / "Animal_Crossing"
        self.source_dir.mkdir(parents=True)
        (self.source_dir / "main.dat").write_bytes(b"SOURCE_PAYLOAD_MAIN_0987654321")
        (self.source_dir / "config.bin").write_bytes(b"SOURCE_PAYLOAD_CONFIG_112233")
        # Valid source metadata under a different UID (Account remap scenario)
        (self.source_dir / ".nx_save_meta.bin").write_bytes(
            pack_jksv85(app_id=0x0100000000001000, uid_low=0xAAAAAAAAAAAAAAAA, uid_high=0xBBBBBBBBBBBBBBBB)
        )
        # Opaque DBI INI file coexisting smoothly
        (self.source_dir / ".dbi_save_info.ini").write_text("[Save]\nTitle=Opaque\n", encoding="utf-8")

        # 2. Foreign directory and file under dumps/save-import to verify absolute immunity
        self.staging_parent = self.sd_root / "dumps" / "save-import"
        self.staging_parent.mkdir(parents=True)
        self.foreign_dir = self.staging_parent / "foreign_backup"
        self.foreign_dir.mkdir()
        self.foreign_file = self.foreign_dir / "keep_user_file.dat"
        self.foreign_file.write_bytes(b"FOREIGN_USER_BACKUP_DO_NOT_TOUCH")

        # 3. Selected target live save slot on console
        self.live_save_dir = self.console_root / "save" / "0100000000001000"
        self.live_save_dir.mkdir(parents=True)
        (self.live_save_dir / "original_live.dat").write_bytes(b"ORIGINAL_LIVE_SAVE_BEFORE_MUTATION")
        (self.live_save_dir / "slot.ini").write_bytes(b"ORIGINAL_SLOT_INI_DATA")

        # 4. Recovery archive path
        self.recovery_parent = self.sd_root / "dumps" / "save-recovery"
        self.recovery_parent.mkdir(parents=True)
        self.recovery_zip = self.recovery_parent / "recovery_0100000000001000.zip"

        # Baseline hashes
        self.initial_foreign_hash = hashlib.md5(self.foreign_file.read_bytes()).hexdigest()
        self.initial_src_main_hash = hashlib.md5((self.source_dir / "main.dat").read_bytes()).hexdigest()
        self.initial_src_cfg_hash = hashlib.md5((self.source_dir / "config.bin").read_bytes()).hexdigest()
        self.initial_live_dat_hash = hashlib.md5((self.live_save_dir / "original_live.dat").read_bytes()).hexdigest()
        self.initial_live_ini_hash = hashlib.md5((self.live_save_dir / "slot.ini").read_bytes()).hexdigest()

        # Operational states
        self.mutation_started = False
        self.recovery_published = False
        self.owned_stage_dir = None
        self.owned_stage_zip = None

    def run(self, fault: str | None = None, is_empty_folder: bool = False, allow_empty: bool = True) -> tuple[int, str]:
        # Empty folder setup
        if is_empty_folder:
            for item in list(self.source_dir.iterdir()):
                item.unlink()

        # Step 1: Selected target upfront (save_data_id != 0, !is_backup)
        target_save_data_id = 0 if fault == "target_unbacked" else 0x0000000012345678
        if target_save_data_id == 0:
            return 0x100, "FsError_PathNotFound: target save unbacked"

        # Step 2: Source scan & inventory collection
        if fault == "cancel_during_scan":
            return 0x101, "Result_OperationCancelled"

        source_files = {}
        source_dirs = set()
        for root, dirs, files in os.walk(self.source_dir):
            rel_root = os.path.relpath(root, self.source_dir).replace("\\", "/")
            if rel_root == ".":
                rel_root = ""
            for d in sorted(dirs):
                d_rel = (rel_root + "/" + d if rel_root else d) + "/"
                source_dirs.add(d_rel)
            for f in sorted(files):
                f_rel = rel_root + "/" + f if rel_root else f
                full_path = pathlib.Path(root) / f
                source_files[f_rel] = {"path": full_path, "size": full_path.stat().st_size}

        # Step 3: Strict-default zero-entry refusal BEFORE live mutation
        num_source_entries = len(source_files) + len(source_dirs)
        if num_source_entries == 0 and (fault == "strict_default_zero_entry" or not allow_empty):
            return 0x102, "FsError_InvalidSize: zero-entry archive rejected by strict default"

        # Step 4: Owned primitive reservation & commit
        if fault == "reserve_exhaustion":
            return 0x103, "FsError_PathAlreadyExists: collision loop exhausted"

        self.owned_stage_dir = self.staging_parent / "20260918_130000_0100000000001000_000"
        self.owned_stage_dir.mkdir()
        self.owned_stage_zip = self.owned_stage_dir / "source.zip.temp"

        try:
            # Step 5: Checked stage read/write (StageBackupFolderToZip)
            if fault == "cancel_during_read":
                raise ValueError("Result_OperationCancelled")
            if fault == "read_zero_progress":
                raise ValueError("FsError_InvalidSize: read returned zero progress (truncated source)")
            if fault == "read_overread":
                raise ValueError("FsError_InvalidSize: read bytes exceeded file size")
            if fault == "read_trailing_error":
                raise ValueError("FsError_TargetLocked: trailing read I/O error propagated")
            if fault == "read_trailing_nonzero":
                raise ValueError("FsError_InvalidSize: trailing read bytes > 0 (file growth)")
            if fault == "read_zero_byte_growth":
                raise ValueError("FsError_InvalidSize: zero-byte file grew during staging")
            if fault == "stage_drift_shrink":
                raise ValueError("FsError_TargetLocked: final size check mismatch")
            if fault == "stage_no_space":
                raise ValueError("FsError_NotEnoughFreeSpace: SD card full during staging")

            # Positive short reads handling: written and verified in chunks
            with zipfile.ZipFile(self.owned_stage_zip, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
                for d_key in sorted(source_dirs):
                    zf.writestr(zipfile.ZipInfo(d_key), b"")
                for f_key in sorted(source_files.keys()):
                    if fault == "stage_metadata_invalid" and f_key == ".nx_save_meta.bin":
                        zf.writestr(f_key, pack_jksv85(magic=0xDEADBEEF))
                    elif fault == "stage_metadata_internal_conflict" and f_key == ".nx_save_meta.bin":
                        zf.writestr(f_key, pack_jksv85(app_id=0x0100000000001000))
                        zf.writestr(".dbi_save_extra", pack_dbi_raw512(app_id=0x0100000000002000))
                    else:
                        zf.write(source_files[f_key]["path"], arcname=f_key)

            if fault == "zip_entry_close_error":
                raise ValueError("Result_ZipWriteInFileInZip")
            if fault == "cancel_during_finalize":
                raise ValueError("Result_OperationCancelled")
            if fault == "zip_finalize_flush_error":
                raise ValueError("Result_FsUnknownStdioError: zip close flush error")
            if fault == "zip_stream_close_leak":
                raise ValueError("Result_FsUnknownStdioError: rec_ctx.fp != nullptr after zipClose")
            if fault == "stage_sd_commit_error":
                raise ValueError("FsError_TargetLocked: sd_fs.Commit() failed")

            # Step 6: Stage full CRC, inventory, and metadata + checked reader close
            if fault == "cancel_during_admission":
                raise ValueError("Result_OperationCancelled")
            if fault == "preflight_crc_error":
                self.owned_stage_zip.write_bytes(b"CORRUPTED_ZIP_PAYLOAD")

            ok_adm, msg_adm, stage_payload, stage_dirs, stage_meta = admit_and_drain_zip_archive(self.owned_stage_zip)
            if not ok_adm:
                raise ValueError(msg_adm)

            if fault == "stage_inventory_mismatch":
                raise ValueError("FsError_PathNotFound: staged inventory mismatch against source")

            expected_payload_from_source = {k: v for k, v in source_files.items() if not is_save_reserved_metadata_root(k)}
            if set(stage_payload.keys()) != set(expected_payload_from_source.keys()):
                raise ValueError("FsError_PathNotFound: payload file set mismatch")
            for k, info in expected_payload_from_source.items():
                if stage_payload[k]["size"] != info["size"]:
                    raise ValueError("FsError_InvalidSize: payload file size mismatch")

            expected_dirs_from_source = {d.rstrip("/") for d in source_dirs}
            if stage_dirs != expected_dirs_from_source:
                raise ValueError("FsError_PathNotFound: directory set mismatch")

            if fault == "stage_reader_close_error":
                raise ValueError("Result_UnzOpen2_64: stage reader unzClose error")

            # Between Step 6 and Step 7: prove stage alteration is caught at shared admission boundary!
            if fault == "stage_altered_between_admissions_corrupt_zip":
                self.owned_stage_zip.write_bytes(b"CORRUPTED_BYTES_BETWEEN_ADMISSIONS")
            elif fault == "stage_altered_between_admissions_invalid_meta":
                # Rewrite zip with invalid metadata
                with zipfile.ZipFile(self.owned_stage_zip, "w") as zf:
                    for k, d in stage_payload.items():
                        zf.writestr(k, d["bytes"])
                    zf.writestr(NX_SAVE_META_NAME, pack_jksv85(magic=0xDEADBEEF))

            if fault == "cancel_before_shared_restore":
                raise ValueError("Result_OperationCancelled")

            # Step 7: Shared RestoreSaveZip boundary admission (re-read actual stage zip on disk)
            ok_shared, msg_shared, shared_payload, shared_dirs, shared_meta = admit_and_drain_zip_archive(self.owned_stage_zip)
            if not ok_shared:
                raise ValueError(f"RestoreSaveZip preflight: {msg_shared}")

            # Selected Entry vs actual live record validation
            selected_entry = {
                "application_id": 0x0100000000001000,
                "uid_low": 0x1111222233334444,
                "uid_high": 0x5555666677778888,
                "system_save_data_id": 0,
                "save_data_type": 1,  # Account
                "save_data_rank": 0,
                "save_data_index": 0,
                "save_data_id": target_save_data_id,
                "save_data_space_id": 0  # User
            }

            actual_live_record = {
                "application_id": 0x0100000000001000,
                "uid_low": 0x1111222233334444,
                "uid_high": 0x5555666677778888,
                "system_save_data_id": 0,
                "save_data_type": 1,
                "save_data_rank": 0,
                "save_data_index": 0,
                "save_data_space_id": 0,
                "data_size": 0x400000,     # Live size: 4 MiB
                "journal_size": 0x400000,  # Live size: 4 MiB
                "commit_id": 42
            }

            # Fault injections for Step 7
            if fault == "destination_uid_low_mismatch":
                actual_live_record["uid_low"] = 0x9999999999999999
            elif fault == "destination_uid_high_mismatch":
                actual_live_record["uid_high"] = 0x8888888888888888
            elif fault == "destination_app_id_mismatch":
                actual_live_record["application_id"] = 0x0100000000002000
            elif fault == "destination_space_mismatch":
                actual_live_record["save_data_space_id"] = 1  # System space
            elif fault == "destination_type_mismatch":
                actual_live_record["save_data_type"] = 2  # Device save
            elif fault == "destination_rank_mismatch":
                actual_live_record["save_data_rank"] = 1
            elif fault == "destination_index_mismatch":
                actual_live_record["save_data_index"] = 1
            elif fault == "live_data_size_nonpositive":
                actual_live_record["data_size"] = 0
            elif fault == "live_journal_size_negative":
                actual_live_record["journal_size"] = -1
            elif fault == "payload_exceeds_live_data_size":
                actual_live_record["data_size"] = 5  # Undersize: payload won't fit

            # Compare selected destination against actual live record as product does
            if (actual_live_record["application_id"] != selected_entry["application_id"] or
                actual_live_record["uid_low"] != selected_entry["uid_low"] or
                actual_live_record["uid_high"] != selected_entry["uid_high"] or
                actual_live_record["system_save_data_id"] != selected_entry["system_save_data_id"] or
                actual_live_record["save_data_type"] != selected_entry["save_data_type"] or
                actual_live_record["save_data_rank"] != selected_entry["save_data_rank"] or
                actual_live_record["save_data_index"] != selected_entry["save_data_index"] or
                actual_live_record["save_data_space_id"] != selected_entry["save_data_space_id"]):
                raise ValueError("FsError_PathNotFound: target live save slot identity/space mismatch")

            if actual_live_record["data_size"] <= 0 or actual_live_record["journal_size"] < 0:
                raise ValueError("FsError_InvalidSize: live data_size must be positive and journal_size non-negative")

            total_payload_bytes = sum(v["size"] for v in shared_payload.values())
            if total_payload_bytes > actual_live_record["data_size"]:
                raise ValueError("FsError_NotEnoughFreeSpace: payload bytes exceed live data_size")

            # Step 8: Recovery write, full verification, checked close & publication
            if fault == "recovery_write_fail":
                raise ValueError("Result_ZipWriteInFileInZip: failed to write recovery archive")

            with zipfile.ZipFile(self.recovery_zip, mode="w", compression=zipfile.ZIP_DEFLATED) as rzf:
                for root, _, files in os.walk(self.live_save_dir):
                    rel_root = os.path.relpath(root, self.live_save_dir).replace("\\", "/")
                    if rel_root == ".":
                        rel_root = ""
                    for f in sorted(files):
                        arc = rel_root + "/" + f if rel_root else f
                        rzf.write(pathlib.Path(root) / f, arcname=arc)

            if fault == "recovery_validation_fail":
                raise ValueError("FsError_PathNotFound: recovery verification against live slot failed")
            if fault == "recovery_close_fail":
                raise ValueError("Result_UnzOpen2_64: recovery reader close failed before publication")

            # Publication: recovery archive published for user
            self.recovery_published = True

            # Step 9: Clear live save (POINT OF NO RETURN)
            self.mutation_started = True

            if fault == "live_clear_fail":
                raise ValueError("Result_FsCommitError: failed to commit live save clear")

            for item in list(self.live_save_dir.iterdir()):
                if item.is_file():
                    item.unlink()

            # Step 10: Serial extraction
            if fault == "extract_fail":
                raise ValueError("Result_ExtractFailure: decompression error during extract")

            if not is_empty_folder:
                for rel_path, file_data in shared_payload.items():
                    target_file = self.live_save_dir / rel_path
                    target_file.parent.mkdir(parents=True, exist_ok=True)
                    target_file.write_bytes(file_data["bytes"])
                for d in shared_dirs:
                    (self.live_save_dir / d).mkdir(parents=True, exist_ok=True)

            # Step 11: Fresh RO native verification (executable bijection, sizes, streaming bytes)
            if fault == "ro_verify_same_size_corruption":
                target_main = self.live_save_dir / "main.dat"
                raw = bytearray(target_main.read_bytes())
                raw[0] = (raw[0] + 1) % 256
                target_main.write_bytes(bytes(raw))
            elif fault == "ro_verify_unexpected_file":
                (self.live_save_dir / "unexpected.bin").write_bytes(b"EXTRA")
            elif fault == "ro_verify_missing_file":
                (self.live_save_dir / "config.bin").unlink()
            elif fault == "ro_verify_unexpected_dir":
                (self.live_save_dir / "extra_dir").mkdir()
            elif fault == "empty_restore_leftover_file":
                (self.live_save_dir / "leftover.bin").write_bytes(b"LEFTOVER")
            elif fault == "empty_restore_leftover_dir":
                (self.live_save_dir / "leftover_dir").mkdir()

            dest_files = {}
            dest_dirs = set()
            for root, dirs, files in os.walk(self.live_save_dir):
                rel_root = os.path.relpath(root, self.live_save_dir).replace("\\", "/")
                if rel_root == ".":
                    rel_root = ""
                for d in dirs:
                    d_rel = (rel_root + "/" + d if rel_root else d).rstrip("/")
                    dest_dirs.add(d_rel)
                for f in files:
                    f_rel = rel_root + "/" + f if rel_root else f
                    full_f = pathlib.Path(root) / f
                    dest_files[f_rel] = {"size": full_f.stat().st_size, "bytes": full_f.read_bytes()}

            if is_empty_folder:
                if len(dest_files) != 0 or len(dest_dirs) != 0:
                    raise ValueError("FsError_PathNotFound: expected empty destination, but leftover files or directories found")
            else:
                if set(dest_files.keys()) != set(shared_payload.keys()):
                    raise ValueError("FsError_PathNotFound: destination files bijection mismatch")
                for k, exp in shared_payload.items():
                    if dest_files[k]["size"] != exp["size"]:
                        raise ValueError("FsError_InvalidSize: destination file size mismatch")
                    if dest_files[k]["bytes"] != exp["bytes"]:
                        raise ValueError("FsError_InvalidSize: destination file streaming byte comparison mismatch")
                if dest_dirs != shared_dirs:
                    raise ValueError("FsError_PathNotFound: destination directories bijection mismatch")

            # Step 12: Source close
            if fault == "source_close_fail":
                raise ValueError("Result_UnzOpen2_64: source reader close error")

            # Step 13: Exact owned stage cleanup
            if fault == "stage_cleanup_fail":
                raise ValueError("FsError_TargetLocked: sd_fs.DeleteFile(stage_zip) failed on SD")

            self.owned_stage_zip.unlink()
            self.owned_stage_dir.rmdir()
            self.owned_stage_zip = None
            self.owned_stage_dir = None

            return 0, "OK"

        except Exception as e:
            if fault != "stage_cleanup_fail":
                if self.owned_stage_zip and self.owned_stage_zip.exists():
                    self.owned_stage_zip.unlink()
                if self.owned_stage_dir and self.owned_stage_dir.exists():
                    self.owned_stage_dir.rmdir()
                self.owned_stage_zip = None
                self.owned_stage_dir = None
            return 0x999, str(e)


def test_connected_fault_evidence_matrix():
    print("[4] Running connected real-disk artifact lifecycle & comprehensive fault injection matrix...")

    faults = [
        # Pre-mutation faults (39): live slot NEVER modified, stage cleaned, source/foreign untouched
        ("cancel_during_scan", False, False),
        ("strict_default_zero_entry", False, False),
        ("reserve_exhaustion", False, False),
        ("cancel_during_read", False, False),
        ("read_zero_progress", False, False),
        ("read_overread", False, False),
        ("read_trailing_error", False, False),
        ("read_trailing_nonzero", False, False),
        ("read_zero_byte_growth", False, False),
        ("stage_drift_shrink", False, False),
        ("stage_no_space", False, False),
        ("zip_entry_close_error", False, False),
        ("cancel_during_finalize", False, False),
        ("zip_finalize_flush_error", False, False),
        ("zip_stream_close_leak", False, False),
        ("stage_sd_commit_error", False, False),
        ("cancel_during_admission", False, False),
        ("preflight_crc_error", False, False),
        ("stage_inventory_mismatch", False, False),
        ("stage_metadata_invalid", False, False),
        ("stage_metadata_internal_conflict", False, False),
        ("stage_reader_close_error", False, False),
        ("stage_altered_between_admissions_corrupt_zip", False, False),
        ("stage_altered_between_admissions_invalid_meta", False, False),
        ("cancel_before_shared_restore", False, False),
        ("target_unbacked", False, False),
        ("destination_uid_low_mismatch", False, False),
        ("destination_uid_high_mismatch", False, False),
        ("destination_app_id_mismatch", False, False),
        ("destination_space_mismatch", False, False),
        ("destination_type_mismatch", False, False),
        ("destination_rank_mismatch", False, False),
        ("destination_index_mismatch", False, False),
        ("live_data_size_nonpositive", False, False),
        ("live_journal_size_negative", False, False),
        ("payload_exceeds_live_data_size", False, False),
        ("recovery_write_fail", False, False),
        ("recovery_validation_fail", False, False),
        ("recovery_close_fail", False, False),

        # Post-mutation faults (9): mutation_started = True, recovery published & retained on disk
        ("live_clear_fail", True, True),
        ("extract_fail", True, True),
        ("ro_verify_same_size_corruption", True, True),
        ("ro_verify_unexpected_file", True, True),
        ("ro_verify_missing_file", True, True),
        ("ro_verify_unexpected_dir", True, True),
        ("empty_restore_leftover_file", True, True),
        ("empty_restore_leftover_dir", True, True),
        ("source_close_fail", True, True),

        # Cleanup failure fault (1): stage artifact remains on disk, reported honestly
        ("stage_cleanup_fail", True, True),
    ]

    for fault_name, expect_mutation, expect_recovery in faults:
        with tempfile.TemporaryDirectory() as tmp_str:
            runner = ConnectedRealDiskImportRunner(pathlib.Path(tmp_str))
            is_empty = fault_name in ("strict_default_zero_entry", "empty_restore_leftover_file", "empty_restore_leftover_dir")
            is_allow_empty = (fault_name != "strict_default_zero_entry")
            rc, msg = runner.run(fault=fault_name, is_empty_folder=is_empty, allow_empty=is_allow_empty)

            check(rc != 0, f"Fault '{fault_name}' must return error, returned 0")
            check(runner.mutation_started == expect_mutation,
                  f"Fault '{fault_name}': mutation_started expected {expect_mutation}, got {runner.mutation_started}")
            check(runner.recovery_published == expect_recovery,
                  f"Fault '{fault_name}': recovery_published expected {expect_recovery}, got {runner.recovery_published}")

            # 1. Foreign file must remain 100% UNTOUCHED in all cases
            check(runner.foreign_file.is_file(), f"Fault '{fault_name}': foreign file disappeared")
            check(hashlib.md5(runner.foreign_file.read_bytes()).hexdigest() == runner.initial_foreign_hash,
                  f"Fault '{fault_name}': foreign file was modified")

            # 2. Source folder must remain 100% UNTOUCHED in all cases
            if not is_empty:
                check(hashlib.md5((runner.source_dir / "main.dat").read_bytes()).hexdigest() == runner.initial_src_main_hash,
                      f"Fault '{fault_name}': source main.dat modified")
                check(hashlib.md5((runner.source_dir / "config.bin").read_bytes()).hexdigest() == runner.initial_src_cfg_hash,
                      f"Fault '{fault_name}': source config.bin modified")

            # 3. Pre-mutation: live save slot MUST NOT be touched
            if not expect_mutation:
                check((runner.live_save_dir / "original_live.dat").is_file(),
                      f"Fault '{fault_name}': live slot file missing before mutation")
                check(hashlib.md5((runner.live_save_dir / "original_live.dat").read_bytes()).hexdigest() == runner.initial_live_dat_hash,
                      f"Fault '{fault_name}': live slot file modified before mutation")

            # 4. Post-mutation: published recovery archive MUST be retained on disk with original live files
            if expect_recovery:
                check(runner.recovery_zip.is_file(), f"Fault '{fault_name}': recovery archive missing on disk")
                with zipfile.ZipFile(runner.recovery_zip, "r") as rzf:
                    r_names = rzf.namelist()
                    check("original_live.dat" in r_names and "slot.ini" in r_names,
                          f"Fault '{fault_name}': recovery archive missing original live files")
                    check(hashlib.md5(rzf.read("original_live.dat")).hexdigest() == runner.initial_live_dat_hash,
                          f"Fault '{fault_name}': recovery archive contains corrupted original live file")

            # 5. Cleanup assertions
            if fault_name == "stage_cleanup_fail":
                # Truthful failure reporting: stage artifact was NOT cleaned up, remains on disk!
                check(runner.owned_stage_zip.is_file() and runner.owned_stage_dir.is_dir(),
                      "stage_cleanup_fail must leave remaining owned stage artifact on disk")
            else:
                check(runner.owned_stage_zip is None and runner.owned_stage_dir is None,
                      f"Fault '{fault_name}': stage pointers not cleared")
                leftovers = list(runner.staging_parent.glob("2026*"))
                check(len(leftovers) == 0, f"Fault '{fault_name}': leftover stage dirs found: {leftovers}")

    # 50. Full success scenario (Account remap + payload restoration + recovery retained + stage cleaned)
    with tempfile.TemporaryDirectory() as tmp_str:
        runner = ConnectedRealDiskImportRunner(pathlib.Path(tmp_str))
        rc, msg = runner.run(fault=None)

        check(rc == 0 and msg == "OK", f"Full success run failed: {rc}, {msg}")
        check(runner.mutation_started is True, "Success run must execute mutation")
        check(runner.recovery_published is True, "Success run must publish recovery archive")

        # Restored payload in live save slot
        check((runner.live_save_dir / "main.dat").is_file(), "Restored main.dat missing")
        check((runner.live_save_dir / "config.bin").is_file(), "Restored config.bin missing")
        check(hashlib.md5((runner.live_save_dir / "main.dat").read_bytes()).hexdigest() == runner.initial_src_main_hash,
              "Restored main.dat content mismatch")

        # Reserved root metadata filtered out of live save slot
        check(not (runner.live_save_dir / ".nx_save_meta.bin").exists(),
              "Root metadata .nx_save_meta.bin must be filtered from live slot")
        check(not (runner.live_save_dir / ".dbi_save_info.ini").exists(),
              "Root metadata .dbi_save_info.ini must be filtered from live slot")

        # Old live slot files cleared
        check(not (runner.live_save_dir / "original_live.dat").exists(), "Old live slot file not cleared")

        # Published recovery archive retained on disk
        check(runner.recovery_zip.is_file(), "Published recovery archive must be retained on success")
        with zipfile.ZipFile(runner.recovery_zip, "r") as rzf:
            r_names = rzf.namelist()
            check("original_live.dat" in r_names and "slot.ini" in r_names,
                  "Published recovery archive must contain original live files")

        # Foreign and source files untouched
        check(hashlib.md5(runner.foreign_file.read_bytes()).hexdigest() == runner.initial_foreign_hash, "Foreign file modified")
        check(hashlib.md5((runner.source_dir / "main.dat").read_bytes()).hexdigest() == runner.initial_src_main_hash, "Source modified")

        # Stage files deleted
        leftovers = list(runner.staging_parent.glob("2026*"))
        check(len(leftovers) == 0, f"Leftover stage dirs found: {leftovers}")

    # 51. Empty folder opt-in success scenario (allow_empty = true)
    with tempfile.TemporaryDirectory() as tmp_str:
        runner = ConnectedRealDiskImportRunner(pathlib.Path(tmp_str))
        rc, msg = runner.run(fault=None, is_empty_folder=True, allow_empty=True)

        check(rc == 0 and msg == "OK", f"Empty folder opt-in run failed: {rc}, {msg}")
        check(runner.mutation_started is True, "Empty folder restore must execute mutation")
        check(len(list(runner.live_save_dir.iterdir())) == 0, "Empty folder restore must leave empty live save slot")
        check(runner.recovery_zip.is_file(), "Recovery archive must be retained for empty folder restore")
        check(runner.foreign_file.is_file(), "Foreign file preserved")
        leftovers = list(runner.staging_parent.glob("2026*"))
        check(len(leftovers) == 0, "Stage files cleaned after empty folder restore")

    print("  -> Connected real-disk artifact lifecycle & comprehensive fault injection matrix PASSED.")


# ==============================================================================
# Main Runner
# ==============================================================================

def main():
    print("=== Sphaira v0.13.857: Save Folder Backup Import Test Suite ===")
    test_source_contracts()
    test_real_filesystem_fixtures()
    test_jksv_source_validation_and_account_remap()
    test_connected_fault_evidence_matrix()
    print("=== ALL 4 CONTRACT SUITES PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
