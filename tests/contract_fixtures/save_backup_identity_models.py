import os
import shutil
RES_OK = 0
FS_ERROR_PATH_NOT_FOUND = 0x202
ERR_IPC_READ_FAILED = 0x1002
FS_SAVE_DATA_TYPE_SYSTEM = 0
FS_SAVE_DATA_TYPE_ACCOUNT = 1
FS_SAVE_DATA_TYPE_BCAT = 2
FS_SAVE_DATA_TYPE_DEVICE = 3
FS_SAVE_DATA_TYPE_TEMPORARY = 4
FS_SAVE_DATA_TYPE_CACHE = 5
FS_SAVE_DATA_TYPE_SYSTEM_BCAT = 6
NX_SAVE_META_MAGIC = 0x4A4B5356
NX_SAVE_META_VERSION = 1
NX_SAVE_META_NAME = '.nx_save_meta.bin'
DBI_SAVE_INFO_NAME = '.dbi_save_info.ini'
DBI_SAVE_EXTRA_NAME = '.dbi_save_extra'
from dataclasses import dataclass
# Models and encoder/decoder helpers for save backup identity contract.
import struct
import io
import zipfile

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


def decode_extra_data(b: bytes) -> tuple[SaveAttribute, dict]:
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


def encode_nx_save_meta(attr: SaveAttribute, extra_meta: dict, raw_size: int) -> bytes:
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


# ==============================================================================
# 1. Static Source Contracts
# ==============================================================================


