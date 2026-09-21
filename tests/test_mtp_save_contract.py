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
    with open(save_proxy_path, "r", encoding="utf-8") as f:
        src = f.read()

    proxy_body = extract_scoped_class(src, "FsSaveProxy")

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
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(865, 875)),
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

class SyntheticSaveInfo:
    def __init__(self, save_data_id, space_id, save_type, app_id, uid, rank=0, index=0, sys_save_id=0):
        self.save_data_id = save_data_id
        self.save_data_space_id = space_id
        self.save_data_type = save_type
        self.application_id = app_id
        self.uid = uid  # (u64, u64)
        self.save_data_rank = rank
        self.save_data_index = index
        self.system_save_data_id = sys_save_id

    @property
    def identity_tuple(self):
        return (
            self.save_data_type,
            self.save_data_index,
            self.save_data_rank,
            self.save_data_id,
            self.save_data_space_id,
            self.system_save_data_id,
            self.application_id,
            self.uid[0],
            self.uid[1],
        )

def is_same_save_record(a, b):
    return a.identity_tuple == b.identity_tuple

def is_windows_reserved_device_name(name):
    stem = name.split(".")[0]
    if len(stem) == 3 and stem.upper() in {"CON", "PRN", "AUX", "NUL"}:
        return True
    if len(stem) == 4 and stem[:3].upper() in {"COM", "LPT"} and stem[3] in "123456789":
        return True
    return False

def truncate_utf8(text, max_bytes):
    raw = text.encode("utf-8")
    if len(raw) <= max_bytes:
        return text
    while max_bytes > 0:
        try:
            return raw[:max_bytes].decode("utf-8")
        except UnicodeDecodeError:
            max_bytes -= 1
    return ""

def sanitize_component(input_str):
    illegal = set('\\/:*?"<>|')
    out = []
    for ch in input_str:
        if ord(ch) < 0x20 or ord(ch) == 0x7F or ch in illegal:
            out.append('_')
        else:
            out.append(ch)
    s = "".join(out).strip(" \t")
    while s.endswith(".") or s.endswith(" ") or s.endswith("\t"):
        s = s[:-1]
    if not s or s == "." or s == "..":
        return ""
    if is_windows_reserved_device_name(s):
        s = "_" + s
    return s

def format_save_game_dir_name(localized_name, app_id, max_len=255):
    suffix = f" [{app_id:016X}]"
    if not localized_name:
        return f"[{app_id:016X}]"
    base = localized_name.strip(" \t")
    if not base:
        return f"[{app_id:016X}]"

    is_reserved = is_windows_reserved_device_name(base)
    reserved_space = len(suffix) + (1 if is_reserved else 0)
    max_title_len = max(0, max_len - reserved_space)

    base = truncate_utf8(base, max_title_len).rstrip(" \t")
    if not base:
        return f"[{app_id:016X}]"

    return ("_" if is_reserved else "") + base + suffix

class AccEntry:
    def __init__(self, r, base, has_nick, needs_suffix):
        self.r = r
        self.base = base
        self.has_nick = has_nick
        self.needs_suffix = needs_suffix
        self.candidate_name = ""

def disambiguate_final_name(existing_map, base, info, max_len=255):
    disambig = f" [{info.save_data_id:016X}-s{info.save_data_space_id}-t{info.save_data_type}-r{info.save_data_rank}-i{info.save_data_index}]"
    extra_len = len(disambig)
    max_base = max(0, max_len - extra_len)
    dbase = truncate_utf8(base if base else "Account", max_base)
    out_name = dbase + disambig

    counter = 2
    while out_name.lower() in existing_map:
        cnt = f" ({counter})"
        counter += 1
        total_suffix_len = extra_len + len(cnt)
        mb = max(0, max_len - total_suffix_len)
        cb = truncate_utf8(base if base else "Account", mb)
        out_name = cb + cnt + disambig
    return out_name

def allocate_save_names(records, accounts_dict, max_len=255):
    # 1. Deterministic sort before name allocation
    sorted_records = sorted(records, key=lambda r: r.identity_tuple)

    # Deduplicate identical records
    deduped = []
    for r in sorted_records:
        if not deduped or not is_same_save_record(deduped[-1], r):
            deduped.append(r)

    game_map = {}  # lower_name -> (actual_name, record)
    reserved_buckets = {"bcat", "device", "cache"}

    # 1. Non-account buckets in deterministic order
    for r in deduped:
        if r.save_data_type == 1:  # Account
            continue
        if r.save_data_type == 2:
            base = "BCAT"
        elif r.save_data_type == 3:
            base = "Device"
        elif r.save_data_type == 5:
            base = f"Cache {r.save_data_index}" if r.save_data_index else "Cache"
        else:
            base = f"Bucket_{r.save_data_type}"

        name = base
        if name.lower() in game_map:
            name = f"{base} [{r.save_data_id:016X}]"
        if name.lower() in game_map:
            name = disambiguate_final_name(game_map, base, r, max_len)
        game_map[name.lower()] = (name, r)

    # 2. Account saves in deterministic order
    acc_entries = []
    for r in deduped:
        if r.save_data_type != 1:
            continue
        raw_nick = accounts_dict.get(r.uid, "")
        sanitized = sanitize_component(raw_nick)
        if not sanitized:
            base = "Account"
            needs_suffix = True
            has_nick = False
        else:
            base = sanitized
            has_nick = True
            needs_suffix = (base.lower() in reserved_buckets or base.lower().startswith("cache ") or base.lower() in game_map)
        acc_entries.append(AccEntry(r, base, has_nick, needs_suffix))

    # O(n^2) duplicate checks
    for i in range(len(acc_entries)):
        for j in range(i + 1, len(acc_entries)):
            if acc_entries[i].base.lower() == acc_entries[j].base.lower():
                acc_entries[i].needs_suffix = True
                acc_entries[j].needs_suffix = True

    suffix_len = 19
    max_base = max(0, max_len - suffix_len)
    for i in range(len(acc_entries)):
        for j in range(i + 1, len(acc_entries)):
            if acc_entries[i].has_nick and acc_entries[j].has_nick:
                ti = truncate_utf8(acc_entries[i].base, max_base).strip(" \t.").lower()
                tj = truncate_utf8(acc_entries[j].base, max_base).strip(" \t.").lower()
                if ti == tj:
                    acc_entries[i].needs_suffix = True
                    acc_entries[j].needs_suffix = True

    def format_acc(entry):
        if entry.needs_suffix:
            sfx = f" [{entry.r.save_data_id:016X}]"
            tbase = truncate_utf8(entry.base, max_len - len(sfx)).rstrip(" \t.")
            if not tbase:
                tbase = "Account"
            name = tbase + sfx
        else:
            name = truncate_utf8(entry.base, max_len).rstrip(" \t.")
        if is_windows_reserved_device_name(name):
            name = "_" + name
        if len(name.encode("utf-8")) > max_len:
            name = truncate_utf8(name, max_len).rstrip(" \t.")
        return name

    for e in acc_entries:
        e.candidate_name = format_acc(e)

    # Collision against other candidate names or existing buckets
    for i in range(len(acc_entries)):
        if not acc_entries[i].needs_suffix and acc_entries[i].candidate_name.lower() in game_map:
            acc_entries[i].needs_suffix = True
            acc_entries[i].candidate_name = format_acc(acc_entries[i])
        for j in range(i + 1, len(acc_entries)):
            if acc_entries[i].candidate_name.lower() == acc_entries[j].candidate_name.lower():
                if not acc_entries[i].needs_suffix:
                    acc_entries[i].needs_suffix = True
                    acc_entries[i].candidate_name = format_acc(acc_entries[i])
                if not acc_entries[j].needs_suffix:
                    acc_entries[j].needs_suffix = True
                    acc_entries[j].candidate_name = format_acc(acc_entries[j])

    for e in acc_entries:
        name = e.candidate_name
        if name.lower() in game_map:
            name = disambiguate_final_name(game_map, e.base, e.r, max_len)
        game_map[name.lower()] = (name, e.r)

    # Map record identity -> visible name
    res = {}
    for lower_k, (act_name, r) in game_map.items():
        res[r.identity_tuple] = act_name
    return res

