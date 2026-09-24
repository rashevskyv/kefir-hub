# Synthetic FS proxy models for MTP save contract.
import os
import re
import io
import copy
from contract_fixtures.mtp_save_models import (
    SyntheticSaveInfo, build_mtp_save_tree, allocate_save_names
)

FS_SUCCESS = 0
FS_ERROR_NOT_IMPLEMENTED = 0x202
FS_ERROR_PATH_NOT_FOUND = 0x203
FS_ERROR_MOUNT_FAILED = 0x1234

FS_ENTRY_TYPE_FILE = 1
FS_ENTRY_TYPE_DIR = 2

FS_OPEN_READ = 1
FS_OPEN_WRITE = 2
FS_OPEN_APPEND = 4

class SyntheticNativeFs:
    def __init__(self, key, total_space=100*1024*1024, free_space=50*1024*1024):
        self.key = key
        self.total_space = total_space
        self.free_space = free_space
        self.files = {
            "/slot0/savedata.bin": bytearray(b"original_save_data_bytes_12345"),
            "/slot0/metadata.json": bytearray(b'{"version": 1}'),
        }
        self.dirs = {"/", "/slot0"}
        self.side_effects = 0
        self.commits = 0

    def create_file(self, path, size):
        self.side_effects += 1
        self.files[path] = bytearray(size)

    def write(self, path, off, buf):
        self.side_effects += 1
        f = self.files[path]
        if off + len(buf) > len(f):
            f.extend(b'\0' * (off + len(buf) - len(f)))
        f[off:off+len(buf)] = buf

    def set_size(self, path, size):
        self.side_effects += 1
        f = self.files[path]
        if size < len(f):
            self.files[path] = f[:size]
        else:
            f.extend(b'\0' * (size - len(f)))

    def delete_file(self, path):
        self.side_effects += 1
        self.files.pop(path, None)

    def rename_file(self, old_path, new_path):
        self.side_effects += 1
        if old_path in self.files:
            self.files[new_path] = self.files.pop(old_path)

    def create_directory(self, path):
        self.side_effects += 1
        self.dirs.add(path)

    def delete_directory_recursively(self, path):
        self.side_effects += 1
        self.dirs = {d for d in self.dirs if not d.startswith(path)}
        self.files = {f: v for f, v in self.files.items() if not f.startswith(path)}

    def rename_directory(self, old_path, new_path):
        self.side_effects += 1
        if old_path in self.dirs:
            self.dirs.remove(old_path)
            self.dirs.add(new_path)

    def commit(self):
        self.commits += 1

