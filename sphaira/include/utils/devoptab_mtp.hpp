#pragma once

#include "utils/devoptab_common.hpp"
#include <switch.h>
#include <string>
#include <vector>
#include <memory>

#include <unordered_map>

namespace sphaira::devoptab::mtp {

constexpr u16 MTP_CONTAINER_TYPE_COMMAND  = 1;
constexpr u16 MTP_CONTAINER_TYPE_DATA     = 2;
constexpr u16 MTP_CONTAINER_TYPE_RESPONSE = 3;
constexpr u16 MTP_CONTAINER_TYPE_EVENT    = 4;

constexpr u16 MTP_OP_GET_DEVICE_INFO    = 0x1001;
constexpr u16 MTP_OP_OPEN_SESSION         = 0x1002;
constexpr u16 MTP_OP_CLOSE_SESSION        = 0x1003;
constexpr u16 MTP_OP_GET_STORAGE_IDS      = 0x1004;
constexpr u16 MTP_OP_GET_STORAGE_INFO     = 0x1005;
constexpr u16 MTP_OP_GET_OBJECT_HANDLES   = 0x1007;
constexpr u16 MTP_OP_GET_OBJECT_INFO      = 0x1008;
constexpr u16 MTP_OP_GET_OBJECT           = 0x1009;
constexpr u16 MTP_OP_DELETE_OBJECT        = 0x100B;
constexpr u16 MTP_OP_SEND_OBJECT_INFO     = 0x100C;
constexpr u16 MTP_OP_SEND_OBJECT          = 0x100D;
constexpr u16 MTP_OP_GET_PARTIAL_OBJECT   = 0x101B;

constexpr u16 MTP_RESPONSE_OK             = 0x2001;
constexpr u16 MTP_FORMAT_FOLDER           = 0x3001;

#pragma pack(push, 1)
struct MtpContainerHeader {
    u32 length;
    u16 type;
    u16 code;
    u32 transaction_id;
};
#pragma pack(pop)

struct MtpStorageInfo {
    u32 storage_id;
    u16 storage_type;
    u16 filesystem_type;
    u16 access_capability;
    u64 max_capacity;
    u64 free_space;
    std::string volume_label;
};

struct MtpObject {
    u32 handle;
    u32 storage_id;
    u16 format;
    u64 size;
    u32 parent_handle;
    std::string filename;
    bool is_dir;
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

class MtpMountDevice final : public common::MountDevice {
public:
    MtpMountDevice(const common::MountConfig& config, u32 storage_id, const std::string& volume_label, u64 capacity, u64 free_space);
    ~MtpMountDevice() override;

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

private:
    u32 ResolvePathToHandle(const char* path, bool* is_dir = nullptr, u64* out_size = nullptr);
    bool FetchDirectoryEntries(u32 parent_handle, std::vector<MtpObject>& out_entries);

private:
    u32 m_storage_id{};
    std::string m_label{};
    u64 m_capacity{};
    u64 m_free_space{};
    std::unordered_map<u32, std::vector<MtpObject>> m_dir_cache{};
    std::unordered_map<std::string, MtpObject> m_path_cache{};
};

auto ScanAndMountMtpDevices() -> common::MountConfigs;
void CloseMtpSession();

} // namespace sphaira::devoptab::mtp
