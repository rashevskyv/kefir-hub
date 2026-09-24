# Behavioral model and arithmetic guards for save payload summary contract.

INT64_MAX = 9223372036854775807
UINT64_MAX = 18446744073709551615

# Distinct synthetic diagnostic result tokens for behavioral verification
RES_OK = 0
ERR_INVALID_SIZE = 1
ERR_INVALID_CHARACTER = 2
ERR_UNZ_OPEN2_64 = 3
ERR_UNZ_GET_GLOBAL_INFO64 = 4
ERR_UNZ_GO_TO_FIRST_FILE = 5
ERR_UNZ_GO_TO_NEXT_FILE = 6
ERR_UNZ_GET_CURRENT_FILE_INFO64 = 7
ERR_UNZ_OPEN_CURRENT_FILE = 8
ERR_UNZ_READ_CURRENT_FILE = 9
ERR_CRC_MISMATCH = 10
ERR_CANCELLED = 11

def check_counter_overflow(current_count: int) -> bool:
    """
    Defensive arithmetic check mirroring C++:
    local_summary.file_count == std::numeric_limits<s64>::max()
    local_summary.directory_count == std::numeric_limits<s64>::max()
    Returns True if an increment would overflow s64 max.
    Unreachable in a valid archive due to entry_count <= INT64_MAX bound.
    """
    return current_count >= INT64_MAX

def check_aggregate_overflow(current_bytes: int, to_add: int) -> bool:
    """
    Checked aggregate addition mirroring C++:
    static_cast<u64>(std::numeric_limits<s64>::max()) - static_cast<u64>(local_summary.file_bytes) < info.uncompressed_size
    Returns True if addition would overflow s64 max.
    """
    if to_add < 0 or current_bytes < 0:
        return True
    return (INT64_MAX - current_bytes) < to_add

def check_drained_read_overflow(bytes_drained: int, read_len: int, declared_size: int) -> tuple[bool, str]:
    """
    Checked drained read accumulation mirroring C++:
    std::numeric_limits<u64>::max() - bytes_drained < read_u64 || bytes_drained + read_u64 > info.uncompressed_size
    Returns (overflowed, reason) where reason is 'u64_wrap' or 'declared_excess'.
    """
    if UINT64_MAX - bytes_drained < read_len:
        return True, "u64_wrap"
    if bytes_drained + read_len > declared_size:
        return True, "declared_excess"
    return False, "ok"

