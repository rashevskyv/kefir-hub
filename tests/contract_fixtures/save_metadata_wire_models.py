# Reference decoders, wire packers, and admission models for save metadata wire contract.
import struct
import io
import zipfile

RES_OK = 0
ERR_CORRUPT_METADATA = 0xEE01
ERR_IDENTITY_REFUSED = 0xEE02
ERR_TARGET_LOCKED = 0x244E02
ERR_CANNOT_RESTORE_TO_SYSTEM = 0xEE03
ERR_INDEX_MISMATCH = 0xEE04
ERR_APP_MISMATCH = 0xEE05
ERR_NO_VALID_METADATA = 0xEE06
ERR_DISA_NOT_SUPPORTED = 0xEE07

META_FORMAT_JKSV85 = 1
META_FORMAT_JKSV86 = 2
META_FORMAT_SPHAIRA128 = 3
META_FORMAT_DBI512 = 4

JKSV_MAGIC = 0x56534B4A
JKSV_REVISION = 1
SPHAIRA_MAGIC = 0x4A4B5356
SPHAIRA_VERSION = 1

VALID_SPACES = {0, 1, 2, 3, 4, 100, 101}

def pack_jksv85(app_id=0x0100000000010000, uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000010000,
                ts=20260917120000, flags=0, data_size=0x200000, journal_size=0x200000, commit_id=1,
                magic=JKSV_MAGIC, rev=JKSV_REVISION) -> bytes:
    return struct.pack(
        "<IBQQQQBBHQQIqqQ",
        magic, rev, app_id, uid_low, uid_high, sys_id, save_type, rank, index,
        owner_id, ts, flags, data_size, journal_size, commit_id
    )

def pack_jksv_tail86(app_id=0x0100000000010000, uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                     sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000010000,
                     ts=20260917120000, flags=0, data_size=0x200000, journal_size=0x200000, commit_id=1,
                     source_space=1, magic=JKSV_MAGIC, rev=JKSV_REVISION) -> bytes:
    return pack_jksv85(app_id, uid_low, uid_high, sys_id, save_type, rank, index, owner_id,
                       ts, flags, data_size, journal_size, commit_id, magic, rev) + struct.pack("<B", source_space)

def pack_jksv_middle86(app_id=0x0100000000010000, uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                       sys_id=0, save_type=1, rank=0, index=0, source_space=1, owner_id=0x0100000000010000,
                       ts=20260917120000, flags=0, data_size=0x200000, journal_size=0x200000, commit_id=1,
                       magic=JKSV_MAGIC, rev=JKSV_REVISION) -> bytes:
    head = struct.pack("<IBQQQQBBH", magic, rev, app_id, uid_low, uid_high, sys_id, save_type, rank, index)
    sp = struct.pack("<B", source_space)
    tail = struct.pack("<QQIqqQ", owner_id, ts, flags, data_size, journal_size, commit_id)
    return head + sp + tail

def pack_sphaira128(app_id=0x0100000000010000, uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                    sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000010000,
                    ts=20260917120000, flags=0, unk_x54=0, data_size=0x200000, journal_size=0x200000,
                    commit_id=1, raw_size=0, pad=b"\x00"*28, magic=SPHAIRA_MAGIC, ver=SPHAIRA_VERSION) -> bytes:
    head = struct.pack("<IIQQQQBBH", magic, ver, app_id, uid_low, uid_high, sys_id, save_type, rank, index)
    tail = struct.pack("<QQIIqqQQ", owner_id, ts, flags, unk_x54, data_size, journal_size, commit_id, raw_size)
    return head + pad + tail

def pack_dbi_raw512(app_id=0x0100000000010000, uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                    sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000010000,
                    ts=20260917120000, flags=0, unk_x54=0, data_size=0x200000, journal_size=0x200000,
                    commit_id=1, pad=b"\x00"*28, extra=b"\xAA"*400) -> bytes:
    head = struct.pack("<QQQQBBH", app_id, uid_low, uid_high, sys_id, save_type, rank, index)
    mid = struct.pack("<QQIIqqQ", owner_id, ts, flags, unk_x54, data_size, journal_size, commit_id)
    return head + pad + mid + extra


