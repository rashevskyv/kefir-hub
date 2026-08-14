#include "utils/devoptab_common.hpp"
#include "utils/nfs_url.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <cstring>
#include <string>
#include <algorithm>
#include <cerrno>

#include <nfsc/libnfs.h>

namespace sphaira::devoptab::nfs {
namespace {

struct File {
    nfsfh* fd{nullptr};
};

struct Dir {
    nfsdir* dir{nullptr};
};

struct Device final : common::MountDevice {
    using MountDevice::MountDevice;
    ~Device() override;

    bool Mount() override;
    int devoptab_open(void *fileStruct, const char *path, int flags, int mode) override;
    int devoptab_close(void *fd) override;
    ssize_t devoptab_read(void *fd, char *ptr, size_t len) override;
    ssize_t devoptab_write(void *fd, const char *ptr, size_t len) override;
    ssize_t devoptab_seek(void *fd, off_t pos, int dir) override;
    int devoptab_fstat(void *fd, struct stat *st) override;
    int devoptab_unlink(const char *path) override;
    int devoptab_rename(const char *oldName, const char *newName) override;
    int devoptab_mkdir(const char *path, int mode) override;
    int devoptab_rmdir(const char *path) override;
    int devoptab_diropen(void* fd, const char *path) override;
    int devoptab_dirreset(void* fd) override;
    int devoptab_dirnext(void* fd, char *filename, struct stat *filestat) override;
    int devoptab_dirclose(void* fd) override;
    int devoptab_lstat(const char *path, struct stat *st) override;
    int devoptab_ftruncate(void *fd, off_t len) override;
    int devoptab_statvfs(const char *path, struct statvfs *buf) override;
    int devoptab_fsync(void *fd) override;
    int devoptab_utimes(const char *path, const struct timeval times[2]) override;

private:
    nfs_context* nfs{nullptr};
    bool mounted{false};
};

Device::~Device() {
    if (nfs) {
        if (mounted) {
            nfs_umount(nfs);
            mounted = false;
        }
        nfs_destroy_context(nfs);
        nfs = nullptr;
    }
}

bool Device::Mount() {
    if (mounted) {
        return true;
    }

    if (!sphaira::nfs::ValidateUrl(this->config.url)) {
        log_write("[NFS] URL validation failed before mount\n");
        return false;
    }

    if (!nfs) {
        nfs = nfs_init_context();
        if (!nfs) {
            log_write("[NFS] nfs_init_context() failed\n");
            return false;
        }
        nfs_set_readonly(nfs, 1);
    }

    const auto& url = this->config.url;
    auto* nfs_url = nfs_parse_url_dir(nfs, url.c_str());
    if (!nfs_url) {
        log_write("[NFS] nfs_parse_url_dir() failed\n");
        if (nfs) {
            nfs_destroy_context(nfs);
            nfs = nullptr;
        }
        return false;
    }
    ON_SCOPE_EXIT(nfs_destroy_url(nfs_url));

    const auto ret = nfs_mount(nfs, nfs_url->server, nfs_url->path);
    if (ret) {
        log_write("[NFS] nfs_mount() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        if (nfs) {
            nfs_destroy_context(nfs);
            nfs = nullptr;
        }
        return false;
    }

    log_write("[NFS] Mount success\n");
    return mounted = true;
}

int Device::devoptab_open(void *fileStruct, const char *path, int flags, int mode) {
    auto file = static_cast<File*>(fileStruct);

    // Initial public behavior is read-only
    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) {
        return -EROFS;
    }

