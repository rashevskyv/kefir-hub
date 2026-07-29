#pragma once

#include "utils/devoptab_common.hpp"
#include <switch.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace sphaira::devoptab::mtp {

// one entry of an MTP directory listing.
struct MtpObject {
    u32 handle{};
    u16 format{};
    u64 size{};
    bool is_dir{};
    std::string filename{};
};

struct MtpFileHandle {
    u32 object_handle;
    u64 size;
    u64 offset;
};

struct MtpDirHandle {
    std::vector<MtpObject> entries;
    size_t index;
};

// Read-only view of one storage on a connected MTP responder (a phone, a
// camera, ...). Paths are resolved lazily and cached: listing a directory also
// caches every child, so walking into a folder the browser already showed
// costs a single GetObjectHandles round trip instead of a full re-walk.
class MtpMountDevice final : public common::MountDevice {
public:
    MtpMountDevice(const common::MountConfig& config, u32 storage_id, u64 capacity, u64 free_space);

    bool Mount() override;
    int devoptab_open(void *fileStruct, const char *path, int flags, int mode) override;
    int devoptab_close(void *fd) override;
    ssize_t devoptab_read(void *fd, char *ptr, size_t len) override;
    ssize_t devoptab_seek(void *fd, off_t pos, int dir) override;
    int devoptab_fstat(void *fd, struct stat *st) override;
    int devoptab_diropen(void* fd, const char *path) override;
    int devoptab_dirreset(void* fd) override;
    int devoptab_dirnext(void* fd, char *filename, struct stat *filestat) override;
    int devoptab_dirclose(void* fd) override;
    int devoptab_statvfs(const char *_path, struct statvfs *buf) override;
    int devoptab_lstat(const char *path, struct stat *st) override;

    // Point an already mounted device at the storage of a freshly reconnected
    // responder. devoptab mounts outlive a USB disconnect (see the comment on
    // MountRecord in the .cpp), so reconnecting reuses the same object rather
    // than unmounting one that open handles may still reference.
    void Rebind(u32 storage_id, u64 capacity, u64 free_space);

private:
    // Every path here is normalised: "" is the storage root, everything else
    // looks like "Android/data" with no leading or trailing slash. The public
    // pair takes the session lock; the *Locked pair assumes it is held and may
    // call each other freely (resolving a path lists its parent).
    bool Lookup(const std::string& path, MtpObject* out);
    bool List(const std::string& path, std::vector<MtpObject>* out);
    bool LookupLocked(const std::string& path, MtpObject* out);
    bool ListLocked(const std::string& path, std::vector<MtpObject>* out);
    int LookupErrno() const;
    void DropCaches();
    // drops the caches when the session reconnected since they were filled.
    void SyncGenerationLocked();

    u32 m_storage_id{};
    u32 m_generation{};
    u64 m_capacity{};
    u64 m_free_space{};

    std::unordered_map<std::string, std::vector<MtpObject>> m_dir_cache{};
    std::unordered_map<std::string, MtpObject> m_obj_cache{};
};

// Probes USB for an MTP responder and (re)registers a devoptab mount for each
// of its storages. Returns the mounts that are usable right now; an empty
// result means "no phone connected", not an error.
auto ScanAndMountMtpDevices() -> common::MountConfigs;

// Closes the USB side of the session. Safe to call when nothing is connected.
void CloseMtpSession();

} // namespace sphaira::devoptab::mtp
