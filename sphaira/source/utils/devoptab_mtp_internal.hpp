#pragma once

#include "utils/devoptab_mtp.hpp"
#include "utils/devoptab.hpp"
#include "log.hpp"
#include "defines.hpp"

#include <new>
#include <span>
#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <vector>
#include <string>

namespace sphaira::devoptab::mtp {

// Protocol constants
constexpr u16 CONTAINER_COMMAND  = 1;
constexpr u16 CONTAINER_DATA     = 2;
constexpr u16 CONTAINER_RESPONSE = 3;

constexpr u16 OP_GET_DEVICE_INFO       = 0x1001;
constexpr u16 OP_OPEN_SESSION          = 0x1002;
constexpr u16 OP_CLOSE_SESSION         = 0x1003;
constexpr u16 OP_GET_STORAGE_IDS       = 0x1004;
constexpr u16 OP_GET_STORAGE_INFO      = 0x1005;
constexpr u16 OP_GET_OBJECT_HANDLES    = 0x1007;
constexpr u16 OP_GET_OBJECT_INFO       = 0x1008;
constexpr u16 OP_GET_OBJECT            = 0x1009;
constexpr u16 OP_GET_PARTIAL_OBJECT    = 0x101B;
constexpr u16 OP_GET_OBJECT_PROP_VALUE = 0x9803;
constexpr u16 OP_GET_PARTIAL_OBJECT_64 = 0x95C1;

constexpr u16 RESP_OK = 0x2001;

constexpr u16 FORMAT_ASSOCIATION = 0x3001;
constexpr u16 PROP_OBJECT_SIZE   = 0xDC04;

constexpr u32 HANDLE_ROOT = 0xFFFFFFFF;
constexpr u64 SIZE_NEEDS_64BIT = 0xFFFFFFFF;
constexpr blksize_t STDIO_BLOCK_SIZE = 512 * 1024;

constexpr Result ResultTransport  = MAKERESULT(Module_Libnx, LibnxError_IoError);
constexpr Result ResultProtocol   = MAKERESULT(Module_Libnx, LibnxError_BadInput);
constexpr Result ResultMtpFailed  = MAKERESULT(Module_Libnx, LibnxError_NotFound);
constexpr Result ResultNoSession  = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

#pragma pack(push, 1)
struct ContainerHeader {
    u32 length;
    u16 type;
    u16 code;
    u32 transaction_id;
};
#pragma pack(pop)
static_assert(sizeof(ContainerHeader) == 12);

constexpr u32 XFER_BUF_SIZE = 0x100000;
constexpr u32 XFER_ALIGN    = 0x1000;
constexpr u16 USB_FEATURE_ENDPOINT_HALT = 0;
constexpr u32 RESPONSE_POST_SIZE = XFER_ALIGN;
constexpr u32 MAX_READ_CHUNK = XFER_BUF_SIZE - XFER_ALIGN;
constexpr u64 XFER_TIMEOUT_NS = 5000000000ULL;
constexpr u64 STREAM_DRAIN_LIMIT = 8 * 1024 * 1024;

struct Session {
    UsbHsClientIfSession iface{};
    UsbHsClientEpSession ep_in{};
    UsbHsClientEpSession ep_out{};
    u32 transaction_id{1};
    bool connected{};

    bool has_partial{};
    bool has_partial64{};
    bool has_prop_value{};

    u64 last_ok_tick{};
    u32 generation{};
    u64 last_recover_tick{};
    u32 packet_size{512};
};

struct LinkStats {
    u64 tick{};
    u32 commands{};
    u64 bytes{};
};

struct ReadStream {
    bool active{};
    bool response_done{};
    u32 handle{};
    u64 next_offset{};
    u64 remaining{};
    u32 carry_len{};
    u32 carry_off{};
    u8 carry[XFER_ALIGN];
};

class Reader final {
public:
    explicit Reader(std::span<const u8> data) : m_data{data} {}

    bool Ok() const { return !m_bad; }

    bool Skip(size_t n) {
        if (m_bad || m_pos + n > m_data.size()) {
            m_bad = true;
            return false;
        }
        m_pos += n;
        return true;
    }

    template <typename T>
    bool Read(T* out) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (m_bad || m_pos + sizeof(T) > m_data.size()) {
            m_bad = true;
            return false;
        }
        std::memcpy(out, m_data.data() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return true;
    }

    bool ReadString(std::string* out) {
        u8 len{};
        if (!Read(&len)) {
            return false;
        }

        if (out) {
            out->clear();
        }
        if (!len) {
            return true;
        }

        if (m_pos + len * sizeof(u16) > m_data.size()) {
            m_bad = true;
            return false;
        }

        if (out) {
            AppendUtf16(*out, m_data.subspan(m_pos, len * sizeof(u16)));
        }
        m_pos += len * sizeof(u16);
        return true;
    }

