#include "utils/devoptab_common.hpp"
#include "utils/devoptab_buffered.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include "utils/devoptab_curl_device.hpp"
#include "utils/thread.hpp"

#include "defines.hpp"
#include "log.hpp"
#include "download.hpp"

#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <minIni.h>
#include <curl/curl.h>

// see FixDkpBug();
extern "C" {
    extern const devoptab_t dotab_stdnull;
}

namespace sphaira::devoptab::common {
namespace {

RwLock g_rwlock{};

void EnsureRwLockInitialized() {
    static Mutex init_mutex{};
    static bool initialized{};
    SCOPED_MUTEX(&init_mutex);
    if (!initialized) {
        rwlockInit(&g_rwlock);
        initialized = true;
    }
}

// curl_url_strerror doesn't exist in the switch version of libcurl as its so old.
// todo: update libcurl and send patches to dkp.
[[maybe_unused]] const char* curl_url_strerror_wrap(CURLUcode code) {
    switch (code) {
        case CURLUE_OK: return "No error";
        case CURLUE_BAD_HANDLE: return "Invalid handle";
        case CURLUE_BAD_PARTPOINTER: return "Invalid pointer to a part of the URL";
        case CURLUE_MALFORMED_INPUT: return "Malformed input";
        case CURLUE_BAD_PORT_NUMBER: return "Invalid port number";
        case CURLUE_UNSUPPORTED_SCHEME: return "Unsupported scheme";
        case CURLUE_URLDECODE: return "Failed to decode URL component";
        case CURLUE_OUT_OF_MEMORY: return "Out of memory";
        case CURLUE_USER_NOT_ALLOWED: return "User not allowed in URL";
        case CURLUE_UNKNOWN_PART: return "Unknown URL part";
        case CURLUE_NO_SCHEME: return "No scheme found in URL";
        case CURLUE_NO_USER: return "No user found in URL";
        case CURLUE_NO_PASSWORD: return "No password found in URL";
        case CURLUE_NO_OPTIONS: return "No options found in URL";
        case CURLUE_NO_HOST: return "No host found in URL";
        case CURLUE_NO_PORT: return "No port number found in URL";
        case CURLUE_NO_QUERY: return "No query found in URL";
        case CURLUE_NO_FRAGMENT: return "No fragment found in URL";
        default: return "Unknown error code";
    }
}

struct Device {
    std::unique_ptr<MountDevice> mount_device;
    size_t file_size;
    size_t dir_size;

    MountConfig config{};
    Mutex mutex{};
};

struct File {
    Device* device;
    void* fd;
};

struct Dir {
    Device* device;
    void* fd;
};

int set_errno(struct _reent *r, int err) {
    r->_errno = err;
    return -1;
}

int devoptab_open(struct _reent *r, void *fileStruct, const char *_path, int flags, int mode) {
    auto device = static_cast<Device*>(r->deviceData);
    auto file = static_cast<File*>(fileStruct);
    std::memset(file, 0, sizeof(*file));
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    log_write("[FILE] open %s (flags: 0x%x)\n", _path, flags);

    if (device->config.read_only && (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND))) {
        log_write("[FILE] open failed: read-only\n");
        return set_errno(r, EROFS);
    }

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        log_write("[FILE] open failed: invalid path\n");
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        log_write("[FILE] open failed: mount error\n");
        return set_errno(r, EIO);
    }

    file->fd = calloc(1, device->file_size);
    if (!file->fd) {
        log_write("[FILE] open failed: out of memory\n");
        return set_errno(r, ENOMEM);
    }

    const auto ret = device->mount_device->devoptab_open(file->fd, path, flags, mode);
    if (ret) {
        free(file->fd);
        file->fd = nullptr;
        log_write("[FILE] open failed: %d\n", -ret);
        return set_errno(r, -ret);
    }

    log_write("[FILE] open success: %s\n", _path);
    file->device = device;
    return r->_errno = 0;
}

int devoptab_close(struct _reent *r, void *fd) {
    auto file = static_cast<File*>(fd);
    // keep a local copy: the memset below wipes file->device, and the scoped
    // unlock re-evaluates the expression on scope exit.
    auto device = file->device;
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    log_write("[FILE] close\n");
    if (file->fd) {
        device->mount_device->devoptab_close(file->fd);
        free(file->fd);
    }

    std::memset(file, 0, sizeof(*file));
    return r->_errno = 0;
}

