#include "utils/devoptab_common_internal.hpp"
#include "defines.hpp"
#include "log.hpp"
#include <cstring>
#include <fcntl.h>
#include <sys/errno.h>

namespace sphaira::devoptab::common {
namespace {

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

} // namespace

const devoptab_t DEVOPTAB = {
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


} // namespace sphaira::devoptab::common