class BackupPipelineSimulator:
    """Simulates the BackupSaveInternal pipeline with event tracking and real filesystem I/O."""

    def __init__(self):
        self.events: list[str] = []
        self.destructive_events: list[str] = []

    def execute(
        self,
        selected: SelectedEntry,
        extra_raw_bytes: bytes | None,
        read_rc: int = RES_OK,
        source_payload_dir: str | None = None,
        output_dir: str | None = None,
    ) -> tuple[int, str | None]:
        """
        Executes simulated BackupSaveInternal:
        - Early exit on zero save ID.
        - Extra read with read_rc check.
        - Decode 512-byte extra data.
        - 7-field identity comparison.
        - Read-only save filesystem mount event.
        - Collection enumeration and reading from real source payload directory.
        - Real ZIP creation, write metadata, write payload, rename.
        Returns: (result_code, final_zip_path_or_none)
        """
        self.events.clear()
        self.destructive_events.clear()

        # Step 0: Zero ID check
        if selected.save_data_id == 0:
            self.events.append("zero_id_early_exit")
            return RES_OK, None

        # Step 1: Extra read event
        self.events.append(f"extra_read(space={selected.save_data_space_id}, id=0x{selected.save_data_id:X})")
        if read_rc != RES_OK:
            self.events.append(f"extra_read_failed(rc=0x{read_rc:X})")
            return read_rc, None

        if extra_raw_bytes is None or len(extra_raw_bytes) != 512:
            self.events.append("extra_read_invalid_size")
            return FS_ERROR_PATH_NOT_FOUND, None

        # Step 2: Decode extra data
        live_attr, extra_meta = decode_extra_data(extra_raw_bytes)
        self.events.append("extra_decoded")

        # Step 3: Identity comparison guard (the 7 fields)
        target_attr = selected.attr
        if (
            live_attr.application_id != target_attr.application_id
            or live_attr.uid_low != target_attr.uid_low
            or live_attr.uid_high != target_attr.uid_high
            or live_attr.system_save_data_id != target_attr.system_save_data_id
            or live_attr.save_data_type != target_attr.save_data_type
            or live_attr.save_data_rank != target_attr.save_data_rank
            or live_attr.save_data_index != target_attr.save_data_index
        ):
            self.events.append("identity_guard_refusal(FsError_PathNotFound)")
            return FS_ERROR_PATH_NOT_FOUND, None

        self.events.append("identity_guard_passed")

        # Step 4: FsNativeSave read-only open event
        self.events.append(
            f"ro_mount(type={target_attr.save_data_type}, space={selected.save_data_space_id}, read_only=True)"
        )

        # Step 5: Collection enumeration & bounded payload file reads from source subtree
        self.events.append("enum_collections")
        payload_items: list[tuple[str, bytes]] = []
        if source_payload_dir and os.path.exists(source_payload_dir):
            for root, dirs, files in os.walk(source_payload_dir):
                dirs.sort()
                files.sort()
                for fname in files:
                    abs_fpath = os.path.join(root, fname)
                    rel_fpath = os.path.relpath(abs_fpath, source_payload_dir).replace("\\", "/")
                    self.events.append(f"payload_read({rel_fpath})")
                    with open(abs_fpath, "rb") as f:
                        payload_items.append((rel_fpath, f.read()))

        if len(payload_items) == 0:
            self.events.append("empty_collections_early_exit(0x0)")
            return RES_OK, None

        # From this point on, output FS operations begin
        if output_dir is None:
            raise ValueError("output_dir must be provided for non-empty export")

        # Step 6: Create temp directory and files
        temp_zip_path = os.path.join(output_dir, f"{selected.name}.zip.temp")
        final_zip_path = os.path.join(output_dir, f"{selected.name}.zip")

        self.events.append(f"create_temp_path({temp_zip_path})")
        self.destructive_events.append(f"create({temp_zip_path})")

        # Step 7: Write ZIP
        self.events.append(f"write_zip({temp_zip_path})")
        self.destructive_events.append(f"write({temp_zip_path})")

        is_system_like = target_attr.save_data_type in (
            FS_SAVE_DATA_TYPE_SYSTEM,
            FS_SAVE_DATA_TYPE_SYSTEM_BCAT,
        )

        with zipfile.ZipFile(temp_zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            if not is_system_like:
                # DBI format writes explicit directory entries with trailing slash
                seen_dirs = set()
                for rel_path, _ in payload_items:
                    dir_part = os.path.dirname(rel_path)
                    if dir_part and dir_part not in seen_dirs:
                        seen_dirs.add(dir_part)
                        zf.writestr(f"{dir_part}/", b"")

                for rel_path, data in payload_items:
                    zf.writestr(rel_path, data)

                # DBI metadata entries
                dbi_info = (
                    f"TitleId={target_attr.application_id:016X}\n"
                    f"TitleName={selected.name}\n"
                    f"BackupDate=2026-09-18 16:00:00\n"
                    f"Account=TestAccount\n"
                    f"Space=User"
                ).encode("utf-8")
                zf.writestr(DBI_SAVE_INFO_NAME, dbi_info)
                zf.writestr(DBI_SAVE_EXTRA_NAME, extra_raw_bytes)
            else:
                # System format: Sphaira NXSaveMeta first
                nx_meta_bytes = encode_nx_save_meta(target_attr, extra_meta, selected.size)
                zf.writestr(NX_SAVE_META_NAME, nx_meta_bytes)

                for rel_path, data in payload_items:
                    zf.writestr(rel_path, data)

        # Step 8: Rename temp -> final
        self.events.append(f"rename_file({temp_zip_path} -> {final_zip_path})")
        self.destructive_events.append(f"rename({temp_zip_path} -> {final_zip_path})")
        os.replace(temp_zip_path, final_zip_path)

        self.events.append("export_published")
        return RES_OK, final_zip_path


# ==============================================================================
# 3. Behavioral Fixtures & Matrix Verification
# ==============================================================================

def make_valid_attr(
    app_id: int = 0x0100000000010000,
    uid_low: int = 0x1111222233334444,
    uid_high: int = 0x5555666677778888,
    sys_id: int = 0,
    save_type: int = FS_SAVE_DATA_TYPE_ACCOUNT,
    rank: int = 0,
    index: int = 0,
) -> SaveAttribute:
    return SaveAttribute(
        application_id=app_id,
        uid_low=uid_low,
        uid_high=uid_high,
        system_save_data_id=sys_id,
        save_data_type=save_type,
        save_data_rank=rank,
        save_data_index=index,
    )