ssize_t devoptab_read(struct _reent *r, void *fd, char *ptr, size_t len) {
    auto file = static_cast<File*>(fd);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&file->device->mutex);

    const auto ret = file->device->mount_device->devoptab_read(file->fd, ptr, len);
    if (ret < 0) {
        log_write("[FILE] read failed: %zd\n", -ret);
        return set_errno(r, -ret);
    }

    if (ret > 0) {
        log_write("[FILE] read %zd bytes\n", ret);
    }
    return ret;
}

ssize_t devoptab_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
    auto file = static_cast<File*>(fd);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&file->device->mutex);

    const auto ret = file->device->mount_device->devoptab_write(file->fd, ptr, len);
    if (ret < 0) {
        log_write("[FILE] write failed: %zd\n", -ret);
        return set_errno(r, -ret);
    }

    if (ret > 0) {
        log_write("[FILE] write %zd bytes\n", ret);
    }
    return ret;
}

off_t devoptab_seek(struct _reent *r, void *fd, off_t pos, int dir) {
    auto file = static_cast<File*>(fd);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&file->device->mutex);

    log_write("[FILE] seek pos: %lld dir: %d\n", static_cast<long long>(pos), dir);
    const auto ret = file->device->mount_device->devoptab_seek(file->fd, pos, dir);
    if (ret < 0) {
        log_write("[FILE] seek failed: %lld\n", static_cast<long long>(-ret));
        set_errno(r, -ret);
        return 0;
    }

    r->_errno = 0;
    return ret;
}

int devoptab_fstat(struct _reent *r, void *fd, struct stat *st) {
    auto file = static_cast<File*>(fd);
    std::memset(st, 0, sizeof(*st));
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&file->device->mutex);

    const auto ret = file->device->mount_device->devoptab_fstat(file->fd, st);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

int devoptab_unlink(struct _reent *r, const char *_path) {
    auto device = static_cast<Device*>(r->deviceData);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    log_write("[FILE] unlink %s\n", _path);

    if (device->config.read_only) {
        log_write("[FILE] unlink failed: read-only\n");
        return set_errno(r, EROFS);
    }

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        log_write("[FILE] unlink failed: invalid path\n");
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        log_write("[FILE] unlink failed: mount error\n");
        return set_errno(r, EIO);
    }

    const auto ret = device->mount_device->devoptab_unlink(path);
    if (ret) {
        log_write("[FILE] unlink failed: %d\n", -ret);
        return set_errno(r, -ret);
    }

    log_write("[FILE] unlink success: %s\n", _path);
    return r->_errno = 0;
}

int devoptab_rename(struct _reent *r, const char *_oldName, const char *_newName) {
    auto device = static_cast<Device*>(r->deviceData);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    log_write("[FILE] rename %s -> %s\n", _oldName, _newName);

    if (device->config.read_only) {
        log_write("[FILE] rename failed: read-only\n");
        return set_errno(r, EROFS);
    }

    char oldName[PATH_MAX]{};
    if (!device->mount_device->fix_path(_oldName, oldName)) {
        log_write("[FILE] rename failed: invalid old path\n");
        return set_errno(r, ENOENT);
    }

    char newName[PATH_MAX]{};
    if (!device->mount_device->fix_path(_newName, newName)) {
        log_write("[FILE] rename failed: invalid new path\n");
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        log_write("[FILE] rename failed: mount error\n");
        return set_errno(r, EIO);
    }

    const auto ret = device->mount_device->devoptab_rename(oldName, newName);
    if (ret) {
        log_write("[FILE] rename failed: %d\n", -ret);
        return set_errno(r, -ret);
    }

    log_write("[FILE] rename success: %s -> %s\n", _oldName, _newName);
    return r->_errno = 0;
}

int devoptab_mkdir(struct _reent *r, const char *_path, int mode) {
    auto device = static_cast<Device*>(r->deviceData);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    log_write("[FILE] mkdir %s\n", _path);

    if (device->config.read_only) {
        log_write("[FILE] mkdir failed: read-only\n");
        return set_errno(r, EROFS);
    }

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        log_write("[FILE] mkdir failed: invalid path\n");
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        log_write("[FILE] mkdir failed: mount error\n");
        return set_errno(r, EIO);
    }

    const auto ret = device->mount_device->devoptab_mkdir(path, mode);
    if (ret) {
        log_write("[FILE] mkdir failed: %d\n", -ret);
        return set_errno(r, -ret);
    }

    log_write("[FILE] mkdir success: %s\n", _path);
    return r->_errno = 0;
}

