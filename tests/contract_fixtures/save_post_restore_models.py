# Synthetic models and verification engine for post restore contract.
import os
import io
import zipfile
import zlib
import struct

RES_OK = 0
ERR_VERIFICATION_FAILED = 0xEE01
ERR_UNZ_READ = 0xEE02
ERR_UNZ_CLOSE = 0xEE03
ERR_CANCELLED = 0xEE04
ERR_OVERFLOW = 0xEE05
ERR_INVALID_PATH = 0xEE06
ERR_INVALID_SIZE = 0xEE07
ERR_REWIND_FAILED = 0xEE08
INVALID_CHARS = set(":*?\"<>|\\")
MAX_S64 = 0x7FFFFFFFFFFFFFFF
RESERVED_NAMES = {".nx_save_meta.bin", ".dbi_save_info.ini", ".dbi_save_extra"}

class SyntheticSaveFs:
    """Mock filesystem simulating the Nintendo Switch FsNativeSave."""
    def __init__(self, files: dict[str, bytes] = None, dirs: set[str] = None):
        self.files = dict(files) if files else {}
        self.dirs = set(dirs) if dirs else set()
        self.is_open = True
        self.committed = False
        self.read_only = False

    def close(self):
        self.is_open = False

    def commit(self):
        if self.read_only:
            raise PermissionError("Cannot commit read-only mount")
        self.committed = True

    def get_collections(self):
        # Returns list of collection-like dicts
        cols = {}
        cols["/"] = {"files": [], "dirs": []}
        for d in sorted(self.dirs):
            cols[d] = {"files": [], "dirs": []}

        for d in self.dirs:
            parent = os.path.dirname(d)
            if not parent.startswith("/"): parent = "/" + parent
            if parent in cols:
                cols[parent]["dirs"].append(os.path.basename(d))

        for fpath, data in self.files.items():
            parent = os.path.dirname(fpath)
            if not parent.startswith("/"): parent = "/" + parent
            if parent not in cols:
                cols[parent] = {"files": [], "dirs": []}
            cols[parent]["files"].append((os.path.basename(fpath), len(data)))

        result = []
        for p, content in cols.items():
            result.append({"path": p, "files": content["files"], "dirs": content["dirs"]})
        return result


def build_test_zip(files: dict[str, bytes], dirs: list[str] = None, leading_slash: bool = False,
                   corrupt_crc: bool = False, corrupt_file: str = None, truncated_file: str = None) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        if dirs:
            for d in dirs:
                name = ("/" if leading_slash else "") + d.strip("/") + "/"
                zinfo = zipfile.ZipInfo(name)
                zf.writestr(zinfo, b"")

        for name, data in files.items():
            entry_name = ("/" if leading_slash else "") + name.lstrip("/")
            if name == truncated_file:
                # Write truncated payload
                zinfo = zipfile.ZipInfo(entry_name)
                zinfo.file_size = len(data) + 100 # Declared larger than actual
                zf.writestr(zinfo, data)
            else:
                zf.writestr(entry_name, data)

    raw_zip = buf.getvalue()
    if corrupt_crc or corrupt_file:
        raw_zip = bytearray(raw_zip)
        idx = 0
        while True:
            pos = raw_zip.find(b"PK\x01\x02", idx)
            if pos == -1:
                break
            fname_len = struct.unpack_from("<H", raw_zip, pos + 28)[0]
            fname = raw_zip[pos + 46 : pos + 46 + fname_len].decode("utf-8", errors="ignore")
            if corrupt_file:
                if fname.lstrip("/") == corrupt_file.lstrip("/"):
                    raw_zip[pos + 16] ^= 0xFF # Corrupt CRC in central directory
                    break
            elif corrupt_crc:
                raw_zip[pos + 16] ^= 0xFF
                break
            idx = pos + 4
        raw_zip = bytes(raw_zip)

    return raw_zip


def normalize_entry(name: str) -> tuple[str, bool]:
    norm = name[1:] if name.startswith("/") else name
    if (not norm or norm.startswith("/") or "//" in norm
            or any(c in "\\:" or ord(c) < 32 or ord(c) == 127 for c in norm)
            or any(c in (".", "..") for c in norm.split("/"))):
        raise ValueError("Unsafe save archive entry")
    norm = "".join("_" if c in INVALID_CHARS else c for c in norm)
    is_dir = norm.endswith("/")
    if is_dir:
        norm = norm[:-1]
    return norm, is_dir


def is_metadata(name: str, is_dir: bool) -> bool:
    return not is_dir and (name == ".nx_save_meta.bin"
                           or name.lower() in (".dbi_save_info.ini", ".dbi_save_extra"))


def get_parent_directories(path: str) -> list[str]:
    parents = []
    last_slash = path.rfind("/")
    while last_slash > 0:
        parent = path[:last_slash]
        parents.append(parent)
        last_slash = parent.rfind("/")
    return parents


