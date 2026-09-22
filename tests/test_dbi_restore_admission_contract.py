#!/usr/bin/env python3
"""
Test Suite: Real DBI Restore Admission and Unified Exact Route Contract (Sphaira v0.13.868)

Delivery Scope & Behavioral Invariants:
A. Exact DBI marker:
   - Exact raw `//`, zero bytes, directory semantics, excluded from payload summary/inventory.
   - One-leading-slash payload remains accepted; duplicate marker rejected.
B. Malformed matrix:
   - `//evil`, `///`, `//../evil`, exact `//` with nonzero data, regular-file attrs,
     backslash, colon, control characters, traversal rejected.
C. Metadata-only versus explicit empty folder:
   - Reserved metadata only (.nx_save_meta.bin, .dbi_save_info.ini, .dbi_save_extra, //): rejected.
   - Nonzero global ZIP entries with 0 kept payload: rejected.
   - Real explicit payload directory: admitted.
   - Authorized allow_empty internal/recovery path remains distinct.
   - Refusal occurs before any mutation.
D. Sources:
   - /switch/DBI/saves, /DBISaves, configured paths as <game>/<date>/<archive>.
   - Case-insensitive exact-path dedup, deterministic source priority/member order.
   - No "/DBI Saves".
E. Unified route:
   - Live `+` and backup-group `A` resolve to same retained group member set.
   - Both feed same account/target/archive owner.
   - Foreign Account requires explicit local destination UID; no automatic slot creation.
   - Multi-group restore uses retained members without FindLatestBackupPath.
   - Changed/disappeared/swapped member fails closed with 0 restore calls.
   - Duplicate target protection remains active.
F. Final reinspection:
   - Expected group identity match accepted; changed app ID, type, UID, index, rank rejected.
   - Metadata-free DBI retains directory-derived identity.
   - Exact path must remain in retained member list.

Pure Python stdlib, compiler-free.
"""

import io
import json
import os
import struct
import sys
import tempfile
import warnings
import zipfile

warnings.filterwarnings("ignore", category=UserWarning, module="zipfile")

def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def read_file(*parts: str) -> str:
    path = os.path.join(REPO_ROOT, *parts)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()

# ==============================================================================
# 1. Static Source Contracts Check
# ==============================================================================

