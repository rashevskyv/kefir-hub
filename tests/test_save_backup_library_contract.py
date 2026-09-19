#!/usr/bin/env python3
"""
Test Suite: Save Backup Library Group Membership and Action Contract (Sphaira v0.13.862)

Target chat: Аудит і виправлення системи сейвів
Scope & Verification Boundary:
1. Static Source Contracts (Scoped to function bodies):
   - sphaira/CMakeLists.txt: set(sphaira_VERSION 0.13.861).
   - sphaira/include/ui/menus/save_menu.hpp:
     - struct BackupCandidate declared before struct Entry.
     - struct Entry contains std::vector<BackupCandidate> backup_members.
   - sphaira/source/ui/menus/save_menu.cpp:
     - Menu::ReadBackupEntries:
       - Records every admitted candidate into e.backup_members / existing.backup_members.
       - Tracks rep_source in GroupScanMeta.
       - Updates backup_count = backup_members.size().
       - Updates representative (backup_timestamp, backup_path, source_timestamp, commit_id) on newer ts,
         lower source prio, or smaller path string.
       - Sorts each group's backup_members (-ts, +source, +path).
     - Menu::CollectGroupArchives:
       - Evaluates if (group.is_backup) and returns before CollectBackups / fallback.
       - Iterates ONLY group.backup_members.
       - Re-inspects each exact path via InspectBackupArchive.
       - Validates BackupGroupKey.
       - Omits missing, invalid, or changed-group members.
       - Deduplicates exact paths.
       - Sorts candidates (-ts, +source, +path).
       - Returns empty list on no matches without fallback.
     - Menu::PromptBackupGroupAction & Menu::RestoreForUser:
       - } else if (e.is_backup) { precedes old fallback.
       - Displays "No backups found for selected saves." without stale backup_path fallback.
   - sphaira/source/ui/menus/save/save_menu_ops.cpp:
     - Menu::ShowRestorePickerPopup:
       - Counts label duplicates via label_counts.
       - Appends (" + path + ") only to ambiguous labels (count > 1).
       - Passes chosen candidates[*op_index].path to RestoreSavesPicked.

2. Executable Behavioral Regressions (Real Temp Files, Real ZIPs, Wire Decoder):
   - Suite 1: Connected Scanner & Proven Regression:
     - Real filesystem walk (read_backup_entries_model) creates EntryModel from two arbitrary-named ZIPs in /dumps.
     - Old narrow discovery (CollectBackups) returns empty and falls back to only 1 path, losing the 2nd artifact.
     - New library CollectGroupArchives retains both, exactly matching backup_count == 2.
     - Representative attributes match backup_members[0].
   - Suite 2: Location Matrix, Priorities, & Tie-breaking:
     - All scan depths: DBI direct, DBI date folders, /dumps L1..L3, custom roots L1..L3.
     - Exact tie-break ordering (-ts, +source, +path).
     - Path deduplication across overlapping configured roots.
   - Suite 3: Wire & Rejection Fixtures:
     - Genuine POSIX seconds converted via posix_to_timestamp.
     - Filename timestamp overrides metadata fallback; hand-renamed files fall back to metadata calendar.
     - Real ZIP rejection: duplicate metadata, parent directory conflict, conflicting NX/DBI,
       ambiguous 86 layout, malformed/invalid source fields. All fail closed and never enter scan groups.
   - Suite 4: Real Archive Identity & Grouping Matrix:
     - Real ZIPs: rank 0 vs 1 (same group), index 0 vs 1 (separate groups), Cache index (separate groups),
       Account vs Device (separate groups), System IDs (separate groups), UID-high (separate groups),
       source-space facts (same group), System vs SystemBcat (separate groups), mixed wire versions (same group),
       metadata-free DBI legacy (parsed by filename).
     - Exact artifact membership mapping verified for all 16 scan-produced groups via collect_group_archives_model.
   - Suite 5: Connected Picker Callback & Destination Object Boundary:
     - Modeled picker callback passes chosen candidate path and explicit live target to restore boundary.
     - Payload read back from chosen ZIP to prove exact artifact read.
     - Explicit recording boundary assertions: recorded target app/UID/save ID equal explicit live target,
       and recorded UID differs from source candidate UID (Account user remap boundary).
     - All 9 live target fields captured before callback and asserted unchanged after.
     - Picker label disambiguation for duplicate dates and hand-renamed identical basenames.
   - Suite 6: Actions, Sentinels, & Readmission:
     - Scan-produced groups used for Verify, Prune, Delete.
     - Post-scan added ZIP is not touched or rediscovered; bytes identical.
     - Foreign injected path dropped by CollectGroupArchives; bytes identical.
     - Deleted member omitted; all deleted returns empty without stale fallback.
     - Timestamp modification picked up by fresh inspection and re-sorted.
     - Prune deletes older exact paths, preserves newest and foreign sentinels; byte-for-byte preservation on newest.
     - Fresh timestamp reinspection & re-sorting on arbitrary-named ZIPs without mutating group.backup_members scan inventory.
     - Post-scan admission changes on actual scan-produced groups (corrupt member, changed-group member, valid member,
       and empty collection on removal without stale fallback).
     - Delete deletes all group paths, preserves sentinels, cleans parent nonrecursively.
   - Suite 7: Scoped Source Anchors & Model-Level Lifetimes:
     - Scoped checks on extracted function blocks.
     - Model-level container append, copy, move, and lambda capture invariants.

NO C++ COMPILATION, NO BINARIES, NO NRO, NO WSL REQUIRED. Pure Python stdlib.
"""

import io
import os
import re
import shutil
import struct
import sys
import tempfile
import time
import warnings
import zipfile
warnings.filterwarnings("ignore", category=UserWarning)
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Set, Tuple
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

# Reuse bounded reference model decoder and wire constants from test_save_metadata_wire_contract
from tests.test_save_metadata_wire_contract import (
    ReferenceSaveMetadataDecoder,
    pack_jksv85,
    pack_jksv_tail86,
    pack_jksv_middle86,
    pack_sphaira128,
    pack_dbi_raw512,
    make_zip,
    JKSV_MAGIC,
    JKSV_REVISION,
    SPHAIRA_MAGIC,
    SPHAIRA_VERSION,
    VALID_SPACES,
)


def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ==============================================================================
# Wire Constants & Pure-Python Data Structures
# ==============================================================================

NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"

SAVE_TYPE_SYSTEM = 0
SAVE_TYPE_ACCOUNT = 1
SAVE_TYPE_BCAT = 2
SAVE_TYPE_DEVICE = 3
SAVE_TYPE_TEMPORARY = 4
SAVE_TYPE_CACHE = 5
SAVE_TYPE_SYSTEM_BCAT = 6


def is_system_like(stype: int) -> bool:
    return stype in (SAVE_TYPE_SYSTEM, SAVE_TYPE_SYSTEM_BCAT)