class ReferenceSaveMetadataDecoder:
    """Pure-Python behavioral reference model matching ReadArchiveSaveMetadata."""

    @staticmethod
    def classify_root(name: str) -> str:
        s = name.lower()
        if s == ".nx_save_meta.bin":
            return "NxMeta"
        if s == ".dbi_save_extra":
            return "DbiExtra"
        if s == ".dbi_save_info.ini":
            return "DbiInfo"
        return "None"

    @staticmethod
    def is_reserved_root(name: str) -> bool:
        return ReferenceSaveMetadataDecoder.classify_root(name) != "None"

    @staticmethod
    def validate_meta(m: dict, is_86: bool) -> bool:
        if m["save_data_type"] > 6:
            return False
        if m["save_data_rank"] > 1:
            return False
        if m["data_size"] < 0 or m["journal_size"] < 0:
            return False
        if m["save_data_type"] == 1:  # Account
            if m["application_id"] == 0:
                return False
            if m["system_save_data_id"] != 0:
                return False
            if m["uid_low"] == 0 and m["uid_high"] == 0:
                return False
        elif m["save_data_type"] in (0, 6):  # System, SystemBcat
            if m["system_save_data_id"] == 0:
                return False
        else:
            if m["application_id"] == 0:
                return False

        if is_86:
            if m.get("source_space") is None or m["source_space"] not in VALID_SPACES:
                return False

        return True

    @staticmethod
    def decode_jksv85(b: bytes) -> dict | None:
        if len(b) != 85:
            return None
        magic, rev, app, ul, uh, sid, stype, rank, idx, oid, ts, flags, dsize, jsize, cid = struct.unpack(
            "<IBQQQQBBHQQIqqQ", b
        )
        if magic != JKSV_MAGIC or rev != JKSV_REVISION:
            return None
        res = {
            "application_id": app, "uid_low": ul, "uid_high": uh,
            "system_save_data_id": sid, "save_data_type": stype,
            "save_data_rank": rank, "save_data_index": idx,
            "owner_id": oid, "timestamp": ts, "flags": flags,
            "data_size": dsize, "journal_size": jsize, "commit_id": cid,
            "source_space": None, "raw_size": 0
        }
        return res if ReferenceSaveMetadataDecoder.validate_meta(res, False) else None

    @staticmethod
    def decode_jksv_tail86(b: bytes) -> dict | None:
        if len(b) != 86:
            return None
        base = ReferenceSaveMetadataDecoder.decode_jksv85(b[:85])
        if base is None:
            return None
        space = b[85]
        base["source_space"] = space
        return base if ReferenceSaveMetadataDecoder.validate_meta(base, True) else None

    @staticmethod
    def decode_jksv_middle86(b: bytes) -> dict | None:
        if len(b) != 86:
            return None
        magic, rev, app, ul, uh, sid, stype, rank, idx, space = struct.unpack("<IBQQQQBBHB", b[:42])
        oid, ts, flags, dsize, jsize, cid = struct.unpack("<QQIqqQ", b[42:86])
        if magic != JKSV_MAGIC or rev != JKSV_REVISION:
            return None
        res = {
            "application_id": app, "uid_low": ul, "uid_high": uh,
            "system_save_data_id": sid, "save_data_type": stype,
            "save_data_rank": rank, "save_data_index": idx,
            "owner_id": oid, "timestamp": ts, "flags": flags,
            "data_size": dsize, "journal_size": jsize, "commit_id": cid,
            "source_space": space, "raw_size": 0
        }
        return res if ReferenceSaveMetadataDecoder.validate_meta(res, True) else None

    @staticmethod
    def compare_common_source_fields(a: dict, b: dict) -> bool:
        keys = ["application_id", "uid_low", "uid_high", "system_save_data_id",
                "save_data_type", "save_data_rank", "save_data_index",
                "owner_id", "timestamp", "flags", "data_size", "journal_size", "commit_id"]
        return all(a[k] == b[k] for k in keys)

    @staticmethod
    def decode_jksv86_ambiguity(b: bytes) -> dict | None:
        tail = ReferenceSaveMetadataDecoder.decode_jksv_tail86(b)
        mid = ReferenceSaveMetadataDecoder.decode_jksv_middle86(b)
        if not tail and not mid:
            return None
        if tail and not mid:
            return tail
        if not tail and mid:
            return mid
        if ReferenceSaveMetadataDecoder.compare_common_source_fields(tail, mid) and tail["source_space"] == mid["source_space"]:
            return tail
        return None

    @staticmethod
    def decode_sphaira128(b: bytes) -> dict | None:
        if len(b) != 128:
            return None
        magic, ver, app, ul, uh, sid, stype, rank, idx = struct.unpack("<IIQQQQBBH", b[:44])
        if magic != SPHAIRA_MAGIC or ver != SPHAIRA_VERSION:
            return None
        oid, ts, flags, unk_x54, dsize, jsize, cid, raw_size = struct.unpack("<QQIIqqQQ", b[72:128])
        res = {
            "application_id": app, "uid_low": ul, "uid_high": uh,
            "system_save_data_id": sid, "save_data_type": stype,
            "save_data_rank": rank, "save_data_index": idx,
            "owner_id": oid, "timestamp": ts, "flags": flags,
            "data_size": dsize, "journal_size": jsize, "commit_id": cid,
            "source_space": None, "raw_size": raw_size
        }
        return res if ReferenceSaveMetadataDecoder.validate_meta(res, False) else None

    @staticmethod
    def decode_dbi_raw512(b: bytes) -> dict | None:
        if len(b) != 512:
            return None
        app, ul, uh, sid, stype, rank, idx = struct.unpack("<QQQQBBH", b[:36])
        oid, ts, flags, unk_x54, dsize, jsize, cid = struct.unpack("<QQIIqqQ", b[64:112])
        res = {
            "application_id": app, "uid_low": ul, "uid_high": uh,
            "system_save_data_id": sid, "save_data_type": stype,
            "save_data_rank": rank, "save_data_index": idx,
            "owner_id": oid, "timestamp": ts, "flags": flags,
            "data_size": dsize, "journal_size": jsize, "commit_id": cid,
            "source_space": None, "raw_size": 0
        }
        return res if ReferenceSaveMetadataDecoder.validate_meta(res, False) else None

    @classmethod
    def read_archive_metadata(cls, zip_bytes: bytes, injected_failure: str | None = None) -> tuple[str, dict | None]:
        """Simulates ReadArchiveSaveMetadata over a zip archive with failure injection."""
        if injected_failure == "termination":
            # Central directory traversal early EOF or corruption
            return "Invalid", None

        buf = io.BytesIO(zip_bytes)
        try:
            zf = zipfile.ZipFile(buf, "r")
        except Exception:
            return "Invalid", None

        infolist = zf.infolist()
        if not infolist:
            return "NoMetadata", None

        seen_nx = False
        seen_dbi_extra = False
        seen_dbi_info = False

        nx_meta = None
        dbi_extra_meta = None

        for info in infolist:
            name = info.filename.replace("\\", "/")
            while name.startswith("/"):
                name = name[1:]

            is_dir = name.endswith("/") or ((info.external_attr & 0x10) != 0) or (((info.external_attr >> 16) & 0xF000) == 0x4000)
            clean = name.rstrip("/")

            # Check parent directory conflict: clean has a slash
            if "/" in clean:
                first_seg = clean.split("/")[0]
                if cls.classify_root(first_seg) != "None":
                    return "Invalid", None
                continue

            root_kind = cls.classify_root(clean)
            if root_kind == "None":
                continue

            if is_dir:
                return "Invalid", None

            if root_kind == "NxMeta":
                if seen_nx:
                    return "Invalid", None
                seen_nx = True
                if info.file_size not in (85, 86, 128):
                    return "Invalid", None
                try:
                    data = zf.read(info)
                except Exception:
                    return "Invalid", None

                if injected_failure == "short-read":
                    data = data[:len(data) // 2]
                elif injected_failure == "read-error":
                    return "Invalid", None
                elif injected_failure == "overrun":
                    data = data + b"\x00\x00"

                if len(data) != info.file_size:
                    return "Invalid", None

                if len(data) == 85:
                    nx_meta = cls.decode_jksv85(data)
                elif len(data) == 86:
                    nx_meta = cls.decode_jksv86_ambiguity(data)
                elif len(data) == 128:
                    nx_meta = cls.decode_sphaira128(data)
                if nx_meta is None:
                    return "Invalid", None

            elif root_kind == "DbiExtra":
                if seen_dbi_extra:
                    return "Invalid", None
                seen_dbi_extra = True
                if info.file_size != 512:
                    return "Invalid", None
                try:
                    data = zf.read(info)
                except Exception:
                    return "Invalid", None

                if injected_failure == "short-read":
                    data = data[:256]
                elif injected_failure == "read-error":
                    return "Invalid", None

                if len(data) != info.file_size:
                    return "Invalid", None

                dbi_extra_meta = cls.decode_dbi_raw512(data)
                if dbi_extra_meta is None:
                    return "Invalid", None

            elif root_kind == "DbiInfo":
                if seen_dbi_info:
                    return "Invalid", None
                seen_dbi_info = True
                try:
                    data = zf.read(info)
                except Exception:
                    return "Invalid", None
                if len(data) != info.file_size:
                    return "Invalid", None

        if injected_failure == "rewind":
            # unzGoToFirstFile failure on checked rewind
            return "Invalid", None

        if not seen_nx and not seen_dbi_extra and not seen_dbi_info:
            return "NoMetadata", None

        if seen_nx and nx_meta is None:
            return "Invalid", None
        if seen_dbi_extra and dbi_extra_meta is None:
            return "Invalid", None

        if nx_meta and dbi_extra_meta:
            if not cls.compare_common_source_fields(nx_meta, dbi_extra_meta):
                return "Invalid", None
            return "Valid", nx_meta

        if nx_meta:
            return "Valid", nx_meta

        if dbi_extra_meta:
            return "Valid", dbi_extra_meta

        if seen_dbi_info and not seen_nx and not seen_dbi_extra:
            return "NoMetadata", None

        return "Invalid", None


def make_zip(entries: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_STORED) as zf:
        for name, data in entries.items():
            zf.writestr(name, data)
    return buf.getvalue()


# ==============================================================================
# 3. Connected Admission Lifecycle Model & Failure Injections
# ==============================================================================

class ConnectedAdmissionLifecycleModel:
    """
    Simulates the exact gate sequence in RestoreSaveZip:
    1. open
    2. preflight (TransferUnzipPreflight with save_filter)
    3. metadata (ReadArchiveSaveMetadata)
    4. selected-live (destination identity resolution)
    5. recovery (recovery candidate generation & ReadArchiveSaveMetadata validation)
    6. clear (native save filesystem commit/clear - FIRST MUTATION)
    7. extract (TransferUnzipAll into native save filesystem)
    8. fresh-RO (post-restore verification)
    9. source-close (checked close of source archive)
    """

    @classmethod
    def execute_restore(
        cls,
        zip_bytes: bytes,
        target_entry: dict,
        recovery_candidate_valid: bool = True,
        injected_failure: str | None = None
    ) -> tuple[bool, list[str], dict]:
        events = []
        resolved_attr = {}

        # 1. open
        if injected_failure == "open-error":
            return False, events, resolved_attr
        events.append("open")

        # 2. preflight
        if injected_failure == "overflow":
            # aggregate file size overflow
            return False, events, resolved_attr
        events.append("preflight")

        # 3. metadata admission
        meta_status, meta = ReferenceSaveMetadataDecoder.read_archive_metadata(
            zip_bytes, injected_failure=injected_failure
        )
        events.append("metadata")
        if meta_status == "Invalid":
            return False, events, resolved_attr

        # 4. selected-live destination validation
        events.append("selected-live")
        if target_entry.get("save_data_id", 0) == 0:
            # Validated creation from backup is out of scope; require live destination target
            return False, events, resolved_attr

        # Strictly preserve selected destination identity, space, and live sizes
        resolved_attr["application_id"] = target_entry["application_id"]
        resolved_attr["save_data_space_id"] = target_entry["save_data_space_id"]
        resolved_attr["save_data_type"] = target_entry["save_data_type"]
        resolved_attr["save_data_index"] = target_entry["save_data_index"]
        resolved_attr["uid"] = target_entry["uid"]
        resolved_attr["data_size"] = target_entry.get("live_data_size", 0)
        resolved_attr["journal_size"] = target_entry.get("live_journal_size", 0)

        # 5. recovery generation & validation
        events.append("recovery")
        if not recovery_candidate_valid:
            # Recovery candidate fails validation before clear
            return False, events, resolved_attr

        # GATES CLEARED -> FIRST MUTATION
        # 6. clear
        events.append("clear")

        # 7. extract
        events.append("extract")

        # 8. fresh-RO
        events.append("fresh-RO")

        # 9. source-close
        if injected_failure == "owner-close" or injected_failure == "callback-close":
            return False, events, resolved_attr
        events.append("source-close")

        return True, events, resolved_attr
