# Models and fixtures for save recovery contract.
import os
import io
import zipfile
import struct
import copy

RESERVED_NAMES = {".nx_save_meta.bin", ".dbi_save_info.ini", ".dbi_save_extra"}
INVALID_CHARS = set(":*?\"<>|\\")
MAX_S64 = 0x7FFFFFFFFFFFFFFF

class MockProgressBox:
    def __init__(self, cancel_at_step: str = None):
        self.cancel_at_step = cancel_at_step
        self.steps = []

    def set_step(self, step_name: str) -> None:
        self.steps.append(step_name)

    def should_exit(self) -> bool:
        if self.cancel_at_step and self.steps and self.steps[-1] == self.cancel_at_step:
            return True
        return False


def build_real_zip(files: dict[str, bytes], dirs: set[str] = None, meta_first: bool = False,
                   corrupt_payload_crc: bool = False, truncated_file: str = None) -> bytes:
    """Builds a real binary ZIP using python's zipfile module."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        if dirs:
            for d in sorted(dirs):
                norm_d = d.strip("/") + "/"
                zinfo = zipfile.ZipInfo(norm_d)
                zf.writestr(zinfo, b"")

        items = list(files.items())
        if meta_first:
            meta_items = [it for it in items if os.path.basename(it[0]) in RESERVED_NAMES]
            other_items = [it for it in items if os.path.basename(it[0]) not in RESERVED_NAMES]
            items = meta_items + other_items

        for path, data in items:
            norm_p = path.lstrip("/")
            zf.writestr(norm_p, data)

    raw = buf.getvalue()

    if corrupt_payload_crc:
        # Locate deflated payload of the first non-meta file and flip a bit
        # in the compressed data to verify real CRC32 failure (without touching ZIP headers)
        corrupted = bytearray(raw)
        # Flip bit around byte 80 (inside file compressed stream)
        corrupted[min(len(corrupted) - 30, 80)] ^= 0xFF
        return bytes(corrupted)

    if truncated_file:
        # Intentionally truncate the entire archive by 20 bytes
        return raw[:-20]

    return raw


class RealZipRestorePipeline:
    def __init__(self, title_id: int, user_id: int, save_data_id: int,
                 save_data_type: int, save_data_space_id: int,
                 live_data_size: int, live_files: dict[str, bytes], live_dirs: set[str]):
        self.title_id = title_id
        self.user_id = user_id
        self.save_data_id = save_data_id
        self.save_data_type = save_data_type
        self.save_data_space_id = save_data_space_id
        self.live_data_size = live_data_size
        self.live_files = dict(live_files)   # path -> bytes
        self.live_dirs = set(live_dirs)       # set of normalized dir paths
        self.sdmc = {}                        # path -> bytes
        self.sdmc_dirs = set()                # set of dir paths
        self.reservation_collision_count = 0

    def run_restore(self, src_zip_bytes: bytes,
                    is_disa: bool = False,
                    mtp_active: bool = False,
                    pbox: MockProgressBox = None,
                    inject_failure: str = None) -> tuple[int, str]:
        """
        Executes the restore pipeline adhering to v0.13.853 specification.
        Returns (result_code, published_recovery_path)
        """
        if pbox is None:
            pbox = MockProgressBox()

        # Step 0: Shared guard - MTP refusal in RestoreSaveZip
        if mtp_active:
            if not is_disa:
                return 0x244E02, ""  # FsError_TargetLocked

        # RAW DISA bypasses ZIP recovery
        if is_disa:
            return 0, ""

        # Step 1: Preflight source payload with real zip parsing
        pbox.set_step("src_preflight")
        if pbox.should_exit(): return 0xEC01, ""

        try:
            with zipfile.ZipFile(io.BytesIO(src_zip_bytes), "r") as zf:
                bad_file = zf.testzip()
                if bad_file is not None or inject_failure == "src_preflight_fail":
                    return 0x103, ""
                src_total_bytes = sum(info.file_size for info in zf.infolist() if not info.is_dir())
        except Exception:
            return 0x103, ""

        if src_total_bytes > self.live_data_size:
            return 0x2F5C02, ""  # FsError_InvalidSize

        # Step 2: Live mount held - validate reserved names, invalid characters, nonnegative sizes, s64 overflow
        captured_files = {p: len(d) for p, d in self.live_files.items()}
        captured_dirs = set(self.live_dirs)
        live_bytes_sum = 0
        for fpath, data in self.live_files.items():
            if len(data) < 0:
                return 0x2F5C02, ""  # FsError_InvalidSize
            if MAX_S64 - live_bytes_sum < len(data):
                return 0x2F5C02, ""  # FsError_InvalidSize (s64 overflow)
            live_bytes_sum += len(data)

            fname = os.path.basename(fpath.rstrip("/"))
            if fname in RESERVED_NAMES:
                return 0x2EE202, ""  # FsError_PathAlreadyExists
            if any(c in fname for c in INVALID_CHARS):
                return 0x2E6602, ""  # FsError_InvalidCharacter

        for dpath in self.live_dirs:
            dname = os.path.basename(dpath.rstrip("/"))
            if any(c in dname for c in INVALID_CHARS):
                return 0x2E6602, ""  # FsError_InvalidCharacter

        # Step 3: Collision-safe directory reservation on SD
        if inject_failure == "parent_dir_error":
            return 0x244E02, ""

        rec_parent = "/dumps/recovery"
        self.sdmc_dirs.add(rec_parent)

        reserved_dir = None
        for counter in range(1000):
            if pbox.should_exit():
                return 0xEC01, ""
            dir_name = f"/dumps/recovery/20260917_150000_{self.save_data_id:016X}_{counter:03d}"
            if len(dir_name) >= 256:
                return 0x2E8202, ""  # FsError_TooLongPath
            if self.reservation_collision_count > 0:
                self.reservation_collision_count -= 1
                continue  # simulate PathAlreadyExists collision
            reserved_dir = dir_name
            self.sdmc_dirs.add(reserved_dir)
            break

        if not reserved_dir:
            return 0x2EE202, ""  # FsError_PathAlreadyExists

        temp_path = f"{reserved_dir}/recovery.zip.temp"
        final_path = f"{reserved_dir}/recovery.zip"

        # Three-state ownership tracking
        pub_state = "Unpublished"

        def cleanup_scope():
            nonlocal pub_state
            if pub_state == "Unpublished":
                # Clean up only temp and owned directory. NEVER touch final_path on collision!
                self.sdmc.pop(temp_path, None)
                # Only remove dir if empty
                if not any(k.startswith(reserved_dir + "/") and k != temp_path for k in self.sdmc.keys()):
                    self.sdmc_dirs.discard(reserved_dir)
            elif pub_state == "Renamed":
                # Clean up proven final file upon failed commit
                self.sdmc.pop(final_path, None)
                self.sdmc_dirs.discard(reserved_dir)

        # Step 4: Write candidate recovery archive (stdio stream context)
        pbox.set_step("write_recovery")
        if pbox.should_exit():
            cleanup_scope()
            return 0xEC01, ""

        if inject_failure in ("invalid_fd", "fflush_fail", "fsync_fail", "fclose_fail", "write_fail",
                              "entry_close_fail", "archive_close_fail", "no_space"):
            cleanup_scope()
            return 0x2F5C02, ""  # fail closed

        candidate_files = dict(self.live_files)
        meta_payload = struct.pack("<QQQ", self.title_id, self.user_id, self.save_data_id)
        candidate_files["/.nx_save_meta.bin"] = meta_payload

        # Real binary ZIP creation with metadata first
        rec_zip_bytes = build_real_zip(candidate_files, self.live_dirs, meta_first=True)

        if inject_failure == "corrupt_candidate_zip":
            rec_zip_bytes = build_real_zip(candidate_files, self.live_dirs, meta_first=True, corrupt_payload_crc=True)
        elif inject_failure == "truncated_candidate_file":
            rec_zip_bytes = build_real_zip(candidate_files, self.live_dirs, meta_first=True, truncated_file="yes")

        # Real malformed inventories with otherwise valid ZIP checksums.
        if inject_failure in ("duplicate_file", "duplicate_dir", "missing_file", "missing_dir",
                              "extra_file", "extra_dir"):
            rebuilt = io.BytesIO()
            with zipfile.ZipFile(io.BytesIO(rec_zip_bytes)) as original, zipfile.ZipFile(rebuilt, "w") as changed:
                payload = [i for i in original.infolist() if not i.is_dir() and i.filename != ".nx_save_meta.bin"]
                directories = [i for i in original.infolist() if i.is_dir()]
                selected = (directories if inject_failure.endswith("dir") else payload)[0]
                for info in original.infolist():
                    if inject_failure.startswith("missing_") and info.filename == selected.filename:
                        continue
                    changed.writestr(copy.copy(info), original.read(info))
                if inject_failure.startswith("duplicate_"):
                    changed.writestr(copy.copy(selected), original.read(selected))
                elif inject_failure == "extra_file":
                    changed.writestr("unexpected.bin", b"extra")
                elif inject_failure == "extra_dir":
                    changed.writestr("unexpected/", b"")
            rec_zip_bytes = rebuilt.getvalue()

        self.sdmc[temp_path] = rec_zip_bytes

        # Step 5: Reopen candidate recovery ZIP and preflight
        pbox.set_step("reopen_preflight")
        if pbox.should_exit():
            cleanup_scope()
            return 0xEC01, ""

        if inject_failure in ("reader_close_fail", "reopen_fail", "read_fail"):
            cleanup_scope()
            return 0x2F5C02, ""

        try:
            with zipfile.ZipFile(io.BytesIO(self.sdmc[temp_path]), "r") as rzf:
                crc_err = rzf.testzip()
                if crc_err is not None:
                    cleanup_scope()
                    return 0x2F5C02, ""  # FsError_InvalidSize on CRC error

                # Single advance iterator simulation past metadata
                verified_files = set()
                verified_dirs = set()

                for info in rzf.infolist():
                    if pbox.should_exit():
                        cleanup_scope()
                        return 0xEC01, ""

                    norm_name = "/" + info.filename.rstrip("/")
                    if info.filename.endswith("/"):
                        norm_dir = "/" + info.filename.rstrip("/")
                        if norm_dir not in self.live_dirs or norm_dir in verified_dirs:
                            cleanup_scope()
                            return 0x2EE202, ""
                        verified_dirs.add(norm_dir)
                    else:
                        base = os.path.basename(info.filename)
                        if base in RESERVED_NAMES:
                            # Advances past metadata without matching against live files
                            continue
                        if norm_name not in self.live_files or norm_name in verified_files:
                            cleanup_scope()
                            return 0x2EE202, ""  # FsError_PathNotFound

                        # Byte-for-byte stream comparison
                        live_data = self.live_files[norm_name]
                        if info.file_size != len(live_data):
                            cleanup_scope()
                            return 0x2F5C02, ""

                        zip_entry_data = rzf.read(info)
                        if inject_failure == "same_size_wrong_bytes":
                            zip_entry_data = b"X" * len(live_data)
                        if zip_entry_data != live_data:
                            cleanup_scope()
                            return 0x2F5C02, ""

                        verified_files.add(norm_name)

                # Bijection check
                if len(verified_dirs) != len(self.live_dirs) or len(verified_files) != len(self.live_files):
                    cleanup_scope()
                    return 0x2EE202, ""

        except Exception:
            cleanup_scope()
            return 0x2F5C02, ""

        # Candidate reader is explicitly closed before re-enumeration/publication!

        # Step 6: Live re-enumeration immediately before clear (capture vs current comparison)
        pbox.set_step("pre_clear")
        if pbox.should_exit():
            cleanup_scope()
            return 0xEC01, ""

        # Pre-clear live re-enumeration maps
        current_files = {p: len(d) for p, d in self.live_files.items()}
        current_dirs = set(self.live_dirs)

        # Actual map/set comparison
        if current_files != captured_files or current_dirs != captured_dirs:
            cleanup_scope()
            return 0x244E02, ""  # FsError_TargetLocked

        # Step 7: Atomic publication rename using native primitive
        # If final_path already exists (unexpected collision), rename fails!
        if final_path in self.sdmc or inject_failure == "rename_failure":
            cleanup_scope()
            return 0x2EE202, ""  # FsError_PathAlreadyExists

        # fsFsRenameFile succeeds:
        self.sdmc[final_path] = self.sdmc.pop(temp_path)
        pub_state = "Renamed"

        # Commit phase
        if inject_failure == "commit_failure":
            cleanup_scope()
            return 0x244E02, ""

        pub_state = "Published"
        published_recovery_path = final_path

        # Step 8: Pre-clear cancellation check
        pbox.set_step("published")
        if pbox.should_exit():
            # Recovery archive RETAINED on SD
            return 0xEC01, published_recovery_path

        # Step 9: Ponytail milestone - destructive clear
        self.live_files.clear()
        self.live_dirs.clear()

        # Step 10: Unzip source payload to destination
        pbox.set_step("unzip_all")
        if pbox.should_exit() or inject_failure == "extract_failure":
            # Target mutated/cleared, published recovery archive remains safely on SD!
            return 0x10E, published_recovery_path

        with zipfile.ZipFile(io.BytesIO(src_zip_bytes), "r") as zf:
            for info in zf.infolist():
                if info.is_dir():
                    self.live_dirs.add("/" + info.filename.rstrip("/"))
                else:
                    base = os.path.basename(info.filename)
                    if base not in RESERVED_NAMES:
                        self.live_files["/" + info.filename.lstrip("/")] = zf.read(info)

        return 0, published_recovery_path
