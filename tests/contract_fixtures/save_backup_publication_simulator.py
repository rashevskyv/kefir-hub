# Simulation engine and recovery sink for save backup publication contract.
import os
import io
import zipfile
import shutil
from typing import List, Dict, Optional, Tuple
from contract_fixtures.save_backup_publication_encoding import (
    SaveAttribute, SelectedEntry, encode_extra_data, decode_extra_data,
    encode_nx_save_meta, decode_nx_save_meta, get_actual_dbi_account_label,
    get_actual_dbi_space_label, NX_SAVE_META_MAGIC, NX_SAVE_META_VERSION,
    NX_SAVE_META_NAME, DBI_SAVE_INFO_NAME, DBI_SAVE_EXTRA_NAME,
    FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_TYPE_SYSTEM_BCAT
)

RES_OK = 0
FS_ERROR_PATH_NOT_FOUND = 0x202
FS_ERROR_PATH_ALREADY_EXISTS = 0x402
FS_ERROR_NOT_MOUNTED = 0x244E02
FS_ERROR_TARGET_LOCKED = 0x244E02
FS_ERROR_TOO_LONG_PATH = 0x2EE602
FS_ERROR_INVALID_SIZE = 0x2F5C02
RESULT_ZIP_WRITE_IN_FILE = 0x30001
RESULT_FS_UNKNOWN_STDIO_ERROR = 0x30002
RESULT_UNZ_OPEN2_64 = 0x30003
SVC_ERROR_CANCELLED = 0xEC01
ERR_IPC_READ_FAILED = 0x1002
ERR_CANNOT_RESTORE_TO_SYSTEM = 0xEE01
ERR_PREFLIGHT_EMPTY = 0xEE02
ERR_OVERFLOW = 0xEE03
ERR_VERIFICATION_MISMATCH = 0xEE04
ERR_PREFLIGHT_FAILED = 0xEE05

def check(condition: bool, msg: str) -> None:
    if not condition:
        raise AssertionError(msg)

class ModeledRecoverySink:
    """
    Modeled stdio file-like sink mirroring RecoveryOpen/Write/Close:
    - Real OS file writing on disk.
    - Short-write hook that actually writes fewer bytes than requested.
    - Sink lifecycle hooks for fflush, fileno (-1), fsync, fclose.
    """

    def __init__(self, filepath: str, sim: "ConnectedBackupSimulator"):
        self.filepath = filepath
        self.sim = sim
        self.raw_file = open(filepath, "wb")
        self.write_failed = False
        self.flush_failed = False
        self.sync_failed = False
        self.close_failed = False
        self.fp_valid = True

    def seek(self, offset: int, whence: int = os.SEEK_SET) -> int:
        return self.raw_file.seek(offset, whence)

    def tell(self) -> int:
        return self.raw_file.tell()

    def flush(self) -> None:
        self.raw_file.flush()

    def write(self, b: bytes) -> int:
        if self.sim.inject_short_write:
            # Actually write fewer bytes than requested
            short_len = min(len(b), 4)
            written = self.raw_file.write(b[:short_len])
            if written != len(b):
                self.write_failed = True
            return written
        written = self.raw_file.write(b)
        return written

    def close(self) -> None:
        self.sim.events.append("stream_sink_fflush")
        if self.sim.inject_fflush_error:
            self.flush_failed = True
        else:
            try:
                self.raw_file.flush()
            except Exception:
                self.flush_failed = True

        self.sim.events.append("stream_sink_fsync")
        fd = -1 if self.sim.inject_invalid_fd else self.raw_file.fileno()
        if fd == -1:
            self.sync_failed = True
        else:
            try:
                if self.sim.fsync_fault_hook:
                    self.sim.fsync_fault_hook(self.raw_file.fileno())
                elif self.sim.inject_fsync_error:
                    # Invalidate descriptor so real os.fsync executes and raises OSError
                    os.close(self.raw_file.fileno())
                os.fsync(self.raw_file.fileno())
            except OSError:
                self.sync_failed = True
            except Exception:
                self.sync_failed = True

        self.sim.events.append("stream_sink_fclose")
        if self.sim.inject_fclose_error:
            self.close_failed = True
            try:
                self.raw_file.close()
            except Exception:
                pass
        else:
            try:
                self.raw_file.close()
            except Exception:
                pass

        self.fp_valid = False


