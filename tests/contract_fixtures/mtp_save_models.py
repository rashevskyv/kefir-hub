# Naming and tree allocation models for MTP save contract.
import os
import re
import sys
from dataclasses import dataclass

SAVE_DATA_SPACE_ID_SD_USER = 4

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
