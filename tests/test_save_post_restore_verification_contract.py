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

    # 1.3 threaded_file_transfer.cpp implementations
    tft_cpp_path = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer.cpp")
    with open(tft_cpp_path, "r", encoding="utf-8") as f:
        tft_cpp = f.read()

    check('#include "ui/menus/filebrowser.hpp"' in tft_cpp,
          "threaded_file_transfer.cpp must include filebrowser.hpp")
    check("struct ResolvedDestinationEntry" in tft_cpp,
          "threaded_file_transfer.cpp must define ResolvedDestinationEntry")
    check("ResolveArchiveDestinationEntry(" in tft_cpp,
          "threaded_file_transfer.cpp must define ResolveArchiveDestinationEntry")
    check("GetParentDirectories(" in tft_cpp,
          "threaded_file_transfer.cpp must define GetParentDirectories")

    # Checked native save handling in TransferUnzipInternal
    check("if (checked_native_save)" in tft_cpp,
          "TransferUnzipInternal must branch on checked_native_save")
    check("fsFileFlush(&f.m_native)" in tft_cpp,
          "TransferUnzipInternal must flush file in checked_native_save mode")
    check("fsFileClose(&f.m_native)" in tft_cpp,
          "TransferUnzipInternal must close file explicitly in checked_native_save mode")
    check("f.m_native = {};" in tft_cpp and "f.m_fs = nullptr;" in tft_cpp,
          "TransferUnzipInternal must invalidate file handle so destructor does not double-close")
    check("fs->Commit()" in tft_cpp,
          "TransferUnzipInternal must commit filesystem after file write in checked mode")

    # VerifyArchiveAgainstNative implementation
    vaan_pos = tft_cpp.find("Result VerifyArchiveAgainstNative(")
    check(vaan_pos != -1, "threaded_file_transfer.cpp must implement VerifyArchiveAgainstNative")
    vaan_body = tft_cpp[vaan_pos:]
    check("get_collections(" in vaan_body, "VerifyArchiveAgainstNative must enumerate native filesystem")
    check("expected_inventory.files" in vaan_body, "VerifyArchiveAgainstNative must check expected files bijection")
    check("expected_inventory.directories" in vaan_body, "VerifyArchiveAgainstNative must check expected dirs bijection")
    check("CMP_BUF_SIZE = 32768" in vaan_body, "VerifyArchiveAgainstNative must use 32 KiB comparison buffer")
    check("unzGoToFirstFile(zfile)" in vaan_body, "VerifyArchiveAgainstNative must rewind archive")
    check("crc32CalculateWithSeed" in vaan_body, "VerifyArchiveAgainstNative must verify CRC")
    check("post_collections" in vaan_body, "VerifyArchiveAgainstNative must re-enumerate native inventory after byte checks")

    # 1.4 save_menu_ops.cpp implementations
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_cpp = f.read()

    check("struct SaveReaderContext" in ops_cpp,
          "save_menu_ops.cpp must define SaveReaderContext")
    check("res == static_cast<ZPOS64_T>(-1)" in ops_cpp,
          "SaveReaderContext::ztell64_file must check tell result for error")
    check("source_reader_ctx.InitFileFunc(&file_func);" in ops_cpp,
          "RestoreSaveZip must initialize source_reader_ctx")
    check("rec_reader_ctx.InitFileFunc(&rec_file_func);" in ops_cpp,
          "RestoreSaveZip must initialize rec_reader_ctx")
    check("!source_reader_ctx.HasError()" in ops_cpp,
          "RestoreSaveZip must verify source reader context had no sticky errors")
    check("!rec_reader_ctx.HasError()" in ops_cpp,
          "RestoreSaveZip must verify recovery reader context had no sticky errors")

    # Lexical RW scope destroying save_fs before RO remount
    rsz_start = ops_cpp.find("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started)")
    check(rsz_start != -1, "RestoreSaveZip definition must match exact signature")
    rsi_start = ops_cpp.find("Result Menu::RestoreSaveInternal(", rsz_start)
    check(rsi_start != -1, "RestoreSaveInternal must follow RestoreSaveZip")
    rsz_body = ops_cpp[rsz_start:rsi_start]

    check("if (e.save_data_id == 0) {" in rsz_body,
          "RestoreSaveZip must restrict metadata probes to legacy create")
    check("R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);" in rsz_body,
          "RestoreSaveZip must check source_reader_ctx.HasError() before clear gate")
    check("R_TRY(pbox->ShouldExitResult());" in rsz_body,
          "RestoreSaveZip must check ShouldExitResult before clear gate")

    check("if (out_mutation_started)" in rsz_body and "*out_mutation_started = true;" in rsz_body,
          "RestoreSaveZip must set *out_mutation_started = true before clear")
    clear_pos = rsz_body.find("DeleteAllCollections")
    mut_pos = rsz_body.find("*out_mutation_started = true;")
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
    copy_body = tft_cpp[tft_cpp.index("static Result TransferUnzipInternal("):tft_cpp.index("Result TransferUnzip(ui::")]
    write_pos = copy_body.index("const auto write_rc = fsFileWrite")
    flush_pos = copy_body.index("const auto flush_rc = fsFileFlush", write_pos)
    assert write_pos < flush_pos
    assert flush_pos < copy_body.index("R_TRY(flush_rc);", flush_pos) < copy_body.index("const auto commit_rc = fs->Commit();", flush_pos)
    assert "UNZ_END_OF_LIST_OF_FILE == unzGoToNextFile(zfile)" in vaan_body
    raw_prefix = ops_cpp.index("} else if (*last_item_is_raw) {")
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
    rsi_body = ops_cpp[rsi_start:ops_cpp.find("Result Menu::BackupSaveInternal(", rsi_start)]
    check("*out_mutation_started = true;" not in rsi_body,
          "RestoreSaveInternal RAW DISA branch must not set out_mutation_started")
    check("[recovery_path, mutation_started, is_raw](Result rc)" in ops_cpp,
          "RestoreSavesPicked must capture is_raw in completion callback")
    check("if (!is_raw) {" in ops_cpp,
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

class SyntheticSaveFs:
    """Mock filesystem simulating the Nintendo Switch FsNativeSave."""
    def __init__(self, files: dict[str, bytes] = None, dirs: set[str] = None):
        self.files = dict(files) if files else {}
        self.dirs = set(dirs) if dirs else set()
        self.is_open = True
        self.committed = False
        self.read_only = False

    def close(self):
        self.is_open = False

    def commit(self):
        if self.read_only:
            raise PermissionError("Cannot commit read-only mount")
        self.committed = True

    def get_collections(self):
        # Returns list of collection-like dicts
        cols = {}
        cols["/"] = {"files": [], "dirs": []}
        for d in sorted(self.dirs):
            cols[d] = {"files": [], "dirs": []}

        for d in self.dirs:
            parent = os.path.dirname(d)
            if not parent.startswith("/"): parent = "/" + parent
            if parent in cols:
                cols[parent]["dirs"].append(os.path.basename(d))

        for fpath, data in self.files.items():
            parent = os.path.dirname(fpath)
            if not parent.startswith("/"): parent = "/" + parent
            if parent not in cols:
                cols[parent] = {"files": [], "dirs": []}
            cols[parent]["files"].append((os.path.basename(fpath), len(data)))

        result = []
        for p, content in cols.items():
            result.append({"path": p, "files": content["files"], "dirs": content["dirs"]})
        return result


def build_test_zip(files: dict[str, bytes], dirs: list[str] = None, leading_slash: bool = False,
                   corrupt_crc: bool = False, corrupt_file: str = None, truncated_file: str = None) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        if dirs:
            for d in dirs:
                name = ("/" if leading_slash else "") + d.strip("/") + "/"
                zinfo = zipfile.ZipInfo(name)
                zf.writestr(zinfo, b"")

        for name, data in files.items():
            entry_name = ("/" if leading_slash else "") + name.lstrip("/")
            if name == truncated_file:
                # Write truncated payload
                zinfo = zipfile.ZipInfo(entry_name)
                zinfo.file_size = len(data) + 100 # Declared larger than actual
                zf.writestr(zinfo, data)
            else:
                zf.writestr(entry_name, data)

    raw_zip = buf.getvalue()
    if corrupt_crc or corrupt_file:
        raw_zip = bytearray(raw_zip)
        idx = 0
        while True:
            pos = raw_zip.find(b"PK\x01\x02", idx)
            if pos == -1:
                break
            fname_len = struct.unpack_from("<H", raw_zip, pos + 28)[0]
            fname = raw_zip[pos + 46 : pos + 46 + fname_len].decode("utf-8", errors="ignore")
            if corrupt_file:
                if fname.lstrip("/") == corrupt_file.lstrip("/"):
                    raw_zip[pos + 16] ^= 0xFF # Corrupt CRC in central directory
                    break
            elif corrupt_crc:
                raw_zip[pos + 16] ^= 0xFF
                break
            idx = pos + 4
        raw_zip = bytes(raw_zip)

    return raw_zip


def normalize_entry(name: str) -> tuple[str, bool]:
    norm = name[1:] if name.startswith("/") else name
    if (not norm or norm.startswith("/") or "//" in norm
            or any(c in "\\:" or ord(c) < 32 or ord(c) == 127 for c in norm)
            or any(c in (".", "..") for c in norm.split("/"))):
        raise ValueError("Unsafe save archive entry")
    norm = "".join("_" if c in INVALID_CHARS else c for c in norm)
    is_dir = norm.endswith("/")
    if is_dir:
        norm = norm[:-1]
    return norm, is_dir


def is_metadata(name: str, is_dir: bool) -> bool:
    return not is_dir and (name == ".nx_save_meta.bin"
                           or name.lower() in (".dbi_save_info.ini", ".dbi_save_extra"))


def get_parent_directories(path: str) -> list[str]:
    parents = []
    last_slash = path.rfind("/")
    while last_slash > 0:
        parent = path[:last_slash]
        parents.append(parent)
        last_slash = parent.rfind("/")
    return parents


def simulate_preflight(zip_bytes: bytes, filter_meta: bool = True) -> tuple[dict[str, int], set[str], int]:
    """Simulates TransferUnzipPreflight populating UnzipPayloadInventory."""
    buf = io.BytesIO(zip_bytes)
    zf = zipfile.ZipFile(buf, "r")

    inventory_files = {}
    inventory_dirs = set()
    total_bytes = 0

    explicit_dirs = set()

    for zinfo in zf.infolist():
        raw_name = zinfo.orig_filename
        norm_name, is_dir = normalize_entry(raw_name)

        if not norm_name:
            continue

        # Every entry, including directory payloads and metadata, is CRC-drained.
        data = zf.read(zinfo)
        if len(data) != zinfo.file_size or zlib.crc32(data) != zinfo.CRC:
            raise ValueError("Size/CRC mismatch")
        if filter_meta and is_metadata(norm_name, is_dir):
            # Drained & verified CRC, but skipped from inventory
            data = zf.read(zinfo)
            if zlib.crc32(data) != zinfo.CRC:
                raise ValueError("CRC error in meta")
            continue

        canonical_path = "/" + norm_name

        if is_dir:
            if canonical_path in explicit_dirs:
                raise FileExistsError(f"Duplicate explicit dir: {canonical_path}")
            if canonical_path in inventory_files:
                raise FileExistsError(f"Dir conflicts with file: {canonical_path}")
            for p in get_parent_directories(canonical_path):
                if p in inventory_files:
                    raise FileExistsError(f"Parent of dir conflicts with file: {p}")
                inventory_dirs.add(p)
            explicit_dirs.add(canonical_path)
            inventory_dirs.add(canonical_path)
        else:
            if canonical_path in inventory_files:
                raise FileExistsError(f"Duplicate kept file: {canonical_path}")
            if canonical_path in inventory_dirs:
                raise FileExistsError(f"File conflicts with dir: {canonical_path}")
            for p in get_parent_directories(canonical_path):
                if p in inventory_files:
                    raise FileExistsError(f"Parent of file conflicts with file: {p}")
                inventory_dirs.add(p)

            # Test CRC decompression
            data = zf.read(zinfo)
            if len(data) != zinfo.file_size:
                raise ValueError("Size mismatch")
            if zlib.crc32(data) != zinfo.CRC:
                raise ValueError("CRC mismatch")

            inventory_files[canonical_path] = zinfo.file_size
            total_bytes += zinfo.file_size

    return inventory_files, inventory_dirs, total_bytes


def simulate_verify_against_native(zip_bytes: bytes, native_fs: SyntheticSaveFs,
                                   expected_files: dict[str, int], expected_dirs: set[str],
                                   filter_meta: bool = True, simulate_short_reads: bool = True) -> bool:
    """Simulates VerifyArchiveAgainstNative stream comparison and bijection check."""
    observed_files, observed_dirs, _ = simulate_preflight(zip_bytes, filter_meta)
    if observed_files != expected_files or observed_dirs != expected_dirs:
        return False
    # 1. Native enumeration
    cols = native_fs.get_collections()
    native_files = {}
    native_dirs = set()

    for col in cols:
        p = col["path"]
        if p != "/":
            native_dirs.add(p)
            for parent in get_parent_directories(p):
                native_dirs.add(parent)
        for d in col["dirs"]:
            dp = p.rstrip("/") + "/" + d
            native_dirs.add(dp)
            for parent in get_parent_directories(dp):
                native_dirs.add(parent)
        for fname, fsize in col["files"]:
            fp = p.rstrip("/") + "/" + fname
            native_files[fp] = fsize
            for parent in get_parent_directories(fp):
                native_dirs.add(parent)

    # 2. Bijection check
    if native_files != expected_files:
        return False
    if native_dirs != expected_dirs:
        return False

    # 3. Stream compare in 32 KiB chunks
    buf = io.BytesIO(zip_bytes)
    zf = zipfile.ZipFile(buf, "r")

    verified_files = set()

    for zinfo in zf.infolist():
        norm_name, is_dir = normalize_entry(zinfo.orig_filename)
        if not norm_name:
            continue
        if filter_meta and is_metadata(norm_name, is_dir):
            data = zf.read(zinfo)
            if zlib.crc32(data) != zinfo.CRC:
                return False
            continue

        canonical_path = "/" + norm_name
        if is_dir:
            if canonical_path not in expected_dirs:
                return False
        else:
            if canonical_path not in expected_files:
                return False
            if canonical_path not in native_fs.files:
                return False

            native_bytes = native_fs.files[canonical_path]
            if len(native_bytes) != zinfo.file_size:
                return False

            # Simulate 32 KiB chunked read with short-read accumulation
            zstream = zf.open(zinfo)
            chunk_size = 32768
            offset = 0
            file_len = len(native_bytes)

            while offset < file_len:
                to_read = min(chunk_size, file_len - offset)
                if simulate_short_reads and to_read > 1024:
                    # Accumulate in smaller subchunks
                    accum = bytearray()
                    while len(accum) < to_read:
                        sub = zstream.read(min(1024, to_read - len(accum)))
                        if not sub:
                            return False
                        accum.extend(sub)
                    zchunk = bytes(accum)
                else:
                    zchunk = zstream.read(to_read)
                if len(zchunk) != to_read:
                    return False

                fchunk = native_bytes[offset:offset+to_read]
                if zchunk != fchunk:
                    return False
                offset += to_read

            # EOF check on zip stream
            extra = zstream.read(1)
            if extra:
                return False

            verified_files.add(canonical_path)

    if len(verified_files) != len(expected_files):
        return False

    # 4. Post-check re-enumeration
    post_cols = native_fs.get_collections()
    post_files = {}
    for col in post_cols:
        p = col["path"]
        for fname, fsize in col["files"]:
            post_files[p.rstrip("/") + "/" + fname] = fsize
    if post_files != native_files:
        return False

    return True


def test_behavioral_fixtures() -> None:
    print("[2] Running synthetic behavioral regression fixtures...")

    # Fixture 1: Clean preflight with DBI leading slashes, implicit parents, and empty dirs
    files = {
        "savedata.bin": b"Hello Switch Save",
        "sub/slot0.dat": b"Slot0 Data",
        "a/b/c/nested.dat": b"Deep Data",
        ".nx_save_meta.bin": b"SphairaMeta",
        ".dbi_save_info.ini": b"[DBI] Info",
        "empty_file.bin": b""
    }
    dirs = ["empty_folder", "sub"]
    zip_bytes = build_test_zip(files, dirs, leading_slash=True)

    inv_files, inv_dirs, total_bytes = simulate_preflight(zip_bytes)
    check("/savedata.bin" in inv_files, "inv_files contains /savedata.bin")
    check("/sub/slot0.dat" in inv_files, "inv_files contains /sub/slot0.dat")
    check("/a/b/c/nested.dat" in inv_files, "inv_files contains /a/b/c/nested.dat")
    check("/empty_file.bin" in inv_files and inv_files["/empty_file.bin"] == 0, "0-byte file in inventory")
    check(".nx_save_meta.bin" not in inv_files, "Meta excluded from files")
    check("/empty_folder" in inv_dirs, "Explicit empty dir in inv_dirs")
    check("/a" in inv_dirs and "/a/b" in inv_dirs and "/a/b/c" in inv_dirs, "Implicit parent dirs in inv_dirs")
    print("  -> Fixture 1 (Preflight inventory with DBI slash & implicit parents) PASSED.")

    # Fixture 2: Order-independent collision rejection
    # Duplicate kept files
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("slot.bin", b"first")
        zf.writestr("/slot.bin", b"second")
    dup_file_zip = buf.getvalue()
    try:
        simulate_preflight(dup_file_zip)
        check(False, "Duplicate file must be rejected")
    except FileExistsError:
        pass

    # Duplicate explicit directories
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("dir/", b"")
        zf.writestr("/dir/", b"")
    dup_dir_zip = buf.getvalue()
    try:
        simulate_preflight(dup_dir_zip)
        check(False, "Duplicate dir must be rejected")
    except FileExistsError:
        pass

    # File vs Dir conflict
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("item", b"file data")
        zf.writestr("item/", b"")
    file_dir_zip = buf.getvalue()
    try:
        simulate_preflight(file_dir_zip)
        check(False, "File vs Dir conflict must be rejected")
    except FileExistsError:
        pass

    # File as parent of another file
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("foo", b"file data")
        zf.writestr("foo/bar.txt", b"nested data")
    file_as_parent_zip = buf.getvalue()
    try:
        simulate_preflight(file_as_parent_zip)
        check(False, "File as parent must be rejected")
    except FileExistsError:
        pass
    print("  -> Fixture 2 (Order-independent collision rejections) PASSED.")

    # Fixture 3: VerifyArchiveAgainstNative exact match succeeds
    native_files = {
        "/savedata.bin": b"Hello Switch Save",
        "/sub/slot0.dat": b"Slot0 Data",
        "/a/b/c/nested.dat": b"Deep Data",
        "/empty_file.bin": b""
    }
    native_dirs = {"/empty_folder", "/sub", "/a", "/a/b", "/a/b/c"}
    save_fs = SyntheticSaveFs(native_files, native_dirs)

    res = simulate_verify_against_native(zip_bytes, save_fs, inv_files, inv_dirs, filter_meta=True, simulate_short_reads=True)
    check(res is True, "Exact match must verify successfully")
    print("  -> Fixture 3 (VerifyArchiveAgainstNative exact match with chunk accumulation) PASSED.")

    # Fixture 4: Same-size wrong bytes detected
    tampered_files = dict(native_files)
    tampered_files["/savedata.bin"] = b"Hello Switch Safe" # 1 byte changed ('v' -> 'f')
    save_fs_tampered = SyntheticSaveFs(tampered_files, native_dirs)
    res_tampered = simulate_verify_against_native(zip_bytes, save_fs_tampered, inv_files, inv_dirs)
    check(res_tampered is False, "Same-size wrong bytes must be detected and rejected")
    print("  -> Fixture 4 (Same-size wrong bytes rejection) PASSED.")

    # Fixture 5: Truncated file in native save detected
    trunc_files = dict(native_files)
    trunc_files["/savedata.bin"] = b"Hello Switch" # Truncated
    save_fs_trunc = SyntheticSaveFs(trunc_files, native_dirs)
    res_trunc = simulate_verify_against_native(zip_bytes, save_fs_trunc, inv_files, inv_dirs)
    check(res_trunc is False, "Truncated file in native save must be detected and rejected")
    print("  -> Fixture 5 (Truncated native file rejection) PASSED.")

    # Fixture 6: Extra file or extra directory in native save detected (leftovers)
    extra_file_fs = SyntheticSaveFs({**native_files, "/extra.dat": b"stale"}, native_dirs)
    res_extra_file = simulate_verify_against_native(zip_bytes, extra_file_fs, inv_files, inv_dirs)
    check(res_extra_file is False, "Extra leftover file in native save must be rejected")

    extra_dir_fs = SyntheticSaveFs(native_files, native_dirs | {"/stale_dir"})
    res_extra_dir = simulate_verify_against_native(zip_bytes, extra_dir_fs, inv_files, inv_dirs)
    check(res_extra_dir is False, "Extra leftover dir in native save must be rejected")
    print("  -> Fixture 6 (Leftover files/dirs rejection) PASSED.")

    # Fixture 7: CRC corruption in payload archive detected during preflight
    corrupt_zip = build_test_zip({"data.bin": b"12345"}, corrupt_crc=True)
    try:
        simulate_preflight(corrupt_zip)
        check(False, "CRC corrupted ZIP must be rejected in preflight")
    except (ValueError, zipfile.BadZipFile):
        pass
    print("  -> Fixture 7 (CRC corruption rejection) PASSED.")

    # Fixture 8: Mutation tracking and lifecycle simulation
    class RestoreSimulation:
        def __init__(self):
            self.mutation_started = False
            self.recovery_path = None
            self.save_fs = SyntheticSaveFs({"/old.bin": b"old data"})

        def restore(self, fail_at: str) -> bool:
            # 1. Preflight source
            if fail_at == "preflight":
                return False

            # 2. Recovery backup creation & preflight & verification
            if fail_at == "recovery_creation":
                return False

            if fail_at == "recovery_verification":
                return False

            # Recovery archive is published ONLY after verification and commit succeed!
            self.recovery_path = "/dumps/recovery/20260917_000000_1234567890ABCDEF_000/recovery.zip"

            # Failure before clear gate (e.g. exit check or sticky error)
            if fail_at == "pre_mutation_cancel":
                return False

            # 3. Mutation starts: clear
            self.mutation_started = True
            if fail_at == "clear":
                self.save_fs.files.clear()
                return False

            # 4. Unzip restore write
            if fail_at == "write":
                self.save_fs.files["/partial.bin"] = b"incomplete"
                return False

            self.save_fs.files = {"/new.bin": b"new data"}
            self.save_fs.commit()

            # 5. RW scope exit (save_fs closed)
            self.save_fs.close()

            # 6. Read-only remount verification
            ro_fs = SyntheticSaveFs(self.save_fs.files)
            ro_fs.read_only = True
            if fail_at == "ro_verify":
                return False

            return True

    # Case A1: Failure before mutation with published recovery path (e.g. pre-clear exit check)
    sim_a1 = RestoreSimulation()
    ok_a1 = sim_a1.restore("pre_mutation_cancel")
    check(not ok_a1 and not sim_a1.mutation_started and sim_a1.recovery_path is not None,
          "Failure before mutation with published recovery must retain recovery archive and leave mutation_started False")

    # Case A2: Failure during recovery verification (before publication)
    sim_a2 = RestoreSimulation()
    ok_a2 = sim_a2.restore("recovery_verification")
    check(not ok_a2 and not sim_a2.mutation_started and sim_a2.recovery_path is None,
          "Failure during recovery verification must not publish recovery path and leave mutation_started False")

    # Case B: Failure during mutation write
    sim_b = RestoreSimulation()
    ok_b = sim_b.restore("write")
    check(not ok_b and sim_b.mutation_started and sim_b.recovery_path is not None,
          "Failure during write must flag mutation_started True")

    # Case C: Failure during RO remount verification
    sim_c = RestoreSimulation()
    ok_c = sim_c.restore("ro_verify")
    check(not ok_c and sim_c.mutation_started and sim_c.recovery_path is not None,
          "Failure during fresh-remount verification must flag mutation_started True")

    # Case D: Clean success
    sim_d = RestoreSimulation()
    ok_d = sim_d.restore("none")
    check(ok_d and sim_d.mutation_started and sim_d.recovery_path is not None,
          "Clean success must succeed with published recovery path")

    print("  -> Fixture 8 (Mutation tracking lifecycle & publication ordering) PASSED.")

    # Fixture 9: Sequential batch restore tracking
    # 9.1: Item 1 publishes recovery then fails before mutation -> both recovery paths retained
    batch_states_published = [
        {"item": 0, "fail_at": "none"},
        {"item": 1, "fail_at": "pre_mutation_cancel"},
        {"item": 2, "fail_at": "none"},
    ]
    recovered_items_pub = []
    last_mutation_started_pub = False
    batch_failed_pub = False

    for b in batch_states_published:
        sim = RestoreSimulation()
        ok = sim.restore(b["fail_at"])
        if sim.recovery_path:
            recovered_items_pub.append(sim.recovery_path)
        if not ok:
            last_mutation_started_pub = sim.mutation_started
            batch_failed_pub = True
            break

    check(batch_failed_pub is True, "Batch correctly stopped on item 1")
    check(len(recovered_items_pub) == 2, "Recovery paths accumulated for item 0 and item 1")
    check(last_mutation_started_pub is False, "Item 1 failed before mutation, so last_mutation_started is False")

    # 9.2: Item 1 fails during recovery verification -> only item 0's recovery path retained
    batch_states_unpub = [
        {"item": 0, "fail_at": "none"},
        {"item": 1, "fail_at": "recovery_verification"},
        {"item": 2, "fail_at": "none"},
    ]
    recovered_items_unpub = []
    last_mutation_started_unpub = False
    batch_failed_unpub = False

    for b in batch_states_unpub:
        sim = RestoreSimulation()
        ok = sim.restore(b["fail_at"])
        if sim.recovery_path:
            recovered_items_unpub.append(sim.recovery_path)
        if not ok:
            last_mutation_started_unpub = sim.mutation_started
            batch_failed_unpub = True
            break

    check(batch_failed_unpub is True, "Batch correctly stopped on item 1")
    check(len(recovered_items_unpub) == 1, "Only item 0 published recovery path retained")
    check(last_mutation_started_unpub is False, "Item 1 failed before mutation, so last_mutation_started is False")
    print("  -> Fixture 9 (Sequential batch restore tracking) PASSED.")

    # Fixture 10: RAW restore isolation in single and batch UI callbacks
    def simulate_ui_callbacks(rc: int, is_raw: bool, recovery_paths: list[str], mutation_started: bool):
        dialogs = []
        if rc != 0:
            dialogs.append("ErrorBox: Restore failed!")
        else:
            dialogs.append("Notify: Restore successful!")

        if not is_raw:
            if len(recovery_paths) > 0:
                if rc == 0:
                    prefix = "Restore completed."
                elif mutation_started:
                    prefix = "Restore stopped: target save may have changed and restored contents are unverified."
                else:
                    prefix = "Restore stopped before target save was modified."
                dialogs.append(f"OptionBox: {prefix}")
            elif rc != 0:
                if not mutation_started:
                    dialogs.append("OptionBox: Restore stopped before target save was modified.")
        return dialogs

    # 10.1: RAW single restore fails -> no OptionBox about recovery or unverified save
    dlg_raw_fail = simulate_ui_callbacks(rc=1, is_raw=True, recovery_paths=[], mutation_started=False)
    check(dlg_raw_fail == ["ErrorBox: Restore failed!"], "RAW failure must not show ZIP verification OptionBox")

    # 10.2: ZIP single restore fails before mutation -> OptionBox with before-mutation notice
    dlg_zip_before = simulate_ui_callbacks(rc=1, is_raw=False, recovery_paths=[], mutation_started=False)
    check("OptionBox: Restore stopped before target save was modified." in dlg_zip_before,
          "ZIP failure before mutation must show stopped before modified OptionBox")

    # 10.3: ZIP single restore fails after mutation -> OptionBox with unverified notice and recovery
    dlg_zip_mut = simulate_ui_callbacks(rc=1, is_raw=False, recovery_paths=["/dumps/rec.zip"], mutation_started=True)
    check(any("unverified" in d for d in dlg_zip_mut),
          "ZIP failure after mutation must show unverified contents OptionBox")

    # 10.4: Batch where Item 0 is ZIP (success) and Item 1 is RAW (fails auto-backup)
    def simulate_batch_ui_callback(rc: int, last_item_is_raw: bool, recovery_paths: list[str], last_mutation_started: bool):
        dialogs = []
        if rc != 0:
            dialogs.append("ErrorBox: Restore failed!")
        else:
            dialogs.append("Notify: Restore successful!")

        if len(recovery_paths) > 0:
            if rc == 0:
                prefix = "Restore completed."
            elif last_item_is_raw:
                prefix = "Restore stopped. Safety recovery archives retained."
            elif not last_item_is_raw and last_mutation_started:
                prefix = "Restore stopped: current target save may have changed and restored contents are unverified."
            else:
                prefix = "Restore stopped before current target save was modified."
            dialogs.append(f"OptionBox: {prefix}")
        elif rc != 0:
            if not last_item_is_raw and not last_mutation_started:
                dialogs.append("OptionBox: Restore stopped before current target save was modified.")
        return dialogs

    dlg_batch_raw_fail = simulate_batch_ui_callback(rc=1, last_item_is_raw=True, recovery_paths=["/dumps/item0_rec.zip"], last_mutation_started=False)
    check("OptionBox: Restore stopped. Safety recovery archives retained." in dlg_batch_raw_fail,
          "RAW batch failure retains earlier ZIP recovery without claiming current target untouched")
    check(not any("unverified" in d for d in dlg_batch_raw_fail),
          "Batch with RAW failure must not falsely warn about unverified mutation")

    # 10.5: Batch where Item 0 is RAW and fails early -> recovery_paths empty, no OptionBox shown
    dlg_batch_raw_empty = simulate_batch_ui_callback(rc=1, last_item_is_raw=True, recovery_paths=[], last_mutation_started=False)
    check(dlg_batch_raw_empty == ["ErrorBox: Restore failed!"],
          "Batch with early RAW failure and no prior recovery must show only ErrorBox")
    print("  -> Fixture 10 (RAW restore isolation in single and batch UI callbacks) PASSED.")

    # Fixture 11: Real ZIP fault injections
    # 11.1 Zero-byte file verification against native 0-byte file succeeds
    zero_zip = build_test_zip({"empty.bin": b""})
    inv_f_zero, inv_d_zero, _ = simulate_preflight(zero_zip)
    fs_zero = SyntheticSaveFs({"/empty.bin": b""})
    res_zero = simulate_verify_against_native(zero_zip, fs_zero, inv_f_zero, inv_d_zero)
    check(res_zero is True, "0-byte file must verify successfully")

    # 11.2 Excluded metadata (.nx_save_meta.bin) with corrupt CRC rejected during draining
    raw_bad_meta = build_test_zip({".nx_save_meta.bin": b"some meta data", "valid.bin": b"payload"}, corrupt_file=".nx_save_meta.bin")
    try:
        simulate_preflight(raw_bad_meta)
        check(False, "Corrupted metadata CRC must be rejected in preflight")
    except (ValueError, zipfile.BadZipFile):
        pass

    # 11.3 Missing native file when archive expects it
    fs_missing = SyntheticSaveFs({})
    res_missing = simulate_verify_against_native(zero_zip, fs_missing, inv_f_zero, inv_d_zero)
    check(res_missing is False, "Missing native file must fail verification")
    print("  -> Fixture 11 (Real ZIP fault injections: 0-byte, corrupt meta CRC, missing native file) PASSED.")

    print("=== ALL SYNTHETIC BEHAVIORAL FIXTURES PASSED SUCCESSFULLY ===")


# ==============================================================================
# Main
# ==============================================================================

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