def simulate_preflight(zip_bytes: bytes, filter_meta: bool = True) -> tuple[dict[str, int], set[str], int]:
    """Simulates TransferUnzipPreflight populating UnzipPayloadInventory."""
    buf = io.BytesIO(zip_bytes)
    zf = zipfile.ZipFile(buf, "r")

    inventory_files = {}
    inventory_dirs = set()
    total_bytes = 0

    explicit_dirs = set()

    for zinfo in zf.infolist():
        raw_name = zinfo.orig_filename
        norm_name, is_dir = normalize_entry(raw_name)

        if not norm_name:
            continue

        # Every entry, including directory payloads and metadata, is CRC-drained.
        data = zf.read(zinfo)
        if len(data) != zinfo.file_size or zlib.crc32(data) != zinfo.CRC:
            raise ValueError("Size/CRC mismatch")
        if filter_meta and is_metadata(norm_name, is_dir):
            # Drained & verified CRC, but skipped from inventory
            data = zf.read(zinfo)
            if zlib.crc32(data) != zinfo.CRC:
                raise ValueError("CRC error in meta")
            continue

        canonical_path = "/" + norm_name

        if is_dir:
            if canonical_path in explicit_dirs:
                raise FileExistsError(f"Duplicate explicit dir: {canonical_path}")
            if canonical_path in inventory_files:
                raise FileExistsError(f"Dir conflicts with file: {canonical_path}")
            for p in get_parent_directories(canonical_path):
                if p in inventory_files:
                    raise FileExistsError(f"Parent of dir conflicts with file: {p}")
                inventory_dirs.add(p)
            explicit_dirs.add(canonical_path)
            inventory_dirs.add(canonical_path)
        else:
            if canonical_path in inventory_files:
                raise FileExistsError(f"Duplicate kept file: {canonical_path}")
            if canonical_path in inventory_dirs:
                raise FileExistsError(f"File conflicts with dir: {canonical_path}")
            for p in get_parent_directories(canonical_path):
                if p in inventory_files:
                    raise FileExistsError(f"Parent of file conflicts with file: {p}")
                inventory_dirs.add(p)

            # Test CRC decompression
            data = zf.read(zinfo)
            if len(data) != zinfo.file_size:
                raise ValueError("Size mismatch")
            if zlib.crc32(data) != zinfo.CRC:
                raise ValueError("CRC mismatch")

            inventory_files[canonical_path] = zinfo.file_size
            total_bytes += zinfo.file_size

    return inventory_files, inventory_dirs, total_bytes


def simulate_verify_against_native(zip_bytes: bytes, native_fs: SyntheticSaveFs,
                                   expected_files: dict[str, int], expected_dirs: set[str],
                                   filter_meta: bool = True, simulate_short_reads: bool = True) -> bool:
    """Simulates VerifyArchiveAgainstNative stream comparison and bijection check."""
    observed_files, observed_dirs, _ = simulate_preflight(zip_bytes, filter_meta)
    if observed_files != expected_files or observed_dirs != expected_dirs:
        return False
    # 1. Native enumeration
    cols = native_fs.get_collections()
    native_files = {}
    native_dirs = set()

    for col in cols:
        p = col["path"]
        if p != "/":
            native_dirs.add(p)
            for parent in get_parent_directories(p):
                native_dirs.add(parent)
        for d in col["dirs"]:
            dp = p.rstrip("/") + "/" + d
            native_dirs.add(dp)
            for parent in get_parent_directories(dp):
                native_dirs.add(parent)
        for fname, fsize in col["files"]:
            fp = p.rstrip("/") + "/" + fname
            native_files[fp] = fsize
            for parent in get_parent_directories(fp):
                native_dirs.add(parent)

    # 2. Bijection check
    if native_files != expected_files:
        return False
    if native_dirs != expected_dirs:
        return False

    # 3. Stream compare in 32 KiB chunks
    buf = io.BytesIO(zip_bytes)
    zf = zipfile.ZipFile(buf, "r")

    verified_files = set()

    for zinfo in zf.infolist():
        norm_name, is_dir = normalize_entry(zinfo.orig_filename)
        if not norm_name:
            continue
        if filter_meta and is_metadata(norm_name, is_dir):
            data = zf.read(zinfo)
            if zlib.crc32(data) != zinfo.CRC:
                return False
            continue

        canonical_path = "/" + norm_name
        if is_dir:
            if canonical_path not in expected_dirs:
                return False
        else:
            if canonical_path not in expected_files:
                return False
            if canonical_path not in native_fs.files:
                return False

            native_bytes = native_fs.files[canonical_path]
            if len(native_bytes) != zinfo.file_size:
                return False

            # Simulate 32 KiB chunked read with short-read accumulation
            zstream = zf.open(zinfo)
            chunk_size = 32768
            offset = 0
            file_len = len(native_bytes)

            while offset < file_len:
                to_read = min(chunk_size, file_len - offset)
                if simulate_short_reads and to_read > 1024:
                    # Accumulate in smaller subchunks
                    accum = bytearray()
                    while len(accum) < to_read:
                        sub = zstream.read(min(1024, to_read - len(accum)))
                        if not sub:
                            return False
                        accum.extend(sub)
                    zchunk = bytes(accum)
                else:
                    zchunk = zstream.read(to_read)
                if len(zchunk) != to_read:
                    return False

                fchunk = native_bytes[offset:offset+to_read]
                if zchunk != fchunk:
                    return False
                offset += to_read

            # EOF check on zip stream
            extra = zstream.read(1)
            if extra:
                return False

            verified_files.add(canonical_path)

    if len(verified_files) != len(expected_files):
        return False

    # 4. Post-check re-enumeration
    post_cols = native_fs.get_collections()
    post_files = {}
    for col in post_cols:
        p = col["path"]
        for fname, fsize in col["files"]:
            post_files[p.rstrip("/") + "/" + fname] = fsize
    if post_files != native_files:
        return False

    return True
