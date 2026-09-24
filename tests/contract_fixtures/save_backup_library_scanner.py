# Directory scanner and group collection behavioral reference models.
import os
import io
import re
import struct
import zipfile
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Any, Tuple, Set, Callable
from contract_fixtures.save_backup_library_models import (
    RES_OK, FS_ERROR_PATH_NOT_FOUND, FS_ERROR_TARGET_LOCKED,
    FS_SAVE_DATA_SPACE_ID_SYSTEM, FS_SAVE_DATA_SPACE_ID_USER,
    FS_SAVE_DATA_SPACE_ID_SD_USER, FS_SAVE_DATA_TYPE_SYSTEM,
    FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_TYPE_DEVICE,
    FS_SAVE_DATA_TYPE_SYSTEM_BCAT, SAVE_TYPE_SYSTEM, SAVE_TYPE_ACCOUNT,
    SAVE_TYPE_BCAT, SAVE_TYPE_DEVICE, SAVE_TYPE_TEMPORARY,
    SAVE_TYPE_CACHE, SAVE_TYPE_SYSTEM_BCAT, NX_SAVE_META_MAGIC,
    NX_SAVE_META_NAME, DBI_SAVE_INFO_NAME, DBI_SAVE_EXTRA_NAME,
    is_system_like, make_zip_file, read_zip_entry, BackupCandidateModel,
    BackupArchiveInfoModel, EntryModel, backup_group_key, posix_to_timestamp,
    parse_backup_name_timestamp, parse_dbi_type_letter,
    parse_dbi_backup_app_id, parse_dbi_backup_index,
    inspect_backup_archive_model, check, make_zip
)

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

    out.sort(key=lambda c: (-c.ts, c.source, c.path))
    return out


@dataclass
class FsSaveDataExtraDataModel:
    attr: int = 0
    owner_id: int = 0
    timestamp: int = 0
    commit_id: int = 0
    unused: bytes = b""


def create_backup_if_newer_model(
    seeds: List[EntryModel],
    fs_root: str,
    backup_root: str = "/dumps",
    live_extra_provider: Optional[Callable[[EntryModel], Optional[FsSaveDataExtraDataModel]]] = None,
    should_exit_at_index: Optional[int] = None,
    predicate_mode: str = "new",
) -> Tuple[List[EntryModel], int, bool]:
    """Pure-Python behavioral reference model matching Menu::CreateBackupIfNewer in save_menu.cpp."""
    to_backup: List[EntryModel] = []
    up_to_date_count = 0

    for i, e in enumerate(seeds):
        if should_exit_at_index is not None and i == should_exit_at_index:
            return (to_backup, up_to_date_count, False)

        archives = collect_group_archives_model(e, fs_root, backup_root)
        if not archives:
            to_backup.append(e)
            continue

        newest = archives[0]
        fname = os.path.basename(newest.path)
        real_newest_path = os.path.join(fs_root, newest.path.lstrip("/"))
        binfo = inspect_backup_archive_model(real_newest_path, fname, e.dbi_game_dir)
        if not binfo:
            to_backup.append(e)
            continue

        live_extra = live_extra_provider(e) if live_extra_provider else None
        if live_extra is None:
            to_backup.append(e)
            continue

        if predicate_mode == "new":
            is_up_to_date = (
                live_extra.timestamp != 0 and
                binfo.source_timestamp != 0 and
                live_extra.commit_id != 0 and
                binfo.commit_id != 0 and
                live_extra.timestamp == binfo.source_timestamp and
                live_extra.commit_id == binfo.commit_id
            )
        else:  # "old"
            is_up_to_date = False
            if live_extra.timestamp != 0 and binfo.source_timestamp != 0:
                if live_extra.timestamp == binfo.source_timestamp:
                    if live_extra.commit_id != 0 and binfo.commit_id != 0:
                        is_up_to_date = (live_extra.commit_id == binfo.commit_id)
                    else:
                        is_up_to_date = True
            elif live_extra.commit_id != 0 and binfo.commit_id != 0:
                is_up_to_date = (live_extra.commit_id == binfo.commit_id)

        if is_up_to_date:
            up_to_date_count += 1
        else:
            to_backup.append(e)

    return (to_backup, up_to_date_count, True)


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
