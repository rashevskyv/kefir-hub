# Models, parsers, and inspection routines for save backup library contract.
import os
import io
import re
import sys
import time
import struct
import zipfile
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Any, Tuple, Set

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

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

RES_OK = 0
FS_ERROR_PATH_NOT_FOUND = 0x202
FS_ERROR_TARGET_LOCKED = 0x244E02
ERR_IPC_READ_FAILED = 0x1002
ERR_CANNOT_RESTORE_TO_SYSTEM = 0xEE01
ERR_PREFLIGHT_EMPTY = 0xEE02
ERR_OVERFLOW = 0xEE03
ERR_VERIFICATION_MISMATCH = 0xEE04
ERR_PREFLIGHT_FAILED = 0xEE05

FS_SAVE_DATA_SPACE_ID_SYSTEM = 0
FS_SAVE_DATA_SPACE_ID_USER = 1
FS_SAVE_DATA_SPACE_ID_SD_SYSTEM = 2
FS_SAVE_DATA_SPACE_ID_TEMPORARY = 3
FS_SAVE_DATA_SPACE_ID_SD_USER = 4
FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM = 100
FS_SAVE_DATA_SPACE_ID_SAFE_MODE = 101

FS_SAVE_DATA_TYPE_SYSTEM = 0
FS_SAVE_DATA_TYPE_ACCOUNT = 1
FS_SAVE_DATA_TYPE_BCAT = 2
FS_SAVE_DATA_TYPE_DEVICE = 3
FS_SAVE_DATA_TYPE_TEMPORARY = 4
FS_SAVE_DATA_TYPE_CACHE = 5
FS_SAVE_DATA_TYPE_SYSTEM_BCAT = 6

SAVE_TYPE_SYSTEM = 0
SAVE_TYPE_ACCOUNT = 1
SAVE_TYPE_BCAT = 2
SAVE_TYPE_DEVICE = 3
SAVE_TYPE_TEMPORARY = 4
SAVE_TYPE_CACHE = 5
SAVE_TYPE_SYSTEM_BCAT = 6

NX_SAVE_META_MAGIC = 0x4A4B5356
NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"

def check(condition: bool, msg: str) -> None:
    if not condition:
        raise AssertionError(msg)

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