def make_zip_file(file_path: str, entries: Dict[str, bytes]) -> None:
    os.makedirs(os.path.dirname(file_path), exist_ok=True)
    with zipfile.ZipFile(file_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries.items():
            zf.writestr(name, data)


def read_zip_entry(file_path: str, entry_name: str) -> Optional[bytes]:
    try:
        with zipfile.ZipFile(file_path, "r") as zf:
            return zf.read(entry_name)
    except Exception:
        return None


@dataclass
class BackupCandidateModel:
    ts: int = 0
    path: str = ""
    source: int = 0


@dataclass
class BackupArchiveInfoModel:
    application_id: int = 0
    system_save_data_id: int = 0
    save_data_type: int = 0xFF
    uid_low: int = 0
    uid_high: int = 0
    save_data_index: int = 0
    save_data_rank: int = 0
    rank_known: bool = False
    timestamp: int = 0
    dbi_game_dir: str = ""
    path: str = ""
    source: int = 0
    commit_id: int = 0
    source_timestamp: int = 0


@dataclass
class EntryModel:
    application_id: int = 0
    system_save_data_id: int = 0
    save_data_type: int = 0xFF
    save_data_space_id: int = 1
    save_data_id: int = 0
    uid_low: int = 0
    uid_high: int = 0
    save_data_index: int = 0
    save_data_rank: int = 0
    is_backup: bool = False
    backup_rank_known: bool = False
    backup_timestamp: int = 0
    backup_count: int = 0
    backup_path: str = ""
    dbi_game_dir: str = ""
    source_timestamp: int = 0
    commit_id: int = 0
    name: str = ""
    backup_members: List[BackupCandidateModel] = field(default_factory=list)


def backup_group_key(app_id: int, sys_id: int, save_type: int, uid_low: int, uid_high: int, index: int, rank_known: bool = True, rank: int = 0) -> str:
    rk = "rk:1" if (rank_known and rank == 1) else ("rk:0" if rank_known else "rk:?")
    if is_system_like(save_type):
        return f"backup:system:{save_type}:{sys_id:016X}:{index}:{rk}"
    return f"backup:app:{app_id:016X}:{save_type}:{uid_low:016X}{uid_high:016X}:{index}:{rk}"


def posix_to_timestamp(posix_sec: int) -> int:
    """Pure-Python calendar conversion matching PosixToTimestamp in save_paths.cpp."""
    if not posix_sec:
        return 0
    try:
        t = time.localtime(posix_sec)
        return (t.tm_year * 10000000000 +
                t.tm_mon * 100000000 +
                t.tm_mday * 1000000 +
                t.tm_hour * 10000 +
                t.tm_min * 100 +
                t.tm_sec)
    except Exception:
        return 0


def parse_backup_name_timestamp(name: str) -> int:
    """Pure-Python filename timestamp parser matching ParseBackupNameTimestamp in save_paths.cpp."""
    # 1. DBI timestamp format: "<16 hex>_<letter>_<14 digits>_..."
    if len(name) >= 34 and name[16] == '_' and name[18] == '_' and name[33] == '_':
        sub = name[19:33]
        if sub.isdigit():
            return int(sub)

    # 2. "YYYY.MM.DD @ HH.MM.SS"
    at_pos = name.find(" @ ")
    if at_pos != -1 and at_pos >= 10 and at_pos + 11 <= len(name):
        date_str = name[at_pos - 10: at_pos + 11]
        m = re.match(r"^(\d{4})\.(\d{2})\.(\d{2}) @ (\d{2})\.(\d{2})\.(\d{2})$", date_str)
        if m:
            year, mon, day, hour, minute, sec = map(int, m.groups())
            return (year * 10000000000 + mon * 100000000 + day * 1000000 +
                    hour * 10000 + minute * 100 + sec)

    # 3. YYYYMMDD_HHMMSS or YYYYMMDDHHMMSS
    for i in range(len(name)):
        if i + 15 <= len(name):
            sub = name[i:i+15]
            m = re.match(r"^(\d{4})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2})$", sub)
            if m:
                year, mon, day, hour, minute, sec = map(int, m.groups())
                if 2000 <= year <= 2099 and 1 <= mon <= 12 and 1 <= day <= 31 and 0 <= hour <= 23 and 0 <= minute <= 59 and 0 <= sec <= 59:
                    return (year * 10000000000 + mon * 100000000 + day * 1000000 +
                            hour * 10000 + minute * 100 + sec)
        if i + 14 <= len(name):
            sub = name[i:i+14]
            m = re.match(r"^(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})$", sub)
            if m:
                year, mon, day, hour, minute, sec = map(int, m.groups())
                if 2000 <= year <= 2099 and 1 <= mon <= 12 and 1 <= day <= 31 and 0 <= hour <= 23 and 0 <= minute <= 59 and 0 <= sec <= 59:
                    return (year * 10000000000 + mon * 100000000 + day * 1000000 +
                            hour * 10000 + minute * 100 + sec)
    return 0


def parse_dbi_type_letter(c: str) -> int:
    return {
        "A": SAVE_TYPE_ACCOUNT,
        "B": SAVE_TYPE_BCAT,
        "D": SAVE_TYPE_DEVICE,
        "T": SAVE_TYPE_TEMPORARY,
        "C": SAVE_TYPE_CACHE,
        "S": SAVE_TYPE_SYSTEM,
        "Y": SAVE_TYPE_SYSTEM_BCAT,
    }.get(c.upper(), 0xFF)


def parse_dbi_backup_app_id(name: str) -> int:
    if len(name) < 19 or name[16] != '_' or name[18] != '_':
        return 0
    try:
        return int(name[:16], 16)
    except ValueError:
        return 0


def parse_dbi_backup_index(name: str) -> int:
    if len(name) < 36 or name[16] != '_' or name[18] != '_' or name[33] != '_':
        return 0
    idx_str = ""
    for c in name[34:]:
        if c.isdigit():
            idx_str += c
        else:
            break
    if idx_str:
        try:
            return int(idx_str)
        except ValueError:
            return 0
    return 0


def inspect_backup_archive_model(fs_path: str, filename: str, dbi_game_dir: str = "") -> Optional[BackupArchiveInfoModel]:
    """Pure-Python behavioral model matching InspectBackupArchive in save_paths.cpp."""
    if not (filename.lower().endswith(".zip") or filename.lower().endswith(".bin")):
        return None
    if not os.path.isfile(fs_path):
        return None

    try:
        with open(fs_path, "rb") as f:
            raw_data = f.read()
    except Exception:
        return None

    info = BackupArchiveInfoModel()
    info.path = fs_path.replace("\\", "/")
    info.dbi_game_dir = dbi_game_dir
    info.timestamp = parse_backup_name_timestamp(filename)

    loaded = False
    if filename.lower().endswith(".zip"):
        # Precedence 1: valid embedded archive metadata via bounded reference model ReferenceSaveMetadataDecoder
        meta_status, meta = ReferenceSaveMetadataDecoder.read_archive_metadata(raw_data)
        if meta_status == "Invalid":
            # Present invalid metadata: fail closed, NO fallback!
            return None
        if meta_status == "Valid" and meta is not None:
            info.application_id = meta["application_id"]
            info.system_save_data_id = meta["system_save_data_id"]
            info.save_data_type = meta["save_data_type"]
            info.uid_low = meta["uid_low"]
            info.uid_high = meta["uid_high"]
            info.save_data_index = meta["save_data_index"]
            info.save_data_rank = meta["save_data_rank"]
            info.rank_known = True
            info.commit_id = meta["commit_id"]
            info.source_timestamp = meta["timestamp"]  # POSIX seconds
            if info.timestamp == 0 and meta["timestamp"] != 0:
                info.timestamp = posix_to_timestamp(meta["timestamp"])
            loaded = True

        if not loaded:
            # Precedence 2: DBI filename fields
            if info.application_id == 0 and info.system_save_data_id == 0:
                info.application_id = parse_dbi_backup_app_id(filename)
            if info.save_data_type == 0xFF:
                if len(filename) >= 19 and filename[16] == '_' and filename[18] == '_':
                    info.save_data_type = parse_dbi_type_letter(filename[17])
            if info.save_data_index == 0:
                info.save_data_index = parse_dbi_backup_index(filename)
            if info.application_id != 0 or info.system_save_data_id != 0:
                loaded = True

    if not loaded:
        # Precedence 3: explicit DBI directory/folder information
        if info.application_id == 0 and info.system_save_data_id == 0:
            if dbi_game_dir:
                try:
                    info.application_id = int(dbi_game_dir, 16)
                    if info.application_id != 0:
                        loaded = True
                except ValueError:
                    pass

    return info if loaded else None


# ==============================================================================
# Directory Scanner & Group Collection Behavioral Reference Models
# ==============================================================================

def read_backup_entries_model(fs_root: str, custom_search_paths: Optional[List[str]] = None) -> List[EntryModel]:
    """Pure-Python behavioral model matching Menu::ReadBackupEntries in save_menu.cpp.
    Walks actual filesystem directories matching exact scan depths and source priorities.
    """
    seen_paths: Set[str] = set()
    group_map: Dict[str, Dict[str, Any]] = {}
    groups: List[EntryModel] = []

    def process_archive(virtual_path: str, filename: str, dbi_game_dir_name: str, source_prio: int):
        if virtual_path in seen_paths:
            return
        seen_paths.add(virtual_path)

        real_path = os.path.join(fs_root, virtual_path.lstrip("/"))
        info = inspect_backup_archive_model(real_path, filename, dbi_game_dir_name)
        if not info:
            return

        info.source = source_prio
        key = backup_group_key(
            info.application_id, info.system_save_data_id, info.save_data_type,
            info.uid_low, info.uid_high, info.save_data_index,
            rank_known=info.rank_known, rank=info.save_data_rank
        )

        if key not in group_map:
            e = EntryModel()
            e.application_id = info.application_id
            e.system_save_data_id = info.system_save_data_id
            e.save_data_type = info.save_data_type
            e.uid_low = info.uid_low
            e.uid_high = info.uid_high
            e.save_data_index = info.save_data_index
            e.save_data_rank = info.save_data_rank
            e.backup_rank_known = info.rank_known
            e.is_backup = True
            e.backup_timestamp = info.timestamp
            e.backup_count = 1
            e.backup_path = virtual_path
            e.dbi_game_dir = info.dbi_game_dir
            e.source_timestamp = info.source_timestamp
            e.commit_id = info.commit_id
            e.backup_members.append(BackupCandidateModel(ts=info.timestamp, path=virtual_path, source=source_prio))

            group_map[key] = {"index": len(groups), "rep_source": source_prio}
            groups.append(e)
        else:
            meta = group_map[key]
            existing = groups[meta["index"]]
            existing.backup_members.append(BackupCandidateModel(ts=info.timestamp, path=virtual_path, source=source_prio))
            existing.backup_count = len(existing.backup_members)

            is_newer = False
            if info.timestamp != existing.backup_timestamp:
                is_newer = info.timestamp > existing.backup_timestamp
            elif source_prio != meta["rep_source"]:
                is_newer = source_prio < meta["rep_source"]
            else:
                is_newer = virtual_path < existing.backup_path

            if is_newer:
                existing.backup_timestamp = info.timestamp
                existing.backup_path = virtual_path
                existing.source_timestamp = info.source_timestamp
                existing.commit_id = info.commit_id
                meta["rep_source"] = source_prio

            if not existing.dbi_game_dir and info.dbi_game_dir:
                existing.dbi_game_dir = info.dbi_game_dir

    # 1. Scan DBI-format game backups: /switch/DBI/saves/<game>/<date>/<appid>_<type>_..zip (prio 0)
    dbi_root_real = os.path.join(fs_root, "switch", "DBI", "saves")
    if os.path.isdir(dbi_root_real):
        try:
            game_entries = sorted(os.listdir(dbi_root_real))
        except Exception:
            game_entries = []
        for game_name in game_entries:
            game_path_real = os.path.join(dbi_root_real, game_name)
            if not os.path.isdir(game_path_real):
                continue
            try:
                sub_entries = sorted(os.listdir(game_path_real))
            except Exception:
                sub_entries = []
            for sub_name in sub_entries:
                sub_path_real = os.path.join(game_path_real, sub_name)
                if os.path.isfile(sub_path_real):
                    vpath = f"/switch/DBI/saves/{game_name}/{sub_name}"
                    process_archive(vpath, sub_name, game_name, 0)
                elif os.path.isdir(sub_path_real):
                    date_name = sub_name
                    try:
                        date_files = sorted(os.listdir(sub_path_real))
                    except Exception:
                        date_files = []
                    for fname in date_files:
                        fpath_real = os.path.join(sub_path_real, fname)
                        if os.path.isfile(fpath_real):
                            vpath = f"/switch/DBI/saves/{game_name}/{date_name}/{fname}"
                            process_archive(vpath, fname, game_name, 0)

    # 2. Scan Sphaira root (/dumps) and custom search paths
    def scan_sphaira_root(root_virtual: str, prio_base: int):
        root_real = os.path.join(fs_root, root_virtual.lstrip("/"))
        if not os.path.isdir(root_real):
            return
        try:
            l1_entries = sorted(os.listdir(root_real))
        except Exception:
            return
        for e1 in l1_entries:
            p1_real = os.path.join(root_real, e1)
            v1 = f"{root_virtual.rstrip('/')}/{e1}"
            if os.path.isfile(p1_real):
                process_archive(v1, e1, "", prio_base)
            elif os.path.isdir(p1_real):
                dir1_real = p1_real
                dir1_virtual = v1
                try:
                    l2_entries = sorted(os.listdir(dir1_real))
                except Exception:
                    continue
                for e2 in l2_entries:
                    p2_real = os.path.join(dir1_real, e2)
                    v2 = f"{dir1_virtual}/{e2}"
                    if os.path.isfile(p2_real):
                        process_archive(v2, e2, "", prio_base + 1)
                    elif os.path.isdir(p2_real):
                        dir2_real = p2_real
                        dir2_virtual = v2
                        try:
                            l3_entries = sorted(os.listdir(dir2_real))
                        except Exception:
                            continue
                        for e3 in l3_entries:
                            p3_real = os.path.join(dir2_real, e3)
                            v3 = f"{dir2_virtual}/{e3}"
                            if os.path.isfile(p3_real):
                                process_archive(v3, e3, "", prio_base + 2)

    scan_sphaira_root("/dumps", 1)

    custom_prio = 10
    if custom_search_paths:
        for cpath in custom_search_paths:
            scan_sphaira_root(cpath, custom_prio)
            custom_prio += 5

    # Sort groups newest first: backup_timestamp descending, application_id ascending
    groups.sort(key=lambda g: (-g.backup_timestamp, g.application_id))

    # Sort each group's backup_members: ts descending, source ascending, path ascending
    out_groups: List[EntryModel] = []
    for g in groups:
        g.backup_members.sort(key=lambda c: (-c.ts, c.source, c.path))
        out_groups.append(g)

    return out_groups


def collect_group_archives_model(group: EntryModel, fs_root: str, backup_root: str = "/dumps") -> List[BackupCandidateModel]:
    """Pure-Python behavioral model matching Menu::CollectGroupArchives in save_menu.cpp."""
    target_key = backup_group_key(
        group.application_id, group.system_save_data_id, group.save_data_type,
        group.uid_low, group.uid_high, group.save_data_index,
        rank_known=group.backup_rank_known if group.is_backup else True,
        rank=group.save_data_rank
    )

    if group.is_backup:
        seen_paths: Set[str] = set()
        out: List[BackupCandidateModel] = []

        for m in group.backup_members:
            if not m.path or m.path in seen_paths:
                continue
            seen_paths.add(m.path)

            fname = os.path.basename(m.path)
            real_os_path = os.path.join(fs_root, m.path.lstrip("/"))
            info = inspect_backup_archive_model(real_os_path, fname, group.dbi_game_dir)
            if not info:
                continue

            info.source = m.source
            arch_key = backup_group_key(
                info.application_id, info.system_save_data_id, info.save_data_type,
                info.uid_low, info.uid_high, info.save_data_index,
                rank_known=info.rank_known,
                rank=info.save_data_rank
            )
            if arch_key != target_key:
                continue

            out.append(BackupCandidateModel(ts=info.timestamp, path=m.path, source=m.source))

        out.sort(key=lambda c: (-c.ts, c.source, c.path))
        return out

    # Live entries (is_backup == False): original rediscovery behavior
    return collect_group_archives_legacy_model(group, fs_root, backup_root)


def collect_group_archives_legacy_model(group: EntryModel, fs_root: str, backup_root: str = "/dumps") -> List[BackupCandidateModel]:
    """Pre-861 legacy behavior: runs narrow directory search looking only in /dumps/<game_name>/ and DBI, then falls back to single backup_path."""
    out: List[BackupCandidateModel] = []
    target_key = backup_group_key(
        group.application_id, group.system_save_data_id, group.save_data_type,
        group.uid_low, group.uid_high, group.save_data_index,
        rank_known=group.backup_rank_known if group.is_backup else True,
        rank=group.save_data_rank
    )

    game_folder = os.path.join(fs_root, backup_root.lstrip("/"), group.name) if group.name else ""
    if game_folder and os.path.isdir(game_folder):
        for fname in sorted(os.listdir(game_folder)):
            fpath = os.path.join(game_folder, fname)
            info = inspect_backup_archive_model(fpath, fname, group.dbi_game_dir)
            if info and backup_group_key(info.application_id, info.system_save_data_id, info.save_data_type, info.uid_low, info.uid_high, info.save_data_index, rank_known=info.rank_known, rank=info.save_data_rank) == target_key:
                out.append(BackupCandidateModel(ts=info.timestamp, path=f"{backup_root.rstrip('/')}/{group.name}/{fname}", source=1))

    # Stale fallback when narrow search is empty:
    if not out and group.backup_path:
        out.append(BackupCandidateModel(ts=group.backup_timestamp, path=group.backup_path, source=0))

    return out


def disambiguate_picker_labels(candidates: List[BackupCandidateModel]) -> List[str]:
    """Pure-Python behavioral model matching ShowRestorePickerPopup in save_menu_ops.cpp."""
    def label_for(c: BackupCandidateModel) -> str:
        if c.ts == 0:
            return os.path.basename(c.path)
        ts = c.ts
        year = ts // 10000000000
        mon = (ts // 100000000) % 100
        day = (ts // 1000000) % 100
        hour = (ts // 10000) % 100
        minute = (ts // 100) % 100
        sec = ts % 100
        return f"{year:04d}.{mon:02d}.{day:02d}  {hour:02d}:{minute:02d}:{sec:02d}"

    raw_labels = [label_for(c) for c in candidates]
    label_counts: Dict[str, int] = {}
    for lbl in raw_labels:
        label_counts[lbl] = label_counts.get(lbl, 0) + 1

    final_labels = []
    for i, c in enumerate(candidates):
        lbl = raw_labels[i]
        if label_counts[lbl] > 1:
            lbl += f" ({c.path})"
        final_labels.append(lbl)
    return final_labels


# ==============================================================================
# 1. Static Source Contracts Check (Scoped to actual functions)
# ==============================================================================

def test_static_source_contracts() -> None:
    print("[1] Verifying static source contracts in Sphaira codebase (scoped to functions)...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 CMakeLists.txt version check
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check("set(sphaira_VERSION 0.13.862)" in cmake_src,
          "sphaira/CMakeLists.txt must define sphaira_VERSION as 0.13.862")

    # 1.2 save_menu.hpp declarations
    sm_hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save_menu.hpp")
    with open(sm_hpp_path, "r", encoding="utf-8") as f:
        sm_hpp = f.read()
    cand_pos = sm_hpp.find("struct BackupCandidate {")
    entry_pos = sm_hpp.find("struct Entry final : FsSaveDataInfo {")
    check(cand_pos != -1, "save_menu.hpp must define struct BackupCandidate")
    check(entry_pos != -1, "save_menu.hpp must define struct Entry")
    check(cand_pos < entry_pos, "struct BackupCandidate must be declared before struct Entry")
    check("std::vector<BackupCandidate> backup_members" in sm_hpp,
          "struct Entry must contain backup_members vector")
    check("bool backup_rank_known" in sm_hpp,
          "struct Entry must contain backup_rank_known boolean member")

    # 1.3 save_paths.hpp declarations
    sp_hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(sp_hpp_path, "r", encoding="utf-8") as f:
        sp_hpp = f.read()
    check("bool rank_known{false};" in sp_hpp,
          "save_paths.hpp must define rank_known in struct BackupArchiveInfo")
    check("FormatBackupRankMarker" not in sp_hpp,
          "save_paths.hpp must NOT declare FormatBackupRankMarker")

    # 1.4 save_paths.cpp rank provenance & key format
    sp_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_paths.cpp")
    with open(sp_cpp_path, "r", encoding="utf-8") as f:
        sp_cpp = f.read()
    check("out.rank_known = true;" in sp_cpp,
          "save_paths.cpp InspectBackupArchive must set rank_known to true on Valid metadata")
    check("auto FormatBackupRankMarker(const Entry& e) -> std::string" in sp_cpp,
          "save_paths.cpp must define FormatBackupRankMarker")
    check("rk:0" in sp_cpp and "rk:1" in sp_cpp and "rk:?" in sp_cpp,
          "save_paths.cpp must format explicit rank markers rk:0, rk:1, and rk:?")

    # 1.5 save_menu.cpp function-scoped checks
    sm_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save_menu.cpp")
    with open(sm_cpp_path, "r", encoding="utf-8") as f:
        sm_cpp = f.read()
    check("auto FormatBackupRankMarker(const Entry& e) -> std::string" in sm_cpp,
          "save_menu.cpp must define file-local FormatBackupRankMarker")

    # Scope A: ReadBackupEntries
    p_rbe = sm_cpp.find("void Menu::ReadBackupEntries(")
    check(p_rbe != -1, "Menu::ReadBackupEntries must exist")
    p_rbe_end = sm_cpp.find("void Menu::Sort() {", p_rbe)
    check(p_rbe_end != -1, "End of Menu::ReadBackupEntries must be found")
    rbe_src = sm_cpp[p_rbe:p_rbe_end]

    check("e.backup_rank_known = info.rank_known;" in rbe_src,
          "ReadBackupEntries must record info.rank_known into e.backup_rank_known")
    check("e.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio});" in rbe_src,
          "ReadBackupEntries must record new candidate into e.backup_members")
    check("existing.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio});" in rbe_src,
          "ReadBackupEntries must record candidate into existing.backup_members")
    check("meta.rep_source" in rbe_src, "ReadBackupEntries must track rep_source in GroupScanMeta")
    check("existing.backup_count = existing.backup_members.size();" in rbe_src,
          "ReadBackupEntries must update existing.backup_count to backup_members.size()")
    check("is_newer = info.timestamp > existing.backup_timestamp;" in rbe_src,
          "ReadBackupEntries must compare timestamp for newer representative")
    check("is_newer = source_prio < meta.rep_source;" in rbe_src,
          "ReadBackupEntries must compare source priority for equal timestamp")
    check("is_newer = (path.toString() < existing.backup_path.toString());" in rbe_src,
          "ReadBackupEntries must compare path for equal timestamp and source")
    check("a.ts > b.ts" in rbe_src and "a.source < b.source" in rbe_src and "a.path.toString() < b.path.toString()" in rbe_src,
          "ReadBackupEntries must sort backup_members with (-ts, +source, +path)")

    # Scope B: CollectGroupArchives
    p_cga = sm_cpp.find("auto Menu::CollectGroupArchives(")
    check(p_cga != -1, "Menu::CollectGroupArchives must exist")
    p_cga_end = sm_cpp.find("auto FormatTargetSlotLabel", p_cga)
    check(p_cga_end != -1, "End of Menu::CollectGroupArchives must be found")
    cga_src = sm_cpp[p_cga:p_cga_end]

    check("if (group.is_backup) {" in cga_src, "CollectGroupArchives must branch on group.is_backup")
    pos_ret = cga_src.find("return out;")
    pos_collect_legacy = cga_src.find("CollectBackups(fs, group, backup_root);")
    check(pos_ret != -1 and pos_collect_legacy != -1, "CollectGroupArchives must have return out and CollectBackups")
    check(pos_ret < pos_collect_legacy, "if (group.is_backup) block must return before live CollectBackups / fallback")
    check("for (const auto& m : group.backup_members)" in cga_src,
          "CollectGroupArchives must iterate ONLY group.backup_members for is_backup")
    check("InspectBackupArchive(fs, m.path, fname, group.dbi_game_dir, info)" in cga_src,
          "CollectGroupArchives must re-inspect each exact member path")
    check("if (BackupGroupKey(info) != target_key)" in cga_src,
          "CollectGroupArchives must validate BackupGroupKey")
    check("a.ts > b.ts" in cga_src and "a.source < b.source" in cga_src and "a.path.toString() < b.path.toString()" in cga_src,
          "CollectGroupArchives must sort candidates with (-ts, +source, +path)")

    # Scope C: PromptBackupGroupAction
    p_pba = sm_cpp.find("void Menu::PromptBackupGroupAction(")
    check(p_pba != -1, "Menu::PromptBackupGroupAction must exist")
    p_pba_end = sm_cpp.find("void Menu::RestoreForUser(Entry e) {", p_pba)
    check(p_pba_end != -1, "End of Menu::PromptBackupGroupAction must be found")
    pba_src = sm_cpp[p_pba:p_pba_end]

    pos_pba_bkp = pba_src.find("} else if (e.is_backup) {")
    pos_pba_stale = pba_src.find("} else if (!e.backup_path.empty()) {")
    check(pos_pba_bkp != -1 and pos_pba_stale != -1,
          "PromptBackupGroupAction must have e.is_backup and !e.backup_path.empty() branches")
    check(pos_pba_bkp < pos_pba_stale,
          "PromptBackupGroupAction: e.is_backup guard must precede stale backup_path fallback")
    check("No backups found for selected saves." in pba_src,
          "PromptBackupGroupAction must report 'No backups found for selected saves.' on empty")

    # Scope D: RestoreForUser
    p_rfu = sm_cpp.find("void Menu::RestoreForUser(Entry e) {")
    check(p_rfu != -1, "Menu::RestoreForUser must exist")
    p_rfu_end = sm_cpp.find("void Menu::PromptBatchRestoreTargets(", p_rfu)
    check(p_rfu_end != -1, "End of Menu::RestoreForUser must be found")
    rfu_src = sm_cpp[p_rfu:p_rfu_end]

    pos_rfu_bkp = rfu_src.find("} else if (e.is_backup) {")
    pos_rfu_stale = rfu_src.find("} else if (!e.backup_path.empty()) {")
    check(pos_rfu_bkp != -1 and pos_rfu_stale != -1,
          "RestoreForUser must have e.is_backup and !e.backup_path.empty() branches")
    check(pos_rfu_bkp < pos_rfu_stale,
          "RestoreForUser: e.is_backup guard must precede stale backup_path fallback")
    check("No backups found for selected saves." in rfu_src,
          "RestoreForUser must report 'No backups found for selected saves.' on empty")

    # Scope E: Menu::Draw HbMenu layout header
    p_draw = sm_cpp.find("void Menu::Draw(NVGcontext* vg, Theme* theme) {")
    check(p_draw != -1, "Menu::Draw must exist")
    p_draw_end = sm_cpp.find("if (m_layout.Get() == grid::LayoutType_List) {", p_draw)
    check(p_draw_end != -1, "End of Menu::Draw HbMenu block must be found")
    draw_hb_src = sm_cpp[p_draw:p_draw_end]

    check("m_layout.Get() == grid::LayoutType_HbMenu" in draw_hb_src,
          "Menu::Draw must contain grid::LayoutType_HbMenu branch")
    check("FormatBackupRankMarker(e)" in draw_hb_src,
          "HbMenu backup branch must format rank marker via FormatBackupRankMarker(e)")
    check("((e.save_data_type == FsSaveDataType_Account && !m_all_accounts) ?" in draw_hb_src,
          "HbMenu live branch must preserve account logic")
    check("GetAccountName(e.uid) : GetAccountSummary()" in draw_hb_src,
          "HbMenu live branch must preserve account naming")

    # 1.6 save_menu_ops.cpp ShowRestorePickerPopup checks
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_cpp = f.read()

    p_pop = ops_cpp.find("void Menu::ShowRestorePickerPopup(")
    check(p_pop != -1, "Menu::ShowRestorePickerPopup must exist")
    p_pop_end = ops_cpp.find("void Menu::RestoreSavesPicked(", p_pop)
    check(p_pop_end != -1, "End of Menu::ShowRestorePickerPopup must be found")
    pop_src = ops_cpp[p_pop:p_pop_end]

    check("label_counts[raw_labels[i]] > 1" in pop_src,
          "ShowRestorePickerPopup must count duplicate labels and check > 1")
    check('label += " (" + std::string(c.path.s) + ")";' in pop_src,
          "ShowRestorePickerPopup must append path discriminator to ambiguous labels")
    check("candidates[*op_index].path" in pop_src,
          "ShowRestorePickerPopup must pass chosen candidates[*op_index].path")

    print("  -> Static source contracts verified successfully.")


# ==============================================================================
# 2. Executable Behavioral Regressions with Real Temp Files & ZIPs
# ==============================================================================

def test_suite_1_connected_scanner_and_proven_regression() -> None:
    print("[2] Running Suite 1: Connected Scanner & Proven Regression (Arbitrary ZIPs in /dumps)...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        os.makedirs(dumps_dir, exist_ok=True)

        app_id = 0x0100000000010000
        uid = (0x1111222233334444, 0x5555666677778888)

        # Create two valid metadata ZIPs directly in /dumps with arbitrary filenames
        zip1_path = os.path.join(dumps_dir, "my_custom_backup_alpha.zip")
        zip2_path = os.path.join(dumps_dir, "my_custom_backup_beta.zip")

        # Use actual POSIX seconds: 1773835200 (alpha) and 1773838800 (beta)
        meta1 = pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835200)
        meta2 = pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773838800)

        make_zip_file(zip1_path, {NX_SAVE_META_NAME: meta1, "game.sav": b"alpha_payload"})
        make_zip_file(zip2_path, {NX_SAVE_META_NAME: meta2, "game.sav": b"beta_payload"})

        # Actual filesystem scanner builds the EntryModel (NOT manually supplied!)
        groups = read_backup_entries_model(tmpdir)
        check(len(groups) == 1, f"Scanner must find exactly 1 group, got {len(groups)}")
        group = groups[0]

        # Verify discovery results:
        check(group.backup_count == 2, f"Group backup_count must be 2, got {group.backup_count}")
        check(len(group.backup_members) == 2, f"Group backup_members length must be 2, got {len(group.backup_members)}")
        check(group.backup_members[0].path == "/dumps/my_custom_backup_beta.zip", "Newer beta must be 1st in backup_members")
        check(group.backup_members[1].path == "/dumps/my_custom_backup_alpha.zip", "Older alpha must be 2nd in backup_members")

        # Representative attributes match backup_members[0]:
        check(group.backup_path == group.backup_members[0].path,
              "Representative backup_path must match backup_members[0].path")
        check(group.backup_timestamp == group.backup_members[0].ts,
              "Representative backup_timestamp must match backup_members[0].ts")
        check(group.backup_timestamp == posix_to_timestamp(1773838800),
              "Timestamp must match posix_to_timestamp of 1773838800")

        # Demonstrate the proven legacy defect:
        # Pre-861 narrow discovery looking in /dumps/<game_name>/ finds 0 items, then falls back to ONLY representative path!
        legacy_candidates = collect_group_archives_legacy_model(group, tmpdir)
        check(len(legacy_candidates) == 1,
              f"Proven Defect: legacy narrow discovery finds 0 and falls back to only 1 candidate, got {len(legacy_candidates)}")
        check(legacy_candidates[0].path == "/dumps/my_custom_backup_beta.zip",
              "Legacy fallback returned only beta, losing alpha!")

        # Contrast with new .861 library membership:
        # Uses retained members, re-inspects, and returns BOTH candidates!
        new_candidates = collect_group_archives_model(group, tmpdir)
        check(len(new_candidates) == 2,
              f"New library CollectGroupArchives must return both candidates! Got {len(new_candidates)}")
        check(new_candidates[0].path == "/dumps/my_custom_backup_beta.zip", "Beta must be first")
        check(new_candidates[1].path == "/dumps/my_custom_backup_alpha.zip", "Alpha must be second")
        check(len(new_candidates) == group.backup_count, "Candidate count must equal library backup_count")

    print("  -> Suite 1 (Connected Scanner & Proven Regression) PASSED.")


def test_suite_2_location_matrix_priority_and_tiebreaking() -> None:
    print("[3] Running Suite 2: Location Matrix, Priorities, Tie-breaking, & Overlapping Roots...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dbi_dir = os.path.join(tmpdir, "switch", "DBI", "saves", "GameAlpha")
        dbi_date_dir = os.path.join(dbi_dir, "2026-09-18")
        dumps_root = os.path.join(tmpdir, "dumps")
        dumps_l2 = os.path.join(dumps_root, "GameAlpha")
        dumps_l3 = os.path.join(dumps_l2, "sub")
        custom_root = os.path.join(tmpdir, "custom1")
        custom_l2 = os.path.join(custom_root, "dir1")
        custom_l3 = os.path.join(custom_l2, "dir2")

        for d in (dbi_date_dir, dumps_l3, custom_l3):
            os.makedirs(d, exist_ok=True)

        app_id = 0x0100000000020000
        uid = (0xAAAA111122223333, 0xBBBB444455556666)

        # 1. DBI direct file (source priority 0)
        p_dbi = os.path.join(dbi_dir, "0100000000020000_A_20260918100000.zip")
        make_zip_file(p_dbi, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773830000)})

        # 2. DBI date folder file (source priority 0)
        p_dbi_date = os.path.join(dbi_date_dir, "0100000000020000_A_20260918110000.zip")
        make_zip_file(p_dbi_date, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773831000)})

        # 3. Sphaira dumps level 1 (source priority 1)
        p_dumps_l1 = os.path.join(dumps_root, "0100000000020000_A_20260918120000.zip")
        make_zip_file(p_dumps_l1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773832000)})

        # 4. Sphaira dumps level 2 (source priority 2)
        p_dumps_l2 = os.path.join(dumps_l2, "20260918123000.zip")
        make_zip_file(p_dumps_l2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773832500)})

        # 5. Sphaira dumps level 3 (source priority 3)
        p_dumps_l3 = os.path.join(dumps_l3, "20260918124500.zip")
        make_zip_file(p_dumps_l3, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773832700)})

        # 6. Custom search path level 1 (source priority 10)
        p_custom_l1 = os.path.join(custom_root, "save_1.zip")
        make_zip_file(p_custom_l1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773833000)})

        # 7. Custom search path level 2 (source priority 11)
        p_custom_l2 = os.path.join(custom_l2, "save_2.zip")
        make_zip_file(p_custom_l2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773833500)})

        # 8. Custom search path level 3 (source priority 12)
        p_custom_l3 = os.path.join(custom_l3, "save_3.zip")
        make_zip_file(p_custom_l3, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773834000)})

        # 9. Identical basenames in different folders with equal timestamp:
        p_twin_dumps = os.path.join(dumps_l2, "twin_save.zip")  # prio 2
        p_twin_custom = os.path.join(custom_l2, "twin_save.zip")  # prio 11
        make_zip_file(p_twin_dumps, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000)})
        make_zip_file(p_twin_custom, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000)})

        # Scan with overlapping configured search paths (/dumps configured as custom search path too!)
        groups = read_backup_entries_model(tmpdir, custom_search_paths=["/custom1", "/dumps"])
        check(len(groups) == 1, f"Expected 1 group, got {len(groups)}")
        group = groups[0]

        # Total unique files created: 10
        check(group.backup_count == 10, f"Expected 10 distinct files, got {group.backup_count}")
        check(len(group.backup_members) == 10, f"Expected 10 backup_members, got {len(group.backup_members)}")

        # Check deduplication: no path appears more than once
        paths = [m.path for m in group.backup_members]
        check(len(paths) == len(set(paths)), "Overlapping configured roots must not duplicate any file path")

        # Tie-break verification 1: Equal timestamp (twin_save.zip), lower source priority (2 vs 11) comes first:
        twins = [m for m in group.backup_members if os.path.basename(m.path) == "twin_save.zip"]
        check(len(twins) == 2, "Both twin saves must be present")
        check(twins[0].source == 2 and twins[0].path == "/dumps/GameAlpha/twin_save.zip",
              "Source prio 2 must precede source prio 11 on equal timestamp")
        check(twins[1].source == 11 and twins[1].path == "/custom1/dir1/twin_save.zip",
              "Source prio 11 must follow source prio 2")

        # Tie-break verification 2: Equal timestamp and equal source priority: lexicographically smaller path wins:
        p_lex_a = os.path.join(dumps_l2, "a_tie.zip")
        p_lex_b = os.path.join(dumps_l2, "b_tie.zip")
        make_zip_file(p_lex_a, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773836000)})
        make_zip_file(p_lex_b, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773836000)})

        groups_tie = read_backup_entries_model(tmpdir, custom_search_paths=["/custom1"])
        gt = groups_tie[0]
        ties = [m for m in gt.backup_members if os.path.basename(m.path) in ("a_tie.zip", "b_tie.zip")]
        check(len(ties) == 2, "Both tie-break candidates must be present")
        check(ties[0].path < ties[1].path, "Lexicographically smaller path must come first on tie")

        # Representative attributes match backup_members[0]:
        check(gt.backup_path == gt.backup_members[0].path, "Representative path must match backup_members[0].path")
        check(gt.backup_timestamp == gt.backup_members[0].ts, "Representative timestamp must match backup_members[0].ts")

    print("  -> Suite 2 (Location Matrix, Priorities, & Tie-breaking) PASSED.")