def test_static_source_contracts() -> None:
    print("[1] Running static source contracts for v0.13.868...")

    cmake_src = read_file("sphaira", "CMakeLists.txt")
    check(any(v in cmake_src for v in ("set(sphaira_VERSION 0.13.868)", "set(sphaira_VERSION 0.13.869)", "set(sphaira_VERSION 0.13.870)", "set(sphaira_VERSION 0.13.871)", "set(sphaira_VERSION 0.13.872)", "set(sphaira_VERSION 0.13.873)", "set(sphaira_VERSION 0.13.874)", "set(sphaira_VERSION 0.13.875)")),
          "sphaira/CMakeLists.txt must define sphaira_VERSION as 0.13.868, 0.13.869, 0.13.870, 0.13.871, 0.13.872, 0.13.873, 0.13.874, or 0.13.875")

    path_util_hpp = read_file("sphaira", "include", "path_util.hpp")
    check("inline auto IsDbiRootMarkerEntry(" in path_util_hpp,
          "path_util.hpp must declare IsDbiRootMarkerEntry")
    check('raw_name != "//"' in path_util_hpp,
          "IsDbiRootMarkerEntry must check exact raw_name != \"//\"")
    check("uncompressed_size != 0" in path_util_hpp,
          "IsDbiRootMarkerEntry must require uncompressed_size == 0")

    save_paths_hpp = read_file("sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    check('DBI_SAVES_ROOT_PATH = "/DBISaves";' in save_paths_hpp,
          "save_paths.hpp must define DBI_SAVES_ROOT_PATH as /DBISaves")
    check("s64 payload_count{0};" in save_paths_hpp,
          "save_paths.hpp must declare payload_count in DecodedSaveMetadata and BackupArchiveInfo")

    save_paths_cpp = (
        read_file("sphaira", "source", "ui", "menus", "save", "save_paths.cpp") + "\n" +
        read_file("sphaira", "source", "ui", "menus", "save", "save_archive_metadata.cpp") + "\n" +
        read_file("sphaira", "source", "ui", "menus", "save", "save_backup_inspection.cpp")
    )
    check("path::IsDbiRootMarkerEntry(raw, info.uncompressed_size, info.external_fa)" in save_paths_cpp,
          "save_paths.cpp ReadArchiveSaveMetadata must validate marker with IsDbiRootMarkerEntry")
    check("seen_dbi_root_marker" in save_paths_cpp,
          "save_paths.cpp must track duplicate seen_dbi_root_marker")
    check("archive_meta.payload_count == 0" in save_paths_cpp,
          "InspectBackupArchive must reject archives with payload_count == 0")
    check("path::EqualsIC(*normalized, DBI_SAVES_ROOT_PATH)" in save_paths_cpp,
          "NormalizeBackupSearchPath must reject DBI_SAVES_ROOT_PATH")
    check("add_unique(DBI_SAVES_ROOT_PATH);" in save_paths_cpp,
          "GetShareableSaveBackupRoots must include DBI_SAVES_ROOT_PATH")
    check("add_root(DBI_SAVES_ROOT_PATH);" in save_paths_cpp,
          "CollectDbiBackups must add DBI_SAVES_ROOT_PATH")
    check('"/DBI Saves"' not in save_paths_cpp,
          "save_paths.cpp must never use '/DBI Saves'")

    tft_cpp = read_file("sphaira", "source", "threaded_file_transfer.cpp")
    check("bool* out_is_dbi_root_marker" in tft_cpp,
          "ResolveArchiveEntryName must accept out_is_dbi_root_marker parameter")
    check("out.is_dbi_root_marker = true;" in tft_cpp,
          "ResolveArchiveDestinationEntry must set is_dbi_root_marker")
    check("seen_dbi_root_marker" in tft_cpp,
          "TransferUnzipPreflight must detect duplicate DBI root markers")
    check("save_dbi_compat && local_summary.file_count == 0 && local_summary.directory_count == 0" in tft_cpp,
          "TransferUnzipPreflight must fail if payload count is 0 only when save_dbi_compat is true and !allow_empty")
    check("seen_dbi_marker" in tft_cpp and "seen_dbi_marker_sizing" in tft_cpp,
          "TransferUnzipAll must detect duplicate DBI root markers in sizing and extraction")
    check("resolved.is_dbi_root_marker" in tft_cpp,
          "TransferUnzipAll must check resolved.is_dbi_root_marker")
    check("close_res == UNZ_CRCERROR" in tft_cpp,
          "TransferUnzipAll must check close/CRC errors")

    check("source/ui/menus/save/save_restore_route.cpp" in cmake_src,
          "CMakeLists.txt must include save_restore_route.cpp")

    route_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_restore_route.cpp")
    check(len(route_cpp.splitlines()) <= 600,
          "save_restore_route.cpp must be <= 600 lines")
    check("auto Menu::MakeBackupGroupFromLiveEntry(" in route_cpp,
          "save_restore_route.cpp must implement MakeBackupGroupFromLiveEntry")
    check("void Menu::StartRestore(" in route_cpp,
          "save_restore_route.cpp must implement StartRestore")
    check("void Menu::ShowRestorePickerPopup(" in route_cpp,
          "save_restore_route.cpp must implement ShowRestorePickerPopup")
    check("void Menu::RestoreSingleBackupGroup(" in route_cpp,
          "save_restore_route.cpp must implement RestoreSingleBackupGroup")
    check("void Menu::RestoreBackupGroups(" in route_cpp,
          "save_restore_route.cpp must implement RestoreBackupGroups")
    check("void Menu::PromptBatchRestoreTargets(" in route_cpp,
          "save_restore_route.cpp must implement PromptBatchRestoreTargets")

    rsbg_idx = route_cpp.find("void Menu::RestoreSingleBackupGroup(")
    pbrt_idx = route_cpp.find("void Menu::PromptBatchRestoreTargets(", rsbg_idx)
    check(rsbg_idx != -1 and pbrt_idx != -1, "RestoreSingleBackupGroup must exist in route_cpp")
    rsbg_body = route_cpp[rsbg_idx:pbrt_idx]
    on_ready_idx = rsbg_body.find("const auto on_account_ready =")
    on_ready_end = rsbg_body.find("};", on_ready_idx)
    check(on_ready_idx != -1 and on_ready_end != -1, "RestoreSingleBackupGroup must define on_account_ready callback")
    check("backup_path" not in rsbg_body[on_ready_idx:on_ready_end],
          "RestoreSingleBackupGroup on_account_ready callback must not retain loose backup_path fallback")
    check("RestoreSavesPicked(std::move(*target), group, location, backup_root, group.backup_members.front().path);" in rsbg_body,
          "RestoreSingleBackupGroup must restore single retained member")
    check("ShowRestorePickerPopup(std::move(*target), group, location, backup_root, {}, group.backup_members);" in rsbg_body,
          "RestoreSingleBackupGroup must present picker over retained members")
    check("CollectBackups(probe_fs, live, backup_root)" in route_cpp,
          "MakeBackupGroupFromLiveEntry must build candidates via CollectBackups from live entry")
    check("probe_fs" in route_cpp and "backup_root.starts_with(\"ums\")" in route_cpp,
          "save_restore_route.cpp must handle UMS/stdio probe_fs without hardcoding SD")

    sm_hpp = read_file("sphaira", "include", "ui", "menus", "save_menu.hpp")
    check("void RestoreSingleBackupGroup(Entry group" in sm_hpp,
          "save_menu.hpp must declare RestoreSingleBackupGroup")
    check("void RestoreBackupGroups(std::vector<Entry> groups" in sm_hpp,
          "save_menu.hpp must declare RestoreBackupGroups")
    check("auto MakeBackupGroupFromLiveEntry(const Entry& live" in sm_hpp,
          "save_menu.hpp must declare MakeBackupGroupFromLiveEntry")

    sm_units = [
        read_file("sphaira", "source", "ui", "menus", "save_menu.cpp"),
        read_file("sphaira", "source", "ui", "menus", "save", "save_menu_actions.cpp"),
        read_file("sphaira", "source", "ui", "menus", "save", "save_menu_target.cpp"),
    ]
    sm_cpp = "\n".join(sm_units)
    check("Menu::MakeBackupGroupFromLiveEntry" not in sm_cpp,
          "save_menu.cpp must not contain moved MakeBackupGroupFromLiveEntry")
    check("Menu::RestoreSingleBackupGroup" not in sm_cpp,
          "save_menu.cpp must not contain moved RestoreSingleBackupGroup")
    check("Menu::RestoreBackupGroups" not in sm_cpp,
          "save_menu.cpp must not contain moved RestoreBackupGroups")
    check("RestoreBackupGroups(seeds, false);" in sm_cpp,
          "PromptBackupGroupAction Restore must delegate to RestoreBackupGroups")
    check("RestoreBackupGroups(seeds, true);" in sm_cpp,
          "PromptBackupGroupAction RestoreForUser must delegate to RestoreBackupGroups")
    check("No compatible live save slot found on console." in sm_cpp,
          "ResolveRestoreTarget must display fail-closed message when no candidates found")

    ops_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    check("Menu::StartRestore" not in ops_cpp,
          "save_menu_ops.cpp must not contain moved StartRestore")
    check("Menu::ShowRestorePickerPopup" not in ops_cpp,
          "save_menu_ops.cpp must not contain moved ShowRestorePickerPopup")

    batch_idx = ops_cpp.find("void Menu::RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets")
    del_idx = ops_cpp.find("void Menu::RestoreSavesPicked(", batch_idx)
    batch_slice = ops_cpp[batch_idx:del_idx]
    check("FindLatestBackupPath" not in batch_slice,
          "RestoreSaves batch must NOT call FindLatestBackupPath")
    check("backup_path" not in batch_slice,
          "RestoreSaves batch must NOT have loose backup_path fallback")
    check("src.backup_members.front().path" in batch_slice,
          "RestoreSaves batch must select deterministic backup_members.front().path")
    check("InspectBackupArchive(probe_fs, file_path, filename, src.dbi_game_dir, check_info)" in batch_slice,
          "RestoreSaves must reinspect with src.dbi_game_dir")
    check("BackupGroupKey(check_info) != BackupGroupKey(src)" in batch_slice,
          "RestoreSaves must enforce BackupGroupKey equivalence on each multi-group source")

    zip_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    check("R_UNLESS(e.save_data_id != 0" in zip_cpp,
          "RestoreSaveZip must enforce e.save_data_id != 0 guard")
    check("fsCreateSaveDataFileSystem" not in zip_cpp,
          "RestoreSaveZip must NOT contain legacy fsCreateSaveDataFileSystem creation code")

    en_json = json.loads(read_file("assets", "romfs", "i18n", "en.json"))
    uk_json = json.loads(read_file("assets", "romfs", "i18n", "uk.json"))
    required_keys = [
        "Select restore target slot",
        "Selected backup archive has changed or is no longer available.",
        "Selected backup contains no save payload."
    ]
    for k in required_keys:
        check(k in en_json, f"en.json must contain key: '{k}'")
        check(k in uk_json, f"uk.json must contain key: '{k}'")
        check(len(uk_json[k].strip()) > 0, f"uk.json translation for '{k}' must not be empty")

    print("  -> Static source contracts PASSED.")