def build_mtp_save_tree(records, accounts_dict, title_dict=None, max_len=255):
    title_dict = title_dict or {}
    # Accept only the seven known types: 0 (System) to 6 (SystemBcat)
    valid_records = [r for r in records if 0 <= r.save_data_type <= 6]

    game_save_records = {}
    temporary_records = []
    system_records = []
    system_bcat_records = []

    for r in valid_records:
        if r.save_data_type in (1, 2, 3, 5):  # Account, BCAT, Device, Cache
            game_save_records.setdefault(r.application_id, []).append(r)
        elif r.save_data_type == 4:  # Temporary
            temporary_records.append(r)
        elif r.save_data_type == 0:  # System
            system_records.append(r)
        elif r.save_data_type == 6:  # System BCAT
            system_bcat_records.append(r)

    tree = {}

    for app_id, recs in game_save_records.items():
        recs = sorted(recs, key=lambda r: r.identity_tuple)
        deduped = []
        for r in recs:
            if not deduped or not is_same_save_record(deduped[-1], r):
                deduped.append(r)

        loc_name = title_dict.get(app_id, "")
        game_name = format_save_game_dir_name(loc_name, app_id, max_len)
        game_map = {}

        for r in deduped:
            if r.save_data_type == 1:
                continue
            if r.save_data_type == 2:
                base = "BCAT"
            elif r.save_data_type == 3:
                base = "Device"
            elif r.save_data_type == 5:
                base = f"Cache {r.save_data_index}" if r.save_data_index else "Cache"
            else:
                base = f"Save_{r.save_data_type}"

            name = base
            if name.lower() in game_map:
                name = f"{base} [{r.save_data_id:016X}]"
            if name.lower() in game_map:
                name = disambiguate_final_name(game_map, base, r, max_len)
            game_map[name.lower()] = (name, r)

        acc_entries = []
        for r in deduped:
            if r.save_data_type != 1:
                continue
            raw_nick = accounts_dict.get(r.uid, "")
            sanitized = sanitize_component(raw_nick)
            if not sanitized:
                base = "Account"
                needs_suffix = True
                has_nick = False
            else:
                base = sanitized
                has_nick = True
                reserved_buckets = {"bcat", "device", "cache"}
                needs_suffix = (base.lower() in reserved_buckets or base.lower().startswith("cache ") or base.lower() in game_map)
            acc_entries.append(AccEntry(r, base, has_nick, needs_suffix))

        for i in range(len(acc_entries)):
            for j in range(i + 1, len(acc_entries)):
                if acc_entries[i].base.lower() == acc_entries[j].base.lower():
                    acc_entries[i].needs_suffix = True
                    acc_entries[j].needs_suffix = True

        suffix_len = 19
        max_base = max(0, max_len - suffix_len)
        for i in range(len(acc_entries)):
            for j in range(i + 1, len(acc_entries)):
                if acc_entries[i].has_nick and acc_entries[j].has_nick:
                    ti = truncate_utf8(acc_entries[i].base, max_base).strip(" \t.").lower()
                    tj = truncate_utf8(acc_entries[j].base, max_base).strip(" \t.").lower()
                    if ti == tj:
                        acc_entries[i].needs_suffix = True
                        acc_entries[j].needs_suffix = True

        def format_acc(entry):
            if entry.needs_suffix:
                sfx = f" [{entry.r.save_data_id:016X}]"
                tbase = truncate_utf8(entry.base, max_len - len(sfx)).rstrip(" \t.")
                if not tbase:
                    tbase = "Account"
                name = tbase + sfx
            else:
                name = truncate_utf8(entry.base, max_len).rstrip(" \t.")
            if is_windows_reserved_device_name(name):
                name = "_" + name
            if len(name.encode("utf-8")) > max_len:
                name = truncate_utf8(name, max_len).rstrip(" \t.")
            return name

        for e in acc_entries:
            e.candidate_name = format_acc(e)

        for i in range(len(acc_entries)):
            if not acc_entries[i].needs_suffix and acc_entries[i].candidate_name.lower() in game_map:
                acc_entries[i].needs_suffix = True
                acc_entries[i].candidate_name = format_acc(acc_entries[i])
            for j in range(i + 1, len(acc_entries)):
                if acc_entries[i].candidate_name.lower() == acc_entries[j].candidate_name.lower():
                    if not acc_entries[i].needs_suffix:
                        acc_entries[i].needs_suffix = True
                        acc_entries[i].candidate_name = format_acc(acc_entries[i])
                    if not acc_entries[j].needs_suffix:
                        acc_entries[j].needs_suffix = True
                        acc_entries[j].candidate_name = format_acc(acc_entries[j])

        for e in acc_entries:
            name = e.candidate_name
            if name.lower() in game_map:
                name = disambiguate_final_name(game_map, e.base, e.r, max_len)
            game_map[name.lower()] = (name, e.r)

        tree[game_name] = {act_name: r for act_name, r in game_map.values()}

    if temporary_records:
        temporary_records = sorted(temporary_records, key=lambda r: r.identity_tuple)
        deduped_temp = []
        for r in temporary_records:
            if not deduped_temp or not is_same_save_record(deduped_temp[-1], r):
                deduped_temp.append(r)

        temp_map = {}
        for r in deduped_temp:
            id_val = r.application_id if r.application_id != 0 else r.save_data_id
            base = f"{id_val:016X}"
            name = base
            if name.lower() in temp_map:
                name = f"{base} [{r.save_data_id:016X}]"
            if name.lower() in temp_map:
                name = disambiguate_final_name(temp_map, base, r, max_len)
            temp_map[name.lower()] = (name, r)

        tree["Temporary"] = {act_name: r for act_name, r in temp_map.values()}

    if system_records:
        system_records = sorted(system_records, key=lambda r: r.identity_tuple)
        deduped_sys = []
        for r in system_records:
            if not deduped_sys or not is_same_save_record(deduped_sys[-1], r):
                deduped_sys.append(r)

        sys_map = {}
        for r in deduped_sys:
            base = f"System [{r.system_save_data_id:016X}]"
            name = base
            if name.lower() in sys_map:
                name = f"{base} [{r.save_data_id:016X}]"
            if name.lower() in sys_map:
                name = disambiguate_final_name(sys_map, base, r, max_len)
            sys_map[name.lower()] = (name, r)

        tree["System"] = {act_name: r for act_name, r in sys_map.values()}

    if system_bcat_records:
        system_bcat_records = sorted(system_bcat_records, key=lambda r: r.identity_tuple)
        deduped_bcat = []
        for r in system_bcat_records:
            if not deduped_bcat or not is_same_save_record(deduped_bcat[-1], r):
                deduped_bcat.append(r)

        bcat_map = {}
        for r in deduped_bcat:
            base = f"System BCAT [{r.system_save_data_id:016X}]"
            name = base
            if name.lower() in bcat_map:
                name = f"{base} [{r.save_data_id:016X}]"
            if name.lower() in bcat_map:
                name = disambiguate_final_name(bcat_map, base, r, max_len)
            bcat_map[name.lower()] = (name, r)

        tree["System BCAT"] = {act_name: r for act_name, r in bcat_map.values()}

    return tree