def test_suite_3_wire_and_rejection_fixtures() -> None:
    print("[4] Running Suite 3: Wire Fields, POSIX Conversion, & Rejection Fixtures...")
    with tempfile.TemporaryDirectory() as tmpdir:
        # 1. Genuine POSIX seconds converted via posix_to_timestamp
        posix_sec = 1773835200
        converted_cal = posix_to_timestamp(posix_sec)
        check(converted_cal > 20000000000000, f"Converted calendar timestamp must be valid 14-digit int, got {converted_cal}")

        # 2. Filename timestamp overrides metadata timestamp
        p_named = os.path.join(tmpdir, "20260918_180000.zip")
        make_zip_file(p_named, {NX_SAVE_META_NAME: pack_jksv85(ts=posix_sec)})
        info_named = inspect_backup_archive_model(p_named, "20260918_180000.zip")
        check(info_named is not None, "Named archive must decode")
        check(info_named.timestamp == 20260918180000,
              f"Valid filename timestamp must override metadata fallback! Got {info_named.timestamp}")

        # 3. Hand-renamed / zero-timestamp filename falls back to metadata posix_to_timestamp
        p_hand = os.path.join(tmpdir, "hand_renamed_no_date.zip")
        make_zip_file(p_hand, {NX_SAVE_META_NAME: pack_jksv85(ts=posix_sec)})
        info_hand = inspect_backup_archive_model(p_hand, "hand_renamed_no_date.zip")
        check(info_hand is not None, "Hand-renamed archive must decode")
        check(info_hand.timestamp == converted_cal,
              f"Hand-renamed archive must fall back to posix_to_timestamp! Got {info_hand.timestamp}")

        # 4. Real ZIP Rejection Fixtures (Must return None and NEVER enter scan groups):
        # 4.1 Duplicate .nx_save_meta.bin entries in the same ZIP
        buf_dup = io.BytesIO()
        with zipfile.ZipFile(buf_dup, "w") as zf:
            zf.writestr(".nx_save_meta.bin", pack_jksv85(ts=posix_sec))
            zf.writestr(".nx_save_meta.bin", pack_jksv85(ts=posix_sec))
        p_dup = os.path.join(tmpdir, "dumps", "reject_dup.zip")
        os.makedirs(os.path.dirname(p_dup), exist_ok=True)
        with open(p_dup, "wb") as f:
            f.write(buf_dup.getvalue())
        check(inspect_backup_archive_model(p_dup, "reject_dup.zip") is None,
              "Duplicate metadata in ZIP must fail closed (None)")

        # 4.2 Parent directory conflict / nested reserved root (.nx_save_meta.bin/sub)
        buf_nested = io.BytesIO()
        with zipfile.ZipFile(buf_nested, "w") as zf:
            zf.writestr(".nx_save_meta.bin/subfile", b"garbage")
        p_nested = os.path.join(tmpdir, "dumps", "reject_nested.zip")
        with open(p_nested, "wb") as f:
            f.write(buf_nested.getvalue())
        check(inspect_backup_archive_model(p_nested, "reject_nested.zip") is None,
              "Parent directory conflict with reserved root must fail closed (None)")

        # 4.3 Conflicting NX metadata and DBI extra metadata (different app IDs)
        meta_nx = pack_jksv85(app_id=0x0100000000010000, ts=posix_sec)
        meta_dbi = pack_dbi_raw512(app_id=0x0100000000020000, ts=posix_sec)
        p_conflict = os.path.join(tmpdir, "dumps", "reject_conflict.zip")
        make_zip_file(p_conflict, {NX_SAVE_META_NAME: meta_nx, DBI_SAVE_EXTRA_NAME: meta_dbi})
        check(inspect_backup_archive_model(p_conflict, "reject_conflict.zip") is None,
              "Conflicting NX and DBI metadata must fail closed (None)")

        # 4.4 Ambiguous 86-byte layout (divergent valid interpretations)
        raw_divergent = pack_jksv_tail86(owner_id=0x0100000000010001, source_space=0, ts=posix_sec)
        p_ambig = os.path.join(tmpdir, "dumps", "reject_ambig86.zip")
        make_zip_file(p_ambig, {NX_SAVE_META_NAME: raw_divergent})
        check(inspect_backup_archive_model(p_ambig, "reject_ambig86.zip") is None,
              "Ambiguous 86-byte layout must fail closed (None)")

        # 4.5 Malformed/invalid source fields:
        # Invalid save_data_type > 6
        p_bad_type = os.path.join(tmpdir, "dumps", "reject_bad_type.zip")
        make_zip_file(p_bad_type, {NX_SAVE_META_NAME: pack_jksv85(save_type=7, ts=posix_sec)})
        check(inspect_backup_archive_model(p_bad_type, "reject_bad_type.zip") is None,
              "Save data type 7 must fail closed (None)")

        # Negative data_size
        p_neg_size = os.path.join(tmpdir, "dumps", "reject_neg_size.zip")
        make_zip_file(p_neg_size, {NX_SAVE_META_NAME: pack_jksv85(data_size=-1, ts=posix_sec)})
        check(inspect_backup_archive_model(p_neg_size, "reject_neg_size.zip") is None,
              "Negative data size must fail closed (None)")

        # Account save with zero UID
        p_zero_uid = os.path.join(tmpdir, "dumps", "reject_zero_uid.zip")
        make_zip_file(p_zero_uid, {NX_SAVE_META_NAME: pack_jksv85(save_type=1, uid_low=0, uid_high=0, ts=posix_sec)})
        check(inspect_backup_archive_model(p_zero_uid, "reject_zero_uid.zip") is None,
              "Account save with zero UID must fail closed (None)")

        # System save with zero system_save_data_id
        p_zero_sys = os.path.join(tmpdir, "dumps", "reject_zero_sys.zip")
        make_zip_file(p_zero_sys, {NX_SAVE_META_NAME: pack_jksv85(save_type=0, sys_id=0, ts=posix_sec)})
        check(inspect_backup_archive_model(p_zero_sys, "reject_zero_sys.zip") is None,
              "System save with zero system ID must fail closed (None)")

        # Save data rank > 1 (invalid rank)
        p_bad_rank = os.path.join(tmpdir, "dumps", "reject_bad_rank.zip")
        make_zip_file(p_bad_rank, {NX_SAVE_META_NAME: pack_jksv85(rank=2, ts=posix_sec)})
        check(inspect_backup_archive_model(p_bad_rank, "reject_bad_rank.zip") is None,
              "Save data rank 2 must fail closed (None)")

        # Invalid source_space 255
        p_bad_space = os.path.join(tmpdir, "dumps", "reject_bad_space.zip")
        make_zip_file(p_bad_space, {NX_SAVE_META_NAME: pack_jksv_tail86(owner_id=0x01000000000100FF, source_space=255, ts=posix_sec)})
        check(inspect_backup_archive_model(p_bad_space, "reject_bad_space.zip") is None,
              "Invalid source space 255 must fail closed (None)")

        # Truncated metadata (< 85 bytes)
        p_trunc = os.path.join(tmpdir, "dumps", "reject_trunc.zip")
        make_zip_file(p_trunc, {NX_SAVE_META_NAME: b"\x00" * 50})
        check(inspect_backup_archive_model(p_trunc, "reject_trunc.zip") is None,
              "Truncated metadata must fail closed (None)")

        # Verify that running directory scanner over /dumps rejects all invalid archives:
        groups = read_backup_entries_model(tmpdir)
        check(len(groups) == 0, f"No invalid archives must be admitted by the scanner! Got {len(groups)}")

    print("  -> Suite 3 (Wire Fields, POSIX Conversion, & Rejection Fixtures) PASSED.")