    template <typename T>
    bool ReadArray(std::vector<T>* out) {
        u32 count{};
        if (!Read(&count)) {
            return false;
        }

        if (m_pos + static_cast<size_t>(count) * sizeof(T) > m_data.size()) {
            m_bad = true;
            return false;
        }

        if (out) {
            out->resize(count);
            if (count) {
                std::memcpy(out->data(), m_data.data() + m_pos, count * sizeof(T));
            }
        }
        m_pos += static_cast<size_t>(count) * sizeof(T);
        return true;
    }

private:
    static void AppendUtf16(std::string& out, std::span<const u8> utf16) {
        for (size_t i = 0; i + 1 < utf16.size(); i += 2) {
            u32 cp = static_cast<u32>(utf16[i]) | (static_cast<u32>(utf16[i + 1]) << 8);
            if (!cp) {
                break;
            }

            if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < utf16.size()) {
                const u32 low = static_cast<u32>(utf16[i + 2]) | (static_cast<u32>(utf16[i + 3]) << 8);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                }
            }

            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
    }

    std::span<const u8> m_data;
    size_t m_pos{};
    bool m_bad{};
};

class DataSink final {
public:
    DataSink() = default;
    explicit DataSink(std::vector<u8>* vec) : m_vec{vec} {}
    DataSink(void* raw, size_t capacity) : m_raw{static_cast<u8*>(raw)}, m_capacity{capacity} {}

    void Reset(size_t expected) {
        m_written = 0;
        if (m_vec) {
            m_vec->clear();
            m_vec->reserve(expected);
        }
    }

    void Append(const void* src, size_t len) {
        if (m_vec) {
            const auto* p = static_cast<const u8*>(src);
            m_vec->insert(m_vec->end(), p, p + len);
        } else if (m_raw && m_written < m_capacity) {
            std::memcpy(m_raw + m_written, src, std::min(len, m_capacity - m_written));
        }
        m_written += len;
    }

    size_t Written() const { return m_written; }

private:
    std::vector<u8>* m_vec{};
    u8* m_raw{};
    size_t m_capacity{};
    size_t m_written{};
};

struct MountRecord {
    common::MountConfig config;
    MtpMountDevice* device;
};

struct StorageEntry {
    u32 id;
    u64 capacity;
    u64 free_space;
    std::string label;
};

extern u8 g_xfer_buf[XFER_BUF_SIZE];
extern u8 g_ctrl_buf[XFER_ALIGN];
extern Session g_session;
extern Mutex g_mutex;
extern LinkStats g_stats;
extern ReadStream g_stream;
extern Mutex g_mount_mutex;
extern std::vector<MountRecord> g_mounts;

u64 MsSince(u64 tick);
template <typename T, typename U>
inline auto AlignUp(T val, U align) {
    return (val + align - 1) & ~(align - 1);
}

void CloseUsbLocked(const char* why);
Result GetEndpointStatusLocked(UsbHsClientEpSession* ep, bool* out_halted);
Result ClearEndpointHaltLocked(UsbHsClientEpSession* ep);
bool RecoverLinkLocked(const char* why);
Result PostBufferOnce(UsbHsClientEpSession* ep, u32 size, u32* out_transferred);
Result PostBuffer(UsbHsClientEpSession* ep, u32 size, u32* out_transferred);
Result SendCommand(u16 code, std::span<const u32> params, u32* out_transaction_id);
Result ReceiveContainer(u32 post, ContainerHeader* out_hdr, u32* out_transferred);
Result ReceiveDataPhase(const ContainerHeader& hdr, u32 first_transferred, DataSink* sink);

Result Transact(u16 op, std::span<const u32> params, DataSink* sink, u16* out_code);
Result TransactData(u16 op, std::span<const u32> params, std::vector<u8>* out, u16* out_code = nullptr);
Result TransactNoData(u16 op, std::span<const u32> params, u16* out_code = nullptr);

Result FinishStreamLocked();
Result PullStreamLocked(u8* dst, u64 want, u64* out_got);
void AbortStreamLocked();
Result StartStreamLocked(u32 handle, u64 offset, u64 file_size, u64 want);
bool ParseObjectInfo(std::span<const u8> data, u32 handle, MtpObject* out);
void ResolveLargeSize(MtpObject* obj);
Result QueryDeviceCapabilities();

bool EnsureSessionLocked();
Result ConnectLocked();
Result ListStoragesLocked(std::vector<StorageEntry>& out);
bool IsSessionAliveLocked();

} // namespace sphaira::devoptab::mtp
