#include "utils/devoptab_common_internal.hpp"
#include "utils/devoptab_common.hpp"
#include "utils/devoptab.hpp"
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

namespace sphaira::devoptab::common {

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

namespace {

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


struct Entry {
    Device device{};
    devoptab_t devoptab{};
    fs::FsPath mount{};
    char name[32]{};
    s32 ref_count{};

    ~Entry() {
        // RemoveDevice wants "name:". FindDevice on a string without ':'
        // returns the DEFAULT device (sdmc), so passing the bare mount name
        // nulled the sd card's devoptab slot on every unmount -- the data
        // abort in _open_r that FixDkpBug used to paper over.
        if (mount.s[0]) {
            char device_name[sizeof(mount.s) + 2];
            std::snprintf(device_name, sizeof(device_name), "%s:", mount.s);
            RemoveDevice(device_name);
        }
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

// FixDkpBug() is gone: the NULL holes it papered over came from ~Entry
// passing a colon-less name to RemoveDevice, which resolves to the DEFAULT
// device and nulled the sdmc slot. Filling every empty slot with
// dotab_stdnull also left AddDevice with no free slot, so remounts failed.

} // sphaira::devoptab
