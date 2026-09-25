#pragma once

#include "utils/devoptab_common.hpp"
#include <sys/iosupport.h>
#include <memory>

namespace sphaira::devoptab::common {

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

extern RwLock g_rwlock;
void EnsureRwLockInitialized();
extern const devoptab_t DEVOPTAB;

} // namespace sphaira::devoptab::common
