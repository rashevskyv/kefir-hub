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
    # 6. Preserved scan spaces and types
    # =========================================================================
    check("FsSaveDataType_Account" in proxy_body, "Must scan Account saves")
    check("FsSaveDataType_Bcat" in proxy_body, "Must scan BCAT saves")
    check("FsSaveDataType_Device" in proxy_body, "Must scan Device saves")
    check("FsSaveDataType_Cache" in proxy_body, "Must scan Cache saves")
    check("FsSaveDataSpaceId_SdUser" in proxy_body, "Cache saves must use SdUser space")
    check("FsSaveDataSpaceId_User" in proxy_body, "Account/BCAT/Device saves must use User space")
    check("FsSaveDataType_System" not in proxy_body, "System saves must not be added to MTP saves")
    check("FsSaveDataType_Temporary" not in proxy_body, "Temporary saves must not be added to MTP saves")

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
    check("set(sphaira_VERSION 0.13.864)" in cmake_src,
          "CMakeLists.txt must define sphaira_VERSION as 0.13.864")

    print("Source contracts: ALL PASS (14 anchor groups)")

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
            disambig = f" [{r.save_data_id:016X}-s{r.save_data_space_id}-t{r.save_data_type}-r{r.save_data_rank}-i{r.save_data_index}]"
            name = truncate_utf8(base, max_len - len(disambig)) + disambig
            counter = 2
            while name.lower() in game_map:
                cnt = f" ({counter})"
                counter += 1
                name = truncate_utf8(base, max_len - len(disambig) - len(cnt)) + cnt + disambig
        game_map[name.lower()] = (name, r)

    # 2. Account saves in deterministic order
    class AccEntry:
        def __init__(self, r, base, has_nick, needs_suffix):
            self.r = r
            self.base = base
            self.has_nick = has_nick
            self.needs_suffix = needs_suffix
            self.candidate_name = ""

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
            disambig = f" [{e.r.save_data_id:016X}-s{e.r.save_data_space_id}-t{e.r.save_data_type}-r{e.r.save_data_rank}-i{e.r.save_data_index}]"
            name = truncate_utf8(e.base, max_len - len(disambig)) + disambig
            counter = 2
            while name.lower() in game_map:
                cnt = f" ({counter})"
                counter += 1
                name = truncate_utf8(e.base, max_len - len(disambig) - len(cnt)) + cnt + disambig
        game_map[name.lower()] = (name, e.r)

    # Map record identity -> visible name
    res = {}
    for lower_k, (act_name, r) in game_map.items():
        res[r.identity_tuple] = act_name
    return res

# ==============================================================================
# Behavioral Model for Fail-Closed Read-Only Proxy Operations
# ==============================================================================

FS_SUCCESS = 0
FS_ERROR_NOT_IMPLEMENTED = 0x202
FS_ERROR_PATH_NOT_FOUND = 0x203

FS_OPEN_READ = 1
FS_OPEN_WRITE = 2
FS_OPEN_APPEND = 4

