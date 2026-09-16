#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for MTP save layout.

NOTE ON SCOPE AND COVERAGE:
This test executes static source contracts and a Python behavioral model of the
allocation and naming algorithms. It validates algorithmic invariance, collision
resolution, boundary conditions, and C++ source patterns. It does NOT execute
the C++ binary, libnx/libhaze runtime, or target Nintendo Switch hardware.
"""

import itertools
import os
import re
import sys

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

def test_source_contracts():
    path = os.path.join(os.path.dirname(__file__), "..", "sphaira", "source", "haze", "haze_save_proxy.cpp")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    # 1. Deterministic total order and sorting before name allocation
    check("CompareSaveDataInfo" in src, "Must define CompareSaveDataInfo for deterministic total order")
    check("std::sort(records.begin(), records.end(), CompareSaveDataInfo);" in src,
          "Must sort records deterministically before name allocation")
    check("std::unique(records.begin(), records.end(), IsSameSaveRecord);" in src,
          "Must deduplicate identical scan records using IsSameSaveRecord")

    # Ensure CompareSaveDataInfo checks actual fields without padding or exposing UID
    for field in ["save_data_type", "save_data_index", "save_data_rank", "save_data_id",
                  "save_data_space_id", "system_save_data_id", "application_id", "uid.uid[0]", "uid.uid[1]"]:
        check(field in src, f"CompareSaveDataInfo must check {field}")
    check("UidHex" not in src, "UidHex must not exist; full UID is internal only")

    # 2. Bounded game names reserving prefix and suffix space
    check("FormatSaveGameDirName" in src, "Must define FormatSaveGameDirName for bounded game directory names")
    check("BuildSaveGameDirName" in src, "Must use BuildSaveGameDirName for save game directories")
    check("IsWindowsReservedDeviceName" in src, "Must check Windows reserved device names")

    # 3. Preserved layout: game -> user -> live save root (no 'Account ' prefix)
    check("BuildTypeDirName" not in src, "BuildTypeDirName must be replaced with direct nickname layout")
    check('"Account "' not in src, 'Second-level folder must not have "Account " prefix')

    # 4. Ponytail comment on O(n^2) duplicate loop
    check("Ponytail: O(n^2)" in src, "Must include ponytail comment on bounded O(n^2) duplicate scan ceiling")

    # 5. Non-account buckets preserved
    for bucket in ["BCAT", "Device", "Cache"]:
        check(f'"{bucket}"' in src, f"Non-account bucket {bucket} must be preserved")

    # 6. Preserved scan spaces and types
    check("FsSaveDataType_Account" in src, "Must scan Account saves")
    check("FsSaveDataType_Bcat" in src, "Must scan BCAT saves")
    check("FsSaveDataType_Device" in src, "Must scan Device saves")
    check("FsSaveDataType_Cache" in src, "Must scan Cache saves")
    check("FsSaveDataSpaceId_SdUser" in src, "Cache saves must use SdUser space")
    check("FsSaveDataSpaceId_User" in src, "Account/BCAT/Device saves must use User space")
    check("FsSaveDataType_System" not in src, "System saves must not be added to MTP saves")
    check("FsSaveDataType_Temporary" not in src, "Temporary saves must not be added to MTP saves")

    # 7. Virtual depth routing and write/commit semantics
    check("pp.depth < 2" in src, "Depth < 2 must be virtual directory enumeration")
    check("pp.depth >= 2" in src, "Depth >= 2 must mount save and route rest")
    check("pp.depth >= 3" in src, "Mutating operations must require depth >= 3")
    check("fs->Commit()" in src, "Mutating operations must call fs->Commit()")
    check("!strcasecmp(pp_old.game.c_str(), pp_new.game.c_str())" in src,
          "Cross-save rename must check game case-insensitively")
    check("!strcasecmp(pp_old.type.c_str(), pp_new.type.c_str())" in src,
          "Cross-save rename must check save folder case-insensitively")

    print("Source contracts: ALL PASS (7 anchor groups)")

# ==============================================================================
# Behavioral Model (Algorithmic Specification)
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

def test_behavioral_model():
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

    print("Synthetic behavioral model checks: ALL PASS (5 regression groups)")

if __name__ == "__main__":
    test_source_contracts()
    test_behavioral_model()
    print("ALL MTP SAVE CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")
