#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for
P2-B checked ZIP payload accounting and summary preflight.

NOTE ON SCOPE AND COVERAGE:
This test executes static source contracts and a Python behavioral model of the
ZIP preflight accounting, classification, integer overflow prevention, error
propagation, and summary publication semantics.

It validates:
- C++ source contract patterns restricted to the open-handle TransferUnzipPreflight
  body in threaded_file_transfer.cpp, header declarations in threaded_file_transfer.hpp,
  metadata constants in save_paths.hpp, filter/diagnostic wiring in save_menu_ops.cpp, and
  shared restore ownership across save_menu_ops.cpp and filebrowser_ops.cpp.
- Synthetic behavioral fixtures covering reachable preflight scenarios:
  regular/zero-byte files, explicit directory vs implicit parent distinction,
  actual NX/DBI metadata filtering and leading-slash normalization, corrupt excluded
  metadata rejection, character sanitization (SanitizeZipEntryName), empty destination
  rejection, exact INT64_MAX accounting, 1-byte aggregate overflow, aggregate overflow
  with null caller output, declared-size excess, drained-byte mismatch, read errors,
  CRC errors, non-CRC close errors, cancellation after prior local accumulation, and
  final rewind failure.
- Defensive arithmetic guards for counter overflow and u64 wrapping before addition,
  distinguished from reachable archive fixtures.

PATH VALIDATION SIMPLIFICATIONS:
The Python behavioral model simulates save-entry single-leading-slash normalization
and character sanitization (SanitizeZipEntryName). Full C++ path containment and HOS
filesystem validation (via IsSafeExtractionDestination, IsSafeDestinationPath,
NormalizeAbsoluteSdPath, mount prefix resolution, and libnx filesystem rules) are
tested by host-native tests (e.g. test_path_util.cpp). The Python model checks relative
safety and base-path prefix containment.

NON-EQUIVALENCE OF RESULT CODES:
Numeric result codes in this file are distinct synthetic diagnostic tokens used purely
for model verification. They do not claim numeric equivalence with libnx or C++ Result types.

