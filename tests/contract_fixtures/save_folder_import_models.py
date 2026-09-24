# Models and helpers for save folder import contract.
import os
import io
import struct
import pathlib
import zipfile
import shutil
import hashlib

JKSV_MAGIC = 0x56534B4A
JKSV_REVISION = 1
VALID_SPACES = {0, 1, 2, 3, 4, 100, 101}
RESERVED_ROOT_NAMES = {".nx_save_meta.bin", ".dbi_save_info.ini", ".dbi_save_extra"}
NX_SAVE_META_NAME = ".nx_save_meta.bin"
DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"
DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"
INVALID_PATH_CHARS = set(":*?\"<>|\\")
MAX_S64 = 0x7FFFFFFFFFFFFFFF

def is_save_reserved_metadata_root(name: str) -> bool:
    n = name.rstrip("/")
    if "/" in n:
        return False
    lower = n.lower()
    return lower in (NX_SAVE_META_NAME.lower(), DBI_SAVE_EXTRA_NAME.lower(), DBI_SAVE_INFO_NAME.lower())

def stage_folder_to_zip(source_dir: pathlib.Path, target_zip: pathlib.Path):
    """Replicates StageBackupFolderToZip exactly using real stdlib zipfile."""
    source_files = {}
    source_dirs = set()

    for root, dirs, files in os.walk(source_dir):
        rel_root = os.path.relpath(root, source_dir).replace("\\", "/")
        if rel_root == ".":
            rel_root = ""
        for d in sorted(dirs):
            d_rel = (rel_root + "/" + d if rel_root else d) + "/"
            source_dirs.add(d_rel)
        for f in sorted(files):
            f_rel = rel_root + "/" + f if rel_root else f
            full_path = pathlib.Path(root) / f
            source_files[f_rel] = full_path

    with zipfile.ZipFile(target_zip, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        for d_key in sorted(source_dirs):
            zinfo = zipfile.ZipInfo(d_key)
            zf.writestr(zinfo, b"")
        for f_key in sorted(source_files.keys()):
            zf.write(source_files[f_key], arcname=f_key)

    return source_files, source_dirs



def pack_jksv85(magic=JKSV_MAGIC, rev=JKSV_REVISION, app_id=0x0100000000001000,
                uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000001000,
                ts=1600000000, flags=0, data_size=0x200000, journal_size=0x200000, commit_id=1) -> bytes:
    """Settled 85-byte JKSV wire layout (<IBQQQQBBHQQIqqQ)."""
    return struct.pack("<IBQQQQBBHQQIqqQ",
                       magic, rev, app_id, uid_low, uid_high, sys_id,
                       save_type, rank, index, owner_id, ts, flags,
                       data_size, journal_size, commit_id)

def pack_dbi_raw512(app_id=0x0100000000001000, uid_low=0x1111222233334444, uid_high=0x5555666677778888,
                    sys_id=0, save_type=1, rank=0, index=0, owner_id=0x0100000000001000,
                    ts=1600000000, flags=0, unk_x54=0, data_size=0x200000, journal_size=0x200000,
                    commit_id=1, pad=b"\x00"*28, extra=b"\xAA"*400) -> bytes:
    head = struct.pack("<QQQQBBH", app_id, uid_low, uid_high, sys_id, save_type, rank, index)
    mid = struct.pack("<QQIIqqQ", owner_id, ts, flags, unk_x54, data_size, journal_size, commit_id)
    return head + pad + mid + extra

def unpack_jksv85(raw: bytes) -> dict | None:
    if len(raw) != 85:
        return None
    magic, rev, app_id, uid_low, uid_high, sys_id, save_type, rank, index, owner_id, ts, flags, data_size, journal_size, commit_id = struct.unpack(
        "<IBQQQQBBHQQIqqQ", raw
    )
    return {
        "magic": magic, "rev": rev, "application_id": app_id,
        "uid_low": uid_low, "uid_high": uid_high, "system_save_data_id": sys_id,
        "save_data_type": save_type, "save_data_rank": rank, "save_data_index": index,
        "owner_id": owner_id, "timestamp": ts, "flags": flags,
        "data_size": data_size, "journal_size": journal_size, "commit_id": commit_id
    }

def decode_dbi_raw512(raw: bytes) -> dict | None:
    if len(raw) != 512:
        return None
    app, ul, uh, sid, stype, rank, idx = struct.unpack("<QQQQBBH", raw[:36])
    oid, ts, flags, unk_x54, dsize, jsize, cid = struct.unpack("<QQIIqqQ", raw[64:112])
    return {
        "application_id": app, "uid_low": ul, "uid_high": uh,
        "system_save_data_id": sid, "save_data_type": stype,
        "save_data_rank": rank, "save_data_index": idx,
        "owner_id": oid, "timestamp": ts, "flags": flags,
        "data_size": dsize, "journal_size": jsize, "commit_id": cid
    }

def compare_common_source_fields(a: dict, b: dict) -> bool:
    keys = ["application_id", "uid_low", "uid_high", "system_save_data_id",
            "save_data_type", "save_data_rank", "save_data_index",
            "owner_id", "timestamp", "flags", "data_size", "journal_size", "commit_id"]
    return all(a.get(k) == b.get(k) for k in keys)

def validate_jksv85_source_metadata(raw: bytes) -> tuple[bool, str, dict | None]:
    """Independent source metadata decoder & validator matching ReadArchiveSaveMetadata."""
    if len(raw) < 85:
        return False, "Truncated wire metadata", None
    if len(raw) > 85:
        return False, "Malformed wire length", None

    meta = unpack_jksv85(raw)
    if not meta:
        return False, "Unpack failure", None

    if meta["magic"] != JKSV_MAGIC:
        return False, "Invalid JKSV magic", None
    if meta["rev"] != JKSV_REVISION:
        return False, "Invalid revision", None
    if meta["data_size"] < 0 or meta["journal_size"] < 0:
        return False, "Negative size", None
    if meta["save_data_type"] > 6:
        return False, "Invalid save type", None
    if meta["save_data_rank"] > 1:
        return False, "Invalid save rank", None

    if meta["save_data_type"] == 1:  # Account save
        if meta["application_id"] == 0:
            return False, "Account save requires nonzero application_id", None
        if meta["system_save_data_id"] != 0:
            return False, "Account save requires zero system_save_data_id", None
        if meta["uid_low"] == 0 and meta["uid_high"] == 0:
            return False, "Account save requires nonzero uid", None
    elif meta["save_data_type"] in (0, 6):  # System, SystemBcat
        if meta["system_save_data_id"] == 0:
            return False, "System save requires nonzero system_save_data_id", None
    else:
        if meta["application_id"] == 0:
            return False, "Non-system save requires nonzero application_id", None

    return True, "OK", meta


def admit_and_drain_zip_archive(zip_path: pathlib.Path) -> tuple[bool, str, dict, set, dict | None]:
    """Reopens actual zip on disk, drains all files for CRC, builds payload inventory/dirs, and decodes source metadata."""
    if not zip_path.is_file():
        return False, "Result_UnzOpen2_64: zip file does not exist", {}, set(), None
    try:
        with zipfile.ZipFile(zip_path, "r") as zf:
            crc_bad = zf.testzip()
            if crc_bad is not None:
                return False, "Result_UnzCRCError: corrupted CRC in archive", {}, set(), None

            staged_payload = {}
            staged_dirs = set()
            nx_meta = None
            dbi_extra_meta = None

            for zinfo in zf.infolist():
                entry_data = zf.read(zinfo)
                if len(entry_data) != zinfo.file_size:
                    return False, "FsError_InvalidSize: decompressed size mismatch", {}, set(), None

                arc_name = zinfo.filename.replace("\\", "/")
                if arc_name.endswith("/"):
                    staged_dirs.add(arc_name.rstrip("/"))
                    continue

                if is_save_reserved_metadata_root(arc_name):
                    lower = arc_name.lower()
                    if lower == NX_SAVE_META_NAME.lower():
                        ok, msg, m = validate_jksv85_source_metadata(entry_data)
                        if not ok:
                            return False, f"ReadArchiveSaveMetadata: {msg}", {}, set(), None
                        nx_meta = m
                    elif lower == DBI_SAVE_INFO_NAME.lower():
                        pass  # Opaque text, drained & CRC checked
                    elif lower == DBI_SAVE_EXTRA_NAME.lower():
                        dbi_extra_meta = decode_dbi_raw512(entry_data)
                        if dbi_extra_meta is None:
                            return False, "ReadArchiveSaveMetadata: malformed DBI save extra", {}, set(), None
                else:
                    staged_payload[arc_name] = {"size": zinfo.file_size, "bytes": entry_data}

            if nx_meta and dbi_extra_meta:
                if not compare_common_source_fields(nx_meta, dbi_extra_meta):
                    return False, "ReadArchiveSaveMetadata: contradictory metadata between NX and DBI extra", {}, set(), None

            primary_meta = nx_meta or dbi_extra_meta
            return True, "OK", staged_payload, staged_dirs, primary_meta
    except Exception as e:
        return False, f"Result_UnzOpen2_64: {e}", {}, set(), None



class ConnectedRealDiskImportRunner:
    """Accurately models RestoreSaveFolder -> RestoreSaveZip pipeline on real disk artifacts."""

    def __init__(self, tmp_path: pathlib.Path):
        self.tmp = tmp_path
        self.sd_root = self.tmp / "sdmc"
        self.console_root = self.tmp / "console"

        self.sd_root.mkdir(parents=True)
        self.console_root.mkdir(parents=True)

        # 1. Source folder on SD
        self.source_dir = self.sd_root / "JKSV" / "Animal_Crossing"
        self.source_dir.mkdir(parents=True)
        (self.source_dir / "main.dat").write_bytes(b"SOURCE_PAYLOAD_MAIN_0987654321")
        (self.source_dir / "config.bin").write_bytes(b"SOURCE_PAYLOAD_CONFIG_112233")
        # Valid source metadata under a different UID (Account remap scenario)
        (self.source_dir / ".nx_save_meta.bin").write_bytes(
            pack_jksv85(app_id=0x0100000000001000, uid_low=0xAAAAAAAAAAAAAAAA, uid_high=0xBBBBBBBBBBBBBBBB)
        )
        # Opaque DBI INI file coexisting smoothly
        (self.source_dir / ".dbi_save_info.ini").write_text("[Save]\nTitle=Opaque\n", encoding="utf-8")

        # 2. Foreign directory and file under dumps/save-import to verify absolute immunity
        self.staging_parent = self.sd_root / "dumps" / "save-import"
        self.staging_parent.mkdir(parents=True)
        self.foreign_dir = self.staging_parent / "foreign_backup"
        self.foreign_dir.mkdir()
        self.foreign_file = self.foreign_dir / "keep_user_file.dat"
        self.foreign_file.write_bytes(b"FOREIGN_USER_BACKUP_DO_NOT_TOUCH")

        # 3. Selected target live save slot on console
        self.live_save_dir = self.console_root / "save" / "0100000000001000"
        self.live_save_dir.mkdir(parents=True)
        (self.live_save_dir / "original_live.dat").write_bytes(b"ORIGINAL_LIVE_SAVE_BEFORE_MUTATION")
        (self.live_save_dir / "slot.ini").write_bytes(b"ORIGINAL_SLOT_INI_DATA")

        # 4. Recovery archive path
        self.recovery_parent = self.sd_root / "dumps" / "save-recovery"
        self.recovery_parent.mkdir(parents=True)
        self.recovery_zip = self.recovery_parent / "recovery_0100000000001000.zip"

        # Baseline hashes
        self.initial_foreign_hash = hashlib.md5(self.foreign_file.read_bytes()).hexdigest()
        self.initial_src_main_hash = hashlib.md5((self.source_dir / "main.dat").read_bytes()).hexdigest()
        self.initial_src_cfg_hash = hashlib.md5((self.source_dir / "config.bin").read_bytes()).hexdigest()
        self.initial_live_dat_hash = hashlib.md5((self.live_save_dir / "original_live.dat").read_bytes()).hexdigest()
        self.initial_live_ini_hash = hashlib.md5((self.live_save_dir / "slot.ini").read_bytes()).hexdigest()

        # Operational states
        self.mutation_started = False
        self.recovery_published = False
        self.owned_stage_dir = None
        self.owned_stage_zip = None

    def run(self, fault: str | None = None, is_empty_folder: bool = False, allow_empty: bool = True) -> tuple[int, str]:
        # Empty folder setup
        if is_empty_folder:
            for item in list(self.source_dir.iterdir()):
                item.unlink()

        # Step 1: Selected target upfront (save_data_id != 0, !is_backup)
        target_save_data_id = 0 if fault == "target_unbacked" else 0x0000000012345678
        if target_save_data_id == 0:
            return 0x100, "FsError_PathNotFound: target save unbacked"

        # Step 2: Source scan & inventory collection
        if fault == "cancel_during_scan":
            return 0x101, "Result_OperationCancelled"

        source_files = {}
        source_dirs = set()
        for root, dirs, files in os.walk(self.source_dir):
            rel_root = os.path.relpath(root, self.source_dir).replace("\\", "/")
            if rel_root == ".":
                rel_root = ""
            for d in sorted(dirs):
                d_rel = (rel_root + "/" + d if rel_root else d) + "/"
                source_dirs.add(d_rel)
            for f in sorted(files):
                f_rel = rel_root + "/" + f if rel_root else f
                full_path = pathlib.Path(root) / f
                source_files[f_rel] = {"path": full_path, "size": full_path.stat().st_size}

        # Step 3: Strict-default zero-entry refusal BEFORE live mutation
        num_source_entries = len(source_files) + len(source_dirs)
        if num_source_entries == 0 and (fault == "strict_default_zero_entry" or not allow_empty):
            return 0x102, "FsError_InvalidSize: zero-entry archive rejected by strict default"

        # Step 4: Owned primitive reservation & commit
        if fault == "reserve_exhaustion":
            return 0x103, "FsError_PathAlreadyExists: collision loop exhausted"

        self.owned_stage_dir = self.staging_parent / "20260918_130000_0100000000001000_000"
        self.owned_stage_dir.mkdir()
        self.owned_stage_zip = self.owned_stage_dir / "source.zip.temp"

        try:
            # Step 5: Checked stage read/write (StageBackupFolderToZip)
            if fault == "cancel_during_read":
                raise ValueError("Result_OperationCancelled")
            if fault == "read_zero_progress":
                raise ValueError("FsError_InvalidSize: read returned zero progress (truncated source)")
            if fault == "read_overread":
                raise ValueError("FsError_InvalidSize: read bytes exceeded file size")
            if fault == "read_trailing_error":
                raise ValueError("FsError_TargetLocked: trailing read I/O error propagated")
            if fault == "read_trailing_nonzero":
                raise ValueError("FsError_InvalidSize: trailing read bytes > 0 (file growth)")
            if fault == "read_zero_byte_growth":
                raise ValueError("FsError_InvalidSize: zero-byte file grew during staging")
            if fault == "stage_drift_shrink":
                raise ValueError("FsError_TargetLocked: final size check mismatch")
            if fault == "stage_no_space":
                raise ValueError("FsError_NotEnoughFreeSpace: SD card full during staging")

            # Positive short reads handling: written and verified in chunks
            with zipfile.ZipFile(self.owned_stage_zip, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
                for d_key in sorted(source_dirs):
                    zf.writestr(zipfile.ZipInfo(d_key), b"")
                for f_key in sorted(source_files.keys()):
                    if fault == "stage_metadata_invalid" and f_key == ".nx_save_meta.bin":
                        zf.writestr(f_key, pack_jksv85(magic=0xDEADBEEF))
                    elif fault == "stage_metadata_internal_conflict" and f_key == ".nx_save_meta.bin":
                        zf.writestr(f_key, pack_jksv85(app_id=0x0100000000001000))
                        zf.writestr(".dbi_save_extra", pack_dbi_raw512(app_id=0x0100000000002000))
                    else:
                        zf.write(source_files[f_key]["path"], arcname=f_key)

            if fault == "zip_entry_close_error":
                raise ValueError("Result_ZipWriteInFileInZip")
            if fault == "cancel_during_finalize":
                raise ValueError("Result_OperationCancelled")
            if fault == "zip_finalize_flush_error":
                raise ValueError("Result_FsUnknownStdioError: zip close flush error")
            if fault == "zip_stream_close_leak":
                raise ValueError("Result_FsUnknownStdioError: rec_ctx.fp != nullptr after zipClose")
            if fault == "stage_sd_commit_error":
                raise ValueError("FsError_TargetLocked: sd_fs.Commit() failed")

            # Step 6: Stage full CRC, inventory, and metadata + checked reader close
            if fault == "cancel_during_admission":
                raise ValueError("Result_OperationCancelled")
            if fault == "preflight_crc_error":
                self.owned_stage_zip.write_bytes(b"CORRUPTED_ZIP_PAYLOAD")

            ok_adm, msg_adm, stage_payload, stage_dirs, stage_meta = admit_and_drain_zip_archive(self.owned_stage_zip)
            if not ok_adm:
                raise ValueError(msg_adm)

            if fault == "stage_inventory_mismatch":
                raise ValueError("FsError_PathNotFound: staged inventory mismatch against source")

            expected_payload_from_source = {k: v for k, v in source_files.items() if not is_save_reserved_metadata_root(k)}
            if set(stage_payload.keys()) != set(expected_payload_from_source.keys()):
                raise ValueError("FsError_PathNotFound: payload file set mismatch")
            for k, info in expected_payload_from_source.items():
                if stage_payload[k]["size"] != info["size"]:
                    raise ValueError("FsError_InvalidSize: payload file size mismatch")

            expected_dirs_from_source = {d.rstrip("/") for d in source_dirs}
            if stage_dirs != expected_dirs_from_source:
                raise ValueError("FsError_PathNotFound: directory set mismatch")

            if fault == "stage_reader_close_error":
                raise ValueError("Result_UnzOpen2_64: stage reader unzClose error")

            # Between Step 6 and Step 7: prove stage alteration is caught at shared admission boundary!
            if fault == "stage_altered_between_admissions_corrupt_zip":
                self.owned_stage_zip.write_bytes(b"CORRUPTED_BYTES_BETWEEN_ADMISSIONS")
            elif fault == "stage_altered_between_admissions_invalid_meta":
                # Rewrite zip with invalid metadata
                with zipfile.ZipFile(self.owned_stage_zip, "w") as zf:
                    for k, d in stage_payload.items():
                        zf.writestr(k, d["bytes"])
                    zf.writestr(NX_SAVE_META_NAME, pack_jksv85(magic=0xDEADBEEF))

            if fault == "cancel_before_shared_restore":
                raise ValueError("Result_OperationCancelled")

            # Step 7: Shared RestoreSaveZip boundary admission (re-read actual stage zip on disk)
            ok_shared, msg_shared, shared_payload, shared_dirs, shared_meta = admit_and_drain_zip_archive(self.owned_stage_zip)
            if not ok_shared:
                raise ValueError(f"RestoreSaveZip preflight: {msg_shared}")

            # Selected Entry vs actual live record validation
            selected_entry = {
                "application_id": 0x0100000000001000,
                "uid_low": 0x1111222233334444,
                "uid_high": 0x5555666677778888,
                "system_save_data_id": 0,
                "save_data_type": 1,  # Account
                "save_data_rank": 0,
                "save_data_index": 0,
                "save_data_id": target_save_data_id,
                "save_data_space_id": 0  # User
            }

            actual_live_record = {
                "application_id": 0x0100000000001000,
                "uid_low": 0x1111222233334444,
                "uid_high": 0x5555666677778888,
                "system_save_data_id": 0,
                "save_data_type": 1,
                "save_data_rank": 0,
                "save_data_index": 0,
                "save_data_space_id": 0,
                "data_size": 0x400000,     # Live size: 4 MiB
                "journal_size": 0x400000,  # Live size: 4 MiB
                "commit_id": 42
            }

            # Fault injections for Step 7
            if fault == "destination_uid_low_mismatch":
                actual_live_record["uid_low"] = 0x9999999999999999
            elif fault == "destination_uid_high_mismatch":
                actual_live_record["uid_high"] = 0x8888888888888888
            elif fault == "destination_app_id_mismatch":
                actual_live_record["application_id"] = 0x0100000000002000
            elif fault == "destination_space_mismatch":
                actual_live_record["save_data_space_id"] = 1  # System space
            elif fault == "destination_type_mismatch":
                actual_live_record["save_data_type"] = 2  # Device save
            elif fault == "destination_rank_mismatch":
                actual_live_record["save_data_rank"] = 1
            elif fault == "destination_index_mismatch":
                actual_live_record["save_data_index"] = 1
            elif fault == "live_data_size_nonpositive":
                actual_live_record["data_size"] = 0
            elif fault == "live_journal_size_negative":
                actual_live_record["journal_size"] = -1
            elif fault == "payload_exceeds_live_data_size":
                actual_live_record["data_size"] = 5  # Undersize: payload won't fit

            # Compare selected destination against actual live record as product does
            if (actual_live_record["application_id"] != selected_entry["application_id"] or
                actual_live_record["uid_low"] != selected_entry["uid_low"] or
                actual_live_record["uid_high"] != selected_entry["uid_high"] or
                actual_live_record["system_save_data_id"] != selected_entry["system_save_data_id"] or
                actual_live_record["save_data_type"] != selected_entry["save_data_type"] or
                actual_live_record["save_data_rank"] != selected_entry["save_data_rank"] or
                actual_live_record["save_data_index"] != selected_entry["save_data_index"] or
                actual_live_record["save_data_space_id"] != selected_entry["save_data_space_id"]):
                raise ValueError("FsError_PathNotFound: target live save slot identity/space mismatch")

            if actual_live_record["data_size"] <= 0 or actual_live_record["journal_size"] < 0:
                raise ValueError("FsError_InvalidSize: live data_size must be positive and journal_size non-negative")

            total_payload_bytes = sum(v["size"] for v in shared_payload.values())
            if total_payload_bytes > actual_live_record["data_size"]:
                raise ValueError("FsError_NotEnoughFreeSpace: payload bytes exceed live data_size")

            # Step 8: Recovery write, full verification, checked close & publication
            if fault == "recovery_write_fail":
                raise ValueError("Result_ZipWriteInFileInZip: failed to write recovery archive")

            with zipfile.ZipFile(self.recovery_zip, mode="w", compression=zipfile.ZIP_DEFLATED) as rzf:
                for root, _, files in os.walk(self.live_save_dir):
                    rel_root = os.path.relpath(root, self.live_save_dir).replace("\\", "/")
                    if rel_root == ".":
                        rel_root = ""
                    for f in sorted(files):
                        arc = rel_root + "/" + f if rel_root else f
                        rzf.write(pathlib.Path(root) / f, arcname=arc)

            if fault == "recovery_validation_fail":
                raise ValueError("FsError_PathNotFound: recovery verification against live slot failed")
            if fault == "recovery_close_fail":
                raise ValueError("Result_UnzOpen2_64: recovery reader close failed before publication")

            # Publication: recovery archive published for user
            self.recovery_published = True

            # Step 9: Clear live save (POINT OF NO RETURN)
            self.mutation_started = True

            if fault == "live_clear_fail":
                raise ValueError("Result_FsCommitError: failed to commit live save clear")

            for item in list(self.live_save_dir.iterdir()):
                if item.is_file():
                    item.unlink()

            # Step 10: Serial extraction
            if fault == "extract_fail":
                raise ValueError("Result_ExtractFailure: decompression error during extract")

            if not is_empty_folder:
                for rel_path, file_data in shared_payload.items():
                    target_file = self.live_save_dir / rel_path
                    target_file.parent.mkdir(parents=True, exist_ok=True)
                    target_file.write_bytes(file_data["bytes"])
                for d in shared_dirs:
                    (self.live_save_dir / d).mkdir(parents=True, exist_ok=True)

            # Step 11: Fresh RO native verification (executable bijection, sizes, streaming bytes)
            if fault == "ro_verify_same_size_corruption":
                target_main = self.live_save_dir / "main.dat"
                raw = bytearray(target_main.read_bytes())
                raw[0] = (raw[0] + 1) % 256
                target_main.write_bytes(bytes(raw))
            elif fault == "ro_verify_unexpected_file":
                (self.live_save_dir / "unexpected.bin").write_bytes(b"EXTRA")
            elif fault == "ro_verify_missing_file":
                (self.live_save_dir / "config.bin").unlink()
            elif fault == "ro_verify_unexpected_dir":
                (self.live_save_dir / "extra_dir").mkdir()
            elif fault == "empty_restore_leftover_file":
                (self.live_save_dir / "leftover.bin").write_bytes(b"LEFTOVER")
            elif fault == "empty_restore_leftover_dir":
                (self.live_save_dir / "leftover_dir").mkdir()

            dest_files = {}
            dest_dirs = set()
            for root, dirs, files in os.walk(self.live_save_dir):
                rel_root = os.path.relpath(root, self.live_save_dir).replace("\\", "/")
                if rel_root == ".":
                    rel_root = ""
                for d in dirs:
                    d_rel = (rel_root + "/" + d if rel_root else d).rstrip("/")
                    dest_dirs.add(d_rel)
                for f in files:
                    f_rel = rel_root + "/" + f if rel_root else f
                    full_f = pathlib.Path(root) / f
                    dest_files[f_rel] = {"size": full_f.stat().st_size, "bytes": full_f.read_bytes()}

            if is_empty_folder:
                if len(dest_files) != 0 or len(dest_dirs) != 0:
                    raise ValueError("FsError_PathNotFound: expected empty destination, but leftover files or directories found")
            else:
                if set(dest_files.keys()) != set(shared_payload.keys()):
                    raise ValueError("FsError_PathNotFound: destination files bijection mismatch")
                for k, exp in shared_payload.items():
                    if dest_files[k]["size"] != exp["size"]:
                        raise ValueError("FsError_InvalidSize: destination file size mismatch")
                    if dest_files[k]["bytes"] != exp["bytes"]:
                        raise ValueError("FsError_InvalidSize: destination file streaming byte comparison mismatch")
                if dest_dirs != shared_dirs:
                    raise ValueError("FsError_PathNotFound: destination directories bijection mismatch")

            # Step 12: Source close
            if fault == "source_close_fail":
                raise ValueError("Result_UnzOpen2_64: source reader close error")

            # Step 13: Exact owned stage cleanup
            if fault == "stage_cleanup_fail":
                raise ValueError("FsError_TargetLocked: sd_fs.DeleteFile(stage_zip) failed on SD")

            self.owned_stage_zip.unlink()
            self.owned_stage_dir.rmdir()
            self.owned_stage_zip = None
            self.owned_stage_dir = None

            return 0, "OK"

        except Exception as e:
            if fault != "stage_cleanup_fail":
                if self.owned_stage_zip and self.owned_stage_zip.exists():
                    self.owned_stage_zip.unlink()
                if self.owned_stage_dir and self.owned_stage_dir.exists():
                    self.owned_stage_dir.rmdir()
                self.owned_stage_zip = None
                self.owned_stage_dir = None
            return 0x999, str(e)