def test_suite_4_real_archive_identity_and_grouping_matrix() -> None:
    print("[5] Running Suite 4: Real Archive Identity & Grouping Matrix...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        os.makedirs(dumps_dir, exist_ok=True)

        app1 = 0x0100000000030000
        uid1 = (0x1111111111111111, 0x2222222222222222)
        posix_ts = 1773835200

        # 1. Rank 0 vs Rank 1: admitted into SEPARATE groups (v0.13.862 provenance)
        z_rank0 = os.path.join(dumps_dir, "rank0.zip")
        z_rank1 = os.path.join(dumps_dir, "rank1.zip")
        make_zip_file(z_rank0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app1, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_rank1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app1, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 10)})

        # 2. JKSV86 rank 0 and rank 1 distinct groups with source spaces User/SdUser
        app_j86 = 0x0100000000031000
        z_j86_r0 = os.path.join(dumps_dir, "j86_rank0.zip")
        z_j86_r1 = os.path.join(dumps_dir, "j86_rank1.zip")
        make_zip_file(z_j86_r0, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_j86, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, rank=0, source_space=1, ts=posix_ts)})
        make_zip_file(z_j86_r1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_j86, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, rank=1, source_space=2, ts=posix_ts + 10)})

        # 3. Sphaira legacy128 rank 0 and rank 1 distinct groups
        app_sph128 = 0x0100000000032000
        z_sph_r0 = os.path.join(dumps_dir, "sph_rank0.zip")
        z_sph_r1 = os.path.join(dumps_dir, "sph_rank1.zip")
        make_zip_file(z_sph_r0, {NX_SAVE_META_NAME: pack_sphaira128(app_id=app_sph128, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_sph_r1, {NX_SAVE_META_NAME: pack_sphaira128(app_id=app_sph128, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 10)})

        # 4. DBI extra 512 rank 0 and rank 1 distinct groups
        app_dbi512 = 0x0100000000033000
        z_dbi_r0 = os.path.join(dumps_dir, "dbi512_rank0.zip")
        z_dbi_r1 = os.path.join(dumps_dir, "dbi512_rank1.zip")
        make_zip_file(z_dbi_r0, {DBI_SAVE_EXTRA_NAME: pack_dbi_raw512(app_id=app_dbi512, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_dbi_r1, {DBI_SAVE_EXTRA_NAME: pack_dbi_raw512(app_id=app_dbi512, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 10)})

        # 5. 3-way test on same app/UID/index: known Primary (rk:0), known Secondary (rk:1), and unknown metadata-free (rk:?)
        app_3way = 0x0100000000034000
        z_3w_r0 = os.path.join(dumps_dir, "0100000000034000_D_20260918120000.zip")
        z_3w_r1 = os.path.join(dumps_dir, "0100000000034000_D_20260918120001.zip")
        z_3w_un = os.path.join(dumps_dir, "0100000000034000_D_20260918120002.zip")
        make_zip_file(z_3w_r0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_3way, save_type=SAVE_TYPE_DEVICE, uid_low=0, uid_high=0, rank=0, ts=posix_ts)})
        make_zip_file(z_3w_r1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_3way, save_type=SAVE_TYPE_DEVICE, uid_low=0, uid_high=0, rank=1, ts=posix_ts + 1)})
        make_zip_file(z_3w_un, {"save.dat": b"3way_un_bytes"})

        # 6. Index 0 vs Index 1: admitted into SEPARATE groups
        app2 = 0x0100000000040000
        z_idx0 = os.path.join(dumps_dir, "idx0.zip")
        z_idx1 = os.path.join(dumps_dir, "idx1.zip")
        make_zip_file(z_idx0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app2, uid_low=uid1[0], uid_high=uid1[1], index=0, ts=posix_ts)})
        make_zip_file(z_idx1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app2, uid_low=uid1[0], uid_high=uid1[1], index=1, ts=posix_ts)})

        # 7. Cache type (type 5) with Index 0 vs Index 1: admitted into SEPARATE groups
        app_cache = 0x0100000000050000
        z_c0 = os.path.join(dumps_dir, "cache0.zip")
        z_c1 = os.path.join(dumps_dir, "cache1.zip")
        make_zip_file(z_c0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_cache, uid_low=uid1[0], uid_high=uid1[1], save_type=SAVE_TYPE_CACHE, index=0, ts=posix_ts)})
        make_zip_file(z_c1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_cache, uid_low=uid1[0], uid_high=uid1[1], save_type=SAVE_TYPE_CACHE, index=1, ts=posix_ts)})

        # 8. Cache in User vs SdUser (source_space 1 vs 2) merging into 1 group when rank matches
        app_cache_sp = 0x0100000000051000
        z_csp1 = os.path.join(dumps_dir, "cache_sp1.zip")
        z_csp2 = os.path.join(dumps_dir, "cache_sp2.zip")
        make_zip_file(z_csp1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_cache_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, save_type=SAVE_TYPE_CACHE, source_space=1, rank=0, ts=posix_ts)})
        make_zip_file(z_csp2, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_cache_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, save_type=SAVE_TYPE_CACHE, source_space=2, rank=0, ts=posix_ts + 1)})

        # 9. Save data type: Account (1) vs Device (3): admitted into SEPARATE groups
        app_dev = 0x0100000000060000
        z_acc = os.path.join(dumps_dir, "acc.zip")
        z_dev = os.path.join(dumps_dir, "dev.zip")
        make_zip_file(z_acc, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_dev, uid_low=uid1[0], uid_high=uid1[1], save_type=SAVE_TYPE_ACCOUNT, ts=posix_ts)})
        make_zip_file(z_dev, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_dev, uid_low=0, uid_high=0, save_type=SAVE_TYPE_DEVICE, ts=posix_ts)})

        # 10. System saves differing in system_save_data_id: admitted into SEPARATE groups
        z_sys1 = os.path.join(dumps_dir, "sys1.zip")
        z_sys2 = os.path.join(dumps_dir, "sys2.zip")
        make_zip_file(z_sys1, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000010, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, ts=posix_ts)})
        make_zip_file(z_sys2, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000020, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, ts=posix_ts)})

        # 11. System saves across alternate valid system spaces -> merge into 1 group (count 2)
        sys_id_sp = 0x8000000000000030
        z_sys_sp1 = os.path.join(dumps_dir, "sys_sp1.zip")
        z_sys_sp2 = os.path.join(dumps_dir, "sys_sp2.zip")
        make_zip_file(z_sys_sp1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=0, sys_id=sys_id_sp, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, owner_id=0x0100000000010055, source_space=0, rank=0, ts=posix_ts)})
        make_zip_file(z_sys_sp2, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=0, sys_id=sys_id_sp, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, owner_id=0x0100000000010055, source_space=100, rank=0, ts=posix_ts + 1)})

        # 12. UID-high-only difference: admitted into SEPARATE groups
        app_uid = 0x0100000000070000
        z_uh1 = os.path.join(dumps_dir, "uid_high1.zip")
        z_uh2 = os.path.join(dumps_dir, "uid_high2.zip")
        make_zip_file(z_uh1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_uid, uid_low=0x5555, uid_high=0x6666, ts=posix_ts)})
        make_zip_file(z_uh2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_uid, uid_low=0x5555, uid_high=0x7777, ts=posix_ts)})

        # 13. Concrete source-space facts (source_space 1 vs 2): admitted into SAME group
        app_sp = 0x0100000000080000
        z_sp1 = os.path.join(dumps_dir, "sp1.zip")
        z_sp2 = os.path.join(dumps_dir, "sp2.zip")
        make_zip_file(z_sp1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, source_space=1, ts=posix_ts)})
        make_zip_file(z_sp2, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, source_space=2, ts=posix_ts + 5)})

        # 14. System (0) vs SystemBcat (6): admitted into SEPARATE groups
        z_s0 = os.path.join(dumps_dir, "system_0.zip")
        z_s6 = os.path.join(dumps_dir, "system_bcat.zip")
        make_zip_file(z_s0, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000099, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, ts=posix_ts)})
        make_zip_file(z_s6, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000099, save_type=SAVE_TYPE_SYSTEM_BCAT, uid_low=0, uid_high=0, ts=posix_ts)})

        # 15. Mixed wire versions (85, 86, 128, 512) for same save: admitted into SAME group
        app_mix = 0x01000000000A0000
        z_v85 = os.path.join(dumps_dir, "v85.zip")
        z_v86 = os.path.join(dumps_dir, "v86.zip")
        z_v128 = os.path.join(dumps_dir, "v128.zip")
        z_v512 = os.path.join(dumps_dir, "v512.zip")
        make_zip_file(z_v85, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], ts=posix_ts)})
        make_zip_file(z_v86, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, ts=posix_ts + 1)})
        make_zip_file(z_v128, {NX_SAVE_META_NAME: pack_sphaira128(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], ts=posix_ts + 2)})
        make_zip_file(z_v512, {DBI_SAVE_EXTRA_NAME: pack_dbi_raw512(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], ts=posix_ts + 3)})

        # 16. Two compatible metadata-free DBI legacy archives: merge into 1 group with unknown rank
        app_dbi = 0x01000000000B0000
        z_dbi_legacy1 = os.path.join(dumps_dir, "01000000000B0000_A_20260918140000.zip")
        z_dbi_legacy2 = os.path.join(dumps_dir, "01000000000B0000_A_20260918140001.zip")
        make_zip_file(z_dbi_legacy1, {"save.dat": b"dbi_legacy_bytes1"})
        make_zip_file(z_dbi_legacy2, {"save.dat": b"dbi_legacy_bytes2"})

        # Run actual filesystem scanner
        groups = read_backup_entries_model(tmpdir)

        # Map groups by group key
        by_key = {
            backup_group_key(g.application_id, g.system_save_data_id, g.save_data_type, g.uid_low, g.uid_high, g.save_data_index, g.backup_rank_known, g.save_data_rank): g
            for g in groups
        }

        # Assertions on grouping:
        # Rank 0 & 1 -> 2 SEPARATE groups (v0.13.862 provenance)
        k_rank0 = backup_group_key(app1, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_rank1 = backup_group_key(app1, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_rank0 in by_key and by_key[k_rank0].backup_count == 1, "Rank 0 must form distinct group (count 1)")
        check(k_rank1 in by_key and by_key[k_rank1].backup_count == 1, "Rank 1 must form distinct group (count 1)")
        check(k_rank0 != k_rank1, "Rank 0 and Rank 1 group keys must differ")

        # JKSV86 rank 0 and 1 -> 2 distinct groups
        k_j86_r0 = backup_group_key(app_j86, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_j86_r1 = backup_group_key(app_j86, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_j86_r0 in by_key and k_j86_r1 in by_key and k_j86_r0 != k_j86_r1, "JKSV86 rank 0 and 1 must form distinct groups")

        # Sphaira legacy128 rank 0 and 1 -> 2 distinct groups
        k_sph_r0 = backup_group_key(app_sph128, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_sph_r1 = backup_group_key(app_sph128, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_sph_r0 in by_key and k_sph_r1 in by_key and k_sph_r0 != k_sph_r1, "Sphaira legacy128 rank 0 and 1 must form distinct groups")

        # DBI extra 512 rank 0 and 1 -> 2 distinct groups
        k_dbi_r0 = backup_group_key(app_dbi512, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_dbi_r1 = backup_group_key(app_dbi512, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_dbi_r0 in by_key and k_dbi_r1 in by_key and k_dbi_r0 != k_dbi_r1, "DBI 512 rank 0 and 1 must form distinct groups")

        # 3-way test on same app/UID/index: Primary, Secondary, Unknown -> 3 distinct groups
        k_3w_r0 = backup_group_key(app_3way, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=True, rank=0)
        k_3w_r1 = backup_group_key(app_3way, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=True, rank=1)
        k_3w_un = backup_group_key(app_3way, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=False, rank=0)
        check(k_3w_r0 in by_key and k_3w_r1 in by_key and k_3w_un in by_key, "3-way archives must all be admitted")
        check(len({k_3w_r0, k_3w_r1, k_3w_un}) == 3, "3-way archives must form 3 completely distinct groups")

        # Index 0 & 1 -> 2 distinct groups
        k_idx0 = backup_group_key(app2, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_idx1 = backup_group_key(app2, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 1, rank_known=True, rank=0)
        check(k_idx0 in by_key and k_idx1 in by_key and k_idx0 != k_idx1, "Index 0 and Index 1 must form distinct groups")

        # Cache Index 0 & 1 -> 2 distinct groups
        k_c0 = backup_group_key(app_cache, 0, SAVE_TYPE_CACHE, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_c1 = backup_group_key(app_cache, 0, SAVE_TYPE_CACHE, uid1[0], uid1[1], 1, rank_known=True, rank=0)
        check(k_c0 in by_key and k_c1 in by_key and k_c0 != k_c1, "Cache index 0 and 1 must form distinct groups")

        # Cache in User vs SdUser (source space 1 vs 2) -> merge into 1 group (count 2)
        k_csp = backup_group_key(app_cache_sp, 0, SAVE_TYPE_CACHE, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        check(k_csp in by_key and by_key[k_csp].backup_count == 2, "Cache User/SdUser archives must merge into 1 group")

        # Account vs Device -> 2 distinct groups
        k_acc = backup_group_key(app_dev, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_dev = backup_group_key(app_dev, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=True, rank=0)
        check(k_acc in by_key and k_dev in by_key and k_acc != k_dev, "Account and Device must form distinct groups")

        # System saves with different system IDs -> 2 distinct groups
        k_sys1 = backup_group_key(0, 0x8000000000000010, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        k_sys2 = backup_group_key(0, 0x8000000000000020, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        check(k_sys1 in by_key and k_sys2 in by_key and k_sys1 != k_sys2, "Different system IDs must form distinct groups")

        # System saves across alternate valid system spaces -> merge into 1 group (count 2)
        k_sys_sp = backup_group_key(0, sys_id_sp, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        check(k_sys_sp in by_key and by_key[k_sys_sp].backup_count == 2, "System saves across spaces must merge into 1 group")

        # UID-high distinction -> 2 distinct groups
        k_uh1 = backup_group_key(app_uid, 0, SAVE_TYPE_ACCOUNT, 0x5555, 0x6666, 0, rank_known=True, rank=0)
        k_uh2 = backup_group_key(app_uid, 0, SAVE_TYPE_ACCOUNT, 0x5555, 0x7777, 0, rank_known=True, rank=0)
        check(k_uh1 in by_key and k_uh2 in by_key and k_uh1 != k_uh2, "Differing UID-high must form distinct groups")

        # Source-space facts -> single group with backup_count == 2
        k_sp = backup_group_key(app_sp, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        check(k_sp in by_key and by_key[k_sp].backup_count == 2, "Different source spaces must merge into 1 group")

        # System (0) vs SystemBcat (6) -> 2 distinct groups
        k_s0 = backup_group_key(0, 0x8000000000000099, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        k_s6 = backup_group_key(0, 0x8000000000000099, SAVE_TYPE_SYSTEM_BCAT, 0, 0, 0, rank_known=True, rank=0)
        check(k_s0 in by_key and k_s6 in by_key and k_s0 != k_s6, "System and SystemBcat must form distinct groups")

        # Mixed wire versions -> single group with backup_count == 4
        k_mix = backup_group_key(app_mix, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        check(k_mix in by_key and by_key[k_mix].backup_count == 4, "All 4 wire formats must merge into 1 group (count 4)")

        # 2 compatible metadata-free DBI legacy archives -> merge into 1 group with unknown rank
        k_dbi = backup_group_key(app_dbi, 0, SAVE_TYPE_ACCOUNT, 0, 0, 0, rank_known=False, rank=0)
        check(k_dbi in by_key and by_key[k_dbi].backup_count == 2, "Compatible metadata-free DBI archives must merge into 1 group (count 2)")

        # Independent exact artifact membership mapping for all scan-produced groups:
        expected_memberships: Dict[str, Set[str]] = {
            k_rank0: {"/dumps/rank0.zip"},
            k_rank1: {"/dumps/rank1.zip"},
            k_j86_r0: {"/dumps/j86_rank0.zip"},
            k_j86_r1: {"/dumps/j86_rank1.zip"},
            k_sph_r0: {"/dumps/sph_rank0.zip"},
            k_sph_r1: {"/dumps/sph_rank1.zip"},
            k_dbi_r0: {"/dumps/dbi512_rank0.zip"},
            k_dbi_r1: {"/dumps/dbi512_rank1.zip"},
            k_3w_r0: {"/dumps/0100000000034000_D_20260918120000.zip"},
            k_3w_r1: {"/dumps/0100000000034000_D_20260918120001.zip"},
            k_3w_un: {"/dumps/0100000000034000_D_20260918120002.zip"},
            k_idx0: {"/dumps/idx0.zip"},
            k_idx1: {"/dumps/idx1.zip"},
            k_c0: {"/dumps/cache0.zip"},
            k_c1: {"/dumps/cache1.zip"},
            k_csp: {"/dumps/cache_sp1.zip", "/dumps/cache_sp2.zip"},
            k_acc: {"/dumps/acc.zip"},
            k_dev: {"/dumps/dev.zip"},
            k_sys1: {"/dumps/sys1.zip"},
            k_sys2: {"/dumps/sys2.zip"},
            k_sys_sp: {"/dumps/sys_sp1.zip", "/dumps/sys_sp2.zip"},
            k_uh1: {"/dumps/uid_high1.zip"},
            k_uh2: {"/dumps/uid_high2.zip"},
            k_sp: {"/dumps/sp1.zip", "/dumps/sp2.zip"},
            k_s0: {"/dumps/system_0.zip"},
            k_s6: {"/dumps/system_bcat.zip"},
            k_mix: {"/dumps/v85.zip", "/dumps/v86.zip", "/dumps/v128.zip", "/dumps/v512.zip"},
            k_dbi: {"/dumps/01000000000B0000_A_20260918140000.zip", "/dumps/01000000000B0000_A_20260918140001.zip"},
        }

        # Check that scanner produced exactly these groups with no extras or omissions:
        check(len(groups) == len(expected_memberships),
              f"Expected exactly {len(expected_memberships)} groups from scanner, got {len(groups)}")
        check(set(by_key.keys()) == set(expected_memberships.keys()),
              "Scanner group keys must exactly match expected group keys")

        # For every scan-produced group, call collect_group_archives_model and assert exact path membership:
        for key, expected_paths in expected_memberships.items():
            g = by_key[key]
            collected = collect_group_archives_model(g, tmpdir)
            collected_paths = {c.path for c in collected}
            check(collected_paths == expected_paths,
                  f"Group '{key}' collected paths {collected_paths} did not match expected {expected_paths}")
            check(len(collected) == g.backup_count,
                  f"Group '{key}' candidate count {len(collected)} != group.backup_count {g.backup_count}")
            for c in collected:
                check(c.ts > 0, f"Candidate {c.path} in group {key} must have valid positive timestamp, got {c.ts}")

        # Live entry matching: live FsSaveDataInfo entry matches only matching rank archive group
        live_dir = os.path.join(dumps_dir, "LiveApp")
        os.makedirs(live_dir, exist_ok=True)
        app_live = 0x01000000000EE000
        z_live_r0 = os.path.join(live_dir, "live_rank0.zip")
        z_live_r1 = os.path.join(live_dir, "live_rank1.zip")
        make_zip_file(z_live_r0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_live, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_live_r1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_live, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 1)})

        live_e_r0 = EntryModel(application_id=app_live, uid_low=uid1[0], uid_high=uid1[1], save_data_type=SAVE_TYPE_ACCOUNT, save_data_rank=0, is_backup=False, name="LiveApp")
        live_e_r1 = EntryModel(application_id=app_live, uid_low=uid1[0], uid_high=uid1[1], save_data_type=SAVE_TYPE_ACCOUNT, save_data_rank=1, is_backup=False, name="LiveApp")

        cands_live_r0 = collect_group_archives_model(live_e_r0, tmpdir)
        check(len(cands_live_r0) == 1 and cands_live_r0[0].path == "/dumps/LiveApp/live_rank0.zip",
              f"Live entry rank 0 must collect only live_rank0.zip, got {[c.path for c in cands_live_r0]}")

        cands_live_r1 = collect_group_archives_model(live_e_r1, tmpdir)
        check(len(cands_live_r1) == 1 and cands_live_r1[0].path == "/dumps/LiveApp/live_rank1.zip",
              f"Live entry rank 1 must collect only live_rank1.zip, got {[c.path for c in cands_live_r1]}")

    print("  -> Suite 4 (Real Archive Identity & Grouping Matrix) PASSED.")


def test_suite_5_connected_picker_and_destination_boundary() -> None:
    print("[6] Running Suite 5: Connected Picker Callback & Destination Object Boundary...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        os.makedirs(dumps_dir, exist_ok=True)

        app_id = 0x0100000000090000
        source_uid = (0x1111222233334444, 0x5555666677778888)

        # 3 candidates with distinct payloads to verify exact chosen artifact readback:
        p1 = os.path.join(dumps_dir, "alpha_1.zip")
        p2 = os.path.join(dumps_dir, "alpha_2.zip")
        p3 = os.path.join(dumps_dir, "beta_3.zip")

        make_zip_file(p1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=source_uid[0], uid_high=source_uid[1], ts=1773835200), "game.sav": b"PAYLOAD_ONE"})
        make_zip_file(p2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=source_uid[0], uid_high=source_uid[1], ts=1773835200), "game.sav": b"PAYLOAD_TWO"})
        make_zip_file(p3, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=source_uid[0], uid_high=source_uid[1], ts=1773838800), "game.sav": b"PAYLOAD_THREE"})

        # Collect via scanner:
        groups = read_backup_entries_model(tmpdir)
        check(len(groups) == 1, "Expected 1 group from scanner")
        group = groups[0]

        candidates = collect_group_archives_model(group, tmpdir)
        check(len(candidates) == 3, f"Expected 3 candidates, got {len(candidates)}")

        # 1. Test Picker Label Disambiguation:
        labels = disambiguate_picker_labels(candidates)
        # candidates[0] is p3 (ts 1773838800, unique date) -> clean label
        # candidates[1] and [2] are p1 and p2 (ts 1773835200, identical date) -> appended with path
        check("(" not in labels[0], f"Unique date must NOT have path appended, got: {labels[0]}")
        check(f"({candidates[1].path})" in labels[1], f"Duplicate date 1 must have path appended, got: {labels[1]}")
        check(f"({candidates[2].path})" in labels[2], f"Duplicate date 2 must have path appended, got: {labels[2]}")

        # 2. Test Live Destination Target Object Invariance:
        # Explicit live destination target (e.g. on-console target for Account user remap):
        live_target = EntryModel(
            application_id=app_id,
            system_save_data_id=0,
            save_data_type=SAVE_TYPE_ACCOUNT,
            save_data_space_id=1,  # User space
            save_data_id=0xCAFE000000001234,
            uid_low=0xAAAABBBBCCCCDDDD,  # Destination UID differs from source archive UID!
            uid_high=0xEEEEFFFF00001111,
            save_data_index=0,
            save_data_rank=0,
            is_backup=False
        )

        # Snapshot all 9 destination target fields:
        orig_app = live_target.application_id
        orig_sys = live_target.system_save_data_id
        orig_type = live_target.save_data_type
        orig_space = live_target.save_data_space_id
        orig_save_id = live_target.save_data_id
        orig_ul = live_target.uid_low
        orig_uh = live_target.uid_high
        orig_idx = live_target.save_data_index
        orig_rank = live_target.save_data_rank

        # Model picker callback selecting index 1 (which corresponds to payload PAYLOAD_ONE or PAYLOAD_TWO):
        selected_index = 1
        chosen_candidate = candidates[selected_index]

        # Modeled Restore boundary:
        restore_recorded_calls = []

        def modeled_restore_boundary(target: EntryModel, chosen_path: str):
            # Read payload directly from chosen archive
            real_p = os.path.join(tmpdir, chosen_path.lstrip("/"))
            read_payload = read_zip_entry(real_p, "game.sav")
            restore_recorded_calls.append({
                "target_app": target.application_id,
                "target_uid": (target.uid_low, target.uid_high),
                "target_save_id": target.save_data_id,
                "chosen_path": chosen_path,
                "payload": read_payload
            })

        # Execute picker callback:
        modeled_restore_boundary(live_target, chosen_candidate.path)

        # Verify exact payload read back from chosen artifact:
        check(len(restore_recorded_calls) == 1, "Exactly 1 restore call must be recorded")
        rec = restore_recorded_calls[0]
        check(rec["chosen_path"] == chosen_candidate.path, "Chosen path must match candidate path")
        expected_payload = b"PAYLOAD_ONE" if "alpha_1" in chosen_candidate.path else b"PAYLOAD_TWO"
        check(rec["payload"] == expected_payload, f"Expected {expected_payload}, got {rec['payload']}")

        # Explicit recording boundary assertions:
        check(rec["target_app"] == live_target.application_id, "Recorded target_app must equal live target application_id")
        check(rec["target_uid"] == (live_target.uid_low, live_target.uid_high), "Recorded target_uid must equal live target UID")
        check(rec["target_save_id"] == live_target.save_data_id, "Recorded target_save_id must equal live target save_data_id")
        check(rec["target_uid"] != source_uid, "Recorded target UID must differ from source candidate UID (Account user remap)")

        # Verify ALL 9 live target fields are completely UNCHANGED after restore:
        check(live_target.application_id == orig_app, "Target application_id must be unchanged")
        check(live_target.system_save_data_id == orig_sys, "Target system_save_data_id must be unchanged")
        check(live_target.save_data_type == orig_type, "Target save_data_type must be unchanged")
        check(live_target.save_data_space_id == orig_space, "Target save_data_space_id must be unchanged")
        check(live_target.save_data_id == orig_save_id, "Target save_data_id must be unchanged")
        check(live_target.uid_low == orig_ul, "Target uid_low must be unchanged")
        check(live_target.uid_high == orig_uh, "Target uid_high must be unchanged")
        check(live_target.save_data_index == orig_idx, "Target save_data_index must be unchanged")
        check(live_target.save_data_rank == orig_rank, "Target save_data_rank must be unchanged")

    print("  -> Suite 5 (Connected Picker & Destination Boundary) PASSED.")


def test_suite_6_actions_sentinels_and_readmission() -> None:
    print("[7] Running Suite 6: Actions, Sentinels, & Readmission (Verify, Prune, Delete)...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        target_dir = os.path.join(dumps_dir, "TargetGame")
        other_dir = os.path.join(dumps_dir, "OtherGame")
        os.makedirs(target_dir, exist_ok=True)
        os.makedirs(other_dir, exist_ok=True)

        app_id = 0x01000000000C0000
        uid = (0x3333, 0x4444)

        # 3 initial valid group archives
        p_newest = os.path.join(target_dir, "20260918150000.zip")
        p_middle = os.path.join(target_dir, "20260918140000.zip")
        p_oldest = os.path.join(target_dir, "20260918130000.zip")

        make_zip_file(p_newest, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773838800), "data.bin": b"newest"})
        make_zip_file(p_middle, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835200), "data.bin": b"middle"})
        make_zip_file(p_oldest, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773831600), "data.bin": b"oldest"})

        # Foreign sentinels:
        p_other = os.path.join(other_dir, "other_game.zip")
        make_zip_file(p_other, {NX_SAVE_META_NAME: pack_jksv85(app_id=0x01000000000D0000, uid_low=uid[0], uid_high=uid[1], ts=1773835200), "data.bin": b"other"})
        other_bytes_initial = open(p_other, "rb").read()

        p_sentinel = os.path.join(dumps_dir, "root_sentinel.bin")
        with open(p_sentinel, "wb") as f:
            f.write(b"TOP_LEVEL_SENTINEL_PAYLOAD")
        sentinel_bytes_initial = open(p_sentinel, "rb").read()

        # Run scanner to construct group:
        groups = read_backup_entries_model(tmpdir)
        group = next(g for g in groups if g.application_id == app_id)
        check(group.backup_count == 3, "Group must have backup_count 3 from scanner")

        # 1. Invariant: Post-scan added file is NOT touched or rediscovered:
        p_post_scan = os.path.join(target_dir, "post_scan_valid.zip")
        make_zip_file(p_post_scan, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773839999), "data.bin": b"post_scan"})
        post_scan_bytes_initial = open(p_post_scan, "rb").read()

        # CollectGroupArchives on existing group: iterates ONLY group.backup_members!
        cands_verify = collect_group_archives_model(group, tmpdir)
        check(len(cands_verify) == 3, "CollectGroupArchives must not discover post-scan added file!")
        check(all(c.path != "/dumps/TargetGame/post_scan_valid.zip" for c in cands_verify),
              "Post-scan added file must NOT be in collected candidates")
        check(open(p_post_scan, "rb").read() == post_scan_bytes_initial, "Post-scan file bytes must be untouched")

        # 2. Invariant: Foreign injected path into backup_members is dropped:
        group.backup_members.append(BackupCandidateModel(ts=1773835200, path="/dumps/OtherGame/other_game.zip", source=1))
        cands_foreign = collect_group_archives_model(group, tmpdir)
        check(len(cands_foreign) == 3, "Foreign injected path must be dropped by BackupGroupKey validation!")
        check(open(p_other, "rb").read() == other_bytes_initial, "Foreign file bytes must be untouched")
        group.backup_members.pop()  # Clean up injected test member

        # 3. VERIFY operation (all 3 archives readable):
        for c in cands_verify:
            real_p = os.path.join(tmpdir, c.path.lstrip("/"))
            with zipfile.ZipFile(real_p, "r") as zf:
                check(zf.testzip() is None, f"Archive {c.path} must pass ZIP integrity check")

        # 4. PRUNE operation (DeleteOlderBackups):
        # Capture newest archive full bytes before prune to verify byte preservation:
        newest_bytes_before_prune = open(p_newest, "rb").read()

        # Keeps newest, deletes middle and oldest exact paths:
        newest_cand = cands_verify[0]
        older_cands = cands_verify[1:]
        check(len(older_cands) == 2, "Prune must find 2 older candidates")

        for c in older_cands:
            os.remove(os.path.join(tmpdir, c.path.lstrip("/")))

        check(os.path.exists(p_newest), "Newest archive must remain on disk after prune")
        check(open(p_newest, "rb").read() == newest_bytes_before_prune,
              "Newest archive bytes must be perfectly preserved byte-for-byte after prune")
        check(not os.path.exists(p_middle), "Middle archive must be deleted by prune")
        check(not os.path.exists(p_oldest), "Oldest archive must be deleted by prune")
        check(open(p_other, "rb").read() == other_bytes_initial, "Foreign other_game.zip must be untouched by prune")
        check(open(p_sentinel, "rb").read() == sentinel_bytes_initial, "Root sentinel must be untouched by prune")
        check(open(p_post_scan, "rb").read() == post_scan_bytes_initial, "Post-scan file must be untouched by prune")

        # 5. Readmission with deleted member:
        # After prune, CollectGroupArchives returns only remaining 1 archive:
        cands_after_prune = collect_group_archives_model(group, tmpdir)
        check(len(cands_after_prune) == 1, f"Expected 1 candidate after prune, got {len(cands_after_prune)}")
        check(cands_after_prune[0].path == "/dumps/TargetGame/20260918150000.zip", "Remaining archive must be newest")

        # 6. DELETE operation (DeleteBackupGroups):
        for c in cands_after_prune:
            os.remove(os.path.join(tmpdir, c.path.lstrip("/")))
        check(not os.path.exists(p_newest), "Newest archive must be deleted by delete operation")

        # Nonrecursive parent directory cleanup:
        # TargetGame still contains post_scan_valid.zip, so it must NOT be removed!
        if os.path.isdir(target_dir) and not os.listdir(target_dir):
            os.rmdir(target_dir)
        check(os.path.isdir(target_dir), "Parent dir with remaining post_scan file must NOT be deleted")

        # Clean up post_scan file, now TargetGame becomes empty and is removed:
        os.remove(p_post_scan)
        if os.path.isdir(target_dir) and not os.listdir(target_dir):
            os.rmdir(target_dir)
        check(not os.path.exists(target_dir), "Empty parent directory must be removed by nonrecursive cleanup")

        # Dumps root and sentinels remain intact:
        check(os.path.isdir(dumps_dir), "Dumps root directory must NOT be deleted")
        check(os.path.exists(p_sentinel), "Root sentinel must remain intact")
        check(os.path.exists(p_other), "Other game folder and archive must remain intact")

        # 7. Empty result reporting without stale fallback:
        cands_empty = collect_group_archives_model(group, tmpdir)
        check(len(cands_empty) == 0, "CollectGroupArchives must return empty list when all members deleted")

        def simulate_restore_caller(archives: List[BackupCandidateModel], e: EntryModel) -> str:
            if len(archives) == 1:
                return f"RESTORE_PICKED:{archives[0].path}"
            elif len(archives) > 1:
                return f"POPUP_PICKER:{len(archives)}"
            elif e.is_backup:
                return "NO_BACKUPS_FOUND"
            elif e.backup_path:
                return f"STALE_FALLBACK:{e.backup_path}"
            return "FIND_LATEST_FALLBACK"

        res = simulate_restore_caller(cands_empty, group)
        check(res == "NO_BACKUPS_FOUND", f"Empty result for is_backup must report NO_BACKUPS_FOUND without fallback, got {res}")

        # Live entry (is_backup == False) preserves existing fallback:
        live_entry = EntryModel(application_id=app_id, is_backup=False, backup_path="/dumps/live_fallback.zip")
        res_live = simulate_restore_caller([], live_entry)
        check(res_live == "STALE_FALLBACK:/dumps/live_fallback.zip", "Live entry must preserve existing fallback")

        # 8. Fresh timestamp reinspection & re-sorting on arbitrary-named ZIPs:
        resort_dir = os.path.join(dumps_dir, "ResortTest")
        os.makedirs(resort_dir, exist_ok=True)
        app_resort = 0x01000000000E0000
        uid_resort = (0x6666, 0x7777)

        # Arbitrary-named ZIPs without filename timestamps (purely metadata POSIX timestamps):
        p_resort_1 = os.path.join(resort_dir, "custom_backup_one.zip")
        p_resort_2 = os.path.join(resort_dir, "custom_backup_two.zip")

        # Initially: custom_backup_one has older ts (1773831000), custom_backup_two has newer ts (1773832000)
        make_zip_file(p_resort_1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_resort, uid_low=uid_resort[0], uid_high=uid_resort[1], ts=1773831000), "data.bin": b"one"})
        make_zip_file(p_resort_2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_resort, uid_low=uid_resort[0], uid_high=uid_resort[1], ts=1773832000), "data.bin": b"two"})

        # Run scanner to produce group
        resort_groups = read_backup_entries_model(tmpdir)
        group_resort = next(g for g in resort_groups if g.application_id == app_resort)
        check(group_resort.backup_count == 2, "Resort group must have backup_count 2")

        # Verify initial order in backup_members: custom_backup_two is first, custom_backup_one is second
        initial_members_snapshot = [(m.path, m.ts) for m in group_resort.backup_members]
        check(initial_members_snapshot[0][0] == "/dumps/ResortTest/custom_backup_two.zip", "Initial order: two.zip must be first")
        check(initial_members_snapshot[1][0] == "/dumps/ResortTest/custom_backup_one.zip", "Initial order: one.zip must be second")

        # Initial collection reflects initial scan order
        cands_initial = collect_group_archives_model(group_resort, tmpdir)
        check(cands_initial[0].path == "/dumps/ResortTest/custom_backup_two.zip", "Initial collect: two.zip first")
        check(cands_initial[1].path == "/dumps/ResortTest/custom_backup_one.zip", "Initial collect: one.zip second")

        # Overwrite older path (custom_backup_one) with newer POSIX timestamp metadata (1773839999 > 1773832000):
        make_zip_file(p_resort_1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_resort, uid_low=uid_resort[0], uid_high=uid_resort[1], ts=1773839999), "data.bin": b"one_updated"})

        # Collect again via collect_group_archives_model:
        cands_resorted = collect_group_archives_model(group_resort, tmpdir)
        check(len(cands_resorted) == 2, "CollectGroupArchives must return both members after timestamp update")

        # Collection must reflect NEW order: custom_backup_one is now first!
        check(cands_resorted[0].path == "/dumps/ResortTest/custom_backup_one.zip",
              "Fresh reinspection must re-sort custom_backup_one to 1st place due to newer timestamp")
        check(cands_resorted[1].path == "/dumps/ResortTest/custom_backup_two.zip",
              "custom_backup_two must now be 2nd place")
        check(cands_resorted[0].ts == posix_to_timestamp(1773839999),
              "Re-inspected timestamp must reflect updated POSIX timestamp")

        # Assert that group.backup_members scan inventory remains completely UNCHANGED:
        current_members = [(m.path, m.ts) for m in group_resort.backup_members]
        check(current_members == initial_members_snapshot,
              "group.backup_members scan inventory must remain completely unchanged across reinspection and resort")

        # 9. Post-scan admission changes on actual scan-produced groups:
        adm_dir = os.path.join(dumps_dir, "AdmissionTest")
        os.makedirs(adm_dir, exist_ok=True)
        app_adm = 0x01000000000F0000
        uid_adm = (0x8888, 0x9999)

        p_adm_corrupt = os.path.join(adm_dir, "member_corrupt.zip")
        p_adm_diff = os.path.join(adm_dir, "member_diff.zip")
        p_adm_rank = os.path.join(adm_dir, "member_rank.zip")
        p_adm_valid = os.path.join(adm_dir, "member_valid.zip")

        # Create all 4 with valid metadata for app_adm (all rank 0) initially
        make_zip_file(p_adm_corrupt, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773831000), "data.bin": b"to_corrupt"})
        make_zip_file(p_adm_diff, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773832000), "data.bin": b"to_change_group"})
        make_zip_file(p_adm_rank, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773832500), "data.bin": b"to_mutate_rank"})
        make_zip_file(p_adm_valid, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773833000), "data.bin": b"stay_valid"})

        # Run actual scanner to construct group
        adm_groups = read_backup_entries_model(tmpdir)
        group_adm = next(g for g in adm_groups if g.application_id == app_adm)
        check(group_adm.backup_count == 4, f"Group must have 4 members from scanner, got {group_adm.backup_count}")
        check(len(group_adm.backup_members) == 4, "backup_members must have 4 items")

        # Now apply post-scan mutations on disk:
        # 1. Overwrite one retained member with corrupt bytes (invalid ZIP data)
        with open(p_adm_corrupt, "wb") as f:
            f.write(b"CORRUPT_NOT_A_VALID_ZIP_HEADER_DATA_1234567890")

        # 2. Overwrite another with valid metadata for a DIFFERENT group (different app_id)
        app_other = 0x01000000000F0001
        meta_diff = pack_jksv85(app_id=app_other, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773835000)
        make_zip_file(p_adm_diff, {NX_SAVE_META_NAME: meta_diff, "data.bin": b"different_group_payload"})
        diff_bytes_after = open(p_adm_diff, "rb").read()

        # 3. Overwrite another with mutated rank (rank=1 instead of rank=0)
        meta_rank_mut = pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=1, ts=1773836000)
        make_zip_file(p_adm_rank, {NX_SAVE_META_NAME: meta_rank_mut, "data.bin": b"mutated_rank_payload"})
        rank_bytes_after = open(p_adm_rank, "rb").read()

        # 4. Keep one valid member (p_adm_valid) untouched

        # Verify collection returns ONLY the valid member:
        cands_adm = collect_group_archives_model(group_adm, tmpdir)
        check(len(cands_adm) == 1, f"Collection must return only 1 valid member, got {len(cands_adm)}")
        check(cands_adm[0].path == "/dumps/AdmissionTest/member_valid.zip",
              f"Collection must return member_valid.zip, got {cands_adm[0].path}")

        # Verify changed-group and mutated-rank bytes are unaltered on disk:
        check(open(p_adm_diff, "rb").read() == diff_bytes_after,
              "Changed-group archive bytes must remain unaltered on disk during inspection")
        check(open(p_adm_rank, "rb").read() == rank_bytes_after,
              "Mutated-rank archive bytes must remain unaltered on disk during inspection")

        # Verify empty on removal with no stale fallback:
        os.remove(p_adm_valid)
        cands_adm_empty = collect_group_archives_model(group_adm, tmpdir)
        check(len(cands_adm_empty) == 0, f"Collection must return empty after valid member removed, got {len(cands_adm_empty)}")
        sim_res = simulate_restore_caller(cands_adm_empty, group_adm)
        check(sim_res == "NO_BACKUPS_FOUND",
              f"Caller must report NO_BACKUPS_FOUND with no stale fallback on removal, got {sim_res}")

    print("  -> Suite 6 (Actions, Sentinels, & Readmission) PASSED.")


