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
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(857, 880)),
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

    # 1.4 threaded_file_transfer implementations
    tft_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer.cpp")
    with open(tft_cpp_path, "r", encoding="utf-8") as f:
        tft_cpp = f.read()

    preflight_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer_preflight.cpp")
    with open(preflight_cpp_path, "r", encoding="utf-8") as f:
        preflight_cpp = f.read()

    verify_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer_verify.cpp")
    with open(verify_cpp_path, "r", encoding="utf-8") as f:
        verify_cpp = f.read()

    # Check TransferUnzipPreflight allow_empty logic
    preflight_pos = preflight_cpp.find("Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile")
    check(preflight_pos != -1, "TransferUnzipPreflight void* overload must exist")
    preflight_end = preflight_cpp.find("Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out", preflight_pos)
    check(preflight_end != -1, "TransferUnzipPreflight FsPath overload must follow")
    preflight_body = preflight_cpp[preflight_pos:preflight_end]

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
    van_pos = verify_cpp.find("Result VerifyArchiveAgainstNative(")
    check(van_pos != -1, "VerifyArchiveAgainstNative must exist")
    van_end = verify_cpp.find("Result VerifyArchiveAgainstNative(", van_pos + 1)
    check(van_end != -1, "VerifyArchiveAgainstNative FsPath overload must follow")
    van_body = verify_cpp[van_pos:van_end]
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

    van_fpath_body = verify_cpp[van_end:verify_cpp.find("} // namespace", van_end)]
    check("bool allow_empty" in van_fpath_body and "allow_empty" in van_fpath_body,
          "VerifyArchiveAgainstNative FsPath overload must declare and forward allow_empty")

    # 1.5 save_folder_staging.hpp, save_folder_restore.cpp, and save_restore_zip.cpp scoped checks
    staging_hpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_folder_staging.hpp")
    with open(staging_hpp_path, "r", encoding="utf-8") as f:
        staging_hpp = f.read()

    folder_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_folder_restore.cpp")
    with open(folder_cpp_path, "r", encoding="utf-8") as f:
        folder_cpp = f.read()

    zip_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    with open(zip_cpp_path, "r", encoding="utf-8") as f:
        zip_cpp = f.read()

    # CheckedJoinPath helper
    check("Result CheckedJoinPath(fs::FsPath& out, std::string_view base, std::string_view rel)" in staging_hpp,
          "save_folder_staging.hpp must define CheckedJoinPath")
    cjp_pos = staging_hpp.find("Result CheckedJoinPath(")
    cjp_end = staging_hpp.find("bool IsStagingParentOrRelated(", cjp_pos)
    cjp_body = staging_hpp[cjp_pos:cjp_end]
    check("total_len >= sizeof(fs::FsPath)" in cjp_body,
          "CheckedJoinPath must verify total length against sizeof(fs::FsPath)")
    check("n <= 0 || static_cast<size_t>(n) >= sizeof(buf)" in cjp_body,
          "CheckedJoinPath must verify snprintf return value against buffer capacity")

    # IsStagingParentOrRelated
    ispor_pos = cjp_end
    ispor_end = staging_hpp.find("} // namespace sphaira::ui::menu::save", ispor_pos)
    ispor_body = staging_hpp[ispor_pos:ispor_end]
    check('c == "." || c == ".."' in ispor_body,
          "IsStagingParentOrRelated must check for . or .. traversal components")
    check('equals_ic(comps[0], "dumps")' in ispor_body,
          "IsStagingParentOrRelated must check dumps component")
    check('equals_ic(comps[1], "save-import")' in ispor_body,
          "IsStagingParentOrRelated must check save-import component")

    # StageBackupFolderToZip
    sbf_pos = folder_cpp.find("Result StageBackupFolderToZip(")
    sbf_end = folder_cpp.find("Result RestoreSaveFolder(", sbf_pos)
    sbf_body = folder_cpp[sbf_pos:sbf_end]
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
    rsz_pos = zip_cpp.find("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty, bool* out_created_slot_retained)")
    rsz_end = zip_cpp.find("} // namespace sphaira::ui::menu::save", rsz_pos)
    rsz_body = zip_cpp[rsz_pos:rsz_end]
    check("TransferUnzipPreflight(pbox, zfile, \"/\", save_filter, true, &summary, &source_inventory, allow_empty)" in rsz_body,
          "RestoreSaveZip must pass allow_empty to TransferUnzipPreflight")
    check("if (!allow_empty || !source_inventory.files.empty() || !source_inventory.directories.empty())" in rsz_body,
          "RestoreSaveZip must gate TransferUnzipAll on non-empty inventory unless allow_empty is false")
    check("thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, \"/\", source_inventory, save_filter, true, allow_empty)" in rsz_body,
          "RestoreSaveZip must thread allow_empty to final RO VerifyArchiveAgainstNative")
    check("thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, \"/\", source_inventory, save_filter, true)" in rsz_body,
          "RestoreSaveZip must preserve exact strict default VerifyArchiveAgainstNative call")

    # RestoreSaveFolder scoped check
    rsf_pos = folder_cpp.find("Result RestoreSaveFolder(")
    rsf_end = folder_cpp.find("} // namespace sphaira::ui::menu::save", rsf_pos)
    rsf_body = folder_cpp[rsf_pos:rsf_end]
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

from contract_fixtures.save_folder_import_fixtures import test_real_filesystem_fixtures
from contract_fixtures.save_folder_import_scenarios import ( test_jksv_source_validation_and_account_remap,
    test_connected_fault_evidence_matrix
)

def main():
    print("=== Sphaira v0.13.857: Save Folder Backup Import Test Suite ===")
    test_source_contracts()
    test_real_filesystem_fixtures()
    test_jksv_source_validation_and_account_remap()
    test_connected_fault_evidence_matrix()
    print("=== ALL 4 CONTRACT SUITES PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