# ==============================================================================
# Behavioral Model for Fail-Closed Read-Only Proxy Operations
# ==============================================================================

FS_SUCCESS = 0
FS_ERROR_NOT_IMPLEMENTED = 0x202
FS_ERROR_PATH_NOT_FOUND = 0x203
FS_ERROR_MOUNT_FAILED = 0x1234

FS_ENTRY_TYPE_FILE = 1
FS_ENTRY_TYPE_DIR = 2

FS_OPEN_READ = 1
FS_OPEN_WRITE = 2
FS_OPEN_APPEND = 4

class SyntheticNativeFs:
    def __init__(self, key, total_space=100*1024*1024, free_space=50*1024*1024):
        self.key = key
        self.total_space = total_space
        self.free_space = free_space
        self.files = {
            "/slot0/savedata.bin": bytearray(b"original_save_data_bytes_12345"),
            "/slot0/metadata.json": bytearray(b'{"version": 1}'),
        }
        self.dirs = {"/", "/slot0"}
        self.side_effects = 0
        self.commits = 0

    def create_file(self, path, size):
        self.side_effects += 1
        self.files[path] = bytearray(size)

    def write(self, path, off, buf):
        self.side_effects += 1
        f = self.files[path]
        if off + len(buf) > len(f):
            f.extend(b'\0' * (off + len(buf) - len(f)))
        f[off:off+len(buf)] = buf

    def set_size(self, path, size):
        self.side_effects += 1
        f = self.files[path]
        if size < len(f):
            self.files[path] = f[:size]
        else:
            f.extend(b'\0' * (size - len(f)))

    def delete_file(self, path):
        self.side_effects += 1
        self.files.pop(path, None)

    def rename_file(self, old_path, new_path):
        self.side_effects += 1
        if old_path in self.files:
            self.files[new_path] = self.files.pop(old_path)

    def create_directory(self, path):
        self.side_effects += 1
        self.dirs.add(path)

    def delete_directory_recursively(self, path):
        self.side_effects += 1
        self.dirs = {d for d in self.dirs if not d.startswith(path)}
        self.files = {f: v for f, v in self.files.items() if not f.startswith(path)}

    def rename_directory(self, old_path, new_path):
        self.side_effects += 1
        if old_path in self.dirs:
            self.dirs.remove(old_path)
            self.dirs.add(new_path)

    def commit(self):
        self.commits += 1

class SyntheticFsSaveProxy:
    """
    Connected behavioral model of FsSaveProxy enforcing the read-only contract.
    Exercises mutation rejection before parsing/mounting, read-only OpenFile,
    directory enumeration, non-committing CloseFile, LRU cache eviction,
    exact retained field routing, and mount error propagation.
    """
    MOUNT_CACHE_MAX = 4

    def __init__(self, tree, mount_errors=None, remap_candidates=None):
        # tree: top_level_dir -> {sub_dir -> SyntheticSaveInfo}
        self.tree = tree
        self.mount_errors = mount_errors or {}  # key -> int error code
        self.remap_candidates = remap_candidates or {}
        self.remap_attempts = []
        self.remap_candidate_uses = 0
        self.mount_count = 0
        self.mount_tick = 0
        self.mounts = {}  # key -> {"fs": SyntheticNativeFs, "tick": int}
        # attempted_routes records every route attempt (key, space_id, type, app_id, sys_id, uid, rank, index, save_id, read_only)
        # including attempts that subsequently fail due to backend mount_errors.
        self.attempted_routes = []
        self.routed_tuples = self.attempted_routes  # alias for backwards compatibility

    def parse(self, path):
        parts = [p for p in path.split("/") if p]
        if not parts:
            return {"depth": 0, "game": "", "type": "", "rest": "/"}
        elif len(parts) == 1:
            return {"depth": 1, "game": parts[0], "type": "", "rest": "/"}
        elif len(parts) == 2:
            return {"depth": 2, "game": parts[0], "type": parts[1], "rest": "/"}
        else:
            return {"depth": 3, "game": parts[0], "type": parts[1], "rest": "/" + "/".join(parts[2:])}

    def mount_save(self, pp):
        game_entry = None
        game_canonical = None
        for g_name, g_map in self.tree.items():
            if g_name.lower() == pp["game"].lower():
                game_entry = g_map
                game_canonical = g_name
                break
        if game_entry is None:
            return FS_ERROR_PATH_NOT_FOUND, None

        type_info = None
        type_canonical = None
        for t_name, t_info in game_entry.items():
            if t_name.lower() == pp["type"].lower():
                type_info = t_info
                type_canonical = t_name
                break
        if type_info is None:
            return FS_ERROR_PATH_NOT_FOUND, None

        key = game_canonical + "/" + type_canonical

        # Check LRU cache: if already mounted, bump tick and return existing native fs
        # without recording a new mount attempt.
        self.mount_tick += 1
        if key in self.mounts:
            self.mounts[key]["tick"] = self.mount_tick
            return FS_SUCCESS, self.mounts[key]["fs"]

        # Record exact attempted route tuple BEFORE checking backend open result / mount error.
        # This models FsNativeSave construction with immutable stored record fields.
        route_tuple = (
            key,
            type_info.save_data_space_id,
            type_info.save_data_type,
            type_info.application_id,
            type_info.system_save_data_id,
            type_info.uid,
            type_info.save_data_rank,
            type_info.save_data_index,
            type_info.save_data_id,
            True,  # read_only = true
        )
        self.attempted_routes.append(route_tuple)

        # If backend mount fails, return the injected mount error immediately without caching.
        if key in self.mount_errors:
            return self.mount_errors[key], None

        fs = SyntheticNativeFs(key)
        self.mount_count += 1

        if len(self.mounts) >= self.MOUNT_CACHE_MAX:
            lru_key = min(self.mounts.keys(), key=lambda k: self.mounts[k]["tick"])
            del self.mounts[lru_key]

        self.mounts[key] = {"fs": fs, "tick": self.mount_tick}
        return FS_SUCCESS, fs

    # Every mutation callback rejects before Parse, MountSave, or filesystem side effects
    def create_file(self, path, size, option=0):
        return FS_ERROR_NOT_IMPLEMENTED

    def delete_file(self, path):
        return FS_ERROR_NOT_IMPLEMENTED

    def rename_file(self, old_path, new_path):
        return FS_ERROR_NOT_IMPLEMENTED

    def create_directory(self, path):
        return FS_ERROR_NOT_IMPLEMENTED

    def delete_directory_recursively(self, path):
        return FS_ERROR_NOT_IMPLEMENTED

    def rename_directory(self, old_path, new_path):
        return FS_ERROR_NOT_IMPLEMENTED

    def set_file_size(self, file_handle, size):
        return FS_ERROR_NOT_IMPLEMENTED

    def write_file(self, file_handle, off, buf, option=0):
        return FS_ERROR_NOT_IMPLEMENTED

    def open_file(self, path, mode):
        # Fail-closed rejection before Parse or MountSave
        if mode & (FS_OPEN_WRITE | FS_OPEN_APPEND):
            return FS_ERROR_NOT_IMPLEMENTED, None

        pp = self.parse(path)
        if pp["depth"] < 3:
            return FS_ERROR_PATH_NOT_FOUND, None

        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, None

        if pp["rest"] not in fs.files:
            return FS_ERROR_PATH_NOT_FOUND, None

        handle = {"fs": fs, "path": pp["rest"], "closed": False}
        return FS_SUCCESS, handle

    def get_file_size(self, file_handle):
        return len(file_handle["fs"].files[file_handle["path"]])

    def read_file(self, file_handle, off, size):
        f = file_handle["fs"].files[file_handle["path"]]
        return bytes(f[off:off+size])

    def close_file(self, file_handle):
        file_handle["closed"] = True
        # Read-only CloseFile never calls commit!

    def open_directory(self, path, mode=3):
        pp = self.parse(path)
        if pp["depth"] < 2:
            if pp["depth"] == 1:
                game_entry = None
                for g_name, g_map in self.tree.items():
                    if g_name.lower() == pp["game"].lower():
                        game_entry = g_map
                        break
                if game_entry is None:
                    return FS_ERROR_PATH_NOT_FOUND, None
                return FS_SUCCESS, {"is_virtual": True, "entries": list(game_entry.keys())}
            else:
                return FS_SUCCESS, {"is_virtual": True, "entries": list(self.tree.keys())}
        else:
            rc, fs = self.mount_save(pp)
            if rc != FS_SUCCESS:
                return rc, None
            prefix = pp["rest"]
            if not prefix.endswith("/"):
                prefix += "/"
            ents = []
            for d in fs.dirs:
                if d != "/" and d.startswith(prefix):
                    rel = d[len(prefix):].split("/")[0]
                    if rel and rel not in ents:
                        ents.append(rel)
            for f in fs.files:
                if f.startswith(prefix):
                    rel = f[len(prefix):].split("/")[0]
                    if rel and rel not in ents:
                        ents.append(rel)
            return FS_SUCCESS, {"is_virtual": False, "entries": ents}

    def get_total_space(self, path):
        pp = self.parse(path)
        if pp["depth"] < 2:
            return FS_SUCCESS, 0
        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, 0
        return FS_SUCCESS, fs.total_space

    def get_free_space(self, path):
        pp = self.parse(path)
        if pp["depth"] < 2:
            return FS_SUCCESS, 0
        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, 0
        return FS_SUCCESS, fs.free_space

    def get_entry_type(self, path):
        pp = self.parse(path)
        if pp["depth"] < 2:
            return FS_SUCCESS, FS_ENTRY_TYPE_DIR
        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, None
        if pp["rest"] in fs.dirs:
            return FS_SUCCESS, FS_ENTRY_TYPE_DIR
        elif pp["rest"] in fs.files:
            return FS_SUCCESS, FS_ENTRY_TYPE_FILE
        else:
            return FS_ERROR_PATH_NOT_FOUND, None