int devoptab_rmdir(struct _reent *r, const char *_path) {
    auto device = static_cast<Device*>(r->deviceData);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    log_write("[FILE] rmdir %s\n", _path);

    if (device->config.read_only) {
        log_write("[FILE] rmdir failed: read-only\n");
        return set_errno(r, EROFS);
    }

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        log_write("[FILE] rmdir failed: invalid path\n");
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        log_write("[FILE] rmdir failed: mount error\n");
        return set_errno(r, EIO);
    }

    const auto ret = device->mount_device->devoptab_rmdir(path);
    if (ret) {
        log_write("[FILE] rmdir failed: %d\n", -ret);
        return set_errno(r, -ret);
    }

    log_write("[FILE] rmdir success: %s\n", _path);
    return r->_errno = 0;
}

DIR_ITER* devoptab_diropen(struct _reent *r, DIR_ITER *dirState, const char *_path) {
    auto device = static_cast<Device*>(r->deviceData);
    auto dir = static_cast<Dir*>(dirState->dirStruct);
    std::memset(dir, 0, sizeof(*dir));
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    log_write("[DEVOPTAB] diropen %s\n", _path);

    if (!device->mount_device) {
        log_write("[DEVOPTAB] diropen no mount device\n");
        set_errno(r, ENOENT);
        return nullptr;
    }

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        set_errno(r, ENOENT);
        return nullptr;
    }

    log_write("[DEVOPTAB] diropen fixed path %s\n", path);

    if (!device->mount_device->Mount()) {
        set_errno(r, EIO);
        return nullptr;
    }

    log_write("[DEVOPTAB] diropen mounted\n");

    dir->fd = calloc(1, device->dir_size);
    if (!dir->fd) {
        set_errno(r, ENOMEM);
        return nullptr;
    }

    log_write("[DEVOPTAB] diropen allocated dir\n");

    const auto ret = device->mount_device->devoptab_diropen(dir->fd, path);
    if (ret) {
        free(dir->fd);
        dir->fd = nullptr;
        set_errno(r, -ret);
        return nullptr;
    }

    log_write("[DEVOPTAB] diropen opened dir\n");

    dir->device = device;
    return dirState;
}

int devoptab_dirreset(struct _reent *r, DIR_ITER *dirState) {
    auto dir = static_cast<Dir*>(dirState->dirStruct);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&dir->device->mutex);

    const auto ret = dir->device->mount_device->devoptab_dirreset(dir->fd);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

int devoptab_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat) {
    auto dir = static_cast<Dir*>(dirState->dirStruct);
    std::memset(filestat, 0, sizeof(*filestat));
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&dir->device->mutex);

    const auto ret = dir->device->mount_device->devoptab_dirnext(dir->fd, filename, filestat);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

int devoptab_dirclose(struct _reent *r, DIR_ITER *dirState) {
    auto dir = static_cast<Dir*>(dirState->dirStruct);
    // keep a local copy: the memset below wipes dir->device, and the scoped
    // unlock re-evaluates the expression on scope exit.
    auto device = dir->device;
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    if (dir->fd) {
        device->mount_device->devoptab_dirclose(dir->fd);
        free(dir->fd);
    }

    std::memset(dir, 0, sizeof(*dir));
    return r->_errno = 0;
}

int devoptab_lstat(struct _reent *r, const char *_path, struct stat *st) {
    auto device = static_cast<Device*>(r->deviceData);
    std::memset(st, 0, sizeof(*st));
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    // special case: root of the device.
    const auto dilem = std::strchr(_path, ':');
    if (dilem && (dilem > _path) && (dilem[1] == '\0' || (dilem[1] == '/' && dilem[2] == '\0'))) {
        st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
        st->st_nlink = 1;
        return r->_errno = 0;
    }

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        return set_errno(r, EIO);
    }

    const auto ret = device->mount_device->devoptab_lstat(path, st);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