    const auto ret = nfs_open(nfs, path, flags, &file->fd);
    if (ret) {
        log_write("[NFS] nfs_open() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_close(void *fd) {
    auto file = static_cast<File*>(fd);
    if (file->fd) {
        nfs_close(nfs, file->fd);
        file->fd = nullptr;
    }
    return 0;
}

ssize_t Device::devoptab_read(void *fd, char *ptr, size_t len) {
    auto file = static_cast<File*>(fd);

    const auto max_read = nfs_get_readmax(nfs);
    size_t bytes_read = 0;

    while (bytes_read < len) {
        const auto to_read = (max_read > 0) ? std::min<size_t>(len - bytes_read, static_cast<size_t>(max_read)) : (len - bytes_read);
        const auto ret = nfs_read(nfs, file->fd, ptr, to_read);

        if (ret < 0) {
            log_write("[NFS] nfs_read() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
            return (bytes_read > 0) ? static_cast<ssize_t>(bytes_read) : ret;
        }

        ptr += ret;
        bytes_read += ret;

        if (static_cast<size_t>(ret) < to_read) {
            break;
        }
    }

    return bytes_read;
}

ssize_t Device::devoptab_write(void *fd, const char *ptr, size_t len) {
    return -EROFS;
}

ssize_t Device::devoptab_seek(void *fd, off_t pos, int dir) {
    auto file = static_cast<File*>(fd);

    uint64_t current_offset = 0;
    const auto ret = nfs_lseek(nfs, file->fd, pos, dir, &current_offset);
    if (ret < 0) {
        log_write("[NFS] nfs_lseek() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return static_cast<ssize_t>(current_offset);
}

int Device::devoptab_fstat(void *fd, struct stat *st) {
    auto file = static_cast<File*>(fd);

    const auto ret = nfs_fstat(nfs, file->fd, st);
    if (ret) {
        log_write("[NFS] nfs_fstat() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_unlink(const char *path) {
    return -EROFS;
}

int Device::devoptab_rename(const char *oldName, const char *newName) {
    return -EROFS;
}

int Device::devoptab_mkdir(const char *path, int mode) {
    return -EROFS;
}

int Device::devoptab_rmdir(const char *path) {
    return -EROFS;
}

int Device::devoptab_diropen(void* fd, const char *path) {
    auto dir = static_cast<Dir*>(fd);

    const auto ret = nfs_opendir(nfs, path, &dir->dir);
    if (ret) {
        log_write("[NFS] nfs_opendir() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_dirreset(void* fd) {
    auto dir = static_cast<Dir*>(fd);
    if (dir->dir) {
        nfs_rewinddir(nfs, dir->dir);
    }
    return 0;
}

int Device::devoptab_dirnext(void* fd, char *filename, struct stat *filestat) {
    auto dir = static_cast<Dir*>(fd);

    const auto entry = nfs_readdir(nfs, dir->dir);
    if (!entry) {
        return -ENOENT;
    }

    std::strncpy(filename, entry->name, NAME_MAX);
    filename[NAME_MAX - 1] = '\0';

    filestat->st_dev = entry->dev;
    filestat->st_ino = entry->inode;
    filestat->st_mode = entry->mode;
    filestat->st_nlink = entry->nlink;
    filestat->st_uid = entry->uid;
    filestat->st_gid = entry->gid;
    filestat->st_size = entry->size;
    filestat->st_atime = entry->atime.tv_sec;
    filestat->st_mtime = entry->mtime.tv_sec;
    filestat->st_ctime = entry->ctime.tv_sec;
    filestat->st_blksize = entry->blksize;
    filestat->st_blocks = entry->blocks;

    return 0;
}

int Device::devoptab_dirclose(void* fd) {
    auto dir = static_cast<Dir*>(fd);
    if (dir->dir) {
        nfs_closedir(nfs, dir->dir);
        dir->dir = nullptr;
    }
    return 0;
}

int Device::devoptab_lstat(const char *path, struct stat *st) {
    const auto ret = nfs_stat(nfs, path, st);
    if (ret) {
        log_write("[NFS] nfs_stat() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_ftruncate(void *fd, off_t len) {
    return -EROFS;
}

int Device::devoptab_statvfs(const char *path, struct statvfs *buf) {
    const auto ret = nfs_statvfs(nfs, path, buf);
    if (ret) {
        log_write("[NFS] nfs_statvfs() failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return ret;
    }

    return 0;
}

int Device::devoptab_fsync(void *fd) {
    return -EROFS;
}

int Device::devoptab_utimes(const char *path, const struct timeval times[2]) {
    return -EROFS;
}

} // namespace

bool Mount(const common::MountConfig& config, const char* name, const char* mount_name) {
    auto device = std::make_unique<Device>(config);
    return common::MountNetworkDevice2(
        std::move(device),
        config,
        sizeof(File),
        sizeof(Dir),
        name,
        mount_name
    );
}

Result TestConnection(const std::string& url) {
    if (!sphaira::nfs::ValidateUrl(url)) {
        return -1;
    }

    auto* nfs = nfs_init_context();
    if (!nfs) {
        return -1;
    }
    ON_SCOPE_EXIT(nfs_destroy_context(nfs));

    auto* nfs_url = nfs_parse_url_dir(nfs, url.c_str());
    if (!nfs_url) {
        return -1;
    }
    ON_SCOPE_EXIT(nfs_destroy_url(nfs_url));

    const auto ret = nfs_mount(nfs, nfs_url->server, nfs_url->path);
    if (ret) {
        log_write("[NFS] TestConnection failed: %s errno: %s\n", nfs_get_error(nfs), std::strerror(-ret));
        return -1;
    }

    nfs_umount(nfs);
    return 0;
}

} // namespace sphaira::devoptab::nfs