class SyntheticFsSaveProxy:
    """
    Connected behavioral model of FsSaveProxy enforcing the read-only contract.
    Exercises mutation rejection before parsing/mounting, read-only OpenFile,
    directory enumeration, non-committing CloseFile, LRU cache eviction,
    exact retained field routing, and mount error propagation.
    """
    MOUNT_CACHE_MAX = 4

    def __init__(self, tree, mount_errors=None, remap_candidates=None):
        # tree: top_level_dir -> {sub_dir -> SyntheticSaveInfo}
        self.tree = tree
        self.mount_errors = mount_errors or {}  # key -> int error code
        self.remap_candidates = remap_candidates or {}
        self.remap_attempts = []
        self.remap_candidate_uses = 0
        self.mount_count = 0
        self.mount_tick = 0
        self.mounts = {}  # key -> {"fs": SyntheticNativeFs, "tick": int}
        # attempted_routes records every route attempt (key, space_id, type, app_id, sys_id, uid, rank, index, save_id, read_only)
        # including attempts that subsequently fail due to backend mount_errors.
        self.attempted_routes = []
        self.routed_tuples = self.attempted_routes  # alias for backwards compatibility

    def parse(self, path):
        parts = [p for p in path.split("/") if p]
        if not parts:
            return {"depth": 0, "game": "", "type": "", "rest": "/"}
        elif len(parts) == 1:
            return {"depth": 1, "game": parts[0], "type": "", "rest": "/"}
        elif len(parts) == 2:
            return {"depth": 2, "game": parts[0], "type": parts[1], "rest": "/"}
        else:
            return {"depth": 3, "game": parts[0], "type": parts[1], "rest": "/" + "/".join(parts[2:])}

    def mount_save(self, pp):
        game_entry = None
        game_canonical = None
        for g_name, g_map in self.tree.items():
            if g_name.lower() == pp["game"].lower():
                game_entry = g_map
                game_canonical = g_name
                break
        if game_entry is None:
            return FS_ERROR_PATH_NOT_FOUND, None

        type_info = None
        type_canonical = None
        for t_name, t_info in game_entry.items():
            if t_name.lower() == pp["type"].lower():
                type_info = t_info
                type_canonical = t_name
                break
        if type_info is None:
            return FS_ERROR_PATH_NOT_FOUND, None

        key = game_canonical + "/" + type_canonical

        # Check LRU cache: if already mounted, bump tick and return existing native fs
        # without recording a new mount attempt.
        self.mount_tick += 1
        if key in self.mounts:
            self.mounts[key]["tick"] = self.mount_tick
            return FS_SUCCESS, self.mounts[key]["fs"]

        # Record exact attempted route tuple BEFORE checking backend open result / mount error.
        # This models FsNativeSave construction with immutable stored record fields.
        route_tuple = (
            key,
            type_info.save_data_space_id,
            type_info.save_data_type,
            type_info.application_id,
            type_info.system_save_data_id,
            type_info.uid,
            type_info.save_data_rank,
            type_info.save_data_index,
            type_info.save_data_id,
            True,  # read_only = true
        )
        self.attempted_routes.append(route_tuple)

        # If backend mount fails, return the injected mount error immediately without caching.
        if key in self.mount_errors:
            return self.mount_errors[key], None

        fs = SyntheticNativeFs(key)
        self.mount_count += 1

        if len(self.mounts) >= self.MOUNT_CACHE_MAX:
            lru_key = min(self.mounts.keys(), key=lambda k: self.mounts[k]["tick"])
            del self.mounts[lru_key]

        self.mounts[key] = {"fs": fs, "tick": self.mount_tick}
        return FS_SUCCESS, fs

    # Every mutation callback rejects before Parse, MountSave, or filesystem side effects
    def create_file(self, path, size, option=0):
        return FS_ERROR_NOT_IMPLEMENTED

    def delete_file(self, path):
        return FS_ERROR_NOT_IMPLEMENTED

    def rename_file(self, old_path, new_path):
        return FS_ERROR_NOT_IMPLEMENTED

    def create_directory(self, path):
        return FS_ERROR_NOT_IMPLEMENTED

    def delete_directory_recursively(self, path):
        return FS_ERROR_NOT_IMPLEMENTED

    def rename_directory(self, old_path, new_path):
        return FS_ERROR_NOT_IMPLEMENTED

    def set_file_size(self, file_handle, size):
        return FS_ERROR_NOT_IMPLEMENTED

    def write_file(self, file_handle, off, buf, option=0):
        return FS_ERROR_NOT_IMPLEMENTED

    def open_file(self, path, mode):
        # Fail-closed rejection before Parse or MountSave
        if mode & (FS_OPEN_WRITE | FS_OPEN_APPEND):
            return FS_ERROR_NOT_IMPLEMENTED, None

        pp = self.parse(path)
        if pp["depth"] < 3:
            return FS_ERROR_PATH_NOT_FOUND, None

        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, None

        if pp["rest"] not in fs.files:
            return FS_ERROR_PATH_NOT_FOUND, None

        handle = {"fs": fs, "path": pp["rest"], "closed": False}
        return FS_SUCCESS, handle

    def get_file_size(self, file_handle):
        return len(file_handle["fs"].files[file_handle["path"]])

    def read_file(self, file_handle, off, size):
        f = file_handle["fs"].files[file_handle["path"]]
        return bytes(f[off:off+size])

    def close_file(self, file_handle):
        file_handle["closed"] = True
        # Read-only CloseFile never calls commit!

    def open_directory(self, path, mode=3):
        pp = self.parse(path)
        if pp["depth"] < 2:
            if pp["depth"] == 1:
                game_entry = None
                for g_name, g_map in self.tree.items():
                    if g_name.lower() == pp["game"].lower():
                        game_entry = g_map
                        break
                if game_entry is None:
                    return FS_ERROR_PATH_NOT_FOUND, None
                return FS_SUCCESS, {"is_virtual": True, "entries": list(game_entry.keys())}
            else:
                return FS_SUCCESS, {"is_virtual": True, "entries": list(self.tree.keys())}
        else:
            rc, fs = self.mount_save(pp)
            if rc != FS_SUCCESS:
                return rc, None
            prefix = pp["rest"]
            if not prefix.endswith("/"):
                prefix += "/"
            ents = []
            for d in fs.dirs:
                if d != "/" and d.startswith(prefix):
                    rel = d[len(prefix):].split("/")[0]
                    if rel and rel not in ents:
                        ents.append(rel)
            for f in fs.files:
                if f.startswith(prefix):
                    rel = f[len(prefix):].split("/")[0]
                    if rel and rel not in ents:
                        ents.append(rel)
            return FS_SUCCESS, {"is_virtual": False, "entries": ents}

    def get_total_space(self, path):
        pp = self.parse(path)
        if pp["depth"] < 2:
            return FS_SUCCESS, 0
        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, 0
        return FS_SUCCESS, fs.total_space

    def get_free_space(self, path):
        pp = self.parse(path)
        if pp["depth"] < 2:
            return FS_SUCCESS, 0
        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, 0
        return FS_SUCCESS, fs.free_space

    def get_entry_type(self, path):
        pp = self.parse(path)
        if pp["depth"] < 2:
            return FS_SUCCESS, FS_ENTRY_TYPE_DIR
        rc, fs = self.mount_save(pp)
        if rc != FS_SUCCESS:
            return rc, None
        if pp["rest"] in fs.dirs:
            return FS_SUCCESS, FS_ENTRY_TYPE_DIR
        elif pp["rest"] in fs.files:
            return FS_SUCCESS, FS_ENTRY_TYPE_FILE
        else:
            return FS_ERROR_PATH_NOT_FOUND, None
