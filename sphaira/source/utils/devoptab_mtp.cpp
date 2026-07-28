#include "utils/devoptab_mtp.hpp"
#include "utils/devoptab.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "app.hpp"
#include "utils/utils.hpp"

#include <new>

#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace sphaira::devoptab::mtp {
namespace {

struct MtpSession {
    UsbHsInterface iface{};
    UsbHsClientIfSession s{};
    UsbHsClientEpSession ep_in{};
    UsbHsClientEpSession ep_out{};
    u32 transaction_id{1};
    bool connected{};
};

static MtpSession g_mtp_session{};
static Mutex g_mtp_mutex{};

std::string Utf16LeToUtf8(const u8* utf16_bytes, u8 len) {
    std::string utf8;
    for (u8 i = 0; i < len; ++i) {
        u16 ch = static_cast<u16>(utf16_bytes[i * 2]) | (static_cast<u16>(utf16_bytes[i * 2 + 1]) << 8);
        if (ch == 0) break;
        if (ch < 0x80) {
            utf8 += static_cast<char>(ch);
        } else if (ch < 0x800) {
            utf8 += static_cast<char>(0xC0 | (ch >> 6));
            utf8 += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            utf8 += static_cast<char>(0xE0 | (ch >> 12));
            utf8 += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    return utf8;
}

// 64KB DMA-aligned transfer buffer required by usbHsEpPostBufferAsync
// The USB DMA controller on Switch requires 4KB-aligned memory.
static constexpr u32 MTP_XFER_BUF_SIZE = 0x10000;
alignas(0x1000) static u8 g_mtp_xfer_buf[MTP_XFER_BUF_SIZE];

Result PostAndWaitMtpTransfer(UsbHsClientEpSession* ep, void* user_buf, u32 size, bool is_write, u32* out_transferred = nullptr) {
    if (size > MTP_XFER_BUF_SIZE) {
        log_write("[MTP_HOST] transfer too large: %u > %u\n", size, MTP_XFER_BUF_SIZE);
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    // For writes: copy user data into aligned DMA buffer before posting
    u32 post_size = size;
    if (is_write) {
        std::memcpy(g_mtp_xfer_buf, user_buf, size);
    } else {
        // For USB IN reads, host buffer posted to DMA must be aligned to max packet size (512 bytes).
        // Using full MTP_XFER_BUF_SIZE (64KB) prevents USB hardware packet overflow.
        post_size = MTP_XFER_BUF_SIZE;
    }

    u32 xfer_id = 0;
    Result rc = usbHsEpPostBufferAsync(ep, g_mtp_xfer_buf, post_size, 0, &xfer_id);
    if (R_FAILED(rc)) {
        log_write("[MTP_HOST] usbHsEpPostBufferAsync failed: 0x%X\n", rc);
        return rc;
    }

    Event* evt = usbHsEpGetXferEvent(ep);
    eventWait(evt, 5000000000ULL); // 5 second timeout

    u32 count = 0;
    UsbHsXferReport reports[8]{};
    eventClear(evt);
    rc = usbHsEpGetXferReport(ep, reports, 8, &count);
    if (R_FAILED(rc)) {
        log_write("[MTP_HOST] usbHsEpGetXferReport failed: 0x%X\n", rc);
        return rc;
    }

    for (u32 i = 0; i < count; ++i) {
        if (reports[i].xferId == xfer_id) {
            if (out_transferred) *out_transferred = reports[i].transferredSize;
            // For reads: copy DMA buffer back to user buffer
            if (!is_write && R_SUCCEEDED(reports[i].res)) {
                u32 copy_len = std::min(size, reports[i].transferredSize);
                std::memcpy(user_buf, g_mtp_xfer_buf, copy_len);
            }
            if (R_FAILED(reports[i].res)) {
                log_write("[MTP_HOST] xfer report res failed: 0x%X (transferred=%u)\n", reports[i].res, reports[i].transferredSize);
            }
            return reports[i].res;
        }
    }
    log_write("[MTP_HOST] xfer report not found (count=%u)\n", count);
    return MAKERESULT(Module_Libnx, LibnxError_NotFound);
}

Result SendMtpCommand(MtpSession& s, u16 code, const std::vector<u32>& params) {
    const u32 param_count = static_cast<u32>(params.size());
    const u32 container_len = static_cast<u32>(sizeof(MtpContainerHeader) + param_count * sizeof(u32));

    u8 cmd_buf[sizeof(MtpContainerHeader) + 5 * sizeof(u32)]{};
    auto* hdr = reinterpret_cast<MtpContainerHeader*>(cmd_buf);
    hdr->length = container_len;
    hdr->type = MTP_CONTAINER_TYPE_COMMAND;
    hdr->code = code;
    hdr->transaction_id = s.transaction_id++;

    if (param_count > 0) {
        std::memcpy(cmd_buf + sizeof(MtpContainerHeader), params.data(), param_count * sizeof(u32));
    }

    return PostAndWaitMtpTransfer(&s.ep_out, cmd_buf, container_len, true);
}

Result ReceiveMtpData(MtpSession& s, std::vector<u8>& out_data) {
    // Read into the global DMA-aligned transfer buffer.
    // NOTE: PostAndWaitMtpTransfer already reads into g_mtp_xfer_buf and
    // copies back to user_buf, so we pass g_mtp_xfer_buf as user_buf to
    // avoid redundant copies (the memcpy becomes src==dst, which is fine).
    u32 transferred = 0;
    R_TRY(PostAndWaitMtpTransfer(&s.ep_in, g_mtp_xfer_buf, MTP_XFER_BUF_SIZE, false, &transferred));

    if (transferred < sizeof(MtpContainerHeader)) {
        log_write("[MTP_HOST] ReceiveMtpData: too small (%u bytes)\n", transferred);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    auto* hdr = reinterpret_cast<MtpContainerHeader*>(g_mtp_xfer_buf);

    // If the device sent a Response instead of Data, it means there was no
    // data phase (e.g. GetObjectHandles on a file handle). We have already
    // consumed the response from the IN endpoint, so the caller must NOT
    // call ReceiveMtpResponse separately.
    if (hdr->type == MTP_CONTAINER_TYPE_RESPONSE) {
        log_write("[MTP_HOST] ReceiveMtpData: got response instead of data (code=0x%04X)\n", hdr->code);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    if (hdr->type != MTP_CONTAINER_TYPE_DATA || hdr->length < sizeof(MtpContainerHeader)) {
        log_write("[MTP_HOST] ReceiveMtpData: bad type=%u length=%u\n", hdr->type, hdr->length);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    const u32 container_len = hdr->length;
    const u32 payload_len = container_len - sizeof(MtpContainerHeader);
    out_data.resize(payload_len);

    u32 payload_copied = 0;
    if (transferred > sizeof(MtpContainerHeader)) {
        u32 first_copy = std::min(payload_len, transferred - static_cast<u32>(sizeof(MtpContainerHeader)));
        std::memcpy(out_data.data(), g_mtp_xfer_buf + sizeof(MtpContainerHeader), first_copy);
        payload_copied = first_copy;
    }

    while (payload_copied < payload_len) {
        u32 remaining = payload_len - payload_copied;
        u32 chunk_len = std::min(remaining, MTP_XFER_BUF_SIZE);
        u32 chunk_transferred = 0;
        Result rc = PostAndWaitMtpTransfer(&s.ep_in, g_mtp_xfer_buf, chunk_len, false, &chunk_transferred);
        if (R_FAILED(rc) || chunk_transferred == 0) {
            log_write("[MTP_HOST] ReceiveMtpData: failed reading chunk (rc=0x%X, copied=%u/%u)\n", rc, payload_copied, payload_len);
            return R_FAILED(rc) ? rc : MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        u32 copy_len = std::min(remaining, chunk_transferred);
        std::memcpy(out_data.data() + payload_copied, g_mtp_xfer_buf, copy_len);
        payload_copied += copy_len;
    }

    return 0;
}

Result ReceiveMtpResponse(MtpSession& s, u16* out_response_code = nullptr) {
    u8 resp_buf[sizeof(MtpContainerHeader) + 5 * sizeof(u32)]{};
    u32 transferred = 0;
    R_TRY(PostAndWaitMtpTransfer(&s.ep_in, resp_buf, sizeof(resp_buf), false, &transferred));

    auto* hdr = reinterpret_cast<MtpContainerHeader*>(resp_buf);
    if (out_response_code) {
        *out_response_code = hdr->code;
    }

    log_write("[MTP_HOST] response: code=0x%04X type=%u len=%u\n", hdr->code, hdr->type, hdr->length);
    return (hdr->code == MTP_RESPONSE_OK) ? 0 : MAKERESULT(Module_Libnx, LibnxError_NotFound);
}

} // namespace

MtpMountDevice::MtpMountDevice(const common::MountConfig& config, u32 storage_id, const std::string& volume_label, u64 capacity, u64 free_space)
    : MountDevice(config)
    , m_storage_id{storage_id}
    , m_label{volume_label}
    , m_capacity{capacity}
    , m_free_space{free_space} {}

MtpMountDevice::~MtpMountDevice() = default;

bool MtpMountDevice::Mount() {
    return true;
}

u32 MtpMountDevice::ResolvePathToHandle(const char* path, bool* is_dir, u64* out_size) {
    if (!path || path[0] == '\0') {
        if (is_dir) *is_dir = true;
        if (out_size) *out_size = 0;
        return 0xFFFFFFFF; // Root parent handle in MTP
    }

    std::string path_str = path;

    // Strip device name prefix if present (e.g. "mtp0:/", "mtp0:", ":/")
    size_t colon_pos = path_str.find(':');
    if (colon_pos != std::string::npos) {
        path_str.erase(0, colon_pos + 1);
    }

    while (!path_str.empty() && path_str.front() == '/') path_str.erase(0, 1);
    while (!path_str.empty() && path_str.back() == '/') path_str.pop_back();

    if (path_str.empty() || path_str == ".") {
        if (is_dir) *is_dir = true;
        if (out_size) *out_size = 0;
        return 0xFFFFFFFF; // Root parent handle in MTP
    }

    auto cache_it = m_path_cache.find(path_str);
    if (cache_it != m_path_cache.end()) {
        if (is_dir) *is_dir = cache_it->second.is_dir;
        if (out_size) *out_size = cache_it->second.size;
        return cache_it->second.handle;
    }

    u32 current_handle = 0xFFFFFFFF;
    size_t pos = 0;
    std::string current_full_path;

    while (pos < path_str.size()) {
        size_t next_slash = path_str.find('/', pos);
        std::string component = path_str.substr(pos, (next_slash == std::string::npos) ? std::string::npos : next_slash - pos);
        if (!current_full_path.empty()) current_full_path += "/";
        current_full_path += component;

        std::vector<MtpObject> entries;
        if (!FetchDirectoryEntries(current_handle, entries)) {
            return 0;
        }

        auto it = std::ranges::find_if(entries, [&component](const MtpObject& obj) {
            return obj.filename == component;
        });

        if (it == entries.end()) {
            return 0;
        }

        m_path_cache[current_full_path] = *it;

        current_handle = it->handle;
        if (is_dir) *is_dir = it->is_dir;
        if (out_size) *out_size = it->size;

        if (next_slash == std::string::npos) break;
        pos = next_slash + 1;
    }

    return current_handle;
}

bool MtpMountDevice::FetchDirectoryEntries(u32 parent_handle, std::vector<MtpObject>& out_entries) {
    auto cache_it = m_dir_cache.find(parent_handle);
    if (cache_it != m_dir_cache.end()) {
        out_entries = cache_it->second;
        return true;
    }

    SCOPED_MUTEX(&g_mtp_mutex);
    if (!g_mtp_session.connected) return false;

    std::vector<u32> parent_candidates;
    if (parent_handle == 0xFFFFFFFF || parent_handle == 0) {
        // Standard MTP uses 0x00000000 for top-level root objects.
        // Some servers expect 0xFFFFFFFF, so try 0x00000000 first, then 0xFFFFFFFF.
        parent_candidates = { 0x00000000, 0xFFFFFFFF };
    } else {
        parent_candidates = { parent_handle };
    }

    std::vector<u32> storage_candidates{m_storage_id, 0xFFFFFFFF};

    std::vector<u8> data;
    u32 count = 0;
    bool success = false;

    for (u32 st_id : storage_candidates) {
        for (u32 p_handle : parent_candidates) {
            std::vector<u32> params{st_id, 0, p_handle};
            if (R_FAILED(SendMtpCommand(g_mtp_session, MTP_OP_GET_OBJECT_HANDLES, params))) continue;

            data.clear();
            if (R_FAILED(ReceiveMtpData(g_mtp_session, data))) continue;
            if (R_FAILED(ReceiveMtpResponse(g_mtp_session))) continue;

            if (data.size() >= sizeof(u32)) {
                std::memcpy(&count, data.data(), sizeof(u32));
                log_write("[MTP_HOST] GetObjectHandles(storage=0x%08X, parent=0x%08X) -> count=%u\n",
                    st_id, p_handle, count);
                success = true;
                if (count > 0) break;
            }
        }
        if (count > 0) break;
    }

    if (!success) {
        return false;
    }

    if (data.size() < sizeof(u32) || count == 0) {
        m_dir_cache[parent_handle] = out_entries;
        return true;
    }

    const u32 num_handles = std::min(count, static_cast<u32>((data.size() - sizeof(u32)) / sizeof(u32)));

    out_entries.reserve(num_handles);
    for (u32 i = 0; i < num_handles; ++i) {
        u32 handle = 0;
        std::memcpy(&handle, data.data() + sizeof(u32) + i * sizeof(u32), sizeof(u32));

        // Send GetObjectInfo(handle)
        std::vector<u32> info_params{handle};
        if (R_FAILED(SendMtpCommand(g_mtp_session, MTP_OP_GET_OBJECT_INFO, info_params))) continue;

        std::vector<u8> info_data;
        if (R_FAILED(ReceiveMtpData(g_mtp_session, info_data))) continue;
        if (R_FAILED(ReceiveMtpResponse(g_mtp_session))) continue;

        if (info_data.size() < 52) continue;

        u32 obj_storage = 0, obj_size = 0, obj_parent = 0;
        u16 obj_format = 0;
        std::memcpy(&obj_storage, info_data.data(), sizeof(u32));
        std::memcpy(&obj_format, info_data.data() + 4, sizeof(u16));
        std::memcpy(&obj_size, info_data.data() + 8, sizeof(u32));
        std::memcpy(&obj_parent, info_data.data() + 38, sizeof(u32));

        u8 name_len = info_data[52];
        std::string filename;
        if (name_len > 0 && info_data.size() >= 53 + name_len * sizeof(u16)) {
            filename = Utf16LeToUtf8(info_data.data() + 53, name_len);
        }

        if (filename.empty()) {
            char fallback[32];
            std::snprintf(fallback, sizeof(fallback), "object_%08x", handle);
            filename = fallback;
        }

        MtpObject obj{};
        obj.handle = handle;
        obj.storage_id = obj_storage;
        obj.format = obj_format;
        obj.size = obj_size;
        obj.parent_handle = obj_parent;
        obj.filename = filename;
        obj.is_dir = (obj_format == MTP_FORMAT_FOLDER || obj_format == 0xBA05);

        log_write("[MTP_HOST]   entry[%u]: handle=0x%08X name='%s' is_dir=%d size=%llu\n",
            i, handle, filename.c_str(), obj.is_dir, static_cast<unsigned long long>(obj.size));

        out_entries.push_back(obj);
    }

    m_dir_cache[parent_handle] = out_entries;
    return true;
}

int MtpMountDevice::devoptab_diropen(void* fd, const char *path) {
    // Construct MtpDirHandle in-place (calloc'd memory has no constructor called)
    auto* dir_handle = new (fd) MtpDirHandle();
    dir_handle->entries.clear();
    dir_handle->index = 0;

    log_write("[MTP_HOST] devoptab_diropen path='%s'\n", path ? path : "NULL");

    bool is_dir = false;
    u32 handle = ResolvePathToHandle(path, &is_dir);

    log_write("[MTP_HOST] devoptab_diropen resolved handle=0x%08X is_dir=%d\n", handle, is_dir);

    if (!FetchDirectoryEntries(handle, dir_handle->entries)) {
        log_write("[MTP_HOST] FetchDirectoryEntries failed for handle=0x%08X\n", handle);
        return -ENOENT;
    }

    log_write("[MTP_HOST] devoptab_diropen loaded %zu entries\n", dir_handle->entries.size());
    dir_handle->index = 0;
    return 0;
}

int MtpMountDevice::devoptab_dirreset(void* fd) {
    auto* dir_handle = static_cast<MtpDirHandle*>(fd);
    dir_handle->index = 0;
    return 0;
}

int MtpMountDevice::devoptab_dirnext(void* fd, char *filename, struct stat *filestat) {
    auto* dir_handle = static_cast<MtpDirHandle*>(fd);
    if (dir_handle->index >= dir_handle->entries.size()) {
        return -ENOENT;
    }

    const auto& entry = dir_handle->entries[dir_handle->index++];
    std::strncpy(filename, entry.filename.c_str(), 255);
    filename[255] = '\0';

    if (filestat) {
        std::memset(filestat, 0, sizeof(*filestat));
        filestat->st_size = entry.size;
        filestat->st_mode = entry.is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    }

    return 0;
}

int MtpMountDevice::devoptab_dirclose(void* fd) {
    auto* dir_handle = static_cast<MtpDirHandle*>(fd);
    // Explicitly destroy to free std::vector and std::string members
    dir_handle->~MtpDirHandle();
    return 0;
}

int MtpMountDevice::devoptab_open(void *fileStruct, const char *path, int flags, int mode) {
    auto* file_handle = static_cast<MtpFileHandle*>(fileStruct);
    file_handle->object_handle = 0;
    file_handle->size = 0;
    file_handle->offset = 0;

    bool is_dir = false;
    u64 file_size = 0;
    u32 handle = ResolvePathToHandle(path, &is_dir, &file_size);
    if (handle == 0 || handle == 0xFFFFFFFF || is_dir) {
        return -ENOENT;
    }

    file_handle->object_handle = handle;
    file_handle->size = file_size;
    file_handle->offset = 0;
    return 0;
}

int MtpMountDevice::devoptab_close(void *fd) {
    auto* file_handle = static_cast<MtpFileHandle*>(fd);
    file_handle->object_handle = 0;
    file_handle->offset = 0;
    return 0;
}

ssize_t MtpMountDevice::devoptab_read(void *fd, char *ptr, size_t len) {
    auto* file_handle = static_cast<MtpFileHandle*>(fd);
    if (!file_handle->object_handle) return -EIO;

    SCOPED_MUTEX(&g_mtp_mutex);
    if (!g_mtp_session.connected) return -EIO;

    if (file_handle->offset >= file_handle->size) {
        return 0; // EOF
    }

    size_t to_read = std::min(len, static_cast<size_t>(file_handle->size - file_handle->offset));
    if (to_read == 0) return 0;

    size_t total_bytes_read = 0;
    constexpr size_t MAX_CHUNK = 0xF000; // 60KB per chunk to fit safely in DMA buffer + MTP header

    while (total_bytes_read < to_read) {
        u32 chunk_len = static_cast<u32>(std::min(to_read - total_bytes_read, MAX_CHUNK));
        u64 cur_offset = file_handle->offset + total_bytes_read;

        u32 offset_32 = static_cast<u32>(cur_offset);
        std::vector<u32> params{file_handle->object_handle, offset_32, chunk_len};

        Result rc = SendMtpCommand(g_mtp_session, MTP_OP_GET_PARTIAL_OBJECT, params);
        if (R_SUCCEEDED(rc)) {
            std::vector<u8> data;
            if (R_SUCCEEDED(ReceiveMtpData(g_mtp_session, data)) && R_SUCCEEDED(ReceiveMtpResponse(g_mtp_session))) {
                size_t read_bytes = std::min(static_cast<size_t>(chunk_len), data.size());
                if (read_bytes == 0) break;
                std::memcpy(ptr + total_bytes_read, data.data(), read_bytes);
                total_bytes_read += read_bytes;
                if (read_bytes < chunk_len) break; // Device returned less data than requested
                continue;
            }
        }

        // Fallback: GetObject (0x1009) if GetPartialObject is not supported by device
        std::vector<u32> full_params{file_handle->object_handle};
        if (R_FAILED(SendMtpCommand(g_mtp_session, MTP_OP_GET_OBJECT, full_params))) break;

        std::vector<u8> full_data;
        if (R_FAILED(ReceiveMtpData(g_mtp_session, full_data)) || R_FAILED(ReceiveMtpResponse(g_mtp_session))) break;

        if (cur_offset >= full_data.size()) break;

        size_t read_bytes = std::min(to_read - total_bytes_read, full_data.size() - static_cast<size_t>(cur_offset));
        std::memcpy(ptr + total_bytes_read, full_data.data() + cur_offset, read_bytes);
        total_bytes_read += read_bytes;
        break;
    }

    file_handle->offset += total_bytes_read;
    return static_cast<ssize_t>(total_bytes_read);
}

ssize_t MtpMountDevice::devoptab_seek(void *fd, off_t pos, int dir) {
    auto* file_handle = static_cast<MtpFileHandle*>(fd);
    if (dir == SEEK_SET) {
        file_handle->offset = pos;
    } else if (dir == SEEK_CUR) {
        file_handle->offset += pos;
    }
    return static_cast<ssize_t>(file_handle->offset);
}

int MtpMountDevice::devoptab_fstat(void *fd, struct stat *st) {
    auto* file_handle = static_cast<MtpFileHandle*>(fd);
    std::memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0644;
    st->st_size = file_handle->size;
    return 0;
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

int MtpMountDevice::devoptab_lstat(const char *path, struct stat *st) {
    std::memset(st, 0, sizeof(*st));

    bool is_dir = false;
    u64 file_size = 0;
    u32 handle = ResolvePathToHandle(path, &is_dir, &file_size);

    // handle == 0 means "not found" (ResolvePathToHandle returns 0 on failure)
    if (handle == 0) {
        log_write("[MTP_HOST] devoptab_lstat: path '%s' not found\n", path ? path : "NULL");
        return -ENOENT;
    }

    if (is_dir) {
        st->st_mode = S_IFDIR | 0755;
    } else {
        st->st_mode = S_IFREG | 0644;
        st->st_size = file_size;
    }
    st->st_nlink = 1;

    return 0;
}

static common::MountConfigs g_mounted_mtp_configs{};

static std::string CleanMountName(const std::string& url) {
    std::string name = url;
    while (!name.empty() && (name.back() == '/' || name.back() == ':')) {
        name.pop_back();
    }
    return name;
}

static void CloseMtpSessionLocked() {
    if (!g_mtp_session.connected) return;

    log_write("[MTP_HOST] Closing MTP session and unmounting devices\n");

    // Send CloseSession opcode 0x1003
    SendMtpCommand(g_mtp_session, MTP_OP_CLOSE_SESSION, {});

    usbHsEpClose(&g_mtp_session.ep_in);
    usbHsEpClose(&g_mtp_session.ep_out);
    usbHsIfClose(&g_mtp_session.s);

    for (const auto& config : g_mounted_mtp_configs) {
        const std::string mount_name = CleanMountName(config.url);
        devoptab::UmountNeworkDevice(mount_name);
    }
    // RemoveDevice() inside ~Entry sets devoptab_list[i] = NULL.
    // Re-fill those slots with the stdnull stub so that newlib's
    // _open_r / readdir don't Data Abort on a NULL entry.
    devoptab::FixDkpBug();

    g_mounted_mtp_configs.clear();
    g_mtp_session.connected = false;
    std::memset(&g_mtp_session.iface, 0, sizeof(g_mtp_session.iface));
    log_write("[MTP_HOST] MTP session closed completely\n");
}

void CloseMtpSession() {
    SCOPED_MUTEX(&g_mtp_mutex);
    CloseMtpSessionLocked();
}

auto ScanAndMountMtpDevices() -> common::MountConfigs {
    SCOPED_MUTEX(&g_mtp_mutex);

    log_write("[MTP_HOST] scanning USB for MTP devices\n");

    if (g_mtp_session.connected) {
        bool alive = false;
        if (R_SUCCEEDED(SendMtpCommand(g_mtp_session, MTP_OP_GET_DEVICE_INFO, {}))) {
            std::vector<u8> dummy;
            if (R_SUCCEEDED(ReceiveMtpData(g_mtp_session, dummy)) && R_SUCCEEDED(ReceiveMtpResponse(g_mtp_session))) {
                alive = true;
            }
        }
        if (alive && !g_mounted_mtp_configs.empty()) {
            log_write("[MTP_HOST] MTP session active and mounted, returning existing mounts\n");
            return g_mounted_mtp_configs;
        }
        log_write("[MTP_HOST] Active MTP session non-responsive or disconnected, closing session...\n");
        CloseMtpSessionLocked();
    }

    common::MountConfigs mounted_configs{};

    if (R_FAILED(usbHsInitialize())) {
        log_write("[MTP_HOST] usbHsInitialize failed\n");
        return {};
    }

    UsbHsInterfaceFilter filter{};
    filter.Flags = 0; // Query all available interfaces

    Event evt{};
    if (R_FAILED(usbHsCreateInterfaceAvailableEvent(&evt, true, 0, &filter))) {
        log_write("[MTP_HOST] usbHsCreateInterfaceAvailableEvent failed\n");
        return {};
    }

    eventWait(&evt, 200000000ULL); // Wait 200ms for interface event

    UsbHsInterface interfaces[8]{};
    s32 total = 0;
    Result rc = usbHsQueryAvailableInterfaces(&filter, interfaces, sizeof(interfaces), &total);
    usbHsDestroyInterfaceAvailableEvent(&evt, 0);

    if (R_FAILED(rc) || total <= 0) {
        log_write("[MTP_HOST] no USB interfaces available (rc=0x%X, total=%d)\n", rc, total);
        return {};
    }

    log_write("[MTP_HOST] found %d USB interfaces on bus\n", total);

    for (s32 idx = 0; idx < total; ++idx) {
        const auto& iface = interfaces[idx];
        u8 cls = iface.device_desc.bDeviceClass;
        u8 subcls = iface.device_desc.bDeviceSubClass;
        log_write("[MTP_HOST] interface[%d] VID: 0x%04x PID: 0x%04x Class: 0x%02x SubClass: 0x%02x\n",
            idx, iface.device_desc.idVendor, iface.device_desc.idProduct, cls, subcls);

        // Skip non-storage classes (Audio 0x01, HID 0x03, Hub 0x09)
        if (cls != 0x06 && cls != 0xFF && cls != 0x00) {
            continue;
        }

        g_mtp_session.iface = iface;
        if (R_FAILED(usbHsAcquireUsbIf(&g_mtp_session.s, const_cast<UsbHsInterface*>(&iface)))) {
            log_write("[MTP_HOST] usbHsAcquireUsbIf failed for interface %d\n", idx);
            continue;
        }

        // Log interface-level class info (device class 0x00 means class is per-interface)
        auto& if_desc = g_mtp_session.s.inf.inf.interface_desc;
        log_write("[MTP_HOST]   ifClass: 0x%02x ifSubClass: 0x%02x ifProtocol: 0x%02x\n",
            if_desc.bInterfaceClass, if_desc.bInterfaceSubClass, if_desc.bInterfaceProtocol);

        // MTP = Still Image class 0x06, subclass 0x01, protocol 0x01
        // Android often uses Vendor Specific 0xFF with MTP subclass
        // Class 0x00 means unspecified (check anyway)
        // Skip definitely non-MTP classes: Audio(1), HID(3), Printer(7), Hub(9), CDC(2/10)
        u8 ifc = if_desc.bInterfaceClass;
        if (ifc == 0x01 || ifc == 0x02 || ifc == 0x03 || ifc == 0x07 || ifc == 0x09 || ifc == 0x0A || ifc == 0x0E) {
            log_write("[MTP_HOST]   skipping interface %d (non-MTP class 0x%02x)\n", idx, ifc);
            usbHsIfClose(&g_mtp_session.s);
            continue;
        }

        // Log endpoint descriptors for diagnostics
        auto& input_desc = g_mtp_session.s.inf.inf.input_endpoint_descs[0];   // IN: device → host (for RECEIVING)
        auto& output_desc = g_mtp_session.s.inf.inf.output_endpoint_descs[0]; // OUT: host → device (for SENDING)
        log_write("[MTP_HOST]   IN  ep: addr=0x%02x maxPkt=%u\n", input_desc.bEndpointAddress, input_desc.wMaxPacketSize);
        log_write("[MTP_HOST]   OUT ep: addr=0x%02x maxPkt=%u\n", output_desc.bEndpointAddress, output_desc.wMaxPacketSize);

        if (input_desc.wMaxPacketSize == 0 || output_desc.wMaxPacketSize == 0) {
            log_write("[MTP_HOST]   skipping interface %d (endpoint maxPkt is 0)\n", idx);
            usbHsIfClose(&g_mtp_session.s);
            continue;
        }

        // ep_out = OUT endpoint (host → device, for sending commands)
        // ep_in  = IN  endpoint (device → host, for receiving responses)
        Result rc_out = usbHsIfOpenUsbEp(&g_mtp_session.s, &g_mtp_session.ep_out, 1, output_desc.wMaxPacketSize, &output_desc);
        Result rc_in  = usbHsIfOpenUsbEp(&g_mtp_session.s, &g_mtp_session.ep_in,  1, input_desc.wMaxPacketSize,  &input_desc);
        if (R_FAILED(rc_out) || R_FAILED(rc_in)) {
            log_write("[MTP_HOST] opening USB endpoints failed for interface %d (rc_out=0x%X, rc_in=0x%X)\n", idx, rc_out, rc_in);
            usbHsIfClose(&g_mtp_session.s);
            continue;
        }

        g_mtp_session.connected = true;
        g_mtp_session.transaction_id = 1;

        // Open MTP session: OpenSession(session_id = 1)
        if (R_FAILED(SendMtpCommand(g_mtp_session, MTP_OP_OPEN_SESSION, {1}))) {
            log_write("[MTP_HOST] OpenSession failed on interface %d (not an MTP responder)\n", idx);
            usbHsEpClose(&g_mtp_session.ep_in);
            usbHsEpClose(&g_mtp_session.ep_out);
            usbHsIfClose(&g_mtp_session.s);
            g_mtp_session.connected = false;
            continue;
        }

        u16 resp = 0;
        if (R_FAILED(ReceiveMtpResponse(g_mtp_session, &resp)) || resp != MTP_RESPONSE_OK) {
            log_write("[MTP_HOST] OpenSession response failed on interface %d\n", idx);
            usbHsEpClose(&g_mtp_session.ep_in);
            usbHsEpClose(&g_mtp_session.ep_out);
            usbHsIfClose(&g_mtp_session.s);
            g_mtp_session.connected = false;
            continue;
        }

        log_write("[MTP_HOST] MTP session opened successfully on interface %d!\n", idx);

        // Get Storage IDs: GetStorageIDs()
        if (R_FAILED(SendMtpCommand(g_mtp_session, MTP_OP_GET_STORAGE_IDS, {}))) {
            log_write("[MTP_HOST] GetStorageIDs failed\n");
            continue;
        }

        std::vector<u8> storage_data;
        if (R_FAILED(ReceiveMtpData(g_mtp_session, storage_data)) || R_FAILED(ReceiveMtpResponse(g_mtp_session))) {
            log_write("[MTP_HOST] Receive GetStorageIDs data failed\n");
            continue;
        }

        if (storage_data.size() < sizeof(u32)) continue;
        u32 count = 0;
        std::memcpy(&count, storage_data.data(), sizeof(u32));

        for (u32 i = 0; i < count; ++i) {
            u32 st_id = 0;
            if (storage_data.size() < sizeof(u32) + (i + 1) * sizeof(u32)) break;
            std::memcpy(&st_id, storage_data.data() + sizeof(u32) + i * sizeof(u32), sizeof(u32));

            // Send GetStorageInfo(st_id)
            if (R_FAILED(SendMtpCommand(g_mtp_session, MTP_OP_GET_STORAGE_INFO, {st_id}))) continue;

            std::vector<u8> info_data;
            if (R_FAILED(ReceiveMtpData(g_mtp_session, info_data)) || R_FAILED(ReceiveMtpResponse(g_mtp_session))) continue;

            u64 max_cap = 0, free_sp = 0;
            std::string label = "Phone Storage";
            if (count > 1) {
                label = "Phone Storage " + std::to_string(i + 1);
            }

            if (info_data.size() >= 22) {
                std::memcpy(&max_cap, info_data.data() + 6, sizeof(u64));
                std::memcpy(&free_sp, info_data.data() + 14, sizeof(u64));
                if (info_data.size() >= 27) {
                    u8 desc_len = info_data[26];
                    if (desc_len > 0 && info_data.size() >= 27 + desc_len * sizeof(u16)) {
                        std::string parsed_desc = Utf16LeToUtf8(info_data.data() + 27, desc_len);
                        if (!parsed_desc.empty()) {
                            label = parsed_desc;
                        }
                    }
                }
            }

            char mount_name[16];
            std::snprintf(mount_name, sizeof(mount_name), "mtp%u", i);

            common::MountConfig config{};
            config.name = label;
            config.url = std::string(mount_name) + ":/";
            config.read_only = true;

            auto device = std::make_unique<MtpMountDevice>(config, st_id, label, max_cap, free_sp);
            if (common::MountNetworkDevice2(std::move(device), config, sizeof(MtpFileHandle), sizeof(MtpDirHandle), mount_name, mount_name)) {
                mounted_configs.push_back(config);
                log_write("[MTP_HOST] successfully mounted %s: as %s\n", mount_name, label.c_str());
            }
        }
        break;
    }

    g_mounted_mtp_configs = mounted_configs;
    return mounted_configs;
}

} // namespace sphaira::devoptab::mtp
