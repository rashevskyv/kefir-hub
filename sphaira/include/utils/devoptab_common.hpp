#pragma once

#include "utils/devoptab_buffered.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include "location.hpp"

#include <memory>
#include <optional>
#include <span>
#include <functional>
#include <unordered_map>
#include <curl/curl.h>

namespace sphaira::devoptab::common {

// max entries per devoptab, should be enough.
enum { MAX_ENTRIES = 4 };


bool fix_path(const char* str, char* out, bool strip_leading_slash = false);

void update_devoptab_for_read_only(devoptab_t* devoptab, bool read_only);

struct MountConfig {
    std::string name{};
    std::string url{};
    std::string user{};
    std::string pass{};
    std::string dump_path{};
    long port{};
    long timeout{};
    bool read_only{};
    bool no_stat_file{true};
    bool no_stat_dir{true};
    bool fs_hidden{};
    bool dump_hidden{};

    std::unordered_map<std::string, std::string> extra{};
};
using MountConfigs = std::vector<MountConfig>;

struct MountDevice {
    MountDevice(const MountConfig& _config) : config{_config} {}
    virtual ~MountDevice() = default;

    virtual bool fix_path(const char* str, char* out, bool strip_leading_slash = false) {
        return common::fix_path(str, out, strip_leading_slash);
    }

    virtual bool Mount() = 0;
    virtual int devoptab_open(void *fileStruct, const char *path, int flags, int mode) { return -EIO; }
    virtual int devoptab_close(void *fd) { return -EIO; }
    virtual ssize_t devoptab_read(void *fd, char *ptr, size_t len) { return -EIO; }
    virtual ssize_t devoptab_write(void *fd, const char *ptr, size_t len) { return -EIO; }
    virtual ssize_t devoptab_seek(void *fd, off_t pos, int dir) { return 0; }
    virtual int devoptab_fstat(void *fd, struct stat *st) { return -EIO; }
    virtual int devoptab_unlink(const char *path) { return -EIO; }
    virtual int devoptab_rename(const char *oldName, const char *newName) { return -EIO; }
    virtual int devoptab_mkdir(const char *path, int mode) { return -EIO; }
    virtual int devoptab_rmdir(const char *path) { return -EIO; }
    virtual int devoptab_diropen(void* fd, const char *path) { return -EIO; }
    virtual int devoptab_dirreset(void* fd) { return -EIO; }
    virtual int devoptab_dirnext(void* fd, char *filename, struct stat *filestat) { return -EIO; }
    virtual int devoptab_dirclose(void* fd) { return -EIO; }
    virtual int devoptab_lstat(const char *path, struct stat *st) { return -EIO; }
    virtual int devoptab_ftruncate(void *fd, off_t len) { return -EIO; }
    virtual int devoptab_statvfs(const char *_path, struct statvfs *buf) { return -EIO; }
    virtual int devoptab_fsync(void *fd) { return -EIO; }
    virtual int devoptab_utimes(const char *_path, const struct timeval times[2]) { return -EIO; }

    const MountConfig config;
};

void LoadConfigsFromIni(const fs::FsPath& path, MountConfigs& out_configs);

using CreateDeviceCallback = std::function<std::unique_ptr<MountDevice>(const MountConfig& config)>;
Result MountNetworkDevice(const CreateDeviceCallback& create_device, size_t file_size, size_t dir_size, const char* name, bool force_read_only = false);

// same as above but takes in the device and expects the mount name to be set.
bool MountNetworkDevice2(std::unique_ptr<MountDevice>&& device, const MountConfig& config, size_t file_size, size_t dir_size, const char* name, const char* mount_name);

bool MountReadOnlyIndexDevice(const CreateDeviceCallback& create_device, size_t file_size, size_t dir_size, const char* name, fs::FsPath& out_path);

} // namespace sphaira::devoptab::common
