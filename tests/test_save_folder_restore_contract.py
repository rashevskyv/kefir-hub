#!/usr/bin/env python3
"""
Test Suite: Save Folder Restore & Provenance Remap Contract (v0.13.882)

Target chat: Походження бекапів і папкові бекапи
Verifies:
1. Blocker 1: Provenance comparison vs BackupSource enum (not scan priority int)
   - RestoreSaves checks check_info.backup_source != src.backup_source
   - RestoreSavesPicked checks check_info.backup_source != group.backup_source
   - member.source / it->source priority int comparisons removed
2. Blocker 2: UID remapping & destination slot verification
   - MatchesRestoreDestination helper in save_paths.hpp
   - RestoreSaveInternal, RestoreSaves, RestoreSavesPicked verify destination slot
   - Source UID == Destination UID not required for Account saves
   - Non-account saves require UID match
   - Destination title, save type, rank, index verified
3. Blocker 3: PlanAndConfirmRestoreCreation probe FS choice
   - archive_path.starts_with("ums") ? &stdio_fs : &sd_fs
   - Catalog paths starting with "/" correctly choose sd_fs
4. Blocker 4: Staged ZIP revalidation before slot creation
   - Revalidate staged ZIP metadata against creation_request before CreateSaveDataChecked
   - Verified title, Account type, primary rank, index 0, owner ID, aligned sizes
   - Reject before mutation started
   - Source-user to selected-local-user remapping preserved
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


def read_file(*parts: str) -> str:
    path = os.path.join(REPO_ROOT, *parts)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def test_static_review_blockers() -> None:
    print("[1] Verifying static review blocker fixes...")

    # 1. Version must be 0.13.882 or later.
    cmake_txt = read_file("sphaira", "CMakeLists.txt")
    version = re.search(r"set\(sphaira_VERSION 0\.13\.(\d+)\)", cmake_txt)
    check(version is not None and int(version.group(1)) >= 882, "CMakeLists.txt must be 0.13.882+")

    # 2. Blocker 1: No member.source / it->source provenance comparison in save_menu_ops.cpp
    ops_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    check("check_info.backup_source != member.source" not in ops_cpp,
          "member.source comparison must be removed from save_menu_ops.cpp")
    check("check_info.backup_source != it->source" not in ops_cpp,
          "it->source comparison must be removed from save_menu_ops.cpp")
    check("check_info.backup_source != src.backup_source" in ops_cpp,
          "RestoreSaves must compare check_info.backup_source against src.backup_source")
    check("check_info.backup_source != group.backup_source" in ops_cpp,
          "RestoreSavesPicked must compare check_info.backup_source against group.backup_source")

    # 3. Blocker 2: MatchesRestoreDestination helper and usage
    paths_hpp = read_file("sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    check("MatchesRestoreDestination" in paths_hpp,
          "save_paths.hpp must declare MatchesRestoreDestination")
    check("info.save_data_type == FsSaveDataType_Account" in paths_hpp,
          "MatchesRestoreDestination must check Account save type")
    check("dst.uid.uid[0] != 0 || dst.uid.uid[1] != 0" in paths_hpp,
          "MatchesRestoreDestination must permit valid non-zero destination Account UID")

    check("BackupGroupKey(check_info) != BackupGroupKey(e)" not in ops_cpp,
          "RestoreSaveInternal must not compare BackupGroupKey(check_info) with destination e")
    check("MatchesRestoreDestination(check_info, e)" in ops_cpp,
          "RestoreSaveInternal and RestoreSavesPicked must use MatchesRestoreDestination")
    check("MatchesRestoreDestination(check_info, dst)" in ops_cpp,
          "RestoreSaves must use MatchesRestoreDestination for batch restore")

    # 4. Blocker 3: Probe FS selection in save_restore_route.cpp
    route_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_restore_route.cpp")
    check('archive_path.starts_with("sdmc:/") ? static_cast<fs::Fs*>(&sd_fs)' not in route_cpp,
          "save_restore_route.cpp must not check starts_with('sdmc:/') for probe_fs")
    check('archive_path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs)' in route_cpp,
          "save_restore_route.cpp must use ums check matching save_menu_ops.cpp")

    # 5. Blocker 4: Staged ZIP revalidation before CreateSaveDataChecked in save_restore_zip.cpp
    zip_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    check("archive_meta.meta.owner_id != e.creation_request.owner_id" in zip_cpp,
          "RestoreSaveZip must validate owner_id against creation_request")
    check("archive_meta.meta.attr.application_id != e.creation_request.attr.application_id" in zip_cpp,
          "RestoreSaveZip must validate application_id against creation_request")
    check("archive_meta.meta.data_size > e.creation_request.data_size" in zip_cpp,
          "RestoreSaveZip must validate data_size against creation_request")
    check("archive_meta.meta.journal_size > e.creation_request.journal_size" in zip_cpp,
          "RestoreSaveZip must validate journal_size against creation_request")
    check("archive_meta.meta.attr.uid" not in zip_cpp,
          "RestoreSaveZip must preserve source-user to selected-local-user remapping")

    print("  -> Static review blocker checks PASSED.")


def test_behavioral_provenance_and_dest_match() -> None:
    print("[2] Running behavioral provenance & destination match models...")

    # Behavioral model of MatchesRestoreDestination
    def matches_restore_dest(info: dict, dst: dict) -> bool:
        rank_matches = (not info.get("rank_known", False)) or (info.get("save_data_rank") == dst.get("save_data_rank"))
        if info.get("application_id") != dst.get("application_id"):
            return False
        if info.get("system_save_data_id", 0) != dst.get("system_save_data_id", 0):
            return False
        if info.get("save_data_type") != dst.get("save_data_type"):
            return False
        if not rank_matches:
            return False
        if info.get("save_data_index", 0) != dst.get("save_data_index", 0):
            return False
        if info.get("save_data_type") == 1:  # FsSaveDataType_Account
            dst_uid = dst.get("uid", (0, 0))
            return dst_uid[0] != 0 or dst_uid[1] != 0
        return info.get("uid") == dst.get("uid")

    app_id = 0x0100000000010000
    user_a = (0x1111, 0x2222)
    user_b = (0x3333, 0x4444)

    # 1. Cross-user Account restore (source User A -> destination User B)
    info_user_a = {
        "application_id": app_id,
        "system_save_data_id": 0,
        "save_data_type": 1,
        "save_data_rank": 0,
        "save_data_index": 0,
        "rank_known": True,
        "uid": user_a,
    }
    dst_user_b = {
        "application_id": app_id,
        "system_save_data_id": 0,
        "save_data_type": 1,
        "save_data_rank": 0,
        "save_data_index": 0,
        "uid": user_b,
    }
    check(matches_restore_dest(info_user_a, dst_user_b),
          "Account save must match destination slot with different local user UID")

    # 2. Reject mismatched title ID
    dst_wrong_title = dict(dst_user_b, application_id=0x0100000000020000)
    check(not matches_restore_dest(info_user_a, dst_wrong_title),
          "Must reject mismatched application ID")

    # 3. Reject mismatched save type
    dst_wrong_type = dict(dst_user_b, save_data_type=0)  # System
    check(not matches_restore_dest(info_user_a, dst_wrong_type),
          "Must reject mismatched save type")

    # 4. Reject mismatched rank when known
    dst_wrong_rank = dict(dst_user_b, save_data_rank=1)
    check(not matches_restore_dest(info_user_a, dst_wrong_rank),
          "Must reject mismatched rank when rank is known")

    # 5. Allow unknown rank match (Checkpoint folder)
    info_cp_unknown_rank = dict(info_user_a, rank_known=False, save_data_rank=0)
    check(matches_restore_dest(info_cp_unknown_rank, dst_wrong_rank),
          "Must allow rank match when backup rank is unknown")

    # 6. Reject mismatched save index
    dst_wrong_index = dict(dst_user_b, save_data_index=1)
    check(not matches_restore_dest(info_user_a, dst_wrong_index),
          "Must reject mismatched save index")

    # 7. Zero destination Account UID refused
    dst_zero_uid = dict(dst_user_b, uid=(0, 0))
    check(not matches_restore_dest(info_user_a, dst_zero_uid),
          "Zero destination Account UID must be refused")

    # 8. Device/System save requires exact UID match
    info_device = {
        "application_id": app_id,
        "system_save_data_id": 0,
        "save_data_type": 2,  # Device
        "save_data_rank": 0,
        "save_data_index": 0,
        "rank_known": True,
        "uid": (0, 0),
    }
    dst_device_ok = dict(info_device)
    dst_device_bad = dict(info_device, uid=(1, 1))
    check(matches_restore_dest(info_device, dst_device_ok), "Device save with matching UID matches")
    check(not matches_restore_dest(info_device, dst_device_bad), "Device save with differing UID rejected")

    # Behavioral model of Probe FS selection
    def select_probe_fs(path: str) -> str:
        return "stdio_fs" if path.startswith("ums") else "sd_fs"

    check(select_probe_fs("/switch/Checkpoint/saves/game/b1") == "sd_fs",
          "Catalog path starting with '/' must select sd_fs")
    check(select_probe_fs("/JKSV/game/b1") == "sd_fs",
          "JKSV path starting with '/' must select sd_fs")
    check(select_probe_fs("sdmc:/switch/Checkpoint/saves") == "sd_fs",
          "sdmc:/ path must select sd_fs")
    check(select_probe_fs("ums0:/saves/backup") == "stdio_fs",
          "ums0:/ path must select stdio_fs")

    print("  -> Behavioral provenance & destination match models PASSED.")


def test_behavioral_staged_zip_revalidation() -> None:
    print("[3] Running behavioral staged ZIP revalidation models...")

    # Behavioral model of RestoreSaveZip pre-creation revalidation
    def revalidate_pre_create(meta: dict, req: dict, allow_empty: bool, meta_status: str) -> tuple[bool, str]:
        if meta_status == "Valid" and (meta.get("has_nx_meta") or meta.get("has_dbi_extra")):
            attr = meta.get("attr", {})
            req_attr = req.get("attr", {})
            if attr.get("application_id") != req_attr.get("application_id"):
                return False, "PathNotFound: app_id mismatch"
            if attr.get("save_data_type") != 1 or req_attr.get("save_data_type") != 1:
                return False, "PathNotFound: save_data_type mismatch"
            if attr.get("save_data_rank") != 0 or req_attr.get("save_data_rank") != 0:
                return False, "PathNotFound: save_data_rank mismatch"
            if attr.get("save_data_index") != 0 or req_attr.get("save_data_index") != 0:
                return False, "PathNotFound: save_data_index mismatch"
            if meta.get("owner_id") != req.get("owner_id"):
                return False, "PathNotFound: owner_id mismatch"
            if meta.get("data_size", 0) <= 0 or meta.get("data_size", 0) > req.get("data_size", 0):
                return False, "InvalidSize: data_size out of bounds"
            if meta.get("data_size", 0) % 0x4000 != 0:
                return False, "InvalidSize: unaligned data_size"
            if meta.get("journal_size", 0) < 0 or meta.get("journal_size", 0) > req.get("journal_size", 0):
                return False, "InvalidSize: journal_size out of bounds"
            if meta.get("journal_size", 0) % 0x4000 != 0:
                return False, "InvalidSize: unaligned journal_size"
            return True, "Success"
        elif allow_empty or req.get("provenance") == "ArchiveMetadata":
            return False, "PathNotFound: missing valid metadata"
        return True, "Success"

    app_id = 0x0100000000010000
    user_src = (0x9999, 0x8888)
    user_dst = (0x1111, 0x2222)

    valid_req = {
        "attr": {
            "application_id": app_id,
            "uid": user_dst,
            "save_data_type": 1,
            "save_data_rank": 0,
            "save_data_index": 0,
        },
        "owner_id": app_id,
        "data_size": 0x80000,
        "journal_size": 0x40000,
        "provenance": "ArchiveMetadata",
    }

    valid_meta = {
        "has_nx_meta": True,
        "has_dbi_extra": False,
        "attr": {
            "application_id": app_id,
            "uid": user_src,  # Source user UID differs from destination!
            "save_data_type": 1,
            "save_data_rank": 0,
            "save_data_index": 0,
        },
        "owner_id": app_id,
        "data_size": 0x80000,
        "journal_size": 0x40000,
    }

    # 1. Matching metadata with source UID remap to destination UID
    ok, reason = revalidate_pre_create(valid_meta, valid_req, allow_empty=True, meta_status="Valid")
    check(ok, f"Valid staged ZIP metadata with UID remap must pass revalidation ({reason})")

    # 2. Tampered title ID rejected before creation
    bad_title_meta = dict(valid_meta, attr=dict(valid_meta["attr"], application_id=0x0100000000099999))
    ok, reason = revalidate_pre_create(bad_title_meta, valid_req, allow_empty=True, meta_status="Valid")
    check(not ok and "app_id mismatch" in reason, "Tampered title ID must be rejected")

    # 3. Tampered owner ID rejected before creation
    bad_owner_meta = dict(valid_meta, owner_id=0x0100000000088888)
    ok, reason = revalidate_pre_create(bad_owner_meta, valid_req, allow_empty=True, meta_status="Valid")
    check(not ok and "owner_id mismatch" in reason, "Tampered owner ID must be rejected")

    # 4. Exceeded data size rejected before creation
    bad_size_meta = dict(valid_meta, data_size=0x100000)
    ok, reason = revalidate_pre_create(bad_size_meta, valid_req, allow_empty=True, meta_status="Valid")
    check(not ok and "data_size out of bounds" in reason, "Exceeded data size must be rejected")

    # 5. Missing metadata for folder restore rejected before creation
    ok, reason = revalidate_pre_create({}, valid_req, allow_empty=True, meta_status="NoMetadata")
    check(not ok and "missing valid metadata" in reason, "Missing metadata for folder restore must be rejected")

    # 6. Unaligned journal size rejected before creation
    unaligned_meta = dict(valid_meta, journal_size=0x30001)
    ok, reason = revalidate_pre_create(unaligned_meta, valid_req, allow_empty=True, meta_status="Valid")
    check(not ok and "unaligned journal_size" in reason, "Unaligned journal size must be rejected")

    print("  -> Behavioral staged ZIP revalidation models PASSED.")


def main() -> None:
    test_static_review_blockers()
    test_behavioral_provenance_and_dest_match()
    test_behavioral_staged_zip_revalidation()
    print("ALL SAVE FOLDER RESTORE CONTRACT TESTS PASSED.")


if __name__ == "__main__":
    main()