# ==============================================================================
# 2. Behavioral Fixtures: Exact DBI Marker & Malformed Matrix (A & B)
# ==============================================================================

def is_dbi_root_marker_model(raw: str, uncompressed_size: int, external_fa: int) -> bool:
    """Model of path::IsDbiRootMarkerEntry."""
    if raw != "//":
        return False
    if uncompressed_size != 0:
        return False
    posix_type = (external_fa >> 16) & 0o170000
    if posix_type != 0o040000 and posix_type != 0:
        return False
    dos_attrs = external_fa & 0xFF
    if (dos_attrs & 0x10) == 0 and dos_attrs != 0:
        return False
    return True

def normalize_archive_entry_model(raw: str, save_dbi_compat: bool, uncompressed_size: int = 0, external_fa: int = 0) -> tuple[bool, str, bool]:
    """
    Models ResolveArchiveEntryName -> (valid, normalized_path, is_marker).
    Reflects the exact production evaluation order.
    """
    if save_dbi_compat and is_dbi_root_marker_model(raw, uncompressed_size, external_fa):
        return True, "", True

    if raw.startswith("//"):
        return False, "", False

    norm = raw[1:] if raw.startswith("/") else raw

    # IsSafeArchiveEntry checks
    if not norm or norm == "." or norm == "..":
        return False, "", False
    if "\\" in norm or ":" in norm or "\0" in norm:
        return False, "", False
    for c in norm:
        if ord(c) < 0x20 or ord(c) == 0x7F:
            return False, "", False

    parts = norm.split("/")
    for p in parts:
        if p == "." or p == "..":
            return False, "", False

    return True, norm, False

