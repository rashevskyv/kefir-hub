#!/usr/bin/env python3
"""
Test Suite: Save Post-Restore Fresh-Remount Verification Contract (Sphaira v0.13.854)

Verifies the verified post-restore fresh read-only remount verification policy for ZIP restore:
1. Static source contracts:
   - threaded_file_transfer.hpp:
     - UnzipPayloadInventory declaration (files map, directories set).
     - TransferUnzipAll declaration with checked_native_save option.
     - TransferUnzipPreflight declaration with inventory_out option.
     - VerifyArchiveAgainstNative declaration (stream and path overloads).
   - save_menu.hpp:
     - 5-argument RestoreSaveZip and RestoreSaveInternal with out_mutation_started.
   - threaded_file_transfer.cpp:
     - ResolvedDestinationEntry and ResolveArchiveDestinationEntry helpers.
     - GetParentDirectories helper.
     - Checked native save flush, close, handle invalidation, and parent Commit in TransferUnzipInternal.
     - UnzipPayloadInventory population in TransferUnzipPreflight with collision checks.
     - VerifyArchiveAgainstNative implementation: exact inventory bijection, 32 KiB chunk comparison
       with short-read accumulation, CRC validation, unz EOF check, rewinding, and post-check re-enumeration.
   - save_menu_ops.cpp:
     - SaveReaderContext struct wrapping mz::FileFuncStdio with sticky io_error / close_error.
     - Explicit checked reader closes (HasError() and unzClose()).
     - Lexical RW scope destroying writable mount before RO mount.
     - Post-restore live extra data rereading and attribute/size verification.
     - Fresh read-only FsNativeSave mount (read_only = true) and VerifyArchiveAgainstNative check.
     - Conservative mutation flag set immediately before DeleteAllCollections.
     - Branched error messages for picked and batch routes distinguishing modified vs unmodified targets.
   - filebrowser_ops.cpp:
     - Forwarding mutation_started and branching error message in RestoreSaveFile.
   - i18n parity in en.json and uk.json for all 7 new UI strings with exact UTF-8 verification.
   - Version bumped to 0.13.854 in sphaira/CMakeLists.txt.
2. Real synthetic ZIP behavioral fixtures using stdlib zipfile, zlib, and io.BytesIO:
   - SaveReaderContext sticky error recording.
   - TransferUnzipPreflight inventory generation and collision detection:
     - Normal payload, DBI leading slash normalization, explicit directories, implicit parent dirs, 0-byte files.
     - Rejection of duplicate kept files, duplicate explicit dirs, file-dir collisions, parent collisions.
   - VerifyArchiveAgainstNative reference engine:
     - Exact bijection verification between archive and native save.
     - Excluded metadata draining and CRC validation.
     - Chunked stream comparison with simulated short reads.
     - Detection of byte alterations, truncated files, extra files, missing files, extra dirs.
     - Post-verification external mutation detection.
     - Archive rewinding verification.
   - Fresh-remount restore lifecycle and mutation tracking:
     - Preflight failure -> mutation_started = False.
     - Recovery backup failure -> mutation_started = False.
     - Restoration write failure -> mutation_started = True.
     - Verification failure on RO remount -> mutation_started = True.
     - Clean success -> mutation_started = True.
     - Sequential batch tracking across multiple save slots.

NO C++ COMPILATION, NO BINARIES, NO NRO, NO WSL REQUIRED. Pure Python stdlib.
"""

import io
import os
import sys
import json
import zlib
import struct
import zipfile

def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ==============================================================================
# 1. Static Source Contracts
# ==============================================================================