int devoptab_ftruncate(struct _reent *r, void *fd, off_t len) {
    auto file = static_cast<File*>(fd);
    SCOPED_MUTEX(&file->device->mutex);

    if (!file || !file->fd) {
        return set_errno(r, EBADF);
    }

    if (file->device->config.read_only) {
        return set_errno(r, EROFS);
    }

    const auto ret = file->device->mount_device->devoptab_ftruncate(file->fd, len);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

int devoptab_statvfs(struct _reent *r, const char *_path, struct statvfs *buf) {
    auto device = static_cast<Device*>(r->deviceData);
    std::memset(buf, 0, sizeof(*buf));
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        return set_errno(r, EIO);
    }

    const auto ret = device->mount_device->devoptab_statvfs(path, buf);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

int devoptab_fsync(struct _reent *r, void *fd) {
    auto file = static_cast<File*>(fd);
    SCOPED_MUTEX(&file->device->mutex);

    if (!file || !file->fd) {
        return set_errno(r, EBADF);
    }

    if (file->device->config.read_only) {
        return set_errno(r, EROFS);
    }

    const auto ret = file->device->mount_device->devoptab_fsync(file->fd);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

int devoptab_utimes(struct _reent *r, const char *_path, const struct timeval times[2]) {
    auto device = static_cast<Device*>(r->deviceData);
    SCOPED_RWLOCK(&g_rwlock, false);
    SCOPED_MUTEX(&device->mutex);

    if (!times) {
        log_write("[DEVOPTAB] devoptab_utimes() times is null\n");
        return set_errno(r, EINVAL);
    }

    if (device->config.read_only) {
        return set_errno(r, EROFS);
    }

    char path[PATH_MAX]{};
    if (!device->mount_device->fix_path(_path, path)) {
        return set_errno(r, ENOENT);
    }

    if (!device->mount_device->Mount()) {
        return set_errno(r, EIO);
    }

    const auto ret = device->mount_device->devoptab_utimes(path, times);
    if (ret) {
        return set_errno(r, -ret);
    }

    return r->_errno = 0;
}

constexpr devoptab_t DEVOPTAB = {
    .structSize   = sizeof(File),
    .open_r       = devoptab_open,
    .close_r      = devoptab_close,
    .write_r      = devoptab_write,
    .read_r       = devoptab_read,
    .seek_r       = devoptab_seek,
    .fstat_r      = devoptab_fstat,
    .stat_r       = devoptab_lstat,
    .unlink_r     = devoptab_unlink,
    .rename_r     = devoptab_rename,
    .mkdir_r      = devoptab_mkdir,
    .dirStateSize = sizeof(Dir),
    .diropen_r    = devoptab_diropen,
    .dirreset_r   = devoptab_dirreset,
    .dirnext_r    = devoptab_dirnext,
    .dirclose_r   = devoptab_dirclose,
    .statvfs_r    = devoptab_statvfs,
    .ftruncate_r  = devoptab_ftruncate,
    .fsync_r      = devoptab_fsync,
    .rmdir_r      = devoptab_rmdir,
    .lstat_r      = devoptab_lstat,
    .utimes_r     = devoptab_utimes,
};

struct Entry {
    Device device{};
    devoptab_t devoptab{};
    fs::FsPath mount{};
    char name[32]{};
    s32 ref_count{};

    ~Entry() {
        RemoveDevice(mount);
    }
};

std::array<std::unique_ptr<Entry>, 16> g_entries;

} // namespace



bool fix_path(const char* str, char* out, bool strip_leading_slash) {
    str = std::strchr(str, ':');
    if (!str) {
        return false;
    }

    // skip over ':'
    str++;
    size_t len = 0;

    // todo: hanle utf8 paths.
    for (size_t i = 0; str[i]; i++) {
        // skip multiple slashes.
        if (i && str[i] == '/' && str[i - 1] == '/') {
            continue;
        }

        if (!i) {
            // skip leading slash.
            if (strip_leading_slash && str[i] == '/') {
                continue;
            }

            // add leading slash.
            if (!strip_leading_slash && str[i] != '/') {
                out[len++] = '/';
            }
        }

        // save single char.
        out[len++] = str[i];
    }

    // skip trailing slash.
    if (len > 1 && out[len - 1] == '/') {
        out[len - 1] = '\0';
    }

    // null the end.
    out[len] = '\0';

    return true;
}

void update_devoptab_for_read_only(devoptab_t* devoptab, bool read_only) {
    // remove write functions if read_only is set.
    if (read_only) {
        devoptab->write_r = nullptr;
        devoptab->link_r = nullptr;
        devoptab->unlink_r = nullptr;
        devoptab->rename_r = nullptr;
        devoptab->mkdir_r = nullptr;
        devoptab->ftruncate_r = nullptr;
        devoptab->fsync_r = nullptr;
        devoptab->rmdir_r = nullptr;
        devoptab->utimes_r = nullptr;
        devoptab->symlink_r = nullptr;
    }
}

void LoadConfigsFromIni(const fs::FsPath& path, MountConfigs& out_configs) {
    static const auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto e = static_cast<MountConfigs*>(UserData);
        if (!Section || !Key || !Value) {
            return 1;
        }

        // add new entry if use section changed.
        if (e->empty() || std::strcmp(Section, e->back().name.c_str())) {
            e->emplace_back(Section);
        }

        if (!std::strcmp(Key, "url")) {
            e->back().url = Value;
        } else if (!std::strcmp(Key, "user")) {
            e->back().user = Value;
        } else if (!std::strcmp(Key, "pass")) {
            e->back().pass = Value;
        } else if (!std::strcmp(Key, "dump_path")) {
            e->back().dump_path = Value;
        } else if (!std::strcmp(Key, "port")) {
            const auto port = ini_parse_getl(Value, -1);
            if (port < 0 || port > 65535) {
                log_write("[DEVOPTAB] INI: invalid port %s\n", Value);
            } else {
                e->back().port = port;
            }
        } else if (!std::strcmp(Key, "timeout")) {
            e->back().timeout = ini_parse_getl(Value, e->back().timeout);
        } else if (!std::strcmp(Key, "read_only")) {
            e->back().read_only = ini_parse_getbool(Value, e->back().read_only);
        } else if (!std::strcmp(Key, "no_stat_file")) {
            e->back().no_stat_file = ini_parse_getbool(Value, e->back().no_stat_file);
        } else if (!std::strcmp(Key, "no_stat_dir")) {
            e->back().no_stat_dir = ini_parse_getbool(Value, e->back().no_stat_dir);
        } else if (!std::strcmp(Key, "fs_hidden")) {
            e->back().fs_hidden = ini_parse_getbool(Value, e->back().fs_hidden);
        } else if (!std::strcmp(Key, "dump_hidden")) {
            e->back().dump_hidden = ini_parse_getbool(Value, e->back().dump_hidden);
        } else {
            log_write("[DEVOPTAB] INI: extra key %s=%s\n", Key, Value);
            e->back().extra.emplace(Key, Value);
        }

        return 1;
    };

    out_configs.resize(0);
    ini_browse(cb, &out_configs, path);
    log_write("[DEVOPTAB] Found %zu mount configs\n", out_configs.size());
}