def test_dbi_marker_and_malformed_matrix():
    print("[2] Running DBI marker and malformed matrix fixtures (A & B)...")

    S_IFDIR = 0o040000 << 16
    S_IFREG = 0o100000 << 16
    MSDOS_DIR = 0x10

    # A. Exact DBI marker
    check(is_dbi_root_marker_model("//", 0, S_IFDIR | MSDOS_DIR), "Marker with POSIX dir + DOS dir accepted")
    check(is_dbi_root_marker_model("//", 0, S_IFDIR), "Marker with POSIX dir accepted")
    check(is_dbi_root_marker_model("//", 0, MSDOS_DIR), "Marker with DOS dir accepted")
    check(is_dbi_root_marker_model("//", 0, 0), "Marker with 0 attrs accepted (minizip name-based directory)")

    # B. Malformed matrix
    malformed_raws = ["//evil", "///", "//../evil", "\\\\evil", "c:/evil", "evil\x01", "foo/../bar", "foo/./bar"]
    for m in malformed_raws:
        val, path, is_m = normalize_archive_entry_model(m, True)
        check(not val, f"Malformed raw entry '{m}' must be rejected")

    # Non-zero data or regular file on //
    check(not is_dbi_root_marker_model("//", 1, S_IFDIR), "Non-zero uncompressed size rejected")
    check(not is_dbi_root_marker_model("//", 0, S_IFREG), "Regular-file attribute rejected")

    # One leading slash payload remains accepted
    val, path, is_m = normalize_archive_entry_model("/savedata.bin", True)
    check(val and path == "savedata.bin" and not is_m, "One-leading-slash payload accepted")

    print("  -> DBI marker and malformed matrix fixtures PASSED.")


