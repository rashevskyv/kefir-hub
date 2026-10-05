# Encoding and decoding helpers for save backup publication contract.
import os
import struct
from dataclasses import dataclass
from typing import Tuple, Dict, Any

NX_SAVE_META_MAGIC = 0x4A4B5356
NX_SAVE_META_VERSION = 1
NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"

FS_SAVE_DATA_TYPE_SYSTEM = 0
FS_SAVE_DATA_TYPE_ACCOUNT = 1
FS_SAVE_DATA_TYPE_BCAT = 2
FS_SAVE_DATA_TYPE_DEVICE = 3
FS_SAVE_DATA_TYPE_TEMPORARY = 4
FS_SAVE_DATA_TYPE_CACHE = 5
FS_SAVE_DATA_TYPE_SYSTEM_BCAT = 6

FS_SAVE_DATA_SPACE_ID_SYSTEM = 0
FS_SAVE_DATA_SPACE_ID_USER = 1
FS_SAVE_DATA_SPACE_ID_SD_SYSTEM = 2
FS_SAVE_DATA_SPACE_ID_TEMPORARY = 3
FS_SAVE_DATA_SPACE_ID_SD_USER = 4
FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM = 100

@dataclass
class SaveAttribute:
    application_id: int
    uid_low: int
    uid_high: int
    system_save_data_id: int
    save_data_type: int
    save_data_rank: int
    save_data_index: int


@dataclass
class SelectedEntry:
    save_data_id: int
    save_data_space_id: int
    attr: SaveAttribute
    name: str = "TestSave"
    size: int = 0x100000


def encode_extra_data(
    attr: SaveAttribute,
    owner_id: int = 0x0100000000001000,
    timestamp: int = 1710000000,
    flags: int = 0,
    unk_x54: int = 0,
    data_size: int = 0x200000,
    journal_size: int = 0x200000,
    commit_id: int = 42,
    padding: bytes = b"\x00" * 4,
    unknown: bytes = b"\x00" * 24,
) -> bytes:
    """Encodes a 512-byte FsSaveDataExtraData structure."""
    header = struct.pack(
        "<QQQQBBH",
        attr.application_id,
        attr.uid_low,
        attr.uid_high,
        attr.system_save_data_id,
        attr.save_data_type,
        attr.save_data_rank,
        attr.save_data_index,
    )
    assert len(header) == 36
    assert len(padding) == 4
    assert len(unknown) == 24

    meta = struct.pack(
        "<QQIIqqQ",
        owner_id,
        timestamp,
        flags,
        unk_x54,
        data_size,
        journal_size,
        commit_id,
    )
    assert len(meta) == 48

    reserved = b"\x00" * (512 - 36 - 4 - 24 - 48)
    data = header + padding + unknown + meta + reserved
    assert len(data) == 512
    return data


def decode_extra_data(b: bytes) -> Tuple[SaveAttribute, Dict[str, Any]]:
    """Decodes a 512-byte FsSaveDataExtraData structure."""
    if len(b) != 512:
        raise ValueError(f"Expected 512 bytes, got {len(b)}")

    app, ul, uh, sid, stype, rank, idx = struct.unpack("<QQQQBBH", b[:36])
    oid, ts, flags, unk_x54, dsize, jsize, cid = struct.unpack("<QQIIqqQ", b[64:112])

    attr = SaveAttribute(
        application_id=app,
        uid_low=ul,
        uid_high=uh,
        system_save_data_id=sid,
        save_data_type=stype,
        save_data_rank=rank,
        save_data_index=idx,
    )
    meta = {
        "owner_id": oid,
        "timestamp": ts,
        "flags": flags,
        "unk_x54": unk_x54,
        "data_size": dsize,
        "journal_size": jsize,
        "commit_id": cid,
    }
    return attr, meta


def encode_nx_save_meta(attr: SaveAttribute, extra_meta: Dict[str, Any], raw_size: int) -> bytes:
    """Encodes a 128-byte NXSaveMeta structure used by System/SystemBcat."""
    buf = bytearray(128)
    struct.pack_into("<II", buf, 0, NX_SAVE_META_MAGIC, NX_SAVE_META_VERSION)
    struct.pack_into(
        "<QQQQBBH",
        buf,
        8,
        attr.application_id,
        attr.uid_low,
        attr.uid_high,
        attr.system_save_data_id,
        attr.save_data_type,
        attr.save_data_rank,
        attr.save_data_index,
    )
    struct.pack_into(
        "<QQIIqqQQ",
        buf,
        72,
        extra_meta.get("owner_id", 0),
        extra_meta.get("timestamp", 0),
        extra_meta.get("flags", 0),
        extra_meta.get("unk_x54", 0),
        extra_meta.get("data_size", 0),
        extra_meta.get("journal_size", 0),
        extra_meta.get("commit_id", 0),
        raw_size,
    )
    return bytes(buf)


def decode_nx_save_meta(b: bytes) -> Tuple[int, int, SaveAttribute, Dict[str, Any], int]:
    """Decodes a 128-byte NXSaveMeta structure."""
    if len(b) != 128:
        raise ValueError(f"Expected 128 bytes, got {len(b)}")
    magic, ver = struct.unpack("<II", b[:8])
    app, ul, uh, sid, stype, rank, idx = struct.unpack("<QQQQBBH", b[8:44])
    oid, ts, flags, unk_x54, dsize, jsize, cid, raw_size = struct.unpack("<QQIIqqQQ", b[72:128])
    attr = SaveAttribute(app, ul, uh, sid, stype, rank, idx)
    meta = {
        "owner_id": oid,
        "timestamp": ts,
        "flags": flags,
        "unk_x54": unk_x54,
        "data_size": dsize,
        "journal_size": jsize,
        "commit_id": cid,
    }
    return magic, ver, attr, meta, raw_size


def get_actual_dbi_account_label(save_data_type: int, account_nick: str = "TestAccount") -> str:
    """Matches exact Menu::WriteSaveBackupZip / GetSaveTypeLabel logic in sphaira."""
    if save_data_type == FS_SAVE_DATA_TYPE_ACCOUNT:
        return account_nick
    elif save_data_type == FS_SAVE_DATA_TYPE_DEVICE:
        return "Device"
    elif save_data_type == FS_SAVE_DATA_TYPE_BCAT:
        return "BCAT"
    elif save_data_type == FS_SAVE_DATA_TYPE_CACHE:
        return "Cache"
    elif save_data_type == FS_SAVE_DATA_TYPE_TEMPORARY:
        return "Temporary"
    elif save_data_type == FS_SAVE_DATA_TYPE_SYSTEM:
        return "System"
    elif save_data_type == FS_SAVE_DATA_TYPE_SYSTEM_BCAT:
        return "System BCAT"
    return "Unknown"


def get_actual_dbi_space_label(space_id: int) -> str:
    """Matches exact Menu::WriteSaveBackupZip space switch in sphaira."""
    if space_id in (FS_SAVE_DATA_SPACE_ID_SYSTEM, FS_SAVE_DATA_SPACE_ID_SD_SYSTEM, FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM):
        return "System"
    elif space_id == FS_SAVE_DATA_SPACE_ID_SD_USER:
        return "SdUser"
    elif space_id == FS_SAVE_DATA_SPACE_ID_TEMPORARY:
        return "Temporary"
    else:
        return "User"


# ==============================================================================
# 1. Static Source Contracts
# ==============================================================================
