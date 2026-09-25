#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for MTP save layout.

NOTE ON SCOPE AND COVERAGE:
This test executes static source contracts and a Python behavioral model of the
allocation, naming algorithms, and fail-closed read-only proxy enforcement.
It validates algorithmic invariance, collision resolution, boundary conditions,
mutation rejection, and C++ source patterns. It does NOT execute the C++ binary,
libnx/libhaze runtime, Windows MTP stack, or target Nintendo Switch hardware.
"""

import itertools
import json
import os
import re
import sys

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

def extract_scoped_class(src, class_name):
    pattern = rf'struct\s+{class_name}\s+(?:final\s*)?:[^{{]+\{{'
    m = re.search(pattern, src)
    check(m is not None, f"Class/struct {class_name} not found in source")
    start = m.end() - 1
    depth = 0
    for i in range(start, len(src)):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[start:i+1]
    check(False, f"Brace mismatch while extracting {class_name}")

def extract_method_body(class_src, method_name):
    pattern = rf'(?:Result|void|bool|auto)\s+{method_name}\s*\([^)]*\)(?:\s*const)?(?:\s*override)?(?:\s*->\s*[^{{]+)?\s*\{{'
    m = re.search(pattern, class_src)
    check(m is not None, f"Method {method_name} not found in scoped class")
    start = m.end() - 1
    depth = 0
    for i in range(start, len(class_src)):
        if class_src[i] == '{':
            depth += 1
        elif class_src[i] == '}':
            depth -= 1
            if depth == 0:
                return class_src[start:i+1]
    check(False, f"Brace mismatch while extracting method {method_name}")

def test_source_contracts():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    save_proxy_path = os.path.join(repo_root, "sphaira", "source", "haze", "haze_save_proxy.cpp")
    save_scan_path = os.path.join(repo_root, "sphaira", "source", "haze", "haze_save_proxy_scan.cpp")
    with open(save_proxy_path, "r", encoding="utf-8") as f:
        src = f.read()

    proxy_body = extract_scoped_class(src, "FsSaveProxy")
    if os.path.exists(save_scan_path):
        with open(save_scan_path, "r", encoding="utf-8") as f:
            proxy_body += "\n" + f.read()

    # =========================================================================
    # 1. Deterministic total order and sorting before name allocation
    # =========================================================================
    check("CompareSaveDataInfo" in proxy_body, "Must define CompareSaveDataInfo for deterministic total order")
    check("std::sort(records.begin(), records.end(), CompareSaveDataInfo);" in proxy_body,
          "Must sort records deterministically before name allocation")
    check("std::unique(records.begin(), records.end(), IsSameSaveRecord);" in proxy_body,
          "Must deduplicate identical scan records using IsSameSaveRecord")

    # Ensure CompareSaveDataInfo checks actual fields without padding or exposing UID
    for field in ["save_data_type", "save_data_index", "save_data_rank", "save_data_id",
                  "save_data_space_id", "system_save_data_id", "application_id", "uid.uid[0]", "uid.uid[1]"]:
        check(field in proxy_body, f"CompareSaveDataInfo must check {field}")
    check("UidHex" not in proxy_body, "UidHex must not exist; full UID is internal only")

    # =========================================================================
    # 2. Bounded game names reserving prefix and suffix space
    # =========================================================================
    check("FormatSaveGameDirName" in proxy_body, "Must define FormatSaveGameDirName for bounded game directory names")
    check("BuildSaveGameDirName" in proxy_body, "Must use BuildSaveGameDirName for save game directories")
    check("IsWindowsReservedDeviceName" in proxy_body, "Must check Windows reserved device names")

    # =========================================================================
    # 3. Preserved layout: game -> user -> live save root (no 'Account ' prefix)
    # =========================================================================
    check("BuildTypeDirName" not in proxy_body, "BuildTypeDirName must be replaced with direct nickname layout")
    check('"Account "' not in proxy_body, 'Second-level folder must not have "Account " prefix')

    # =========================================================================
    # 4. Ponytail comment on O(n^2) duplicate loop
    # =========================================================================
    check("Ponytail: O(n^2)" in proxy_body, "Must include ponytail comment on bounded O(n^2) duplicate scan ceiling")

    # =========================================================================
    # 5. Non-account buckets preserved
    # =========================================================================
    for bucket in ["BCAT", "Device", "Cache"]:
        check(f'"{bucket}"' in proxy_body, f"Non-account bucket {bucket} must be preserved")

    # =========================================================================
    # 6. Preserved scan spaces and types (v0.13.865: all 7 libnx types & shared discovery)
    # =========================================================================
    for stype in [
        "FsSaveDataType_System",
        "FsSaveDataType_Account",
        "FsSaveDataType_Bcat",
        "FsSaveDataType_Device",
        "FsSaveDataType_Temporary",
        "FsSaveDataType_Cache",
        "FsSaveDataType_SystemBcat",
    ]:
        check(stype in proxy_body, f"Must support save type {stype}")

    # Shared discovery reuse and no second filtered discovery reader
    check("DiscoverSaveDataInfo(nullptr, std::nullopt)" in proxy_body,
          "Must call shared DiscoverSaveDataInfo(nullptr, std::nullopt)")
    check(proxy_body.count("DiscoverSaveDataInfo(nullptr, std::nullopt)") == 1,
          "Must have exactly one call to shared DiscoverSaveDataInfo")
    check("fsOpenSaveDataInfoReaderWithFilter" not in proxy_body,
          "Must not have second filtered discovery reader")
    check("FsSaveDataFilter" not in proxy_body,
          "Must not use FsSaveDataFilter in FsSaveProxy")

    # Explicit top-level typed buckets
    for bucket in ['"Temporary"', '"System"', '"System BCAT"']:
        check(bucket in proxy_body, f"Top-level typed bucket {bucket} must exist in proxy")

    # =========================================================================
    # 7. Fail-closed mutation overrides reject with FsError_NotImplemented
    # =========================================================================
    mutation_methods = [
        "CreateFile",
        "DeleteFile",
        "RenameFile",
        "CreateDirectory",
        "DeleteDirectoryRecursively",
        "RenameDirectory",
        "SetFileSize",
        "WriteFile",
    ]

    for m_name in mutation_methods:
        body = extract_method_body(proxy_body, m_name)
        check("FsError_NotImplemented" in body, f"{m_name} must reject with FsError_NotImplemented")
        check("Parse(" not in body, f"{m_name} must not call Parse before rejecting")
        check("MountSave(" not in body, f"{m_name} must not call MountSave before rejecting")
        check("fs->" not in body, f"{m_name} must not make fs calls")
        check("Commit" not in body, f"{m_name} must not call Commit")
        check("->file" not in body and "h->file" not in body, f"{m_name} must not dereference file handles")

    # Legacy mutating fs calls must be completely absent from FsSaveProxy
    for legacy_call in ["fs->CreateFile", "fs->DeleteFile", "fs->RenameFile",
                        "fs->CreateDirectory", "fs->DeleteDirectoryRecursively", "fs->RenameDirectory",
                        "h->file.SetSize", "h->file.Write"]:
        check(legacy_call not in proxy_body, f"Legacy mutating call {legacy_call} must be absent from FsSaveProxy")

    # =========================================================================
    # 8. OpenFile write/append rejection before Parse and MountSave
    # =========================================================================
    open_file_body = extract_method_body(proxy_body, "OpenFile")
    check("FsOpenMode_Write" in open_file_body and "FsOpenMode_Append" in open_file_body,
          "OpenFile must check for FsOpenMode_Write and FsOpenMode_Append")
    check("FsError_NotImplemented" in open_file_body,
          "OpenFile must reject write/append with FsError_NotImplemented")
    pos_reject = open_file_body.find("FsError_NotImplemented")
    pos_parse = open_file_body.find("Parse(")
    pos_mount = open_file_body.find("MountSave(")
    check(pos_reject != -1 and pos_parse != -1 and pos_mount != -1,
          "OpenFile must contain rejection, Parse, and MountSave")
    check(pos_reject < pos_parse, "OpenFile must reject write/append before Parse")
    check(pos_reject < pos_mount, "OpenFile must reject write/append before MountSave")

    # =========================================================================
    # 9. Read-only MountSave without read-write fallback
    # =========================================================================
    mount_body = extract_method_body(proxy_body, "MountSave")
    check("std::make_shared<fs::FsNativeSave>" in mount_body,
          "MountSave must construct FsNativeSave")
    check(", true);" in mount_body,
          "MountSave must pass read_only=true to FsNativeSave constructor")
    check(", false);" not in proxy_body,
          "FsSaveProxy must have no read_only=false FsNativeSave construction")
    check(proxy_body.count("std::make_shared<fs::FsNativeSave>") == 1,
          "MountSave must have exactly one FsNativeSave construction (no read-write fallback)")

    # Retained FsSaveDataInfo fields in MountSave
    for field in ["info.save_data_space_id", "info.application_id", "info.uid",
                  "info.system_save_data_id", "info.save_data_type", "info.save_data_rank",
                  "info.save_data_index"]:
        check(field in mount_body, f"MountSave must route retained field {field}")

    # =========================================================================
    # 10. FileHandle has no writable state and CloseFile has no Commit
    # =========================================================================
    handle_match = re.search(r'struct\s+FileHandle\s*\{([^}]+)\};', proxy_body)
    check(handle_match is not None, "FileHandle struct definition not found")
    handle_def = handle_match.group(1)
    check("writable" not in handle_def, "FileHandle must not contain writable state")

    close_body = extract_method_body(proxy_body, "CloseFile")
    check("Commit" not in close_body, "CloseFile must not call Commit")
    check("delete h;" in close_body, "CloseFile must release handle")
    check("std::memset(file, 0, sizeof(*file));" in close_body, "CloseFile must zero FsFile object")

    # No Commit calls anywhere in FsSaveProxy
    check("Commit" not in proxy_body, "FsSaveProxy must not call Commit anywhere")

    # =========================================================================
    # 11. Read/list handlers remain intact
    # =========================================================================
    read_list_methods = [
        "GetTotalSpace",
        "GetFreeSpace",
        "GetEntryType",
        "OpenFile",
        "GetFileSize",
        "ReadFile",
        "CloseFile",
        "OpenDirectory",
        "ReadDirectory",
        "GetDirectoryEntryCount",
        "CloseDirectory",
        "MultiThreadTransfer",
    ]
    for r_name in read_list_methods:
        check(f"{r_name}(" in proxy_body, f"FsSaveProxy must implement read/list handler {r_name}")

    # Depth routing in read methods
    check("pp.depth < 2" in proxy_body, "Depth < 2 must be virtual directory enumeration")
    check("pp.depth >= 2" in proxy_body, "Depth >= 2 must mount save and route rest")

    # Real mount error propagation via R_TRY in GetTotalSpace and GetFreeSpace
    get_total_space_body = extract_method_body(proxy_body, "GetTotalSpace")
    check("R_TRY(MountSave(pp, fs));" in get_total_space_body,
          "GetTotalSpace must propagate MountSave errors via R_TRY")
    get_free_space_body = extract_method_body(proxy_body, "GetFreeSpace")
    check("R_TRY(MountSave(pp, fs));" in get_free_space_body,
          "GetFreeSpace must propagate MountSave errors via R_TRY")

    # =========================================================================
    # 12. Pinned Saves registration path in haze_helper.cpp
    # =========================================================================
    haze_helper_path = os.path.join(repo_root, "sphaira", "source", "haze_helper.cpp")
    with open(haze_helper_path, "r", encoding="utf-8") as f:
        hh_src = f.read()
    check('MakeFsSaveProxy("saves", display_name)' in hh_src,
          "haze_helper.cpp must register MakeFsSaveProxy")
    check('App::GetMtpShowSaves()' in hh_src,
          "haze_helper.cpp must check App::GetMtpShowSaves()")
    check('"Saves"' in hh_src,
          'haze_helper.cpp must name drive "Saves"')

    # =========================================================================
    # 13. Settings & i18n text truthfully specify read-only
    # =========================================================================
    settings_cat_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "settings", "settings_categories.cpp")
    with open(settings_cat_path, "r", encoding="utf-8") as f:
        settings_src = f.read()
    check('"Show Saves (read-only)"' in settings_src,
          'settings_categories.cpp must label drive "Show Saves (read-only)"')
    check('"Show a read-only drive with decrypted game saves. Files can be copied to the PC; writing is disabled."' in settings_src,
          'settings_categories.cpp description must state writing is disabled')

    en_json_path = os.path.join(repo_root, "assets", "romfs", "i18n", "en.json")
    with open(en_json_path, "r", encoding="utf-8") as f:
        en_i18n = json.load(f)
    check("Show Saves (read-only)" in en_i18n, "en.json must contain 'Show Saves (read-only)'")

    uk_json_path = os.path.join(repo_root, "assets", "romfs", "i18n", "uk.json")
    with open(uk_json_path, "r", encoding="utf-8") as f:
        uk_i18n = json.load(f)
    check("Show Saves (read-only)" in uk_i18n, "uk.json must contain 'Show Saves (read-only)'")

    # =========================================================================
    # 14. Version check in CMakeLists.txt
    # =========================================================================
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(865, 880)),
          "CMakeLists.txt must define sphaira_VERSION as 0.13.865 or later")

    # =========================================================================
    # 15. Shared mount / LRU lifetime and handle references
    # =========================================================================
    check("MOUNT_CACHE_MAX = 4;" in proxy_body, "Must define MOUNT_CACHE_MAX = 4")
    check("struct CachedMount" in proxy_body, "Must define CachedMount for LRU")
    check("handle->fs = fs;" in proxy_body, "Handles must retain shared_ptr to fs")

    # =========================================================================
    # 16. DisambiguateFinalName wiring across game, temporary, system, and system_bcat maps
    # =========================================================================
    check("DisambiguateFinalName(game_map," in proxy_body, "game_map must wire DisambiguateFinalName")
    check("DisambiguateFinalName(temp_map," in proxy_body, "temp_map must wire DisambiguateFinalName")
    check("DisambiguateFinalName(system_map," in proxy_body, "system_map must wire DisambiguateFinalName")
    check("DisambiguateFinalName(system_bcat_map," in proxy_body, "system_bcat_map must wire DisambiguateFinalName")

    print("Source contracts: ALL PASS (16 anchor groups)")

# ==============================================================================
# Behavioral Model (Algorithmic Specification & Synthetic Proxy Model)
# ==============================================================================

from contract_fixtures.mtp_save_scenarios import test_behavioral_model

if __name__ == "__main__":
    test_source_contracts()
    test_behavioral_model()
    print("ALL MTP SAVE CONTRACT TESTS PASSED.")