class ConnectedBackupSimulator:
    """
    Python policy simulation model of BackupSaveInternal SD checked publication.
    Provides bounded Python policy/artifact evidence of publication lifecycle
    and fault handling. Does NOT claim full concurrency/hardware/IPC/libnx proof
    or atomic hardware rollback.
    """

    def __init__(self, root_dir: str):
        self.root_dir = root_dir
        self.events: List[str] = []
        self.pub_state = "Unpublished"
        self.owned_stage_created = False
        self.stage_dir: Optional[str] = None
        self.owned_temp_path: Optional[str] = None
        self.final_path: Optional[str] = None

        # Fault injection hooks
        self.inject_sd_open_error: Optional[int] = None
        self.inject_pre_probe_error: Optional[int] = None
        self.inject_parent_create_error: Optional[int] = None
        self.inject_stage_primitive_error: Optional[int] = None
        self.inject_stage_commit_error: Optional[int] = None
        self.inject_cancel_at_gate: Optional[int] = None  # 1, 2, 3, or 4
        self.inject_source_read_error: bool = False
        self.inject_short_write: bool = False
        self.inject_zip_entry_close_error: bool = False
        self.inject_zip_final_close_error: bool = False
        self.inject_fflush_error: bool = False
        self.inject_invalid_fd: bool = False
        self.inject_fsync_error: bool = False
        self.fsync_fault_hook: Optional[Callable[[int], None]] = None
        self.inject_fclose_error: bool = False
        self.inject_writer_sdmc_commit_error: Optional[int] = None
        self.inject_writer_sd_commit_error: Optional[int] = None
        self.pre_rename_hook: Optional[Callable[[], None]] = None
        self.inject_post_probe_error: Optional[int] = None
        self.inject_rename_error: Optional[int] = None
        self.inject_post_rename_sdmc_commit_error: Optional[int] = None
        self.inject_post_rename_sd_commit_error: Optional[int] = None
        self.inject_cleanup_delete_file_error: bool = False
        self.inject_cleanup_delete_dir_error: bool = False

    def run_backup(
        self,
        entry: SelectedEntry,
        extra_data_bytes: bytes,
        source_dir: str,
        rel_final_path: str,
        compressed: bool = False,
    ) -> Tuple[int, Optional[str]]:
        self.events.clear()
        self.pub_state = "Unpublished"
        self.owned_stage_created = False

        self.final_path = os.path.join(self.root_dir, rel_final_path.lstrip("/\\"))
        self.stage_dir = self.final_path + ".stage"
        self.owned_temp_path = os.path.join(self.stage_dir, "backup.zip.temp")

        # Zero save ID check
        if entry.save_data_id == 0:
            self.events.append("zero_id_early_return")
            return RES_OK, None

        # Identity comparison
        live_attr, extra_meta = decode_extra_data(extra_data_bytes)
        target_attr = entry.attr
        if (
            live_attr.application_id != target_attr.application_id
            or live_attr.uid_low != target_attr.uid_low
            or live_attr.uid_high != target_attr.uid_high
            or live_attr.system_save_data_id != target_attr.system_save_data_id
            or live_attr.save_data_type != target_attr.save_data_type
            or live_attr.save_data_rank != target_attr.save_data_rank
            or live_attr.save_data_index != target_attr.save_data_index
        ):
            self.events.append("identity_guard_mismatch")
            return FS_ERROR_PATH_NOT_FOUND, None

        # Source collection scan
        source_files: List[Tuple[str, str, int]] = []
        for root, dirs, files in os.walk(source_dir):
            for f in sorted(files):
                full_p = os.path.join(root, f)
                rel_p = os.path.relpath(full_p, source_dir).replace("\\", "/")
                source_files.append((rel_p, full_p, os.path.getsize(full_p)))

        if not source_files:
            self.events.append("empty_collections_early_return")
            return RES_OK, None

        def cleanup():
            self.events.append(f"cleanup_invoked(state={self.pub_state}, owned_stage={self.owned_stage_created})")
            if self.pub_state == "Unpublished":
                if self.owned_stage_created:
                    if os.path.exists(self.owned_temp_path):
                        if not self.inject_cleanup_delete_file_error:
                            os.remove(self.owned_temp_path)
                            self.events.append("cleaned_owned_temp")
                        else:
                            self.events.append("injected_cleanup_file_fail")
                    if os.path.exists(self.stage_dir):
                        if not self.inject_cleanup_delete_dir_error:
                            try:
                                os.rmdir(self.stage_dir)
                                self.events.append("cleaned_stage_dir")
                            except OSError:
                                self.events.append("stage_dir_cleanup_oserror")
                        else:
                            self.events.append("injected_cleanup_dir_fail")
            elif self.pub_state == "Renamed":
                if os.path.exists(self.final_path):
                    if not self.inject_cleanup_delete_file_error:
                        os.remove(self.final_path)
                        self.events.append("cleaned_owned_final")
                    else:
                        self.events.append("injected_cleanup_file_fail")
                if self.owned_stage_created and os.path.exists(self.stage_dir):
                    if not self.inject_cleanup_delete_dir_error:
                        try:
                            os.rmdir(self.stage_dir)
                            self.events.append("cleaned_stage_dir")
                        except OSError:
                            self.events.append("stage_dir_cleanup_oserror")
                    else:
                        self.events.append("injected_cleanup_dir_fail")
            elif self.pub_state == "Published":
                if self.owned_stage_created and os.path.exists(self.stage_dir):
                    if not self.inject_cleanup_delete_dir_error:
                        try:
                            os.rmdir(self.stage_dir)
                            self.events.append("cleaned_stage_dir")
                        except OSError:
                            self.events.append("stage_dir_cleanup_oserror")
                    else:
                        self.events.append("injected_cleanup_dir_fail")

        # --- Step 1: SD open check ---
        self.events.append("sd_open_check")
        if self.inject_sd_open_error is not None:
            self.events.append("sd_open_failed")
            cleanup()
            return self.inject_sd_open_error, None

        # --- Step 2: Final path pre-probe ---
        self.events.append("pre_probe_final")
        if self.inject_pre_probe_error is not None:
            cleanup()
            return self.inject_pre_probe_error, None
        if os.path.exists(self.final_path):
            self.events.append("final_path_already_exists_pre_probe")
            cleanup()
            return FS_ERROR_PATH_ALREADY_EXISTS, None

        # --- Step 3: Parent directory creation ---
        parent_dir = os.path.dirname(self.final_path)
        self.events.append(f"create_parent_dirs({parent_dir})")
        if self.inject_parent_create_error is not None:
            cleanup()
            return self.inject_parent_create_error, None
        os.makedirs(parent_dir, exist_ok=True)

        # Path length bounds check
        if len(self.stage_dir) > 768 or len(self.owned_temp_path) > 768:
            self.events.append("path_too_long")
            cleanup()
            return FS_ERROR_TOO_LONG_PATH, None

        # --- Gate 1: Cancellation before reservation ---
        self.events.append("cancellation_gate_1")
        if self.inject_cancel_at_gate == 1:
            self.events.append("cancelled_at_gate_1")
            cleanup()
            return SVC_ERROR_CANCELLED, None

        # --- Step 4: Reserve exclusive sibling stage directory ---
        self.events.append(f"reserve_stage_dir({self.stage_dir})")
        if os.path.exists(self.stage_dir):
            self.events.append("stage_dir_collision")
            cleanup()
            return FS_ERROR_PATH_ALREADY_EXISTS, None
        if self.inject_stage_primitive_error is not None:
            self.events.append("stage_primitive_failed")
            cleanup()
            return self.inject_stage_primitive_error, None

        os.mkdir(self.stage_dir)
        self.owned_stage_created = True
        self.events.append("ownership_set_owned_stage_true")

        # Stage commit
        self.events.append("stage_dir_commit")
        if self.inject_stage_commit_error is not None:
            self.events.append("stage_commit_failed")
            cleanup()
            return self.inject_stage_commit_error, None

        # --- Gate 2: Cancellation before write ---
        self.events.append("cancellation_gate_2")
        if self.inject_cancel_at_gate == 2:
            self.events.append("cancelled_at_gate_2")
            cleanup()
            return SVC_ERROR_CANCELLED, None

        # --- Step 5: WriteSaveBackupZip using checked stream transport ---
        self.events.append("write_save_backup_zip_start")
        is_dbi_format = (entry.attr.save_data_type not in (FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_TYPE_SYSTEM_BCAT))

        sink = ModeledRecoverySink(self.owned_temp_path, self)
        zip_compress_type = zipfile.ZIP_DEFLATED if compressed else zipfile.ZIP_STORED

        zf = None
        try:
            zf = zipfile.ZipFile(sink, "w", compression=zip_compress_type)

            def write_zip_entry(entry_name: str, data: bytes, is_payload: bool = False):
                self.events.append(f"writer_zip_entry_open({entry_name})")
                handle = zf.open(entry_name, "w")
                try:
                    handle.write(data)
                finally:
                    handle.close()
                if is_payload and self.inject_zip_entry_close_error:
                    raise IOError("Modeled zipCloseFileInZip failure after handle.close()")
                self.events.append(f"writer_zip_entry_close({entry_name})")

            # If Sphaira format: NXSaveMeta at start
            if not is_dbi_format:
                meta_bytes = encode_nx_save_meta(entry.attr, extra_meta, entry.size)
                write_zip_entry(NX_SAVE_META_NAME, meta_bytes, is_payload=False)

            # If DBI format: explicit directory entries
            if is_dbi_format:
                for rel_p, _, _ in source_files:
                    parent_rel = os.path.dirname(rel_p)
                    if parent_rel:
                        dir_entry_name = "/" + parent_rel.replace("\\", "/") + "/"
                        if dir_entry_name not in zf.namelist():
                            write_zip_entry(dir_entry_name, b"", is_payload=False)

            # Payload files
            for rel_p, full_p, size in source_files:
                if self.inject_source_read_error:
                    raise IOError("Simulated source read failure")
                with open(full_p, "rb") as sf:
                    content = sf.read()
                entry_in_zip = "/" + rel_p if is_dbi_format else rel_p
                write_zip_entry(entry_in_zip, content, is_payload=True)

            # If DBI format: meta entries last
            if is_dbi_format:
                account_label = get_actual_dbi_account_label(entry.attr.save_data_type, "TestAccount")
                space_str = get_actual_dbi_space_label(entry.save_data_space_id)
                dbi_info_text = (
                    f"TitleId={entry.attr.application_id:016X}\n"
                    f"TitleName={entry.name}\n"
                    f"BackupDate=2026-09-18 16:25:00\n"
                    f"Account={account_label}\n"
                    f"Space={space_str}"
                )
                write_zip_entry(DBI_SAVE_INFO_NAME, dbi_info_text.encode("utf-8"), is_payload=False)
                write_zip_entry(DBI_SAVE_EXTRA_NAME, extra_data_bytes, is_payload=False)

            # Archive finalization: explicit actual zf.close()
            self.events.append("writer_zip_final_close_call")
            zf.close()
            if self.inject_zip_final_close_error:
                raise IOError("Modeled zipClose failure after actual zf.close()")
            self.events.append("writer_zip_final_close")

        except Exception as ex:
            self.events.append(f"write_zip_exception: {ex}")
            if zf is not None:
                try:
                    zf.close()
                except Exception:
                    pass
            sink.close()
            cleanup()
            return RESULT_ZIP_WRITE_IN_FILE, None

        # Modeled RecoveryClose
        sink.close()

        # Recovery stream callback checks
        if sink.write_failed:
            self.events.append("sink_write_failed_detected")
            cleanup()
            return RESULT_ZIP_WRITE_IN_FILE, None
        if sink.flush_failed:
            self.events.append("sink_flush_failed_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None
        if sink.sync_failed:
            self.events.append("sink_sync_failed_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None
        if sink.close_failed:
            self.events.append("sink_close_failed_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None
        if sink.fp_valid:
            self.events.append("sink_fp_leak_detected")
            cleanup()
            return RESULT_FS_UNKNOWN_STDIO_ERROR, None

        # Writer device and native SD commit BEFORE rename
        self.events.append("writer_fsdev_commit_device_sdmc")
        if self.inject_writer_sdmc_commit_error is not None:
            self.events.append("writer_sdmc_commit_failed")
            cleanup()
            return self.inject_writer_sdmc_commit_error, None

        self.events.append("writer_native_sd_commit")
        if self.inject_writer_sd_commit_error is not None:
            self.events.append("writer_sd_commit_failed")
            cleanup()
            return self.inject_writer_sd_commit_error, None

        # --- Gate 3: Cancellation before rename ---
        self.events.append("cancellation_gate_3")
        if self.inject_cancel_at_gate == 3:
            self.events.append("cancelled_at_gate_3")
            cleanup()
            return SVC_ERROR_CANCELLED, None

        # Operation hook simulating concurrent external action before post-probe
        if self.pre_rename_hook is not None:
            self.pre_rename_hook()

        # --- Step 6: Post-probe final destination before rename ---
        self.events.append("post_probe_final")
        if self.inject_post_probe_error is not None:
            cleanup()
            return self.inject_post_probe_error, None
        if os.path.exists(self.final_path):
            self.events.append("final_path_already_exists_post_probe")
            cleanup()
            return FS_ERROR_PATH_ALREADY_EXISTS, None

        # --- Step 7: Native primitive rename ---
        self.events.append(f"primitive_rename({self.owned_temp_path} -> {self.final_path})")
        if self.inject_rename_error is not None:
            self.events.append("rename_failed")
            cleanup()
            return self.inject_rename_error, None

        os.rename(self.owned_temp_path, self.final_path)
        self.pub_state = "Renamed"
        self.events.append("ownership_set_renamed")

        # --- Step 8: Final SD Commits ---
        self.events.append("post_rename_fsdev_commit_device_sdmc")
        if self.inject_post_rename_sdmc_commit_error is not None:
            self.events.append("post_rename_sdmc_commit_failed")
            cleanup()
            return self.inject_post_rename_sdmc_commit_error, None

        self.events.append("post_rename_native_sd_commit")
        if self.inject_post_rename_sd_commit_error is not None:
            self.events.append("post_rename_sd_commit_failed")
            cleanup()
            return self.inject_post_rename_sd_commit_error, None

        # --- Publication Success ---
        self.pub_state = "Published"
        self.events.append("ownership_set_published")

        # Post-publication cancellation gate must NEVER fail
        if self.inject_cancel_at_gate == 4:
            self.events.append("ignored_cancellation_post_publication")

        cleanup()
        self.events.append("backup_success")
        return RES_OK, self.final_path


# ==============================================================================
# 3. Connected Verification: 7 Concrete Types & Independent Wire Readback
# ==============================================================================

def assert_successful_ordered_subsequence(events: List[str]):
    """
    Asserts the strict ordered subsequence of lifecycle events for a successful publication:
    primitive stage -> owned -> stage commit -> entry close ->
    archive close -> stream flush/sync/close -> writer device/native commits ->
    fresh final probe -> rename -> Renamed -> final device/native commits ->
    Published -> success.
    """
    def find_idx(predicate, start_from=0, name=""):
        for i in range(start_from, len(events)):
            if predicate(events[i]):
                return i
        raise AssertionError(f"Subsequence item '{name}' not found after index {start_from} in events:\n" + "\n".join(events))

    idx_stage = find_idx(lambda e: e.startswith("reserve_stage_dir("), 0, "primitive stage")
    idx_owned = find_idx(lambda e: e == "ownership_set_owned_stage_true", idx_stage + 1, "owned stage true")
    idx_stage_commit = find_idx(lambda e: e == "stage_dir_commit", idx_owned + 1, "stage commit")
    idx_entry_close = find_idx(lambda e: e.startswith("writer_zip_entry_close("), idx_stage_commit + 1, "entry close")
    idx_arch_close = find_idx(lambda e: e == "writer_zip_final_close", idx_entry_close + 1, "archive close")
    idx_flush = find_idx(lambda e: e == "stream_sink_fflush", idx_arch_close + 1, "stream flush")
    idx_sync = find_idx(lambda e: e == "stream_sink_fsync", idx_flush + 1, "stream sync")
    idx_sink_close = find_idx(lambda e: e == "stream_sink_fclose", idx_sync + 1, "stream close")
    idx_w_sdmc = find_idx(lambda e: e == "writer_fsdev_commit_device_sdmc", idx_sink_close + 1, "writer sdmc commit")
    idx_w_sd = find_idx(lambda e: e == "writer_native_sd_commit", idx_w_sdmc + 1, "writer native sd commit")
    idx_probe = find_idx(lambda e: e == "post_probe_final", idx_w_sd + 1, "fresh final probe")
    idx_rename = find_idx(lambda e: e.startswith("primitive_rename("), idx_probe + 1, "rename primitive")
    idx_renamed = find_idx(lambda e: e == "ownership_set_renamed", idx_rename + 1, "Renamed state")
    idx_pr_sdmc = find_idx(lambda e: e == "post_rename_fsdev_commit_device_sdmc", idx_renamed + 1, "post-rename sdmc commit")
    idx_pr_sd = find_idx(lambda e: e == "post_rename_native_sd_commit", idx_pr_sdmc + 1, "post-rename native sd commit")
    idx_published = find_idx(lambda e: e == "ownership_set_published", idx_pr_sd + 1, "Published state")
    idx_success = find_idx(lambda e: e == "backup_success", idx_published + 1, "success")

    ordered_steps = [
        ("primitive stage", idx_stage),
        ("owned", idx_owned),
        ("stage commit", idx_stage_commit),
        ("entry close", idx_entry_close),
        ("archive close", idx_arch_close),
        ("stream flush", idx_flush),
        ("stream sync", idx_sync),
        ("stream close", idx_sink_close),
        ("writer device commit", idx_w_sdmc),
        ("writer native commit", idx_w_sd),
        ("fresh final probe", idx_probe),
        ("rename", idx_rename),
        ("Renamed", idx_renamed),
        ("final device commit", idx_pr_sdmc),
        ("final native commit", idx_pr_sd),
        ("Published", idx_published),
        ("success", idx_success),
    ]
    for i in range(len(ordered_steps) - 1):
        name_curr, idx_curr = ordered_steps[i]
        name_next, idx_next = ordered_steps[i + 1]
        check(idx_curr < idx_next, f"Ordered subsequence violation: {name_curr} (idx {idx_curr}) not before {name_next} (idx {idx_next})")