bool MountNetworkDevice2(std::unique_ptr<MountDevice>&& device, const MountConfig& config, size_t file_size, size_t dir_size, const char* name, const char* mount_name) {
    EnsureRwLockInitialized();
    SCOPED_RWLOCK(&g_rwlock, true);

    if (!device) {
        log_write("[DEVOPTAB] No device for %s\n", mount_name);
        return false;
    }

    bool already_mounted = false;
    for (const auto& entry : g_entries) {
        if (entry && entry->mount == mount_name) {
            already_mounted = true;
            break;
        }
    }

    if (already_mounted) {
        log_write("[DEVOPTAB] Already mounted %s, skipping\n", mount_name);
        return false;
    }

    // otherwise, find next free entry.
    auto itr = std::ranges::find_if(g_entries, [](auto& e){
        return !e;
    });

    if (itr == g_entries.end()) {
        log_write("[DEVOPTAB] No free entries to mount %s\n", mount_name);
        return false;
    }

    auto entry = std::make_unique<Entry>();
    entry->device.mount_device = std::forward<decltype(device)>(device);
    entry->device.file_size = file_size;
    entry->device.dir_size = dir_size;
    entry->device.config = config;

    if (!entry->device.mount_device) {
        log_write("[DEVOPTAB] Failed to create device for %s\n", config.url.c_str());
        return false;
    }

    entry->devoptab = DEVOPTAB;
    entry->devoptab.name = entry->name;
    entry->devoptab.deviceData = &entry->device;
    std::snprintf(entry->name, sizeof(entry->name), "%s", name);
    std::snprintf(entry->mount, sizeof(entry->mount), "%s", mount_name);
    common::update_devoptab_for_read_only(&entry->devoptab, config.read_only);

    if (AddDevice(&entry->devoptab) < 0) {
        log_write("[DEVOPTAB] Failed to add device %s\n", mount_name);
        return false;
    }

    log_write("[DEVOPTAB] DEVICE SUCCESS %s %s\n", name, mount_name);

    entry->ref_count++;
    *itr = std::move(entry);
    log_write("[DEVOPTAB] Mounted %s at /%s\n", name, mount_name);

    return true;
}