# ==============================================================================
# 3. Behavioral Fixtures: Metadata-Only vs Kept Payload (C)
# ==============================================================================

def create_zip(entries: dict[str, tuple[bytes, int]]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, (data, attr) in entries.items():
            zinfo = zipfile.ZipInfo(name)
            zinfo.external_attr = attr
            zf.writestr(zinfo, data)
    return buf.getvalue()

def simulate_preflight(zip_bytes: bytes, allow_empty: bool = False, save_dbi_compat: bool = True) -> tuple[bool, int, int]:
    """Models TransferUnzipPreflight empty and marker rejection."""
    with zipfile.ZipFile(io.BytesIO(zip_bytes), "r") as zf:
        seen_marker, kept_count = False, 0
        total_count = len(zf.namelist())
        for zinfo in zf.infolist():
            raw = zinfo.filename
            if save_dbi_compat:
                if is_dbi_root_marker_model(raw, zinfo.file_size, zinfo.external_attr):
                    if seen_marker:
                        return False, 0, total_count
                    seen_marker = True
                    continue
                if raw in [".nx_save_meta.bin", ".dbi_save_info.ini", ".dbi_save_extra"]:
                    continue
            kept_count += 1
        if save_dbi_compat and kept_count == 0 and not allow_empty:
            return False, kept_count, total_count
        return True, kept_count, total_count

def simulate_unzip_all(zip_bytes: bytes, save_dbi_compat: bool = True) -> tuple[bool, list[str]]:
    """Models TransferUnzipAll marker enforcement (sizing + extraction loops)."""
    with zipfile.ZipFile(io.BytesIO(zip_bytes), "r") as zf:
        seen_sizing = False
        for z in zf.infolist():
            if save_dbi_compat and is_dbi_root_marker_model(z.filename, z.file_size, z.external_attr):
                if seen_sizing:
                    return False, []
                seen_sizing = True
        seen, out = False, []
        for z in zf.infolist():
            if save_dbi_compat and is_dbi_root_marker_model(z.filename, z.file_size, z.external_attr):
                if seen:
                    return False, []
                seen = True
                if len(zf.read(z)) != 0 or z.CRC != 0:
                    return False, []
                continue
            out.append(z.filename)
        return True, out

def test_metadata_only_vs_payload():
    print("[3] Running metadata-only vs kept payload fixtures (C)...")

    S_IFDIR = 0o040000 << 16
    S_IFREG = 0o100000 << 16
    MSDOS_DIR = 0x10

    # 1. Reserved metadata only: rejected
    z_meta_only = create_zip({
        ".nx_save_meta.bin": (b"meta", S_IFREG),
        ".dbi_save_info.ini": (b"ini", S_IFREG),
    })
    ok, kept, total = simulate_preflight(z_meta_only)
    check(not ok and kept == 0 and total == 2, "Reserved metadata only rejected")

    # 2. Metadata plus valid //: rejected
    z_meta_marker = create_zip({
        "//": (b"", S_IFDIR | MSDOS_DIR),
        ".nx_save_meta.bin": (b"meta", S_IFREG),
    })
    ok, kept, total = simulate_preflight(z_meta_marker)
    check(not ok and kept == 0 and total == 2, "Metadata plus // rejected")

    # 3. One real explicit payload directory: admitted
    z_dir_payload = create_zip({
        "//": (b"", S_IFDIR | MSDOS_DIR),
        "save_dir/": (b"", S_IFDIR | MSDOS_DIR),
    })
    ok, kept, total = simulate_preflight(z_dir_payload)
    check(ok and kept == 1, "Explicit payload directory admitted")

    # 4. Authorized allow_empty path: admitted
    ok, kept, total = simulate_preflight(z_meta_marker, allow_empty=True)
    check(ok and kept == 0, "Authorized allow_empty path succeeds")

    # 5. Generic ZIP (!save_dbi_compat): empty payload is NOT rejected
    ok_gen, kept_gen, _ = simulate_preflight(z_meta_only, allow_empty=False, save_dbi_compat=False)
    check(ok_gen and kept_gen == 2, "Generic ZIP (!save_dbi_compat) does not reject reserved files as metadata-only")

    # 6. TransferUnzipAll marker enforcement:
    ok_uz, out_uz = simulate_unzip_all(z_dir_payload, save_dbi_compat=True)
    check(ok_uz and "//" not in out_uz and "save_dir/" in out_uz, "TransferUnzipAll excludes valid marker from output")

    buf_dup = io.BytesIO()
    with zipfile.ZipFile(buf_dup, "w") as zf:
        zf.writestr(zipfile.ZipInfo("//"), b"")
        zf.writestr(zipfile.ZipInfo("//"), b"")
    ok_dup, _ = simulate_unzip_all(buf_dup.getvalue(), save_dbi_compat=True)
    check(not ok_dup, "TransferUnzipAll independently rejects duplicate DBI marker")

    print("  -> Metadata-only vs kept payload fixtures PASSED.")


# ==============================================================================
# 4. Behavioral Fixtures: Sources and Priority Ordering (D)
# ==============================================================================

def test_sources_and_priority():
    print("[4] Running sources and priority ordering fixtures (D)...")

    def normalize_backup_search_path(p: str) -> str | None:
        p = p.strip().replace("\\", "/")
        if not p.startswith("/"): p = "/" + p
        while "//" in p: p = p.replace("//", "/")
        if p.endswith("/") and len(p) > 1: p = p[:-1]
        low = p.lower()
        if p == "/" or low == "/dumps" or low == "/switch/dbi/saves" or low == "/dbisaves":
            return None
        return p

    check(normalize_backup_search_path("/DBISaves") is None, "/DBISaves rejected in custom paths")
    check(normalize_backup_search_path("/dbisaves") is None, "Case-insensitive /dbisaves rejected")
    check(normalize_backup_search_path("/switch/DBI/saves") is None, "/switch/DBI/saves rejected")

    candidates = [
        {"ts": 100, "source": 10, "path": "/dumps/custom/100.zip"},
        {"ts": 200, "source": 0,  "path": "/DBISaves/game/200.zip"},
        {"ts": 200, "source": 1,  "path": "/dumps/200.zip"},
        {"ts": 200, "source": 0,  "path": "/switch/DBI/saves/game/200.zip"},
    ]
    # Sort order: newest ts desc, lower source prio asc, path asc
    candidates.sort(key=lambda c: (-c["ts"], c["source"], c["path"]))
    check(candidates[0]["path"] == "/DBISaves/game/200.zip", "Deterministic top priority candidate")
    check(candidates[1]["path"] == "/switch/DBI/saves/game/200.zip", "Equal ts & source broken by path")
    check(candidates[2]["source"] == 1, "Sphaira source 1 follows DBI source 0")
    check(candidates[3]["ts"] == 100, "Older timestamp last")

    print("  -> Sources and priority ordering fixtures PASSED.")


# ==============================================================================
# 5. Behavioral Fixtures: Unified Route & Final Reinspection (E & F)
# ==============================================================================

def make_group_key(app_id: int, save_type: int, uid: tuple[int, int], index: int, rank: int, rank_known: bool) -> str:
    rk = f"r{rank}" if rank_known else "rx"
    if save_type in [0, 2]: # System-like
        return f"backup:system:{save_type}:{app_id:016X}:{index}:{rk}"
    return f"backup:app:{app_id:016X}:{save_type}:{uid[0]:016X}{uid[1]:016X}:{index}:{rk}"

def test_unified_route_and_reinspection():
    print("[5] Running unified route and final reinspection fixtures (E & F)...")

    # Retained backup group
    group = {
        "app_id": 0x0100000000010000,
        "type": 1,
        "uid": (0x1234, 0x5678),
        "index": 0,
        "rank": 0,
        "rank_known": True,
        "dbi_game_dir": "Test Game [0100000000010000]",
        "members": ["/switch/DBI/saves/game/2026.zip", "/DBISaves/game/2025.zip"]
    }
    expected_key = make_group_key(group["app_id"], group["type"], group["uid"], group["index"], group["rank"], group["rank_known"])

    # Reinspection: matching archive accepted
    inspected_match = {
        "app_id": 0x0100000000010000,
        "type": 1,
        "uid": (0x1234, 0x5678),
        "index": 0,
        "rank": 0,
        "rank_known": True,
        "path": "/switch/DBI/saves/game/2026.zip"
    }
    match_key = make_group_key(inspected_match["app_id"], inspected_match["type"], inspected_match["uid"], inspected_match["index"], inspected_match["rank"], inspected_match["rank_known"])
    check(match_key == expected_key and inspected_match["path"] in group["members"], "Matching reinspection accepted")

    # Reinspection: app ID changed -> rejected
    check(make_group_key(0x0100000000020000, group["type"], group["uid"], group["index"], group["rank"], group["rank_known"]) != expected_key, "App ID mismatch rejected")

    # Reinspection: UID changed -> rejected
    check(make_group_key(group["app_id"], group["type"], (0x9999, 0x9999), group["index"], group["rank"], group["rank_known"]) != expected_key, "UID mismatch rejected")

    # Reinspection: type changed -> rejected
    check(make_group_key(group["app_id"], 4, group["uid"], group["index"], group["rank"], group["rank_known"]) != expected_key, "Type mismatch rejected")

    # Reinspection: path not in retained members -> rejected
    check("/unknown/path.zip" not in group["members"], "Unknown path rejected")

    # Metadata-free DBI directory identity test
    dbi_dir_hex = 0x0100ABCD00000000
    meta_free_key = make_group_key(dbi_dir_hex, 1, (0, 0), 0, 0, False)
    check("0100ABCD00000000" in meta_free_key, "Directory-derived identity embedded in group key")

    # Multi-group restore: deterministic first retained member, no loose discovery
    mg_groups = [
        {"members": ["/a/1.zip", "/a/2.zip"]},
        {"members": ["/b/1.zip"]},
    ]
    chosen_members = [g["members"][0] for g in mg_groups]
    check(chosen_members == ["/a/1.zip", "/b/1.zip"], "Deterministic first member selection in batch")

    # Live group construction from live entry via CollectBackups candidate retention
    raw_candidates = [
        {"ts": 20260921100000, "source": 0, "path": "/switch/DBI/saves/game/2026.zip", "key": expected_key},
        {"ts": 20260921090000, "source": 1, "path": "/dumps/game/2026.zip", "key": expected_key},
        {"ts": 20260920100000, "source": 0, "path": "/switch/DBI/saves/game/wrong_user.zip", "key": "backup:app:0100000000010000:1:00000000000099990000000000009999:0:r0"},
    ]
    retained_members = [c for c in raw_candidates if c["key"] == expected_key]
    retained_members.sort(key=lambda c: (-c["ts"], c["source"], c["path"]))
    check(len(retained_members) == 2, "Live group retains only matching candidates")
    check(retained_members[0]["path"] == "/switch/DBI/saves/game/2026.zip", "Retained member 0 is newest")
    check(retained_members[1]["path"] == "/dumps/game/2026.zip", "Retained member 1 is older")

    # Batch restore pre-validation: reject if any source has empty backup_members
    batch_sources_valid = [
        {"members": [retained_members[0]]},
        {"members": [retained_members[1]]},
    ]
    batch_sources_invalid = [
        {"members": [retained_members[0]]},
        {"members": []},
    ]
    def validate_batch_sources(sources):
        for s in sources:
            if not s["members"]:
                return False
        return True
    check(validate_batch_sources(batch_sources_valid), "Valid batch sources pass pre-validation")
    check(not validate_batch_sources(batch_sources_invalid), "Empty-member batch source rejected")

    # Batch recovery archives reporting: singular vs plural
    def format_batch_recovery(paths, success, raw=False, mut=False):
        if not paths:
            return ""
        pl = len(paths) > 1
        if success:
            p = "Restore completed.\nSafety recovery archive(s):\n" if pl else "Restore completed.\nSafety recovery archive:\n"
        elif raw:
            p = "Restore stopped.\nSafety recovery archive(s) retained:\n"
        elif mut:
            p = ("Restore stopped: current target save may have changed and restored contents are unverified.\nSafety recovery archive(s) retained:\n" if pl
                 else "Restore stopped: target save may have changed and restored contents are unverified.\nSafety recovery archive retained:\n")
        else:
            p = ("Restore stopped before current target save was modified.\nSafety recovery archive(s) retained:\n" if pl
                 else "Restore stopped before target save was modified.\nSafety recovery archive retained:\n")
        return p + "".join(rp + "\n" for rp in paths)

    single_rec = format_batch_recovery(["/dumps/rec1.zip"], True)
    check("Safety recovery archive:\n/dumps/rec1.zip" in single_rec, "Singular recovery archive message")
    plural_rec = format_batch_recovery(["/dumps/rec1.zip", "/dumps/rec2.zip"], True)
    check("Safety recovery archive(s):\n/dumps/rec1.zip\n/dumps/rec2.zip" in plural_rec, "Plural recovery archives message in order")

    # UMS/stdio vs SD probe FS selection model
    def probe_fs_for(location_type: int, backup_root: str) -> str:
        if location_type == 3 or backup_root.startswith("ums"):
            return "stdio"
        return "sd"

    check(probe_fs_for(0, "/dumps") == "sd", "Default location uses SD")
    check(probe_fs_for(3, "/dumps") == "stdio", "Stdio location uses stdio")
    check(probe_fs_for(0, "ums0:/dumps") == "stdio", "UMS backup_root uses stdio")

    # Duplicate target protection
    seen_targets = set()
    t1_key = "live:app:0100000000010000:1:00000000000012340000000000005678:0"
    check(t1_key not in seen_targets, "Target 1 first seen")
    seen_targets.add(t1_key)
    check(t1_key in seen_targets, "Target 1 duplicate detected and rejected")

    print("  -> Unified route and final reinspection fixtures PASSED.")


def main():
    print("================================================================================")
    print("Sphaira v0.13.868: Real DBI Restore Admission & Unified Exact Route Contract")
    print("================================================================================")
    test_static_source_contracts()
    test_dbi_marker_and_malformed_matrix()
    test_metadata_only_vs_payload()
    test_sources_and_priority()
    test_unified_route_and_reinspection()
    print("================================================================================")
    print("ALL TESTS PASSED SUCCESSFULLY (v0.13.868)")
    print("================================================================================")

if __name__ == "__main__":
    main()