class BehavioralPreflightModel:
    """
    Python behavioral reference model reproducing checked accounting,
    normalization, character sanitization, directory classification,
    draining, overflow checks, rewind, and summary publication rules.
    """
    NX_SAVE_META_NAME = ".nx_save_meta.bin"
    DBI_SAVE_INFO_NAME = ".dbi_save_info.ini"
    DBI_SAVE_EXTRA_NAME = ".dbi_save_extra"

    @staticmethod
    def is_invalid_path_char(c: str) -> bool:
        uc = ord(c)
        if uc < 0x20:
            return True
        return c in (':', '*', '?', '"', '<', '>', '|', '\\')

    @classmethod
    def sanitize_zip_entry_name(cls, name: str) -> str:
        """
        Replaces HOS-invalid path characters with '_' per component, preserving '/',
        mirroring SanitizeZipEntryName in threaded_file_transfer.cpp.
        """
        res = []
        for c in name:
            res.append('_' if cls.is_invalid_path_char(c) else c)
        return "".join(res)

    @staticmethod
    def is_safe_archive_entry(path: str) -> bool:
        if not path or path.startswith('/'):
            return False
        for c in path:
            if ord(c) < 0x20 or ord(c) == 0x7F or c in ('\\', ':'):
                return False
        parts = path.split('/')
        for part in parts:
            if part in ('.', '..'):
                return False
        return True

    @classmethod
    def resolve_entry_name(cls, name: str, save_dbi_compat: bool) -> tuple[int, str]:
        if not name:
            return ERR_INVALID_CHARACTER, ""
        norm = name
        if save_dbi_compat and norm.startswith('/'):
            norm = norm[1:]
        if not cls.is_safe_archive_entry(norm):
            return ERR_INVALID_CHARACTER, ""
        return RES_OK, norm

    @staticmethod
    def append_path(base_path: str, name: str) -> str:
        if not base_path:
            return name
        if base_path.endswith('/'):
            return base_path + name
        return base_path + '/' + name

    @staticmethod
    def is_safe_extraction_destination(dest_path: str, base_path: str, save_dbi_compat: bool) -> bool:
        if not save_dbi_compat:
            return True
        if not dest_path:
            return False
        if not dest_path.startswith(base_path):
            return False
        if '//' in dest_path or '\\' in dest_path or ':' in dest_path:
            return False
        parts = dest_path.split('/')
        for part in parts:
            if part in ('.', '..'):
                return False
        return True

    @classmethod
    def is_save_reserved_metadata_root(cls, name: str) -> bool:
        s = name.lower()
        return s in (cls.NX_SAVE_META_NAME.lower(), cls.DBI_SAVE_INFO_NAME.lower(), cls.DBI_SAVE_EXTRA_NAME.lower())

    @classmethod
    def save_filter(cls, name: str, _path: str) -> tuple[bool, str]:
        """
        Actual Save Menu restore filter:
        - Case-insensitive match for reserved root metadata:
          .nx_save_meta.bin, .dbi_save_info.ini, .dbi_save_extra.
        - Root metadata entries are excluded from restore.
        - Nested files with the same basename remain payload.
        """
        if cls.is_save_reserved_metadata_root(name):
            return False, _path
        return True, _path

    def execute_preflight(
        self,
        entries: list[dict],
        base_path: str = "/",
        filter_fn = None,
        save_dbi_compat: bool = True,
        output_summary: dict | None = None,
        cancel_at_step: int | None = None,
        rewind_fails: bool = False
    ) -> tuple[int, dict | None]:
        if not entries:
            return ERR_INVALID_SIZE, output_summary

        if len(entries) > INT64_MAX:
            return ERR_INVALID_SIZE, output_summary

        local_summary = {
            "file_bytes": 0,
            "file_count": 0,
            "directory_count": 0
        }

        step = 0
        for entry in entries:
            step += 1

            uncompressed_size = entry.get("uncompressed_size", 0)
            if uncompressed_size < 0 or uncompressed_size > INT64_MAX:
                return ERR_INVALID_SIZE, output_summary

            raw_name = entry.get("name", "")
            rc, name = self.resolve_entry_name(raw_name, save_dbi_compat)
            if rc != RES_OK:
                return rc, output_summary

            # Sanitizer pass
            name = self.sanitize_zip_entry_name(name)

            dest_path = self.append_path(base_path, name)

            keep = True
            if filter_fn:
                keep, dest_path = filter_fn(name, dest_path)

            if keep:
                path_len = len(dest_path)
                if path_len == 0:
                    return ERR_INVALID_CHARACTER, output_summary

                if not self.is_safe_extraction_destination(dest_path, base_path, save_dbi_compat):
                    return ERR_INVALID_CHARACTER, output_summary

                if dest_path.endswith('/'):
                    if check_counter_overflow(local_summary["directory_count"]):
                        return ERR_INVALID_SIZE, output_summary
                    local_summary["directory_count"] += 1
                else:
                    if check_counter_overflow(local_summary["file_count"]):
                        return ERR_INVALID_SIZE, output_summary
                    if check_aggregate_overflow(local_summary["file_bytes"], uncompressed_size):
                        return ERR_INVALID_SIZE, output_summary
                    local_summary["file_count"] += 1
                    local_summary["file_bytes"] += uncompressed_size

            # Entry open
            if entry.get("open_fails", False):
                return ERR_UNZ_OPEN_CURRENT_FILE, output_summary

            # Draining loop
            chunks = entry.get("chunks", [uncompressed_size])
            bytes_drained = 0
            for chunk in chunks:
                if cancel_at_step is not None and step == cancel_at_step:
                    return ERR_CANCELLED, output_summary

                if entry.get("cancel_during_read", False):
                    return ERR_CANCELLED, output_summary

                if entry.get("read_error", False):
                    return ERR_UNZ_READ_CURRENT_FILE, output_summary

                if chunk > 0:
                    overflowed, _ = check_drained_read_overflow(bytes_drained, chunk, uncompressed_size)
                    if overflowed:
                        # unzCloseCurrentFile called before throw
                        return ERR_INVALID_SIZE, output_summary
                    bytes_drained += chunk

            # Entry close
            if entry.get("close_crc_error", False):
                return ERR_CRC_MISMATCH, output_summary

            if entry.get("close_error", False):
                return ERR_UNZ_READ_CURRENT_FILE, output_summary

            if bytes_drained != uncompressed_size:
                return ERR_INVALID_SIZE, output_summary

            if entry.get("crc_mismatch", False):
                return ERR_CRC_MISMATCH, output_summary

        if rewind_fails:
            return ERR_UNZ_GO_TO_FIRST_FILE, output_summary

        # Published only on complete success
        if output_summary is not None:
            output_summary["file_bytes"] = local_summary["file_bytes"]
            output_summary["file_count"] = local_summary["file_count"]
            output_summary["directory_count"] = local_summary["directory_count"]

        return RES_OK, output_summary