def test_behavioral_model():
    # =========================================================================
    # Part 1: Algorithmic Invariance & Name Allocation Regressions
    # =========================================================================
    # Case 1: Permutation invariance & literal "Player [save-id]" collision
    acc1 = SyntheticSaveInfo(save_data_id=0x10, space_id=1, save_type=1, app_id=0x1000, uid=(1, 0))
    acc2 = SyntheticSaveInfo(save_data_id=0x20, space_id=1, save_type=1, app_id=0x1000, uid=(2, 0))
    acc3 = SyntheticSaveInfo(save_data_id=0x30, space_id=1, save_type=1, app_id=0x1000, uid=(3, 0))

    accounts_dict = {
        (1, 0): "Player",
        (2, 0): "Player",
        (3, 0): "Player [0000000000000010]",  # Literal nickname equals acc1's generated suffixed name
    }

    test_records = [acc1, acc2, acc3]
    baseline = allocate_save_names(test_records, accounts_dict)

    check(len(baseline) == 3, "All 3 distinct account records must be retained")
    check(len(set(baseline.values())) == 3, "All 3 visible names must be distinct")

    for perm in itertools.permutations(test_records):
        mapping = allocate_save_names(list(perm), accounts_dict)
        check(mapping == baseline, f"Permutation {perm} produced different mapping than baseline!")

    # Case 2: Permutation invariance for multiple distinct records sharing a non-account bucket
    dev1 = SyntheticSaveInfo(save_data_id=0x01, space_id=1, save_type=3, app_id=0x1000, uid=(0, 0))
    dev2 = SyntheticSaveInfo(save_data_id=0x02, space_id=1, save_type=3, app_id=0x1000, uid=(0, 0))
    dev_records = [dev1, dev2]
    dev_base = allocate_save_names(dev_records, {})
    check(len(dev_base) == 2, "Both distinct device records must be retained")
    check(dev_base[dev1.identity_tuple] == "Device", "First sorted device record gets unsuffixed Device")
    check(dev_base[dev2.identity_tuple] == "Device [0000000000000002]", "Second device record gets suffixed Device")

    for perm in itertools.permutations(dev_records):
        mapping = allocate_save_names(list(perm), {})
        check(mapping == dev_base, f"Device permutation {perm} produced non-deterministic names")

    # Case 3: Identical record duplicates
    dup_records = [dev1, dev1, acc1, acc1, acc2]
    dup_res = allocate_save_names(dup_records, accounts_dict)
    check(len(dup_res) == 3, "Identical records must be deduplicated by record identity")
    check(dup_res[dev1.identity_tuple] == "Device", "Deduplicated device record retained correctly")

    # Case 4: Long CON.<text> game name at capacity
    cyrillic_long = "CON.Гра_" + ("Пригоди_" * 35)
    game_dir = format_save_game_dir_name(cyrillic_long, 0x0100000000010000, max_len=255)
    game_bytes = game_dir.encode("utf-8")

    check(len(game_bytes) <= 255, f"Game dir name exceeds 255 bytes limit: {len(game_bytes)}")
    check(game_dir.startswith("_CON."), f"Windows reserved stem CON must be prefixed with _: {game_dir[:10]}")
    check(game_dir.endswith(" [0100000000010000]"), f"Stable Title ID suffix must be intact: {game_dir[-25:]}")

    try:
        decoded = game_bytes.decode("utf-8")
        check(decoded == game_dir, "Decoded UTF-8 does not match original string")
    except UnicodeDecodeError:
        check(False, "Game dir name has invalid UTF-8 split")

    enumerated = game_dir[:255]
    check(enumerated == game_dir, "MakeVirtualDirEntry would truncate stored game directory name")

    # Case 5: Nickname collisions with non-account buckets and Unicode preservation
    acc_dev = SyntheticSaveInfo(save_data_id=0x99, space_id=1, save_type=1, app_id=0x2000, uid=(9, 0))
    acc_cyr = SyntheticSaveInfo(save_data_id=0xAA, space_id=1, save_type=1, app_id=0x2000, uid=(10, 0))
    mixed_dict = {
        (9, 0): "Device",
        (10, 0): "Користувач",
    }
    mixed_res = allocate_save_names([acc_dev, acc_cyr], mixed_dict)
    check(mixed_res[acc_dev.identity_tuple] == "Device [0000000000000099]",
          "Account nicknamed 'Device' must be suffixed to not collide with Device bucket")
    check(mixed_res[acc_cyr.identity_tuple] == "Користувач",
          "Unicode Cyrillic nickname must be preserved without distortion")

    # =========================================================================
    # Part 2: All Seven Types Across All Concrete Spaces & Disambiguation
    # =========================================================================
    # Concrete spaces:
    # 0 = System, 1 = User, 2 = SdSystem, 3 = Temporary, 4 = SdUser, 100 = ProperSystem, 101 = SafeMode
    # Save types:
    # 0 = System, 1 = Account, 2 = BCAT, 3 = Device, 4 = Temporary, 5 = Cache, 6 = SystemBcat

    rec_sys1 = SyntheticSaveInfo(save_data_id=0x1001, space_id=0, save_type=0, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000001)
    rec_sys2 = SyntheticSaveInfo(save_data_id=0x1002, space_id=100, save_type=0, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000001) # duplicate sys_id
    rec_sys3 = SyntheticSaveInfo(save_data_id=0x1003, space_id=101, save_type=0, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000003)
    rec_acc_alice = SyntheticSaveInfo(save_data_id=0x2001, space_id=1, save_type=1, app_id=0x0100000000001000, uid=(0x1111, 0x2222))
    rec_acc_bob = SyntheticSaveInfo(save_data_id=0x2002, space_id=4, save_type=1, app_id=0x0100000000001000, uid=(0x3333, 0x4444))
    rec_bcat = SyntheticSaveInfo(save_data_id=0x3001, space_id=1, save_type=2, app_id=0x0100000000001000, uid=(0, 0))
    rec_dev_part2 = SyntheticSaveInfo(save_data_id=0x4001, space_id=1, save_type=3, app_id=0x0100000000001000, uid=(0, 0))
    rec_temp_app = SyntheticSaveInfo(save_data_id=0x5001, space_id=3, save_type=4, app_id=0x0100000000005000, uid=(0, 0))
    rec_temp_noapp = SyntheticSaveInfo(save_data_id=0x5002, space_id=3, save_type=4, app_id=0, uid=(0, 0)) # fallback to save_data_id
    rec_cache0 = SyntheticSaveInfo(save_data_id=0x6001, space_id=1, save_type=5, app_id=0x0100000000001000, uid=(0, 0), index=0)
    rec_cache1 = SyntheticSaveInfo(save_data_id=0x6002, space_id=4, save_type=5, app_id=0x0100000000001000, uid=(0, 0), index=1)
    rec_cache1_dup = SyntheticSaveInfo(save_data_id=0x6003, space_id=4, save_type=5, app_id=0x0100000000001000, uid=(0, 0), index=1) # duplicate index
    rec_sysbcat1 = SyntheticSaveInfo(save_data_id=0x7001, space_id=0, save_type=6, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000010)
    rec_sysbcat2 = SyntheticSaveInfo(save_data_id=0x7002, space_id=2, save_type=6, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000010) # duplicate sys_id
    rec_unknown = SyntheticSaveInfo(save_data_id=0x9999, space_id=1, save_type=99, app_id=0x0100000000009999, uid=(0, 0))

    all_seven_records = [
        rec_sys1, rec_sys2, rec_sys3,
        rec_acc_alice, rec_acc_bob,
        rec_bcat, rec_dev_part2,
        rec_temp_app, rec_temp_noapp,
        rec_cache0, rec_cache1, rec_cache1_dup,
        rec_sysbcat1, rec_sysbcat2,
        rec_unknown,
    ]

    accounts_p2 = {
        (0x1111, 0x2222): "Alice",
        (0x3333, 0x4444): "Bob",
    }
    title_p2 = {
        0x0100000000001000: "Super Game",
    }

    full_tree = build_mtp_save_tree(all_seven_records, accounts_p2, title_p2)

    # Unknown type must be discarded
    check("Super Game [0100000000009999]" not in full_tree and "[0100000000009999]" not in full_tree,
          "Unknown save type 99 must not create a directory")

    # Top level explicit bucket presence
    check("System" in full_tree, "Top-level 'System' bucket must exist")
    check("System BCAT" in full_tree, "Top-level 'System BCAT' bucket must exist")
    check("Temporary" in full_tree, "Top-level 'Temporary' bucket must exist")
    check("Super Game [0100000000001000]" in full_tree, "Game directory must exist")

    # Verify System naming & disambiguation
    sys_entries = full_tree["System"]
    check("System [0100000000000001]" in sys_entries, "First System save gets unsuffixed name")
    check("System [0100000000000001] [0000000000001002]" in sys_entries,
          "Duplicate System save gets save_data_id suffix disambiguation")
    check("System [0100000000000003]" in sys_entries, "System save 3 gets correct name")

    # Verify System BCAT naming & disambiguation
    bcat_sys_entries = full_tree["System BCAT"]
    check("System BCAT [0100000000000010]" in bcat_sys_entries, "First System BCAT gets unsuffixed name")
    check("System BCAT [0100000000000010] [0000000000007002]" in bcat_sys_entries,
          "Duplicate System BCAT gets save_data_id suffix disambiguation")

    # Verify Temporary naming
    temp_entries = full_tree["Temporary"]
    check("0100000000005000" in temp_entries, "Temporary save with application_id uses 16-hex application_id")
    check("0000000000005002" in temp_entries, "Temporary save without application_id uses 16-hex save_data_id")

    # Verify Game entries: Account, BCAT, Device, Cache (index 0, 1, and duplicate 1)
    game_entries = full_tree["Super Game [0100000000001000]"]
    check("Alice" in game_entries, "Account save Alice present")
    check("Bob" in game_entries, "Account save Bob present")
    check("BCAT" in game_entries, "BCAT save present")
    check("Device" in game_entries, "Device save present")
    check("Cache" in game_entries, "Cache index 0 gets unsuffixed 'Cache'")
    check("Cache 1" in game_entries, "First Cache index 1 gets 'Cache 1'")
    check("Cache 1 [0000000000006003]" in game_entries, "Duplicate Cache index 1 gets suffixed disambiguation")

    # Verify permutation independence across all 7 types with explicit deterministic orderings
    test_permutations = [
        all_seven_records,
        list(reversed(all_seven_records)),
        all_seven_records[4:] + all_seven_records[:4],
        all_seven_records[8:] + all_seven_records[:8],
        sorted(all_seven_records, key=lambda r: (r.save_data_id * 1103515245 + 12345) % (2**31)),
    ]
    for perm_idx, reordered in enumerate(test_permutations):
        shuffled_tree = build_mtp_save_tree(reordered, accounts_p2, title_p2)
        check(list(shuffled_tree.keys()) == list(full_tree.keys()),
              f"Top level keys must match regardless of input order (permutation {perm_idx})")
        for k in full_tree:
            check(list(shuffled_tree[k].keys()) == list(full_tree[k].keys()),
                  f"Subtree keys for {k} must match regardless of input order (permutation {perm_idx})")

    # Verify NO Account UID exposure anywhere in the visible tree
    for top_k, sub_tree in full_tree.items():
        for sub_k in sub_tree:
            check("1111" not in top_k and "2222" not in top_k and "3333" not in top_k and "4444" not in top_k,
                  "Account UID must not appear in top-level directory names")
            check("1111" not in sub_k and "2222" not in sub_k and "3333" not in sub_k and "4444" not in sub_k,
                  "Account UID must not appear in save directory names")

    # Verify exact retained routing assertions for every visible node
    p2_proxy = SyntheticFsSaveProxy(full_tree)
    for top_k, sub_tree in full_tree.items():
        for sub_k, info in sub_tree.items():
            rc, _ = p2_proxy.open_directory(f"/{top_k}/{sub_k}")
            check(rc == FS_SUCCESS, f"open_directory failed on /{top_k}/{sub_k}")
            # Find the last routed tuple
            last_route = p2_proxy.routed_tuples[-1]
            key_exp = f"{top_k}/{sub_k}"
            check(last_route[0] == key_exp, f"Routed key mismatch: {last_route[0]} vs {key_exp}")
            check(last_route[1] == info.save_data_space_id, "Routed space_id must match exact retained info")
            check(last_route[2] == info.save_data_type, "Routed save_type must match exact retained info")
            check(last_route[3] == info.application_id, "Routed application_id must match exact retained info")
            check(last_route[4] == info.system_save_data_id, "Routed system_save_data_id must match exact retained info")
            check(last_route[5] == info.uid, "Routed uid must match exact retained info")
            check(last_route[6] == info.save_data_rank, "Routed save_data_rank must match exact retained info")
            check(last_route[7] == info.save_data_index, "Routed save_data_index must match exact retained info")
            check(last_route[8] == info.save_data_id, "Routed save_data_id must match exact retained info")
            check(last_route[9] is True, "Routed read_only must strictly be True")

    # -------------------------------------------------------------------------
    # System Full Disambiguator Collision Matrix (forcing DisambiguateFinalName)
    # -------------------------------------------------------------------------
    # At least three distinct retained System records with:
    # - the same system_save_data_id (0x01000000000000AA)
    # - the same save_data_id (0x5555) for the records needed to collide at base + [save_data_id]
    # - distinct actual spaces, ranks, and indexes
    col_sys1 = SyntheticSaveInfo(save_data_id=0x5555, space_id=0, save_type=0, app_id=0, uid=(0, 0), rank=0, index=0, sys_save_id=0x01000000000000AA)
    col_sys2 = SyntheticSaveInfo(save_data_id=0x5555, space_id=100, save_type=0, app_id=0, uid=(0, 0), rank=0, index=1, sys_save_id=0x01000000000000AA)
    col_sys3 = SyntheticSaveInfo(save_data_id=0x5555, space_id=101, save_type=0, app_id=0, uid=(0, 0), rank=1, index=2, sys_save_id=0x01000000000000AA)

    expected_name1 = "System [01000000000000AA]"
    expected_name2 = "System [01000000000000AA] [0000000000005555]"
    expected_name3 = "System [01000000000000AA] [0000000000005555-s101-t0-r1-i2]"

    # Test explicit orderings: original, reversed, rotated, collision records swapped
    sys_orderings = [
        [col_sys1, col_sys2, col_sys3],  # original
        [col_sys3, col_sys2, col_sys1],  # reversed
        [col_sys2, col_sys3, col_sys1],  # rotated
        [col_sys1, col_sys3, col_sys2],  # collision records swapped
        [col_sys2, col_sys1, col_sys3],
        [col_sys3, col_sys1, col_sys2],
    ]

    tree_col = None
    for ord_idx, perm in enumerate(sys_orderings):
        tree_col = build_mtp_save_tree(perm, {})
        check("System" in tree_col, "System bucket must exist")
        s_entries = tree_col["System"]

        # Every distinct record remains visible
        check(len(s_entries) == 3, f"All 3 colliding records must remain visible (ordering {ord_idx})")

        # All visible names are unique case-insensitively
        lower_names = [name.lower() for name in s_entries.keys()]
        check(len(lower_names) == len(set(lower_names)), f"All visible names must be case-insensitively unique (ordering {ord_idx})")

        # One record gets the base name
        check(expected_name1 in s_entries, f"Record 1 must get exact base name: {expected_name1}")
        check(s_entries[expected_name1] is col_sys1, "Base name must map to col_sys1")

        # One record gets the save-ID suffix
        check(expected_name2 in s_entries, f"Record 2 must get save-ID suffix: {expected_name2}")
        check(s_entries[expected_name2] is col_sys2, "Save-ID suffixed name must map to col_sys2")

        # The next colliding record gets the full deterministic suffix containing exact s<space>-t<type>-r<rank>-i<index>
        check(expected_name3 in s_entries, f"Record 3 must force DisambiguateFinalName with full suffix: {expected_name3}")
        check(s_entries[expected_name3] is col_sys3, "Full disambiguated name must map to col_sys3")

        # No UID value appears in visible names
        for vname in s_entries.keys():
            check("uid" not in vname.lower(), "UID must not appear in visible name")

    # Routing verification for third-stage disambiguated record:
    col_proxy = SyntheticFsSaveProxy(tree_col)
    rc, _ = col_proxy.open_directory(f"/System/{expected_name3}")
    check(rc == FS_SUCCESS, "open_directory must succeed on full disambiguated path")
    check(len(col_proxy.attempted_routes) == 1, "Exactly one route attempt must be made")
    routed_col3 = col_proxy.attempted_routes[0]
    check(routed_col3[0] == f"System/{expected_name3}", "Route key mismatch")
    check(routed_col3[1] == 101, "Space 101 (SafeMode) must survive exact routing")
    check(routed_col3[2] == 0, "Type 0 (System) must survive exact routing")
    check(routed_col3[4] == 0x01000000000000AA, "System save data ID must survive exact routing")
    check(routed_col3[6] == 1, "Rank 1 difference must survive exact routing")
    check(routed_col3[7] == 2, "Index 2 difference must survive exact routing")
    check(routed_col3[8] == 0x5555, "Save data ID must survive exact routing")
    check(routed_col3[9] is True, "read_only=True must survive exact routing")

    # Narrowly scoped test for System BCAT full disambiguator
    col_bcat1 = SyntheticSaveInfo(save_data_id=0x7777, space_id=0, save_type=6, app_id=0, uid=(0, 0), rank=0, index=0, sys_save_id=0x01000000000000BB)
    col_bcat2 = SyntheticSaveInfo(save_data_id=0x7777, space_id=2, save_type=6, app_id=0, uid=(0, 0), rank=0, index=1, sys_save_id=0x01000000000000BB)
    col_bcat3 = SyntheticSaveInfo(save_data_id=0x7777, space_id=2, save_type=6, app_id=0, uid=(0, 0), rank=1, index=2, sys_save_id=0x01000000000000BB)

    expected_bcat1 = "System BCAT [01000000000000BB]"
    expected_bcat2 = "System BCAT [01000000000000BB] [0000000000007777]"
    expected_bcat3 = "System BCAT [01000000000000BB] [0000000000007777-s2-t6-r1-i2]"

    tree_bcat_col = build_mtp_save_tree([col_bcat3, col_bcat1, col_bcat2], {})
    sb_entries = tree_bcat_col["System BCAT"]
    check(len(sb_entries) == 3, "All 3 System BCAT records visible")
    check(expected_bcat1 in sb_entries and sb_entries[expected_bcat1] is col_bcat1, "System BCAT base name correct")
    check(expected_bcat2 in sb_entries and sb_entries[expected_bcat2] is col_bcat2, "System BCAT save-ID suffix correct")
    check(expected_bcat3 in sb_entries and sb_entries[expected_bcat3] is col_bcat3, "System BCAT full disambiguator suffix correct")

    col_bcat_proxy = SyntheticFsSaveProxy(tree_bcat_col)
    rc, _ = col_bcat_proxy.open_directory(f"/System BCAT/{expected_bcat3}")
    check(rc == FS_SUCCESS, "open_directory must succeed on System BCAT full disambiguated path")
    routed_bcat3 = col_bcat_proxy.attempted_routes[0]
    check(routed_bcat3[1] == 2 and routed_bcat3[6] == 1 and routed_bcat3[7] == 2 and routed_bcat3[9] is True,
          "System BCAT rank/index/space/read_only must survive routing")

    # =========================================================================
    # Part 3: Stale Paths, Alternate-Space Fallback Refusal, Presence & Mount Errors
    # =========================================================================
    # 1. Stale paths return FS_ERROR_PATH_NOT_FOUND
    rc, _ = p2_proxy.open_directory("/NonExistentGame")
    check(rc == FS_ERROR_PATH_NOT_FOUND, "open_directory on stale level 1 must return FS_ERROR_PATH_NOT_FOUND")
    rc, _ = p2_proxy.open_file("/NonExistentGame", FS_OPEN_READ)
    check(rc == FS_ERROR_PATH_NOT_FOUND, "open_file on stale level 1 must return FS_ERROR_PATH_NOT_FOUND")

    stale_paths_lvl2 = [
        "/NonExistentGame/slot0",
        "/Super Game [0100000000001000]/NonExistentBucket",
        "/System/NonExistentSystemSave",
        "/Temporary/NonExistentTempSave",
        "/System BCAT/NonExistentSystemBcatSave",
    ]
    for sp in stale_paths_lvl2:
        rc, _ = p2_proxy.open_directory(sp)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"open_directory on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.open_file(f"{sp}/slot0/savedata.bin", FS_OPEN_READ)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"open_file on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.get_total_space(sp)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"get_total_space on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.get_free_space(sp)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"get_free_space on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.get_entry_type(f"{sp}/slot0/savedata.bin")
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"get_entry_type on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")

    # 2. Stale/remapped backend situation & alternate-space fallback refusal
    # Bob is stored with actual space 4 (SdUser).
    # A tempting replacement record with the same identity is available in User space (space 1).
    # The original retained-space mount attempt fails with injected real error.
    bob_key = "Super Game [0100000000001000]/Bob"
    alt_bob_user = SyntheticSaveInfo(
        save_data_id=0x2003,
        space_id=1,
        save_type=1,
        app_id=rec_acc_bob.application_id,
        uid=rec_acc_bob.uid,
    )
    mount_err_proxy = SyntheticFsSaveProxy(
        full_tree,
        mount_errors={
            bob_key: FS_ERROR_MOUNT_FAILED,
            "System/System [0100000000000003]": FS_ERROR_MOUNT_FAILED,
        },
        remap_candidates={bob_key: alt_bob_user},
    )

    # Assert remap_candidates registered and observable on the proxy:
    check(bob_key in mount_err_proxy.remap_candidates, "remap_candidates must be registered and observable")
    check(mount_err_proxy.remap_candidates[bob_key] is alt_bob_user, "Registered remap candidate must be alt_bob_user")
    check(mount_err_proxy.remap_candidates[bob_key].save_data_space_id == 1, "Candidate must offer User space 1")

    # Prior to mount attempt, Bob was discovered and present in directory listing:
    rc, game_dir = mount_err_proxy.open_directory("/Super Game [0100000000001000]")
    check(rc == FS_SUCCESS and "Bob" in game_dir["entries"], "Bob must be visible in listing before mount attempt")
    check(len(mount_err_proxy.attempted_routes) == 0, "Directory enumeration must not attempt any mount")

    # Attempt to open Bob's save directory:
    rc, _ = mount_err_proxy.open_directory("/Super Game [0100000000001000]/Bob")
    check(rc == FS_ERROR_MOUNT_FAILED, "Mount error must be returned unchanged")

    # The model records exactly one attempted route:
    check(len(mount_err_proxy.attempted_routes) == 1, "Exactly one mount attempt must be recorded")
    bob_attempt = mount_err_proxy.attempted_routes[0]
    check(bob_attempt[0] == bob_key, "Attempt key must match Bob's path")
    check(bob_attempt[1] == 4, "Attempt MUST route to stored space 4 (SdUser)")
    check(bob_attempt[1] != 1, "Attempt MUST NOT fall back or redirect to space 1 (User)")
    check(bob_attempt[2] == 1, "Attempt must use stored save_data_type (Account)")
    check(bob_attempt[3] == rec_acc_bob.application_id, "Attempt must use stored application_id")
    check(bob_attempt[5] == rec_acc_bob.uid, "Attempt must use stored uid")
    check(bob_attempt[8] == rec_acc_bob.save_data_id, "Attempt must use stored save_data_id")
    check(bob_attempt[9] is True, "Attempt must strictly use read_only=True")

    # Assert no attempted tuple has space_id == 1:
    check(all(r[1] != 1 for r in mount_err_proxy.attempted_routes), "No attempted route may use candidate space_id=1")

    # Assert remap candidates were never probed or used:
    check(len(mount_err_proxy.remap_attempts) == 0, "remap_attempts must remain empty")
    check(mount_err_proxy.remap_candidate_uses == 0, "remap_candidate_uses must remain 0")

    # Verify no second attempt occurred:
    check(len(mount_err_proxy.attempted_routes) == 1, "No second attempt or alternate-space retry must occur")

    # Verify tree is not rewritten or rescanned:
    check(full_tree["Super Game [0100000000001000]"]["Bob"] is rec_acc_bob, "Tree record must remain immutable")
    check(full_tree["Super Game [0100000000001000]"]["Bob"] is not alt_bob_user, "Tree must not be rewritten with alt_bob_user")
    check(full_tree["Super Game [0100000000001000]"]["Bob"].save_data_space_id == 4, "Stored space ID must remain 4 (SdUser)")

    # 3. Discovered directory presence before mount across root and level 1
    fresh_proxy = SyntheticFsSaveProxy(full_tree)
    rc, root_h = fresh_proxy.open_directory("/")
    check(rc == FS_SUCCESS and root_h["is_virtual"], "Root directory enumeration succeeds")
    check(set(root_h["entries"]) == set(full_tree.keys()), "All top-level buckets discovered")
    check(fresh_proxy.mount_count == 0, "No mount occurred for root directory enumeration")

    for top_k in full_tree:
        rc, top_h = fresh_proxy.open_directory(f"/{top_k}")
        check(rc == FS_SUCCESS and top_h["is_virtual"], f"Level 1 directory /{top_k} enumeration succeeds")
        check(set(top_h["entries"]) == set(full_tree[top_k].keys()), f"All sub-entries in /{top_k} discovered")
        check(fresh_proxy.mount_count == 0, f"No mount occurred for /{top_k} directory enumeration")

    # 4. Mount error propagation across all 5 accessors for System save
    # rec_sys3 has space_id = 101 (SafeMode), sys_save_id = 0x0100000000000003, save_data_id = 0x1003
    sys_err_proxy = SyntheticFsSaveProxy(full_tree, mount_errors={
        "System/System [0100000000000003]": FS_ERROR_MOUNT_FAILED,
    })
    err_path = "/System/System [0100000000000003]"
    expected_sys_route = (
        "System/System [0100000000000003]",
        101,  # SafeMode space_id
        0,    # FsSaveDataType_System
        0,    # application_id
        0x0100000000000003,  # system_save_data_id
        (0, 0),  # uid
        0,    # rank
        0,    # index
        0x1003,  # save_data_id
        True, # read_only
    )

    # 1. OpenFile:
    sys_err_proxy.attempted_routes.clear()
    rc, h = sys_err_proxy.open_file(f"{err_path}/slot0/savedata.bin", FS_OPEN_READ)
    check(rc == FS_ERROR_MOUNT_FAILED and h is None, "OpenFile must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "OpenFile must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "OpenFile must attempt exact stored System record without alternate space")

    # 2. OpenDirectory:
    sys_err_proxy.attempted_routes.clear()
    rc, h = sys_err_proxy.open_directory(f"{err_path}/slot0")
    check(rc == FS_ERROR_MOUNT_FAILED and h is None, "OpenDirectory must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "OpenDirectory must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "OpenDirectory must attempt exact stored System record without alternate space")

    # 3. GetEntryType:
    sys_err_proxy.attempted_routes.clear()
    rc, t = sys_err_proxy.get_entry_type(f"{err_path}/slot0/savedata.bin")
    check(rc == FS_ERROR_MOUNT_FAILED and t is None, "GetEntryType must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "GetEntryType must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "GetEntryType must attempt exact stored System record without alternate space")

    # 4. GetTotalSpace:
    sys_err_proxy.attempted_routes.clear()
    rc, total = sys_err_proxy.get_total_space(err_path)
    check(rc == FS_ERROR_MOUNT_FAILED and total == 0, "GetTotalSpace must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "GetTotalSpace must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "GetTotalSpace must attempt exact stored System record without alternate space")

    # 5. GetFreeSpace:
    sys_err_proxy.attempted_routes.clear()
    rc, free = sys_err_proxy.get_free_space(err_path)
    check(rc == FS_ERROR_MOUNT_FAILED and free == 0, "GetFreeSpace must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "GetFreeSpace must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "GetFreeSpace must attempt exact stored System record without alternate space")

    # =========================================================================
    # Part 4: Fail-Closed Mutation Matrix, Byte Integrity, LRU Eviction & Handle Lifetime
    # =========================================================================
    p4_proxy = SyntheticFsSaveProxy(full_tree)

    # 1. Fail-closed mutation matrix across game and top-level buckets
    target_mut_paths = [
        "/Super Game [0100000000001000]/Alice/slot0/savedata.bin",
        "/System/System [0100000000000001]/slot0/savedata.bin",
        "/Temporary/0100000000005000/slot0/savedata.bin",
        "/System BCAT/System BCAT [0100000000000010]/slot0/savedata.bin",
    ]
    for mp in target_mut_paths:
        check(p4_proxy.create_file(mp, 1024) == FS_ERROR_NOT_IMPLEMENTED, "create_file must reject with NotImplemented")
        check(p4_proxy.delete_file(mp) == FS_ERROR_NOT_IMPLEMENTED, "delete_file must reject with NotImplemented")
        check(p4_proxy.rename_file(mp, mp + ".bak") == FS_ERROR_NOT_IMPLEMENTED, "rename_file must reject with NotImplemented")
        dir_p = mp.rsplit("/", 1)[0]
        check(p4_proxy.create_directory(dir_p + "/newdir") == FS_ERROR_NOT_IMPLEMENTED, "create_directory must reject with NotImplemented")
        check(p4_proxy.delete_directory_recursively(dir_p) == FS_ERROR_NOT_IMPLEMENTED, "delete_directory_recursively must reject with NotImplemented")
        check(p4_proxy.rename_directory(dir_p, dir_p + "_bak") == FS_ERROR_NOT_IMPLEMENTED, "rename_directory must reject with NotImplemented")

        rc, h = p4_proxy.open_file(mp, FS_OPEN_WRITE)
        check(rc == FS_ERROR_NOT_IMPLEMENTED and h is None, "open_file WRITE must reject before mounting")
        rc, h = p4_proxy.open_file(mp, FS_OPEN_APPEND)
        check(rc == FS_ERROR_NOT_IMPLEMENTED and h is None, "open_file APPEND must reject before mounting")
        rc, h = p4_proxy.open_file(mp, FS_OPEN_READ | FS_OPEN_WRITE)
        check(rc == FS_ERROR_NOT_IMPLEMENTED and h is None, "open_file READ|WRITE must reject before mounting")

    check(p4_proxy.mount_count == 0, "Mutations must reject before mounting save")

    # 2. ReadFile byte integrity and CloseFile without commit
    alice_file_path = "/Super Game [0100000000001000]/Alice/slot0/savedata.bin"
    rc, alice_handle = p4_proxy.open_file(alice_file_path, FS_OPEN_READ)
    check(rc == FS_SUCCESS and alice_handle is not None, "open_file READ on Alice save must succeed")
    check(p4_proxy.mount_count == 1, "Save mounted once on read open")

    fs_alice = alice_handle["fs"]
    check(fs_alice.side_effects == 0 and fs_alice.commits == 0, "No side effects on read open")

    check(p4_proxy.write_file(alice_handle, 0, b"malicious") == FS_ERROR_NOT_IMPLEMENTED, "write_file rejected")
    check(p4_proxy.set_file_size(alice_handle, 0) == FS_ERROR_NOT_IMPLEMENTED, "set_file_size rejected")

    sz = p4_proxy.get_file_size(alice_handle)
    expected_data = b"original_save_data_bytes_12345"
    check(sz == len(expected_data), f"File size mismatch: {sz} vs {len(expected_data)}")
    data = p4_proxy.read_file(alice_handle, 0, sz)
    check(data == expected_data, "Read bytes must match unaltered content")

    # 3. LRU Eviction & Open Handle Lifetime (MOUNT_CACHE_MAX = 4)
    # Alice is in p4_proxy.mounts (1 item). Let's mount 4 additional distinct saves:
    distinct_saves = [
        "/Super Game [0100000000001000]/Bob/slot0",
        "/Super Game [0100000000001000]/BCAT/slot0",
        "/System/System [0100000000000001]/slot0",
        "/Temporary/0100000000005000/slot0",
    ]
    for s_path in distinct_saves:
        rc, _ = p4_proxy.open_directory(s_path)
        check(rc == FS_SUCCESS, f"open_directory failed on {s_path}")

    # Now 5 distinct mounts have been requested. Since MOUNT_CACHE_MAX = 4, Alice must have been evicted from cache
    alice_key = "Super Game [0100000000001000]/Alice"
    check(len(p4_proxy.mounts) == 4, f"Mount cache size must be capped at 4, got {len(p4_proxy.mounts)}")
    check(alice_key not in p4_proxy.mounts, "Alice mount must have been evicted from LRU cache")

    # Verify open file handle still accesses valid native fs and reads unaltered bytes
    data_after_evict = p4_proxy.read_file(alice_handle, 0, sz)
    check(data_after_evict == expected_data, "Read from open handle after LRU eviction must succeed unaltered")

    # CloseFile does not commit or alter data
    p4_proxy.close_file(alice_handle)
    check(alice_handle["closed"] is True, "CloseFile marks handle closed")
    check(fs_alice.commits == 0, "CloseFile must not call commit")
    check(fs_alice.side_effects == 0, "CloseFile must have 0 side effects")

    print("Synthetic behavioral model checks: ALL PASS (22 fail-closed, multi-type, and LRU groups)")

if __name__ == "__main__":
    test_source_contracts()
    test_behavioral_model()
    print("ALL MTP SAVE CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")