def test_suite_7_source_anchors_and_model_lifetimes() -> None:
    print("[8] Running Suite 7: Source Anchors & Model-Level Lifetimes...")
    # Model-Level Container and Lifetime Invariants (clearly labeled as pure-Python model behavior,
    # not claiming C++ compiler/vector move proof):
    c1 = BackupCandidateModel(ts=100, path="/dumps/1.zip", source=0)
    c2 = BackupCandidateModel(ts=200, path="/dumps/2.zip", source=1)

    # 1. Container insertion
    entries: List[EntryModel] = []
    e = EntryModel(application_id=0x1234, backup_members=[c1, c2])
    entries.append(e)

    # 2. Model copy semantics
    copied = list(entries)
    check(len(copied[0].backup_members) == 2, "Copied Entry must preserve backup_members")

    # 3. Model move / source container destruction
    moved = EntryModel(application_id=entries[0].application_id, backup_members=list(entries[0].backup_members))
    entries.clear()
    check(len(moved.backup_members) == 2, "Moved Entry must survive destruction of source container")

    # 4. Lambda capture of EntryModel
    captured = moved
    cb = lambda: [c.path for c in captured.backup_members]
    check(cb() == ["/dumps/1.zip", "/dumps/2.zip"], "Lambda capture of Entry must retain backup_members independently")

    print("  -> Suite 7 (Source Anchors & Model-Level Lifetimes) PASSED.")


# ==============================================================================
# Main Runner
# ==============================================================================

def main() -> None:
    print("================================================================================")
    print("Sphaira v0.13.862: Save Backup Library Membership & Operations Contract Suite")
    print("================================================================================")

    test_static_source_contracts()
    test_suite_1_connected_scanner_and_proven_regression()
    test_suite_2_location_matrix_priority_and_tiebreaking()
    test_suite_3_wire_and_rejection_fixtures()
    test_suite_4_real_archive_identity_and_grouping_matrix()
    test_suite_5_connected_picker_and_destination_boundary()
    test_suite_6_actions_sentinels_and_readmission()
    test_suite_7_source_anchors_and_model_lifetimes()

    print("================================================================================")
    print("ALL 7 SAVE BACKUP LIBRARY CONTRACT SUITES PASSED SUCCESSFULLY")
    print("================================================================================")
    print("Compiler-Free Verification Notice:")
    print("Source contracts, structural invariants, wire layouts, and behavioral lifecycles")
    print("verified via pure Python. Bare-metal Switch hardware / C++ runtime execution")
    print("requires testing on actual hardware.")


if __name__ == "__main__":
    main()