It explicitly does NOT execute C++ binaries, libnx IPC calls, target Switch hardware,
or measure save data sizing/growth heuristics.
"""

import os
import sys

INT64_MAX = 9223372036854775807
UINT64_MAX = 18446744073709551615

# Distinct synthetic diagnostic result tokens for behavioral verification
RES_OK = 0
ERR_INVALID_SIZE = 1
ERR_INVALID_CHARACTER = 2
ERR_UNZ_OPEN2_64 = 3
ERR_UNZ_GET_GLOBAL_INFO64 = 4
ERR_UNZ_GO_TO_FIRST_FILE = 5
ERR_UNZ_GO_TO_NEXT_FILE = 6
ERR_UNZ_GET_CURRENT_FILE_INFO64 = 7
ERR_UNZ_OPEN_CURRENT_FILE = 8
ERR_UNZ_READ_CURRENT_FILE = 9
ERR_CRC_MISMATCH = 10
ERR_CANCELLED = 11

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ---------------------------------------------------------------------------
# Defensive arithmetic boundary helpers
# ---------------------------------------------------------------------------

def check_counter_overflow(current_count: int) -> bool:
    """
    Defensive arithmetic check mirroring C++:
    local_summary.file_count == std::numeric_limits<s64>::max()
    local_summary.directory_count == std::numeric_limits<s64>::max()
    Returns True if an increment would overflow s64 max.
    Unreachable in a valid archive due to entry_count <= INT64_MAX bound.
    """
    return current_count >= INT64_MAX

def check_aggregate_overflow(current_bytes: int, to_add: int) -> bool:
    """
    Checked aggregate addition mirroring C++:
    static_cast<u64>(std::numeric_limits<s64>::max()) - static_cast<u64>(local_summary.file_bytes) < info.uncompressed_size
    Returns True if addition would overflow s64 max.
    """
    if to_add < 0 or current_bytes < 0:
        return True
    return (INT64_MAX - current_bytes) < to_add

def check_drained_read_overflow(bytes_drained: int, read_len: int, declared_size: int) -> tuple[bool, str]:
    """
    Checked drained read accumulation mirroring C++:
    std::numeric_limits<u64>::max() - bytes_drained < read_u64 || bytes_drained + read_u64 > info.uncompressed_size
    Returns (overflowed, reason) where reason is 'u64_wrap' or 'declared_excess'.
    """
    if UINT64_MAX - bytes_drained < read_len:
        return True, "u64_wrap"
    if bytes_drained + read_len > declared_size:
        return True, "declared_excess"
    return False, "ok"


# ---------------------------------------------------------------------------
# Source contract checks
# ---------------------------------------------------------------------------

def test_source_contracts():
    repo_root = os.path.join(os.path.dirname(__file__), "..")

    # 1. threaded_file_transfer.hpp: UnzipPayloadSummary struct and TransferUnzipPreflight signatures
    tft_hpp = os.path.join(repo_root, "sphaira", "include", "threaded_file_transfer.hpp")
    with open(tft_hpp, "r", encoding="utf-8") as f:
        hpp_src = f.read()

    check("struct UnzipPayloadSummary {" in hpp_src,
          "threaded_file_transfer.hpp must define UnzipPayloadSummary struct")
    check("s64 file_bytes{0};" in hpp_src,
          "UnzipPayloadSummary must contain zero-initialized s64 file_bytes")
    check("s64 file_count{0};" in hpp_src,
          "UnzipPayloadSummary must contain zero-initialized s64 file_count")
    check("s64 directory_count{0};" in hpp_src,
          "UnzipPayloadSummary must contain zero-initialized s64 directory_count")
    check("Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false, UnzipPayloadSummary* output = nullptr);" in hpp_src,
          "threaded_file_transfer.hpp must declare zfile TransferUnzipPreflight with trailing optional UnzipPayloadSummary* output = nullptr")
    check("Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false, UnzipPayloadSummary* output = nullptr);" in hpp_src,
          "threaded_file_transfer.hpp must declare zip_out TransferUnzipPreflight with trailing optional UnzipPayloadSummary* output = nullptr")

    # 2. threaded_file_transfer.cpp: open-handle TransferUnzipPreflight body checks
    tft_cpp = os.path.join(repo_root, "sphaira", "source", "threaded_file_transfer.cpp")
    with open(tft_cpp, "r", encoding="utf-8") as f:
        cpp_src = f.read()

    start_idx = cpp_src.find("Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile,")
    end_idx = cpp_src.find("Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out,")
    check(start_idx != -1 and end_idx != -1 and start_idx < end_idx,
          "threaded_file_transfer.cpp must define zfile TransferUnzipPreflight overload before path overload")
    preflight_body = cpp_src[start_idx:end_idx]

    check("UnzipPayloadSummary local_summary{};" in preflight_body,
          "TransferUnzipPreflight body must accumulate into local_summary")
    check("info.uncompressed_size > static_cast<u64>(std::numeric_limits<s64>::max())" in preflight_body,
          "TransferUnzipPreflight body must validate per-entry uncompressed_size against s64 max")

    # Pipeline ordering: Resolve -> Sanitize -> Append -> Filter -> Keep -> Classify
    pos_resolve = preflight_body.find("ResolveArchiveEntryName(info, name_buf, save_dbi_compat, name)")
    pos_sanitize = preflight_body.find("SanitizeZipEntryName(name)", pos_resolve)
    pos_append = preflight_body.find("fs::AppendPath(base_path, name)", pos_sanitize)
    pos_filter = preflight_body.find("filter ? filter(name, path) : true", pos_append)
    pos_keep = preflight_body.find("if (keep) {", pos_filter)
    pos_path_len = preflight_body.find("path_len == 0", pos_keep)
    pos_is_safe = preflight_body.find("path::IsSafeExtractionDestination(path, base_path, save_dbi_compat)", pos_path_len)
    pos_classify_dir = preflight_body.find("path[path_len - 1] == '/'", pos_is_safe)
    pos_dir_inc = preflight_body.find("local_summary.directory_count++;", pos_classify_dir)
    pos_file_inc = preflight_body.find("local_summary.file_count++;", pos_dir_inc)
    pos_file_bytes = preflight_body.find("local_summary.file_bytes += static_cast<s64>(info.uncompressed_size);", pos_file_inc)
    pos_open = preflight_body.find("if (UNZ_OK != unzOpenCurrentFile(zfile))", pos_file_bytes)

    check(pos_resolve != -1 and pos_sanitize != -1 and pos_append != -1 and pos_filter != -1 and
          pos_keep != -1 and pos_path_len != -1 and pos_is_safe != -1 and pos_classify_dir != -1 and
          pos_dir_inc != -1 and pos_file_inc != -1 and pos_file_bytes != -1 and pos_open != -1,
          "TransferUnzipPreflight body must contain full Resolve -> Sanitize -> Append -> Filter -> Keep -> Classify pipeline")
    check(pos_resolve < pos_sanitize < pos_append < pos_filter < pos_keep < pos_path_len < pos_is_safe < pos_classify_dir < pos_file_bytes < pos_open,
          "Pipeline stages must occur in strict sequential order within the kept-entry block before unzOpenCurrentFile")

    # Counter overflow guards
    check("local_summary.directory_count == std::numeric_limits<s64>::max()" in preflight_body,
          "TransferUnzipPreflight body must guard directory_count against s64 max overflow")
    check("local_summary.file_count == std::numeric_limits<s64>::max()" in preflight_body,
          "TransferUnzipPreflight body must guard file_count against s64 max overflow")

    # Aggregate file_bytes overflow guard
    check("static_cast<u64>(std::numeric_limits<s64>::max()) - static_cast<u64>(local_summary.file_bytes) < info.uncompressed_size" in preflight_body,
          "TransferUnzipPreflight body must guard file_bytes aggregate addition against INT64_MAX overflow")

    # Complete u64 overflow check and handle closure in specific drained-overflow branch
    check("std::numeric_limits<u64>::max() - bytes_drained < read_u64 || bytes_drained + read_u64 > info.uncompressed_size" in preflight_body,
          "TransferUnzipPreflight body must check complete u64 overflow guard before addition")

    pos_drain_guard = preflight_body.find("std::numeric_limits<u64>::max() - bytes_drained < read_u64 || bytes_drained + read_u64 > info.uncompressed_size")
    pos_drain_close = preflight_body.find("unzCloseCurrentFile(zfile);", pos_drain_guard)
    pos_drain_throw = preflight_body.find("R_THROW(FsError_InvalidSize);", pos_drain_close)
    pos_drain_crc = preflight_body.find("crc32CalculateWithSeed", pos_drain_throw)
    check(pos_drain_guard != -1 and pos_drain_close != -1 and pos_drain_throw != -1 and pos_drain_crc != -1 and
          pos_drain_guard < pos_drain_close < pos_drain_throw < pos_drain_crc,
          "TransferUnzipPreflight body must close zfile handle and throw FsError_InvalidSize in the specific drained-overflow branch")

    # Summary assignment after final rewind failure-return block, guarded by output
    pos_rewind = preflight_body.find("Result_UnzGoToFirstFile")
    pos_output_guard = preflight_body.find("if (output) {", pos_rewind)
    pos_assign = preflight_body.find("*output = local_summary;", pos_output_guard)
    pos_succeed = preflight_body.find("R_SUCCEED();", pos_assign)
    check(pos_rewind != -1 and pos_output_guard != -1 and pos_assign != -1 and pos_succeed != -1 and
          pos_rewind < pos_output_guard < pos_assign < pos_succeed,
          "Summary publication (*output = local_summary) must occur strictly after final rewind failure block, guarded by 'if (output)', before R_SUCCEED")

    # Path overload forwards output
    path_overload_body = cpp_src[end_idx:]
    check("return TransferUnzipPreflight(pbox, zfile, base_path, filter, save_dbi_compat, output);" in path_overload_body,
          "TransferUnzipPreflight path overload must forward output pointer to zfile overload")

    # 3. save_paths.hpp: metadata constants
    save_paths_hpp = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(save_paths_hpp, "r", encoding="utf-8") as f:
        paths_src = f.read()

    check('constexpr const char* NX_SAVE_META_NAME = ".nx_save_meta.bin";' in paths_src,
          "save_paths.hpp must define NX_SAVE_META_NAME as .nx_save_meta.bin")
    check('inline constexpr const char* DBI_SAVE_INFO_NAME = ".dbi_save_info.ini";' in paths_src,
          "save_paths.hpp must define DBI_SAVE_INFO_NAME as .dbi_save_info.ini")
    check('inline constexpr const char* DBI_SAVE_EXTRA_NAME = ".dbi_save_extra";' in paths_src,
          "save_paths.hpp must define DBI_SAVE_EXTRA_NAME as .dbi_save_extra")

    # 4. save_menu_ops.cpp: RestoreSaveZip wiring, actual filter rules, and counts/bytes-only diagnostic
    save_ops = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(save_ops, "r", encoding="utf-8") as f:
        save_ops_src = f.read()

    check("name == NX_SAVE_META_NAME || !strcasecmp(name.s, DBI_SAVE_INFO_NAME) || !strcasecmp(name.s, DBI_SAVE_EXTRA_NAME)" in save_ops_src,
          "RestoreSaveZip save_filter must use exact match for NX_SAVE_META_NAME and case-insensitive strcasecmp for DBI names")
    check("thread::UnzipPayloadSummary summary{};" in save_ops_src,
          "RestoreSaveZip must instantiate UnzipPayloadSummary")
    check('TransferUnzipPreflight(pbox, zfile, "/", save_filter, true, &summary)' in save_ops_src,
          "RestoreSaveZip must pass &summary to TransferUnzipPreflight")
    check('log_write("save preflight payload: %lld bytes, %lld files, %lld dirs\\n",' in save_ops_src,
          "RestoreSaveZip must log counts/bytes-only diagnostic after preflight")

    # Diagnostic does not leak sensitive identifiers or file names
    preflight_diag_pos = save_ops_src.find('save preflight payload:')
    check(preflight_diag_pos != -1, "Diagnostic string must exist")
    diag_snippet = save_ops_src[preflight_diag_pos:preflight_diag_pos + 250]
    check("name" not in diag_snippet and "AccountUid" not in diag_snippet and "token" not in diag_snippet,
          "Preflight diagnostic must not log filenames, account IDs, or tokens")

    # 5. Shared ZIP restore owner across both UI routes
    fb_ops = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_ops.cpp")
    with open(fb_ops, "r", encoding="utf-8") as f:
        fb_ops_src = f.read()

    check("save::RestoreSaveZip(pbox, se, file_path" in fb_ops_src,
          "File Browser restore must delegate to save::RestoreSaveZip")
    check("return RestoreSaveZip(pbox, e, path" in save_ops_src,
          "Save Menu restore must delegate to RestoreSaveZip")
    print("Source contracts: ALL PASS (5 anchor groups)")


# ---------------------------------------------------------------------------
# Behavioral reference model
# ---------------------------------------------------------------------------

class BehavioralPreflightModel:
    """
    Python behavioral reference model reproducing checked accounting,
    normalization, character sanitization, directory classification,
    draining, overflow checks, rewind, and summary publication rules.
    """
    NX_SAVE_META_NAME = ".nx_save_meta.bin"
    DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"
    DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"

    @staticmethod
    def is_invalid_path_char(c: str) -> bool:
        uc = ord(c)
        if uc < 0x20:
            return True
        return c in (':', '*', '?', '"', '<', '>', '|', '\\')

    @classmethod
    def sanitize_zip_entry_name(cls, name: str) -> str:
        """
        Replaces HOS-invalid path characters with '_' per component, preserving '/',
        mirroring SanitizeZipEntryName in threaded_file_transfer.cpp.
        """
        res = []
        for c in name:
            res.append('_' if cls.is_invalid_path_char(c) else c)
        return "".join(res)

    @staticmethod
    def is_safe_archive_entry(path: str) -> bool:
        if not path or path.startswith('/'):
            return False
        for c in path:
            if ord(c) < 0x20 or ord(c) == 0x7F or c in ('\\', ':'):
                return False
        parts = path.split('/')
        for part in parts:
            if part in ('.', '..'):
                return False
        return True

    @classmethod
    def resolve_entry_name(cls, name: str, save_dbi_compat: bool) -> tuple[int, str]:
        if not name:
            return ERR_INVALID_CHARACTER, ""
        norm = name
        if save_dbi_compat and norm.startswith('/'):
            norm = norm[1:]
        if not cls.is_safe_archive_entry(norm):
            return ERR_INVALID_CHARACTER, ""
        return RES_OK, norm

    @staticmethod
    def append_path(base_path: str, name: str) -> str:
        if not base_path:
            return name
        if base_path.endswith('/'):
            return base_path + name
        return base_path + '/' + name

    @staticmethod
    def is_safe_extraction_destination(dest_path: str, base_path: str, save_dbi_compat: bool) -> bool:
        if not save_dbi_compat:
            return True
        if not dest_path:
            return False
        if not dest_path.startswith(base_path):
            return False
        if '//' in dest_path or '\\' in dest_path or ':' in dest_path:
            return False
        parts = dest_path.split('/')
        for part in parts:
            if part in ('.', '..'):
                return False
        return True

    @classmethod
    def save_filter(cls, name: str, _path: str) -> tuple[bool, str]:
        """
        Actual Save Menu restore filter:
        - Exact case-sensitive match for NX_SAVE_META_NAME (".nx_save_meta.bin")
        - Case-insensitive strcasecmp match for DBI_SAVE_INFO_NAME (".dbi_save_info.ini")
        - Case-insensitive strcasecmp match for DBI_SAVE_EXTRA_NAME (".dbi_save_extra")
        """
        if name == cls.NX_SAVE_META_NAME or \
           name.lower() == cls.DBI_SAVE_INFO_NAME.lower() or \
           name.lower() == cls.DBI_SAVE_EXTRA_NAME.lower():
            return False, _path
        return True, _path

    def execute_preflight(
        self,
        entries: list[dict],
        base_path: str = "/",
        filter_fn = None,
        save_dbi_compat: bool = True,
        output_summary: dict | None = None,
        cancel_at_step: int | None = None,
        rewind_fails: bool = False
    ) -> tuple[int, dict | None]:
        if not entries:
            return ERR_INVALID_SIZE, output_summary

        if len(entries) > INT64_MAX:
            return ERR_INVALID_SIZE, output_summary

        local_summary = {
            "file_bytes": 0,
            "file_count": 0,
            "directory_count": 0
        }

        step = 0
        for entry in entries:
            step += 1

            uncompressed_size = entry.get("uncompressed_size", 0)
            if uncompressed_size < 0 or uncompressed_size > INT64_MAX:
                return ERR_INVALID_SIZE, output_summary

            raw_name = entry.get("name", "")
            rc, name = self.resolve_entry_name(raw_name, save_dbi_compat)
            if rc != RES_OK:
                return rc, output_summary

            # Sanitizer pass
            name = self.sanitize_zip_entry_name(name)

            dest_path = self.append_path(base_path, name)

            keep = True
            if filter_fn:
                keep, dest_path = filter_fn(name, dest_path)

            if keep:
                path_len = len(dest_path)
                if path_len == 0:
                    return ERR_INVALID_CHARACTER, output_summary

                if not self.is_safe_extraction_destination(dest_path, base_path, save_dbi_compat):
                    return ERR_INVALID_CHARACTER, output_summary

                if dest_path.endswith('/'):
                    if check_counter_overflow(local_summary["directory_count"]):
                        return ERR_INVALID_SIZE, output_summary
                    local_summary["directory_count"] += 1
                else:
                    if check_counter_overflow(local_summary["file_count"]):
                        return ERR_INVALID_SIZE, output_summary
                    if check_aggregate_overflow(local_summary["file_bytes"], uncompressed_size):
                        return ERR_INVALID_SIZE, output_summary
                    local_summary["file_count"] += 1
                    local_summary["file_bytes"] += uncompressed_size

            # Entry open
            if entry.get("open_fails", False):
                return ERR_UNZ_OPEN_CURRENT_FILE, output_summary

            # Draining loop
            chunks = entry.get("chunks", [uncompressed_size])
            bytes_drained = 0
            for chunk in chunks:
                if cancel_at_step is not None and step == cancel_at_step:
                    return ERR_CANCELLED, output_summary

                if entry.get("cancel_during_read", False):
                    return ERR_CANCELLED, output_summary

                if entry.get("read_error", False):
                    return ERR_UNZ_READ_CURRENT_FILE, output_summary

                if chunk > 0:
                    overflowed, _ = check_drained_read_overflow(bytes_drained, chunk, uncompressed_size)
                    if overflowed:
                        # unzCloseCurrentFile called before throw
                        return ERR_INVALID_SIZE, output_summary
                    bytes_drained += chunk

            # Entry close
            if entry.get("close_crc_error", False):
                return ERR_CRC_MISMATCH, output_summary

            if entry.get("close_error", False):
                return ERR_UNZ_READ_CURRENT_FILE, output_summary

            if bytes_drained != uncompressed_size:
                return ERR_INVALID_SIZE, output_summary

            if entry.get("crc_mismatch", False):
                return ERR_CRC_MISMATCH, output_summary

        if rewind_fails:
            return ERR_UNZ_GO_TO_FIRST_FILE, output_summary

        # Published only on complete success
        if output_summary is not None:
            output_summary["file_bytes"] = local_summary["file_bytes"]
            output_summary["file_count"] = local_summary["file_count"]
            output_summary["directory_count"] = local_summary["directory_count"]

        return RES_OK, output_summary


# ---------------------------------------------------------------------------
# Behavioral test suite
# ---------------------------------------------------------------------------

def test_behavioral_fixtures():
    model = BehavioralPreflightModel()

    def assert_sentinel_untouched(out: dict, label: str):
        check(out["file_bytes"] == -1 and out["file_count"] == -1 and out["directory_count"] == -1,
              f"{label}: all three sentinel fields must remain untouched")

    # Fixture 1: Regular files and zero-byte files
    entries_1 = [
        {"name": "slot.dat", "uncompressed_size": 2048, "chunks": [1024, 1024]},
        {"name": "marker.empty", "uncompressed_size": 0, "chunks": []},
        {"name": "save.bin", "uncompressed_size": 512, "chunks": [512]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_1, output_summary=out)
    check(rc == RES_OK, "Regular files and zero-byte files must succeed")
    check(res["file_bytes"] == 2560, "file_bytes must sum only regular files")
    check(res["file_count"] == 3, "file_count must include zero-byte files")
    check(res["directory_count"] == 0, "directory_count must be 0")

    # Fixture 2: Explicit directory vs implicit parent distinction
    entries_2 = [
        {"name": "dir1/", "uncompressed_size": 0, "chunks": []},
        {"name": "dir1/subdir/", "uncompressed_size": 0, "chunks": []},
        {"name": "dir1/subdir/file.txt", "uncompressed_size": 100, "chunks": [100]},
        {"name": "unseen_parent/leaf.bin", "uncompressed_size": 50, "chunks": [50]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_2, output_summary=out)
    check(rc == RES_OK, "Explicit directories must succeed")
    check(res["file_bytes"] == 150, "Explicit directories must not contribute to file_bytes")
    check(res["file_count"] == 2, "Only actual file entries counted in file_count")
    check(res["directory_count"] == 2, "Only explicit directory entries counted in directory_count")

    # Fixture 3: Character sanitization (SanitizeZipEntryName)
    entries_3 = [
        {"name": "data*01?.sav", "uncompressed_size": 256, "chunks": [256]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_3, output_summary=out)
    check(rc == RES_OK, "Sanitized filename must succeed preflight")
    check(res["file_bytes"] == 256 and res["file_count"] == 1, "Sanitized entry counted in summary")

    # Fixture 4: Actual NX/DBI metadata filtering and leading-slash normalization
    # - NX_SAVE_META_NAME (.nx_save_meta.bin): exact case match
    # - Leading slash /.nx_save_meta.bin: normalized and skipped
    # - Uppercase .NX_SAVE_META.BIN: case mismatch -> NOT skipped, counted
    # - DBI info/extra: case-insensitive match -> skipped with mixed case and leading slash
    entries_4 = [
        {"name": ".nx_save_meta.bin", "uncompressed_size": 512, "chunks": [512]},
        {"name": "/.nx_save_meta.bin", "uncompressed_size": 512, "chunks": [512]},
        {"name": "/.dbi_save_info.ini", "uncompressed_size": 256, "chunks": [256]},
        {"name": "/.DBI_SAVE_INFO.INI", "uncompressed_size": 256, "chunks": [256]},
        {"name": "/.dbi_save_extra", "uncompressed_size": 128, "chunks": [128]},
        {"name": "/.DBI_save_EXTRA", "uncompressed_size": 128, "chunks": [128]},
        {"name": ".NX_SAVE_META.BIN", "uncompressed_size": 77, "chunks": [77]},  # Case mismatch -> kept!
        {"name": "/user_data.sav", "uncompressed_size": 1000, "chunks": [1000]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_4, filter_fn=model.save_filter, save_dbi_compat=True, output_summary=out)
    check(rc == RES_OK, "Actual metadata names with leading-slash normalization must pass preflight")
    check(res["file_bytes"] == 1077, "Skipped metadata excluded; uppercase NX meta kept as payload (1000 + 77)")
    check(res["file_count"] == 2, "Only user_data.sav and uppercase .NX_SAVE_META.BIN counted in file_count")
    check(res["directory_count"] == 0, "No directories in this set")

    # Fixture 5: Corrupt skipped metadata rejects preflight and retains entire sentinel
    entries_5_crc = [
        {"name": "/.nx_save_meta.bin", "uncompressed_size": 512, "chunks": [512], "crc_mismatch": True},
        {"name": "valid.sav", "uncompressed_size": 100, "chunks": [100]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_5_crc, filter_fn=model.save_filter, save_dbi_compat=True, output_summary=out)
    check(rc == ERR_CRC_MISMATCH, "Corrupt skipped metadata CRC mismatch must fail preflight")
    assert_sentinel_untouched(out, "Corrupt skipped metadata CRC mismatch")

    entries_5_read = [
        {"name": "/.dbi_save_info.ini", "uncompressed_size": 256, "chunks": [100], "read_error": True},
        {"name": "valid.sav", "uncompressed_size": 100, "chunks": [100]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_5_read, filter_fn=model.save_filter, save_dbi_compat=True, output_summary=out)
    check(rc == ERR_UNZ_READ_CURRENT_FILE, "Corrupt skipped metadata read error must fail preflight")
    assert_sentinel_untouched(out, "Corrupt skipped metadata read error")

    # Fixture 6: Filter remapping and destination trailing slash classification
    def remapping_filter(name: str, path: str) -> tuple[bool, str]:
        if name == "excluded.bin":
            return False, path
        if name == "dir_target":
            return True, path + "/"
        return True, path

    entries_6 = [
        {"name": "excluded.bin", "uncompressed_size": 9999, "chunks": [9999]},
        {"name": "dir_target", "uncompressed_size": 0, "chunks": []},
        {"name": "normal.dat", "uncompressed_size": 300, "chunks": [300]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_6, filter_fn=remapping_filter, output_summary=out)
    check(rc == RES_OK, "Remapping filter must succeed")
    check(res["file_bytes"] == 300, "Excluded bytes ignored, remapped dir contributes 0 file_bytes")
    check(res["file_count"] == 1, "Only normal.dat is a regular file")
    check(res["directory_count"] == 1, "dir_target classified as directory by trailing slash")

    # Fixture 7: Empty mapped destination rejection
    def empty_dest_filter(name: str, path: str) -> tuple[bool, str]:
        return True, ""

    entries_7 = [{"name": "file.dat", "uncompressed_size": 10, "chunks": [10]}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_7, filter_fn=empty_dest_filter, output_summary=out)
    check(rc == ERR_INVALID_CHARACTER, "Empty mapped destination must fail with ERR_INVALID_CHARACTER")
    assert_sentinel_untouched(out, "Empty mapped destination")

    # Fixture 8: Exact INT64_MAX aggregate size vs 1-byte overflow
    entries_exact = [
        {"name": "big1.dat", "uncompressed_size": INT64_MAX - 100, "chunks": [INT64_MAX - 100]},
        {"name": "big2.dat", "uncompressed_size": 100, "chunks": [100]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, res = model.execute_preflight(entries_exact, output_summary=out)
    check(rc == RES_OK, "Exact INT64_MAX aggregate size must succeed")
    check(res["file_bytes"] == INT64_MAX, "file_bytes must reach exactly INT64_MAX")

    entries_overflow_1b = [
        {"name": "big1.dat", "uncompressed_size": INT64_MAX - 100, "chunks": [INT64_MAX - 100]},
        {"name": "big2.dat", "uncompressed_size": 101, "chunks": [101]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_overflow_1b, output_summary=out)
    check(rc == ERR_INVALID_SIZE, "Aggregate size exceeding INT64_MAX by 1 byte must fail with ERR_INVALID_SIZE")
    assert_sentinel_untouched(out, "1-byte aggregate overflow")

    # Fixture 9: Aggregate overflow enforced with output_summary=None
    rc, res = model.execute_preflight(entries_overflow_1b, output_summary=None)
    check(rc == ERR_INVALID_SIZE and res is None,
          "Aggregate overflow must be enforced even when output_summary is None")

    # Fixture 10: Declared size excess during read
    entries_drain_excess = [
        {"name": "bad.dat", "uncompressed_size": 10, "chunks": [6, 6]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_drain_excess, output_summary=out)
    check(rc == ERR_INVALID_SIZE, "Drained bytes exceeding declared size during read must fail with ERR_INVALID_SIZE")
    assert_sentinel_untouched(out, "Drained size excess during read")

    # Fixture 11: Drained size mismatch at close
    entries_drain_under = [
        {"name": "bad.dat", "uncompressed_size": 10, "chunks": [9]},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_drain_under, output_summary=out)
    check(rc == ERR_INVALID_SIZE, "Drained size fewer than declared size must fail with ERR_INVALID_SIZE")
    assert_sentinel_untouched(out, "Drained size underflow at close")

    # Fixture 12: Read, CRC, non-CRC close, and close CRC errors
    entries_read_err = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [5], "read_error": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_read_err, output_summary=out)
    check(rc == ERR_UNZ_READ_CURRENT_FILE, "Read error must fail with ERR_UNZ_READ_CURRENT_FILE")
    assert_sentinel_untouched(out, "Read error")

    entries_crc_err = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [10], "crc_mismatch": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_crc_err, output_summary=out)
    check(rc == ERR_CRC_MISMATCH, "CRC mismatch must fail with ERR_CRC_MISMATCH")
    assert_sentinel_untouched(out, "CRC mismatch")

    entries_close_err = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [10], "close_error": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_close_err, output_summary=out)
    check(rc == ERR_UNZ_READ_CURRENT_FILE, "Non-CRC close error must fail with ERR_UNZ_READ_CURRENT_FILE")
    assert_sentinel_untouched(out, "Non-CRC close error")

    entries_close_crc = [{"name": "err.dat", "uncompressed_size": 10, "chunks": [10], "close_crc_error": True}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_close_crc, output_summary=out)
    check(rc == ERR_CRC_MISMATCH, "Close CRC error must fail with ERR_CRC_MISMATCH")
    assert_sentinel_untouched(out, "Close CRC error")

    # Fixture 13: Cancellation during drain after prior entry accumulated locally
    entries_cancel = [
        {"name": "first_ok.dat", "uncompressed_size": 500, "chunks": [500]},
        {"name": "second_cancel.dat", "uncompressed_size": 200, "chunks": [100], "cancel_during_read": True},
    ]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_cancel, output_summary=out)
    check(rc == ERR_CANCELLED, "Cancellation during drain must fail with ERR_CANCELLED")
    assert_sentinel_untouched(out, "Cancellation after prior local accumulation")

    # Fixture 14: Final rewind failure
    entries_rewind = [{"name": "ok.dat", "uncompressed_size": 10, "chunks": [10]}]
    out = {"file_bytes": -1, "file_count": -1, "directory_count": -1}
    rc, _ = model.execute_preflight(entries_rewind, rewind_fails=True, output_summary=out)
    check(rc == ERR_UNZ_GO_TO_FIRST_FILE, "Rewind failure must fail with ERR_UNZ_GO_TO_FIRST_FILE")
    assert_sentinel_untouched(out, "Final rewind failure")

    # Fixture 15: Successful run without output summary requested
    rc, res = model.execute_preflight(entries_1, output_summary=None)
    check(rc == RES_OK and res is None, "Preflight with output_summary=None must succeed and return None")

    print("Reachable archive behavioral fixtures: ALL PASS (15 fixture groups)")


def test_defensive_arithmetic():
    # 1. Counter overflow
    check(not check_counter_overflow(0), "Counter 0 must not overflow")
    check(not check_counter_overflow(INT64_MAX - 1), "Counter INT64_MAX - 1 must not overflow")
    check(check_counter_overflow(INT64_MAX), "Counter INT64_MAX must report overflow")

    # 2. Aggregate overflow
    check(not check_aggregate_overflow(INT64_MAX - 100, 100), "Exact INT64_MAX addition must not overflow")
    check(check_aggregate_overflow(INT64_MAX - 100, 101), "INT64_MAX + 1 addition must report overflow")
    check(check_aggregate_overflow(10, -1), "Negative addition must report invalid")

    # 3. Drained read overflow and u64 wrap
    ov, reason = check_drained_read_overflow(UINT64_MAX - 10, 11, UINT64_MAX)
    check(ov and reason == "u64_wrap", "Drained read wrapping u64 must trigger defensive u64_wrap guard")

    ov, reason = check_drained_read_overflow(50, 10, 55)
    check(ov and reason == "declared_excess", "Drained read exceeding declared size must trigger declared_excess")

    ov, reason = check_drained_read_overflow(50, 5, 55)
    check(not ov and reason == "ok", "Valid drained chunk within bounds must be ok")

    print("Defensive arithmetic guards: ALL PASS (3 boundary checks)")


if __name__ == "__main__":
    test_source_contracts()
    test_behavioral_fixtures()
    test_defensive_arithmetic()
    print("ALL SAVE PAYLOAD SUMMARY CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")