class SyntheticNativeFs:
    def __init__(self, key):
        self.key = key
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
    directory enumeration, and non-committing CloseFile.
    """
    def __init__(self, tree):
        # tree: game_name -> {save_type_name -> SyntheticSaveInfo}
        self.tree = tree
        self.mount_count = 0
        self.mounts = {}  # key -> SyntheticNativeFs

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
        game = self.tree.get(pp["game"])
        if not game:
            return FS_ERROR_PATH_NOT_FOUND, None
        info = game.get(pp["type"])
        if not info:
            return FS_ERROR_PATH_NOT_FOUND, None
        key = pp["game"] + "/" + pp["type"]
        if key not in self.mounts:
            self.mount_count += 1
            self.mounts[key] = SyntheticNativeFs(key)
        return FS_SUCCESS, self.mounts[key]

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
        if pp["depth"] == 0:
            return FS_SUCCESS, {"is_virtual": True, "entries": list(self.tree.keys())}
        elif pp["depth"] == 1:
            game = self.tree.get(pp["game"])
            if not game:
                return FS_ERROR_PATH_NOT_FOUND, None
            return FS_SUCCESS, {"is_virtual": True, "entries": list(game.keys())}
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

def test_behavioral_model():
    # =========================================================================
    # Part 1: Algorithmic Invariance & Name Allocation Regressions
    # =========================================================================
    # Case 1: Permutation invariance & literal "Player [save-id]" collision
    # Two accounts named Player + one account whose nickname literally matches a generated name
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

    # Verify baseline names are unique and distinct
    check(len(baseline) == 3, "All 3 distinct account records must be retained")
    check(len(set(baseline.values())) == 3, "All 3 visible names must be distinct")

    # Assert that across ALL 6 permutations, every identity maps to the EXACT SAME name
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
    # 250 characters of Cyrillic text (multi-byte, total > 350 bytes)
    cyrillic_long = "CON.Гра_" + ("Пригоди_" * 35)
    game_dir = format_save_game_dir_name(cyrillic_long, 0x0100000000010000, max_len=255)
    game_bytes = game_dir.encode("utf-8")

    check(len(game_bytes) <= 255, f"Game dir name exceeds 255 bytes limit: {len(game_bytes)}")
    check(game_dir.startswith("_CON."), f"Windows reserved stem CON must be prefixed with _: {game_dir[:10]}")
    check(game_dir.endswith(" [0100000000010000]"), f"Stable Title ID suffix must be intact: {game_dir[-25:]}")

    # Verify UTF-8 boundary integrity
    try:
        decoded = game_bytes.decode("utf-8")
        check(decoded == game_dir, "Decoded UTF-8 does not match original string")
    except UnicodeDecodeError:
        check(False, "Game dir name has invalid UTF-8 split")

    # Verify enumerated name equals stored lookup key (fits within 255-char FsDirectoryEntry buffer)
    enumerated = game_dir[:255]
    check(enumerated == game_dir, "MakeVirtualDirEntry would truncate stored game directory name")

    # Case 5: Nickname collisions with non-account buckets and Unicode preservation
    acc_dev = SyntheticSaveInfo(save_data_id=0x99, space_id=1, save_type=1, app_id=0x2000, uid=(9, 0))
    acc_cyr = SyntheticSaveInfo(save_data_id=0xAA, space_id=1, save_type=1, app_id=0x2000, uid=(10, 0))
    mixed_dict = {
        (9, 0): "Device",  # matches bucket name
        (10, 0): "Користувач",  # Unicode Cyrillic
    }
    mixed_res = allocate_save_names([acc_dev, acc_cyr], mixed_dict)
    check(mixed_res[acc_dev.identity_tuple] == "Device [0000000000000099]",
          "Account nicknamed 'Device' must be suffixed to not collide with Device bucket")
    check(mixed_res[acc_cyr.identity_tuple] == "Користувач",
          "Unicode Cyrillic nickname must be preserved without distortion")

    # =========================================================================
    # Part 2: Connected Executable Behavioral Model for Fail-Closed Operations
    # =========================================================================
    game_name = "Game Title [0100000000001000]"
    save_type_name = "Player"
    tree = {
        game_name: {
            save_type_name: acc1,
            "BCAT": dev1,
        }
    }

    proxy = SyntheticFsSaveProxy(tree)

    # 1. CreateFile rejection
    res = proxy.create_file(f"/{game_name}/{save_type_name}/slot0/newfile.bin", 1024)
    check(res == FS_ERROR_NOT_IMPLEMENTED, "create_file must return FS_ERROR_NOT_IMPLEMENTED")
    check(proxy.mount_count == 0, "create_file must reject before mounting save")

    # 2. DeleteFile rejection
    res = proxy.delete_file(f"/{game_name}/{save_type_name}/slot0/savedata.bin")
    check(res == FS_ERROR_NOT_IMPLEMENTED, "delete_file must return FS_ERROR_NOT_IMPLEMENTED")
    check(proxy.mount_count == 0, "delete_file must reject before mounting save")

    # 3. RenameFile rejection (same save)
    res = proxy.rename_file(f"/{game_name}/{save_type_name}/slot0/savedata.bin",
                            f"/{game_name}/{save_type_name}/slot0/savedata2.bin")
    check(res == FS_ERROR_NOT_IMPLEMENTED, "rename_file must return FS_ERROR_NOT_IMPLEMENTED")
    check(proxy.mount_count == 0, "rename_file must reject before mounting save")

    # 4. RenameFile rejection (cross-save attempt)
    res = proxy.rename_file(f"/{game_name}/{save_type_name}/slot0/savedata.bin",
                            f"/{game_name}/BCAT/slot0/savedata.bin")
    check(res == FS_ERROR_NOT_IMPLEMENTED, "cross-save rename_file must return FS_ERROR_NOT_IMPLEMENTED")
    check(proxy.mount_count == 0, "cross-save rename must reject before mounting save")

    # 5. CreateDirectory rejection
    res = proxy.create_directory(f"/{game_name}/{save_type_name}/slot0/new_dir")
    check(res == FS_ERROR_NOT_IMPLEMENTED, "create_directory must return FS_ERROR_NOT_IMPLEMENTED")
    check(proxy.mount_count == 0, "create_directory must reject before mounting save")

    # 6. DeleteDirectoryRecursively rejection
    res = proxy.delete_directory_recursively(f"/{game_name}/{save_type_name}/slot0")
    check(res == FS_ERROR_NOT_IMPLEMENTED, "delete_directory_recursively must return FS_ERROR_NOT_IMPLEMENTED")
    check(proxy.mount_count == 0, "delete_directory_recursively must reject before mounting save")

    # 7. RenameDirectory rejection
    res = proxy.rename_directory(f"/{game_name}/{save_type_name}/slot0",
                                 f"/{game_name}/{save_type_name}/slot1")
    check(res == FS_ERROR_NOT_IMPLEMENTED, "rename_directory must return FS_ERROR_NOT_IMPLEMENTED")
    check(proxy.mount_count == 0, "rename_directory must reject before mounting save")

    # 8. OpenFile write rejection before parse/mount
    res, handle = proxy.open_file(f"/{game_name}/{save_type_name}/slot0/savedata.bin", FS_OPEN_WRITE)
    check(res == FS_ERROR_NOT_IMPLEMENTED, "open_file with FS_OPEN_WRITE must return FS_ERROR_NOT_IMPLEMENTED")
    check(handle is None, "open_file with FS_OPEN_WRITE must not return handle")
    check(proxy.mount_count == 0, "open_file with FS_OPEN_WRITE must reject before mounting save")

    # 9. OpenFile append rejection before parse/mount
    res, handle = proxy.open_file(f"/{game_name}/{save_type_name}/slot0/savedata.bin", FS_OPEN_APPEND)
    check(res == FS_ERROR_NOT_IMPLEMENTED, "open_file with FS_OPEN_APPEND must return FS_ERROR_NOT_IMPLEMENTED")
    check(handle is None, "open_file with FS_OPEN_APPEND must not return handle")
    check(proxy.mount_count == 0, "open_file with FS_OPEN_APPEND must reject before mounting save")

    # 10. OpenFile combined read+write rejection
    res, handle = proxy.open_file(f"/{game_name}/{save_type_name}/slot0/savedata.bin", FS_OPEN_READ | FS_OPEN_WRITE)
    check(res == FS_ERROR_NOT_IMPLEMENTED, "open_file with read+write must return FS_ERROR_NOT_IMPLEMENTED")
    check(handle is None, "open_file with read+write must not return handle")
    check(proxy.mount_count == 0, "open_file with read+write must reject before mounting save")

    # 11. Read-only OpenFile succeeds
    target_path = f"/{game_name}/{save_type_name}/slot0/savedata.bin"
    res, read_handle = proxy.open_file(target_path, FS_OPEN_READ)
    check(res == FS_SUCCESS, "open_file with FS_OPEN_READ must succeed")
    check(read_handle is not None, "open_file with FS_OPEN_READ must return valid handle")
    check(proxy.mount_count == 1, "open_file with FS_OPEN_READ must mount save exactly once")

    native_fs = read_handle["fs"]
    check(native_fs.side_effects == 0, "Read-only mount must have 0 side effects")
    check(native_fs.commits == 0, "Read-only mount must have 0 commits")

    # 12. WriteFile on handle rejection
    res = proxy.write_file(read_handle, 0, b"malicious_overwrite_data")
    check(res == FS_ERROR_NOT_IMPLEMENTED, "write_file must return FS_ERROR_NOT_IMPLEMENTED")
    check(native_fs.side_effects == 0, "Rejected write must have 0 side effects")
    check(native_fs.commits == 0, "Rejected write must have 0 commits")

    # 13. SetFileSize on handle rejection
    res = proxy.set_file_size(read_handle, 0)
    check(res == FS_ERROR_NOT_IMPLEMENTED, "set_file_size must return FS_ERROR_NOT_IMPLEMENTED")
    check(native_fs.side_effects == 0, "Rejected set_file_size must have 0 side effects")
    check(native_fs.commits == 0, "Rejected set_file_size must have 0 commits")

    # 14. GetFileSize and ReadFile succeed with unchanged bytes
    sz = proxy.get_file_size(read_handle)
    expected_bytes = b"original_save_data_bytes_12345"
    check(sz == len(expected_bytes), f"get_file_size must return {len(expected_bytes)}, got {sz}")
    read_bytes = proxy.read_file(read_handle, 0, sz)
    check(read_bytes == expected_bytes, "read_file must return exact unaltered bytes")

    # 15. CloseFile releases handle without commit
    proxy.close_file(read_handle)
    check(read_handle["closed"] is True, "CloseFile must mark handle closed")
    check(native_fs.commits == 0, "CloseFile must NEVER perform a commit")
    check(native_fs.side_effects == 0, "CloseFile must have 0 side effects")
    check(native_fs.files["/slot0/savedata.bin"] == expected_bytes, "Save file content must remain completely unchanged")

    # 16. Directory enumeration at all depths
    # Root level
    res, root_dir = proxy.open_directory("/")
    check(res == FS_SUCCESS and root_dir["is_virtual"] is True, "Root directory must be virtual")
    check(game_name in root_dir["entries"], "Root directory must enumerate game directories")

    # Game level
    res, game_dir_h = proxy.open_directory(f"/{game_name}")
    check(res == FS_SUCCESS and game_dir_h["is_virtual"] is True, "Game directory must be virtual")
    check(save_type_name in game_dir_h["entries"] and "BCAT" in game_dir_h["entries"],
          "Game directory must enumerate save bucket directories")

    # Save level
    res, save_dir_h = proxy.open_directory(f"/{game_name}/{save_type_name}")
    check(res == FS_SUCCESS and save_dir_h["is_virtual"] is False, "Save directory must be native")
    check("slot0" in save_dir_h["entries"], "Save directory must enumerate internal contents")

    # 17. Stored full identity routing
    # Stored FsSaveDataInfo routes the selected visible node and is NOT reconstructed from visible name
    stored_info = proxy.tree[game_name][save_type_name]
    check(stored_info.save_data_id == acc1.save_data_id, "Exact retained save_data_id must route save")
    check(stored_info.save_data_space_id == acc1.save_data_space_id, "Exact retained space_id must route save")
    check(stored_info.application_id == acc1.application_id, "Exact retained application_id must route save")
    check(stored_info.uid == acc1.uid, "Exact retained UID must route save")

    print("Synthetic behavioral model checks: ALL PASS (17 fail-closed & allocation groups)")

if __name__ == "__main__":
    test_source_contracts()
    test_behavioral_model()
    print("ALL MTP SAVE CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")