bool MountReadOnlyIndexDevice(const CreateDeviceCallback& create_device, size_t file_size, size_t dir_size, const char* name, fs::FsPath& out_path) {
    static Mutex mutex{};
    static u32 next_index{};
    SCOPED_MUTEX(&mutex);

    MountConfig config{};
    config.read_only = true;
    config.no_stat_dir = false;
    config.no_stat_file = false;
    config.fs_hidden = true;
    config.dump_hidden = true;

    const auto index = next_index;
    next_index = (next_index + 1) % 30;

    fs::FsPath _name{};
    std::snprintf(_name, sizeof(_name), "%s_%u", name, index);

    fs::FsPath _mount{};
    std::snprintf(_mount, sizeof(_mount), "%s_%u:/", name, index);

    if (!common::MountNetworkDevice2(
        create_device(config),
        config, file_size, dir_size,
        _name, _mount
    )) {
        return false;
    }

    out_path = _mount;
    return true;
}

Result MountNetworkDevice(const CreateDeviceCallback& create_device, size_t file_size, size_t dir_size, const char* name, bool force_read_only) {
    fs::FsPath config_path{};
    std::snprintf(config_path, sizeof(config_path), "/config/hats-tools/mount/%s.ini", name);

    MountConfigs configs{};
    LoadConfigsFromIni(config_path, configs);

    for (auto& config : configs) {
        if (config.name.empty()) {
            log_write("[DEVOPTAB] Skipping empty name\n");
            continue;
        }

        if (config.url.empty()) {
            log_write("[DEVOPTAB] Skipping empty url for %s\n", config.name.c_str());
            continue;
        }

        if (force_read_only) {
            config.read_only = true;
        }

        fs::FsPath _name{};
        std::snprintf(_name, sizeof(_name), "[%s] %s", name, config.name.c_str());

        fs::FsPath _mount{};
        std::snprintf(_mount, sizeof(_mount), "[%s] %s:/", name, config.name.c_str());

        if (!MountNetworkDevice2(create_device(config), config, file_size, dir_size, _name, _mount)) {
            log_write("[DEVOPTAB] Failed to mount %s\n", config.name.c_str());
            continue;
        }
    }

    R_SUCCEED();
}

bool IsNetworkDeviceMounted(const std::string& url) {
    EnsureRwLockInitialized();
    SCOPED_RWLOCK(&g_rwlock, false);
    for (const auto& entry : g_entries) {
        if (entry && entry->device.config.url == url) {
            return true;
        }
    }
    return false;
}

} // sphaira::devoptab::common

namespace sphaira::devoptab {

using namespace sphaira::devoptab::common;

Result GetNetworkDevices(location::StdioEntries& out) {
    EnsureRwLockInitialized();
    SCOPED_RWLOCK(&g_rwlock, false);
    out.clear();

    for (const auto& entry : g_entries) {
        if (entry) {
            const auto& config = entry->device.config;

            u32 flags = 0;
            if (config.read_only) {
                flags |= location::FsEntryFlag::FsEntryFlag_ReadOnly;
            }
            if (config.no_stat_file) {
                flags |= location::FsEntryFlag::FsEntryFlag_NoStatFile;
            }
            if (config.no_stat_dir) {
                flags |= location::FsEntryFlag::FsEntryFlag_NoStatDir;
            }

            out.emplace_back(entry->mount, entry->name, flags, config.dump_path, config.fs_hidden, config.dump_hidden);
        }
    }

    R_SUCCEED();
}

void UmountAllNeworkDevices() {
    EnsureRwLockInitialized();
    SCOPED_RWLOCK(&g_rwlock, true);

    for (auto& entry : g_entries) {
        if (!entry) {
            continue;
        }

        log_write("[DEVOPTAB] Unmounting %s URL: %s\n", entry->mount.s, entry->device.config.url.c_str());
        entry.reset();
    }
}

void UmountNeworkDevice(const fs::FsPath& mount) {
    EnsureRwLockInitialized();
    SCOPED_RWLOCK(&g_rwlock, true);

    auto it = std::ranges::find_if(g_entries, [&](const auto& e){
        return e && e->mount == mount;
    });

    if (it != g_entries.end()) {
        log_write("[DEVOPTAB] Unmounting %s URL: %s\n", (*it)->mount.s, (*it)->device.config.url.c_str());
        it->reset();
    } else {
        log_write("[DEVOPTAB] No such mount %s\n", mount.s);
    }
}

void FixDkpBug() {
    const int max = 35;

    for (int i = 0; i < max; i++) {
        if (!devoptab_list[i]) {
            devoptab_list[i] = &dotab_stdnull;
            log_write("[DEVOPTAB] Fixing DKP bug at index: %d\n", i);
        }
    }
}

} // sphaira::devoptab