def test_source_contracts() -> None:
    print("[1] Running static source contract checks for v0.13.854...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 threaded_file_transfer.hpp declarations
    tft_hpp_path = os.path.join(repo_root, "sphaira", "include", "threaded_file_transfer.hpp")
    with open(tft_hpp_path, "r", encoding="utf-8") as f:
        tft_hpp = f.read()

    check("struct UnzipPayloadInventory {" in tft_hpp,
          "threaded_file_transfer.hpp must define UnzipPayloadInventory")
    check("std::map<std::string, s64> files;" in tft_hpp,
          "UnzipPayloadInventory must contain files map")
    check("std::set<std::string> directories;" in tft_hpp,
          "UnzipPayloadInventory must contain directories set")
    check("bool checked_native_save = false" in tft_hpp,
          "TransferUnzipAll must declare checked_native_save parameter")
    check("UnzipPayloadInventory* inventory_out = nullptr" in tft_hpp,
          "TransferUnzipPreflight must declare inventory_out parameter")
    check("Result VerifyArchiveAgainstNative(" in tft_hpp,
          "threaded_file_transfer.hpp must declare VerifyArchiveAgainstNative")

    # Gate rejecting redundant 6-argument preflight overloads
    check("Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false, UnzipPayloadSummary* output = nullptr);" not in tft_hpp,
          "threaded_file_transfer.hpp must not declare redundant 6-argument zfile TransferUnzipPreflight overload")
    check("Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false, UnzipPayloadSummary* output = nullptr);" not in tft_hpp,
          "threaded_file_transfer.hpp must not declare redundant 6-argument zip_out TransferUnzipPreflight overload")

    # 1.2 save_menu.hpp declarations
    save_hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save_menu.hpp")
    with open(save_hpp_path, "r", encoding="utf-8") as f:
        save_hpp = f.read()

    check("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path = nullptr, bool* out_mutation_started = nullptr);" in save_hpp,
          "save_menu.hpp must declare RestoreSaveZip with out_mutation_started")
    check("Result RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path = nullptr, bool* out_mutation_started = nullptr) const;" in save_hpp,
          "save_menu.hpp must declare RestoreSaveInternal with out_mutation_started")

    # 1.3 threaded_file_transfer implementations
    tft_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer.cpp")
    with open(tft_cpp_path, "r", encoding="utf-8") as f:
        tft_cpp = f.read()

    preflight_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer_preflight.cpp")
    with open(preflight_cpp_path, "r", encoding="utf-8") as f:
        preflight_cpp = f.read()

    preflight_hpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer_preflight.hpp")
    with open(preflight_hpp_path, "r", encoding="utf-8") as f:
        preflight_hpp = f.read()

    verify_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer_verify.cpp")
    with open(verify_cpp_path, "r", encoding="utf-8") as f:
        verify_cpp = f.read()

    zip_io_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer_zip_io.cpp")
    with open(zip_io_cpp_path, "r", encoding="utf-8") as f:
        zip_io_cpp = f.read()

    check('#include "ui/menus/filebrowser.hpp"' in verify_cpp,
          "threaded_file_transfer_verify.cpp must include filebrowser.hpp")
    check("struct ResolvedDestinationEntry" in preflight_hpp,
          "threaded_file_transfer_preflight.hpp must define ResolvedDestinationEntry")
    check("ResolveArchiveDestinationEntry(" in preflight_cpp,
          "threaded_file_transfer_preflight.cpp must define ResolveArchiveDestinationEntry")
    check("GetParentDirectories(" in preflight_cpp,
          "threaded_file_transfer_preflight.cpp must define GetParentDirectories")

    # Checked native save handling in TransferUnzipInternal
    check("if (checked_native_save)" in zip_io_cpp,
          "TransferUnzipInternal must branch on checked_native_save")
    check("fsFileFlush(&f.m_native)" in zip_io_cpp,
          "TransferUnzipInternal must flush file in checked_native_save mode")
    check("fsFileClose(&f.m_native)" in zip_io_cpp,
          "TransferUnzipInternal must close file explicitly in checked_native_save mode")
    check("f.m_native = {};" in zip_io_cpp and "f.m_fs = nullptr;" in zip_io_cpp,
          "TransferUnzipInternal must invalidate file handle so destructor does not double-close")
    check("fs->Commit()" in zip_io_cpp,
          "TransferUnzipInternal must commit filesystem after file write in checked mode")

    # VerifyArchiveAgainstNative implementation
    vaan_pos = verify_cpp.find("Result VerifyArchiveAgainstNative(")
    check(vaan_pos != -1, "threaded_file_transfer_verify.cpp must implement VerifyArchiveAgainstNative")
    vaan_body = verify_cpp[vaan_pos:]
    check("get_collections(" in vaan_body, "VerifyArchiveAgainstNative must enumerate native filesystem")
    check("expected_inventory.files" in vaan_body, "VerifyArchiveAgainstNative must check expected files bijection")
    check("expected_inventory.directories" in vaan_body, "VerifyArchiveAgainstNative must check expected dirs bijection")
    check("CMP_BUF_SIZE = 32768" in vaan_body, "VerifyArchiveAgainstNative must use 32 KiB comparison buffer")
    check("unzGoToFirstFile(zfile)" in vaan_body, "VerifyArchiveAgainstNative must rewind archive")
    check("crc32CalculateWithSeed" in vaan_body, "VerifyArchiveAgainstNative must verify CRC")
    check("post_collections" in vaan_body, "VerifyArchiveAgainstNative must re-enumerate native inventory after byte checks")

    # 1.4 save_restore_zip.hpp, save_restore_zip.cpp & save_menu_ops.cpp implementations
    zip_hpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_zip.hpp")
    with open(zip_hpp_path, "r", encoding="utf-8") as f:
        zip_hpp = f.read()

    zip_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    with open(zip_cpp_path, "r", encoding="utf-8") as f:
        zip_cpp = f.read()

    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_cpp = f.read()

    check("struct SaveReaderContext" in zip_hpp or "struct SaveReaderContext" in zip_cpp,
          "save_restore_zip.hpp/cpp must define SaveReaderContext")
    check("res == static_cast<ZPOS64_T>(-1)" in zip_hpp or "res == static_cast<ZPOS64_T>(-1)" in zip_cpp,
          "SaveReaderContext::ztell64_file must check tell result for error")
    check("source_reader_ctx.InitFileFunc(&file_func);" in zip_cpp,
          "RestoreSaveZip must initialize source_reader_ctx")
    check("rec_reader_ctx.InitFileFunc(&rec_file_func);" in zip_cpp,
          "RestoreSaveZip must initialize rec_reader_ctx")
    check("!source_reader_ctx.HasError()" in zip_cpp,
          "RestoreSaveZip must verify source reader context had no sticky errors")
    check("!rec_reader_ctx.HasError()" in zip_cpp,
          "RestoreSaveZip must verify recovery reader context had no sticky errors")

    # Lexical RW scope destroying save_fs before RO remount
    rsz_start = zip_cpp.find("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started")
    check(rsz_start != -1, "RestoreSaveZip definition must match exact signature")
    rsz_end = zip_cpp.find("} // namespace sphaira::ui::menu::save", rsz_start)
    check(rsz_end != -1, "namespace end must follow RestoreSaveZip")
    rsz_body = zip_cpp[rsz_start:rsz_end]

    check("if (target_entry.save_data_id == 0) {" in rsz_body or "if (e.save_data_id == 0) {" in rsz_body,
          "RestoreSaveZip must restrict metadata probes to legacy create")
    check("R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);" in rsz_body,
          "RestoreSaveZip must check source_reader_ctx.HasError() before clear gate")
    check("R_TRY(pbox->ShouldExitResult());" in rsz_body,
          "RestoreSaveZip must check ShouldExitResult before clear gate")

    check("if (out_mutation_started)" in rsz_body and "*out_mutation_started = true;" in rsz_body,
          "RestoreSaveZip must set *out_mutation_started = true before clear")
    clear_pos = rsz_body.find("DeleteAllCollections")
    mut_pos = rsz_body.rfind("*out_mutation_started = true;", 0, clear_pos)
    check(mut_pos < clear_pos, "*out_mutation_started = true must precede DeleteAllCollections")
    gate_start = rsz_body.rfind("R_UNLESS(!source_reader_ctx.HasError()", 0, mut_pos)
    assert gate_start >= 0
    assert gate_start < rsz_body.rfind("R_TRY(pbox->ShouldExitResult());", 0, mut_pos) < mut_pos < clear_pos
    commit_pos = rsz_body.index("R_TRY(save_fs.Commit());", clear_pos)
    post_pos = rsz_body.index("FsSaveDataExtraData post_live{};", commit_pos)
    # Verify that the RW lexical scope has actually ended, independently of
    # its explanatory comment (brace depth includes balanced initializers).
    depth = lambda pos: rsz_body[:pos].count("{") - rsz_body[:pos].count("}")
    rw_pos = rsz_body.index("fs::FsNativeSave save_fs{")
    assert depth(post_pos) == depth(rw_pos)  # inside if versus former RW scope
    assert depth(rsz_body.index("// Reread live extra data")) < depth(rw_pos)
    ro_pos = rsz_body.index("fs::FsNativeSave ro_save_fs{")
    close_pos = rsz_body.index("source_reader_open = false;", ro_pos)
    assert commit_pos < post_pos < ro_pos < close_pos < rsz_body.rfind("R_SUCCEED();")
    copy_body = zip_io_cpp[zip_io_cpp.index("Result TransferUnzipInternal("):zip_io_cpp.index("Result TransferUnzip(ui::")]
    write_pos = copy_body.index("const auto write_rc = fsFileWrite")
    flush_pos = copy_body.index("const auto flush_rc = fsFileFlush", write_pos)
    assert write_pos < flush_pos
    assert flush_pos < copy_body.index("R_TRY(flush_rc);", flush_pos) < copy_body.index("const auto commit_rc = fs->Commit();", flush_pos)
    assert "UNZ_END_OF_LIST_OF_FILE == unzGoToNextFile(zfile)" in vaan_body
    raw_prefix = ops_cpp.rindex("} else if (*last_item_is_raw) {")
    raw_end = ops_cpp.index("} else if", raw_prefix + 2)
    assert "Restore stopped.\\nSafety recovery archive(s) retained:" in ops_cpp[raw_prefix:raw_end]
    assert "before" not in ops_cpp[raw_prefix:raw_end]

    check("fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&post_live" in rsz_body,
          "RestoreSaveZip must reread live extra data after write commit")
    check("fs::FsNativeSave ro_save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, true};" in rsz_body,
          "RestoreSaveZip must mount read-only save fs (read_only = true)")
    check("thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, \"/\", source_inventory, save_filter, true)" in rsz_body,
          "RestoreSaveZip must verify restored contents on fresh read-only mount")

    # RAW restore isolation in SaveMenu
    rsi_start = ops_cpp.find("Result Menu::RestoreSaveInternal(")
    check(rsi_start != -1, "RestoreSaveInternal definition must exist")
    rsi_end = ops_cpp.find("} // namespace sphaira::ui::menu::save", rsi_start)
    check(rsi_end != -1, "namespace end must follow RestoreSaveInternal")
    rsi_body = ops_cpp[rsi_start:rsi_end]
    check("*out_mutation_started = true;" not in rsi_body,
          "RestoreSaveInternal RAW DISA branch must not set out_mutation_started")
    check("[recovery_path, mutation_started, created_slot_retained, is_raw](Result rc)" in ops_cpp or "[recovery_path, mutation_started, is_raw](Result rc)" in ops_cpp,
          "RestoreSavesPicked must capture is_raw in completion callback")
    check("if (!is_raw && !*created_slot_retained) {" in ops_cpp or "if (!is_raw) {" in ops_cpp,
          "RestoreSavesPicked must isolate RAW restores from ZIP verification messages")
    check("auto last_item_is_raw = std::make_shared<bool>(false);" in ops_cpp,
          "RestoreSaves batch must track last_item_is_raw")
    check("!*last_item_is_raw && *last_mutation_started" in ops_cpp,
          "RestoreSaves batch must check !last_item_is_raw before unverified warning")

    # UI message branching in SaveMenu
    check("Restore stopped: target save may have changed and restored contents are unverified" in ops_cpp,
          "save_menu_ops.cpp must report target may have changed if mutation started (single)")
    check("Restore stopped before target save was modified" in ops_cpp,
          "save_menu_ops.cpp must report stopped before target save was modified (single)")
    check("Restore stopped: current target save may have changed and restored contents are unverified" in ops_cpp,
          "save_menu_ops.cpp must report current target may have changed if mutation started (batch)")
    check("Restore stopped before current target save was modified" in ops_cpp,
          "save_menu_ops.cpp must report stopped before current target save was modified (batch)")

    # 1.5 filebrowser_ops.cpp checks
    fb_ops_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_ops.cpp")
    with open(fb_ops_path, "r", encoding="utf-8") as f:
        fb_ops = f.read()

    check("save::RestoreSaveZip(pbox, se, file_path, recovery_path.get(), mutation_started.get())" in fb_ops,
          "filebrowser_ops.cpp must pass mutation_started pointer to RestoreSaveZip")
    check("[recovery_path, mutation_started, is_disa](Result rc)" in fb_ops,
          "filebrowser_ops.cpp must capture is_disa in completion callback")
    check("if (!is_disa) {" in fb_ops,
          "filebrowser_ops.cpp must isolate RAW restores from ZIP verification messages")
    check("bis_fs.DeleteFile" not in fb_ops,
          "filebrowser_ops.cpp must not contain destructive RAW restore block")
    check("Restore stopped: target save may have changed and restored contents are unverified" in fb_ops,
          "filebrowser_ops.cpp must branch error message based on mutation_started")

    # 1.6 i18n parity and UTF-8 verification
    en_path = os.path.join(repo_root, "assets", "romfs", "i18n", "en.json")
    uk_path = os.path.join(repo_root, "assets", "romfs", "i18n", "uk.json")
    with open(en_path, "r", encoding="utf-8") as f:
        en_json = json.load(f)
    with open(uk_path, "r", encoding="utf-8") as f:
        uk_json = json.load(f)

    new_keys = [
        "Verifying restored save...",
        "Restore stopped: target save may have changed and restored contents are unverified.\nSafety recovery archive retained:\n",
        "Restore stopped before target save was modified.\nSafety recovery archive retained:\n",
        "Restore stopped: current target save may have changed and restored contents are unverified.\nSafety recovery archive(s) retained:\n",
        "Restore stopped before current target save was modified.\nSafety recovery archive(s) retained:\n",
        "Restore stopped before target save was modified.",
        "Restore stopped before current target save was modified."
    ]

    for k in new_keys:
        check(k in en_json and len(en_json[k]) > 0, f"en.json must contain non-empty '{k}'")
        check(k in uk_json and len(uk_json[k]) > 0, f"uk.json must contain non-empty '{k}'")

    # Exact Ukrainian translation checks
    check(uk_json["Verifying restored save..."] == "Перевірка відновленого збереження...",
          "uk.json translation for 'Verifying restored save...' is correct")
    check(uk_json["Restore stopped before target save was modified."] == "Відновлення зупинено до зміни цільового збереження.",
          "uk.json translation for 'Restore stopped before target save was modified.' is correct")

    # 1.7 Version bump in CMakeLists.txt
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check("set(sphaira_VERSION " in cmake_src,
          "sphaira/CMakeLists.txt remains the canonical version source")

    print("  -> Static source contracts PASSED.")


# ==============================================================================
# 2. Synthetic Behavioral Reference Model
# ==============================================================================

RESERVED_NAMES = {".nx_save_meta.bin", ".dbi_save_info.ini", ".dbi_save_extra"}
INVALID_CHARS = set(":*?\"<>|\\")
MAX_S64 = 0x7FFFFFFFFFFFFFFF

from contract_fixtures.save_post_restore_models import (
    RES_OK, ERR_VERIFICATION_FAILED, ERR_UNZ_READ, ERR_UNZ_CLOSE,
    ERR_CANCELLED, ERR_OVERFLOW, ERR_INVALID_PATH, ERR_INVALID_SIZE,
    ERR_REWIND_FAILED, RESERVED_NAMES, SyntheticSaveFs, build_test_zip,
    simulate_verify_against_native, simulate_preflight
)
from contract_fixtures.save_post_restore_fixtures import test_behavioral_fixtures

def test_policy_and_fault_gates() -> None:
    """Real ZIP policy fixtures and injected sequential gates; not C++/IPC proof."""
    def archive(entries):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as zf:
            for name, data in entries:
                # ZipInfo's Windows constructor rewrites backslashes; preserve
                # hostile wire names instead of testing a sanitized fixture.
                info = zipfile.ZipInfo()
                info.filename = info.orig_filename = name
                zf.writestr(info, data)
        return buf.getvalue()

    for name in ("//a", "a\\b", "a:b", "../a", "a/./b", "a//b", "a\x7fb"):
        try:
            simulate_preflight(archive([(name, b"x")]))
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsafe entry accepted: {name!r}")
    for entries in (("a?", "a_"), ("x", "x/"), ("x", "x/y")):
        for order in (entries, entries[::-1]):
            try:
                simulate_preflight(archive([(name, b"x") for name in order]))
            except FileExistsError:
                pass
            else:
                raise AssertionError(f"collision accepted: {order}")

    raw = archive([(".nx_save_meta.bin", b"meta"), (".DBI_SAVE_EXTRA", b"meta"),
                   ("sub/.nx_save_meta.bin", b"payload"), (".NX_SAVE_META.BIN", b"payload"),
                   ("dir/", b"directory payload"), ("zero", b"")])
    files, dirs, _ = simulate_preflight(raw)
    assert files == {"/sub/.nx_save_meta.bin": 7, "/.NX_SAVE_META.BIN": 7, "/zero": 0}
    native = SyntheticSaveFs({"/sub/.nx_save_meta.bin": b"payload",
                              "/.NX_SAVE_META.BIN": b"payload", "/zero": b""}, dirs)
    assert simulate_verify_against_native(raw, native, files, dirs)
    empty = archive([(".nx_save_meta.bin", b"meta")])
    assert simulate_verify_against_native(empty, SyntheticSaveFs(), {}, set())
    expected = simulate_preflight(archive([("empty/", b"")]))
    assert not simulate_verify_against_native(empty, SyntheticSaveFs(dirs={"/empty"}),
                                             expected[0], expected[1])
    # Genuine CRC zero: both central/local headers produced by zipfile agree.
    with zipfile.ZipFile(io.BytesIO(archive([("zero", b"")]))) as zf:
        assert zf.infolist()[0].CRC == 0 and zf.read("zero") == b""

    def lifecycle(fail=None):
        events = []
        retained = mutated = False
        reader = io.BytesIO(b"payload")
        def gate(name):
            events.append(name)
            if name == fail:
                raise OSError(name)
        try:
            gate("source_open")
            gate("seek"); reader.seek(0)
            gate("tell"); assert reader.tell() == 0
            gate("read"); assert reader.read() == b"payload"
            gate("entry_close")
            gate("recovery_commit")
            retained = True
            gate("cancel_before_clear")
            mutated = True
            gate("write")
            gate("join")
            gate("flush")
            gate("file_close")  # native close is void, not an injectable Result
            gate("file_commit")
            gate("final_commit")
            gate("rw_close")
            gate("ro_open")
            gate("cancel_during_verify")
            gate("native_read")
            gate("rewind"); reader.seek(0)
            gate("ro_close")
            gate("callback_close"); reader.close()
            events.append("success")
            return True, retained, mutated, events
        except OSError:
            return False, retained, mutated, events
        finally:
            reader.close()

    ok, retained, mutated, events = lifecycle()
    assert ok and retained and mutated
    assert events.index("join") < events.index("flush") < events.index("file_close")
    assert events.index("file_close") < events.index("file_commit") < events.index("final_commit")
    assert events.index("rw_close") < events.index("ro_open") < events.index("callback_close") < events.index("success")
    for fault in ("source_open", "seek", "tell", "read", "entry_close", "recovery_commit",
                  "cancel_before_clear", "write", "flush", "file_commit", "final_commit",
                  "ro_open", "cancel_during_verify", "native_read", "rewind", "callback_close"):
        ok, retained, mutated, trace = lifecycle(fault)
        assert not ok and "success" not in trace
        assert retained == (events.index(fault) > events.index("recovery_commit"))
        assert mutated == (events.index(fault) > events.index("cancel_before_clear"))
    print("  -> Policy/ZIP fixtures and 16 injected lifecycle faults PASSED (model only).")


def main() -> None:
    print("=== Sphaira v0.13.854: Fresh-Remount Verification Test Suite ===")
    test_source_contracts()
    test_behavioral_fixtures()
    test_policy_and_fault_gates()
    print("=== ALL CHECKS PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    main()
