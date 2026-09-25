#include "utils/devoptab_mtp_internal.hpp"

namespace sphaira::devoptab::mtp {

Mutex g_mount_mutex{};
std::vector<MountRecord> g_mounts{};

namespace {

// -------------------------------------------------------------------------
// path helpers
// -------------------------------------------------------------------------

// "mtp0:/Android/data/" -> "Android/data", "mtp0:/" -> "".
std::string NormalisePath(const char* path) {
    if (!path) {
        return {};
    }

    std::string out = path;
    if (const auto colon = out.find(':'); colon != std::string::npos) {
        out.erase(0, colon + 1);
    }

    // collapse repeated slashes and drop "." components.
    std::string cleaned;
    cleaned.reserve(out.size());
    for (size_t i = 0; i < out.size();) {
        if (out[i] == '/') {
            i++;
            continue;
        }

        const auto end = out.find('/', i);
        const auto component = out.substr(i, end == std::string::npos ? std::string::npos : end - i);
        if (component != ".") {
            if (!cleaned.empty()) {
                cleaned += '/';
            }
            cleaned += component;
        }

        if (end == std::string::npos) {
            break;
        }
        i = end + 1;
    }

    return cleaned;
}

void SplitPath(const std::string& path, std::string* parent, std::string* name) {
    const auto slash = path.rfind('/');
    if (slash == std::string::npos) {
        parent->clear();
        *name = path;
    } else {
        *parent = path.substr(0, slash);
        *name = path.substr(slash + 1);
    }
}

} // namespace

// -------------------------------------------------------------------------
// MtpMountDevice
// -------------------------------------------------------------------------

MtpMountDevice::MtpMountDevice(const common::MountConfig& config, u32 storage_id, u64 capacity, u64 free_space)
: MountDevice{config}
, m_storage_id{storage_id}
, m_capacity{capacity}
, m_free_space{free_space} {
}

void MtpMountDevice::Rebind(u32 storage_id, u64 capacity, u64 free_space) {
    SCOPED_MUTEX(&g_mutex);
    m_storage_id = storage_id;
    m_capacity = capacity;
    m_free_space = free_space;
    m_generation = g_session.generation;
    DropCaches();
}

void MtpMountDevice::DropCaches() {
    m_dir_cache.clear();
    m_obj_cache.clear();
}

bool MtpMountDevice::Mount() {
    return true;
}

void MtpMountDevice::SyncGenerationLocked() {
    if (m_generation != g_session.generation) {
        DropCaches();
        m_generation = g_session.generation;
    }
}

bool MtpMountDevice::RefreshFileLocked(MtpFileHandle* file) {
    SyncGenerationLocked();
    if (file->generation == m_generation) {
        return true;
    }

    // the session reconnected since this handle was resolved. MTP object
    // handles are per session, so look the path up again on the new one.
    MtpObject obj{};
    if (!LookupLocked(file->path, &obj) || obj.is_dir) {
        return false;
    }

    file->object_handle = obj.handle;
    file->generation = m_generation;
    return true;
}

bool MtpMountDevice::Lookup(const std::string& path, MtpObject* out) {
    SCOPED_MUTEX(&g_mutex);
    SyncGenerationLocked();
    return LookupLocked(path, out);
}

bool MtpMountDevice::List(const std::string& path, std::vector<MtpObject>* out) {
    SCOPED_MUTEX(&g_mutex);
    SyncGenerationLocked();
    return ListLocked(path, out);
}

// EIO tells the browser the phone went away, ENOENT that it simply has no such
// file. Reporting the latter for a dead link is what made a dropped cable look
// like an empty device.
int MtpMountDevice::LookupErrno() const {
    SCOPED_MUTEX(&g_mutex);
    return g_session.connected ? ENOENT : EIO;
}

bool MtpMountDevice::LookupLocked(const std::string& path, MtpObject* out) {
    if (path.empty()) {
        *out = MtpObject{.handle = HANDLE_ROOT, .is_dir = true};
        return true;
    }

    if (const auto it = m_obj_cache.find(path); it != m_obj_cache.end()) {
        *out = it->second;
        return true;
    }

    // Listing the parent caches every sibling, so the entry we want is present
    // afterwards unless it genuinely does not exist.
    std::string parent, name;
    SplitPath(path, &parent, &name);
    if (!ListLocked(parent, nullptr)) {
        return false;
    }

    const auto it = m_obj_cache.find(path);
    if (it == m_obj_cache.end()) {
        return false;
    }

    *out = it->second;
    return true;
}

bool MtpMountDevice::ListLocked(const std::string& path, std::vector<MtpObject>* out) {
    if (const auto it = m_dir_cache.find(path); it != m_dir_cache.end()) {
        if (out) {
            *out = it->second;
        }
        return true;
    }

    MtpObject dir{};
    if (!LookupLocked(path, &dir) || !dir.is_dir) {
        return false;
    }

    if (!EnsureSessionLocked()) {
        return false;
    }

    const u32 handle_params[]{m_storage_id, 0, dir.handle};
    std::vector<u8> data;
    if (R_FAILED(TransactData(OP_GET_OBJECT_HANDLES, handle_params, &data))) {
        // One shot at bringing a dropped link back: phones renegotiate USB on
        // screen lock, which kills the session mid-browse. After a real
        // re-enumeration dir.handle may be stale, in which case this fails
        // again and the next visit re-resolves from the (dropped) caches.
        if (!EnsureSessionLocked() ||
            R_FAILED(TransactData(OP_GET_OBJECT_HANDLES, handle_params, &data))) {
            return false;
        }
    }

    std::vector<u32> handles;
    Reader r{data};
    if (!r.ReadArray(&handles) || !r.Ok()) {
        log_write("[MTP_HOST] malformed GetObjectHandles reply for '%s'\n", path.c_str());
        return false;
    }

    std::vector<MtpObject> entries;
    entries.reserve(handles.size());

    for (const auto handle : handles) {
        const u32 info_params[]{handle};
        std::vector<u8> info;
        if (R_FAILED(TransactData(OP_GET_OBJECT_INFO, info_params, &info))) {
            // The link is gone; a partial listing would look like a phone that
            // lost half its files, so give up on the whole directory.
            if (!g_session.connected) {
                return false;
            }
            continue;
        }

        MtpObject obj{};
        if (!ParseObjectInfo(info, handle, &obj)) {
            continue;
        }

        ResolveLargeSize(&obj);
        entries.push_back(std::move(obj));
    }

    // cap the cache: a deep browse should not grow it without bound.
    if (m_dir_cache.size() >= 64) {
        DropCaches();
    }

    for (const auto& e : entries) {
        m_obj_cache[path.empty() ? e.filename : path + "/" + e.filename] = e;
    }
    const auto& cached = (m_dir_cache[path] = std::move(entries));

    log_write("[MTP_HOST] listed '%s': %zu entries\n", path.c_str(), cached.size());

    if (out) {
        *out = cached;
    }
    return true;
}

int MtpMountDevice::devoptab_diropen(void* fd, const char *path) {
    // devoptab hands us calloc'd storage, so the vector member needs its
    // constructor run before anything touches it.
    auto* dir = new (fd) MtpDirHandle();

    if (!List(NormalisePath(path), &dir->entries)) {
        // devoptab_common frees this allocation without routing back through
        // dirclose, so the vector has to be destroyed here or it leaks.
        const auto err = LookupErrno();
        dir->~MtpDirHandle();
        return -err;
    }

    dir->index = 0;
    return 0;
}

int MtpMountDevice::devoptab_dirreset(void* fd) {
    static_cast<MtpDirHandle*>(fd)->index = 0;
    return 0;
}

int MtpMountDevice::devoptab_dirnext(void* fd, char *filename, struct stat *filestat) {
    auto* dir = static_cast<MtpDirHandle*>(fd);
    if (dir->index >= dir->entries.size()) {
        return -ENOENT;
    }

    const auto& entry = dir->entries[dir->index++];
    std::snprintf(filename, NAME_MAX + 1, "%s", entry.filename.c_str());

    if (filestat) {
        // newlib turns st_mode into dirent::d_type, so getting this right is
        // what keeps the browser from dropping every row as DT_UNKNOWN.
        std::memset(filestat, 0, sizeof(*filestat));
        filestat->st_nlink = 1;
        filestat->st_size = entry.size;
        filestat->st_blksize = STDIO_BLOCK_SIZE;
        filestat->st_mode = entry.is_dir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    }

    return 0;
}

int MtpMountDevice::devoptab_dirclose(void* fd) {
    static_cast<MtpDirHandle*>(fd)->~MtpDirHandle();
    return 0;
}

int MtpMountDevice::devoptab_lstat(const char *path, struct stat *st) {
    MtpObject obj{};
    if (!Lookup(NormalisePath(path), &obj)) {
        return -LookupErrno();
    }

    std::memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_blksize = STDIO_BLOCK_SIZE;
    if (obj.is_dir) {
        st->st_mode = S_IFDIR | 0555;
    } else {
        st->st_mode = S_IFREG | 0444;
        st->st_size = obj.size;
    }

    return 0;
}

int MtpMountDevice::devoptab_open(void *fileStruct, const char *path, int flags, int mode) {
    SCOPED_MUTEX(&g_mutex);
    SyncGenerationLocked();

    const auto norm = NormalisePath(path);
    MtpObject obj{};
    if (!LookupLocked(norm, &obj)) {
        // g_mutex is held, so LookupErrno() (which takes it) would deadlock.
        return g_session.connected ? -ENOENT : -EIO;
    }

    if (obj.is_dir) {
        return -EISDIR;
    }

    // calloc'd storage: run the constructor only on the success path, the
    // common wrapper frees the allocation without calling close on failure.
    auto* file = new (fileStruct) MtpFileHandle();
    file->object_handle = obj.handle;
    file->size = obj.size;
    file->generation = m_generation;
    file->path = norm;
    return 0;
}

int MtpMountDevice::devoptab_close(void *fd) {
    static_cast<MtpFileHandle*>(fd)->~MtpFileHandle();
    return 0;
}

int MtpMountDevice::devoptab_fstat(void *fd, struct stat *st) {
    const auto* file = static_cast<MtpFileHandle*>(fd);
    std::memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_mode = S_IFREG | 0444;
    st->st_size = file->size;
    st->st_blksize = STDIO_BLOCK_SIZE;
    return 0;
}

ssize_t MtpMountDevice::devoptab_seek(void *fd, off_t pos, int dir) {
    auto* file = static_cast<MtpFileHandle*>(fd);

    s64 target{};
    switch (dir) {
        case SEEK_SET: target = pos; break;
        case SEEK_CUR: target = static_cast<s64>(file->offset) + pos; break;
        case SEEK_END: target = static_cast<s64>(file->size) + pos; break;
        default: return -EINVAL;
    }

    if (target < 0) {
        return -EINVAL;
    }

    file->offset = target;
    return static_cast<ssize_t>(file->offset);
}

ssize_t MtpMountDevice::devoptab_read(void *fd, char *ptr, size_t len) {
    auto* file = static_cast<MtpFileHandle*>(fd);
    if (!file->object_handle) {
        return -EBADF;
    }

    SCOPED_MUTEX(&g_mutex);
    if (!EnsureSessionLocked() || !RefreshFileLocked(file)) {
        return -EIO;
    }

    if (file->offset >= file->size) {
        return 0;
    }

    const u64 want = std::min<u64>(len, file->size - file->offset);

    if (!g_session.has_partial64 && !g_session.has_partial) {
        // No partial read support at all. Whole-object reads are only viable
        // for something that fits in the transfer buffer; anything else has
        // no sane access path.
        if (!file->offset && file->size <= MAX_READ_CHUNK) {
            DataSink sink{ptr, want};
            const u32 params[]{file->object_handle};
            if (R_FAILED(Transact(OP_GET_OBJECT, params, &sink, nullptr))) {
                return -EIO;
            }
            const auto got = std::min<u64>(sink.Written(), want);
            file->offset += got;
            return static_cast<ssize_t>(got);
        }
        log_write("[MTP_HOST] device cannot serve partial reads for a %llu byte object\n",
            static_cast<unsigned long long>(file->size));
        return -ENOTSUP;
    }

    u64 done = 0;
    int retries = 0;

    // Callers up the stack (yati's nsp/nca parsers) read headers at exact
    // offsets and cannot cope with a short read, so serve the full request.
    while (done < want) {
        const u64 offset = file->offset + done;
        Result rc{};

        // line the stream up with this read: reuse it when it sits at (or
        // shortly before) the wanted offset, retire it otherwise.
        if (g_stream.active) {
            const u64 avail = g_stream.remaining + g_stream.carry_len;
            const bool usable = g_stream.handle == file->object_handle &&
                offset >= g_stream.next_offset &&
                offset - g_stream.next_offset <= std::min<u64>(avail, STREAM_DRAIN_LIMIT);

            if (!usable) {
                AbortStreamLocked();
            } else if (offset > g_stream.next_offset) {
                u64 skipped{};
                rc = PullStreamLocked(nullptr, offset - g_stream.next_offset, &skipped);
            }
        }

        if (R_SUCCEEDED(rc) && (!g_stream.active || g_stream.next_offset != offset)) {
            AbortStreamLocked();
            rc = StartStreamLocked(file->object_handle, offset, file->size, want - done);
        }

        u64 got{};
        if (R_SUCCEEDED(rc)) {
            rc = PullStreamLocked(reinterpret_cast<u8*>(ptr) + done, want - done, &got);
        }

        if (R_FAILED(rc)) {
            // a few reconnect attempts with growing breathers, otherwise a
            // phone that blinked mid-transfer aborts the whole install. The
            // loop covers EnsureSession too: one failed reconnect used to
            // bail out immediately, even though the phone often just needs
            // another moment to finish enumerating (the 16:43 log died on
            // exactly that). Kept short overall because this runs with the
            // mount's lock held -- the file browser behind the install box
            // blocks on it, which is the ui freeze the user sees.
            // RefreshFileLocked re-resolves the handle after a reconnect.
            bool revived = false;
            while (retries < 3) {
                retries++;
                svcSleepThread(retries * 150'000'000ULL);
                if (EnsureSessionLocked() && RefreshFileLocked(file)) {
                    revived = true;
                    break;
                }
            }
            if (revived) {
                continue;
            }
            file->offset += done;
            return done ? static_cast<ssize_t>(done) : -EIO;
        }

        if (!got) {
            break; // device granted less than requested and the stream ended
        }
        done += got;
    }

    file->offset += done;
    return static_cast<ssize_t>(done);
}

int MtpMountDevice::devoptab_statvfs(const char *_path, struct statvfs *buf) {
    std::memset(buf, 0, sizeof(*buf));
    buf->f_bsize = 512;
    buf->f_frsize = 512;
    buf->f_blocks = m_capacity / 512;
    buf->f_bfree = m_free_space / 512;
    buf->f_bavail = m_free_space / 512;
    return 0;
}


} // namespace sphaira::devoptab::mtp
