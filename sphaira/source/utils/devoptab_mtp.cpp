// MTP host: mounts a connected MTP responder (phone, camera, ...) as a
// read-only devoptab so the file browser can walk it.
//
// Locking: g_mutex is the innermost lock in the process. Every devoptab
// callback arrives holding the devoptab rwlock (read) and the per-device
// mutex, so nothing in here may call back into devoptab mount/unmount while
// holding g_mutex -- that would invert the order against MountNetworkDevice2,
// which takes the same rwlock for write.
//
// Logging: log_write opens, writes and closes a file on the SD card under a
// global mutex. Keep it out of the per-transfer paths; only session-level
// events and failures are logged.

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

namespace sphaira::devoptab::mtp {
namespace {

// -------------------------------------------------------------------------
// protocol constants
// -------------------------------------------------------------------------

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
// android.com vendor extension: same as GetPartialObject but with a 64bit
// offset, so files larger than 4GiB can be read.
constexpr u16 OP_GET_PARTIAL_OBJECT_64 = 0x95C1;

constexpr u16 RESP_OK = 0x2001;

constexpr u16 FORMAT_ASSOCIATION = 0x3001;
constexpr u16 PROP_OBJECT_SIZE   = 0xDC04;

// GetObjectHandles takes the parent handle; 0xFFFFFFFF selects the objects
// sitting directly in the root of the store.
constexpr u32 HANDLE_ROOT = 0xFFFFFFFF;

// ObjectInfo carries a 32bit size, so a responder reports this for anything
// that does not fit and the real size has to be asked for separately.
constexpr u64 SIZE_NEEDS_64BIT = 0xFFFFFFFF;

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

// -------------------------------------------------------------------------
// transfer buffer
// -------------------------------------------------------------------------

// usbHsEpPostBufferAsync wants a 0x1000-aligned buffer, and a single post may
// not exceed the maxXferSize the endpoint was opened with. Both endpoints are
// opened with exactly this size, so any post up to it is legal.
//
// The previous implementation opened the endpoints with maxXferSize =
// wMaxPacketSize (512) and then posted 64KiB buffers. usb:hs answered with
// 2140-0301 for anything that did not fit into a single 512 byte urb, which is
// why listings worked for small directories and died everywhere else.
constexpr u32 XFER_BUF_SIZE = 0x100000;
constexpr u32 XFER_ALIGN    = 0x1000;
alignas(XFER_ALIGN) u8 g_xfer_buf[XFER_BUF_SIZE];

// control transfers want their own 0x1000-aligned buffer.
alignas(XFER_ALIGN) u8 g_ctrl_buf[XFER_ALIGN];

// libnx does not name this one: CLEAR_FEATURE(ENDPOINT_HALT) takes feature 0.
constexpr u16 USB_FEATURE_ENDPOINT_HALT = 0;
// PIMA 15740 D.5.1 class requests on the control pipe.
constexpr u8 MTP_REQ_CANCEL = 0x64;
constexpr u16 MTP_CANCELLATION_CODE = 0x4001;

// a response container is at most 12 + 5*4 bytes; one page covers it.
constexpr u32 RESPONSE_POST_SIZE = XFER_ALIGN;

// biggest payload that still leaves room for the container header.
constexpr u32 MAX_READ_CHUNK = XFER_BUF_SIZE - XFER_ALIGN;

constexpr u64 XFER_TIMEOUT_NS = 5000000000ULL;

// newlib sizes a FILE's stdio buffer from st_blksize and falls back to BUFSIZ
// (1KiB) when it is zero. Every refill is one MTP round trip, so leaving it
// unset would turn reading an nsp into over a million of them.
constexpr blksize_t STDIO_BLOCK_SIZE = 512 * 1024;

constexpr u32 AlignUp(u32 v, u32 align) {
    return (v + align - 1) & ~(align - 1);
}

// -------------------------------------------------------------------------
// session state
// -------------------------------------------------------------------------

struct Session {
    UsbHsClientIfSession iface{};
    UsbHsClientEpSession ep_in{};
    UsbHsClientEpSession ep_out{};
    u32 transaction_id{1};
    bool connected{};

    // advertised in DeviceInfo.OperationsSupported.
    bool has_partial{};
    bool has_partial64{};
    bool has_prop_value{};

    // tick of the last completed transaction, used to skip redundant probes.
    u64 last_ok_tick{};

    // bumped on every successful (re)connect. A device whose cached handles
    // were fetched under an older generation must drop them: the phone
    // re-enumerated and the old object handles may no longer exist.
    u32 generation{};
};

// defined in the session management section below; usable from the devoptab
// callbacks to bring a dropped link back without waiting for a Root rescan.
bool EnsureSessionLocked();

u64 MsSince(u64 tick) {
    return (armTicksToNs(armGetSystemTick()) - armTicksToNs(tick)) / 1000000;
}

Session g_session{};
Mutex g_mutex{};

// -------------------------------------------------------------------------
// streaming read state
// -------------------------------------------------------------------------

// The tested phone wedges its MTP responder after ~100 transactions of any
// size: chunked partial reads (one transaction per 512 KiB) killed it a few
// dozen MB into every install. File reads therefore run as one long
// GetPartialObject(64) whose data phase is consumed incrementally across
// devoptab_read calls -- a whole install costs dozens of transactions instead
// of thousands.
struct ReadStream {
    bool active{};
    bool response_done{};   // closing response consumed (carry may remain)
    u32 handle{};
    u32 transaction_id{};   // needed to cancel the transfer on the control pipe
    u64 next_offset{};      // file offset of the next byte handed to a caller
    u64 remaining{};        // payload bytes still on the bus
    // pulled off the bus past the caller's request (posts are packet sized);
    // served first on the next read. at most one packet.
    u32 carry_len{};
    u32 carry_off{};
    u8 carry[512];
};
ReadStream g_stream{};

// forward seeks up to this are skipped by discarding; anything bigger (or a
// backward seek) abandons the stream.
constexpr u64 STREAM_DRAIN_LIMIT = 8 * 1024 * 1024;

// -------------------------------------------------------------------------
// little endian dataset reader
// -------------------------------------------------------------------------

// MTP datasets are packed little endian with variable length strings, so every
// field has to be bounds checked. Once a read runs past the end the cursor
// latches into a failed state and every later read is a no-op.
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

    // MTP string: u8 length in characters (including the terminator, 0 means
    // an empty string) followed by that many utf16-le code units.
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

    // u32 count followed by that many elements of T.
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

            // surrogate pair: phones happily put emoji in file names.
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

// -------------------------------------------------------------------------
// transport
// -------------------------------------------------------------------------

// Also the recovery path for a failed bulk transfer: once one fails, host and
// device disagree about which phase comes next, because the command was
// accepted but its data/response phases were never drained. There is no way to
// resynchronise from userland, so the session goes away and callers report an
// I/O error. Reporting an empty listing instead is what made a broken link look
// like an empty phone.
void CloseUsbLocked(const char* why) {
    if (!g_session.connected) {
        return;
    }

    log_write("[MTP_HOST] dropping usb session: %s\n", why);
    g_session.connected = false;

    usbHsEpClose(&g_session.ep_in);
    usbHsEpClose(&g_session.ep_out);
    usbHsIfClose(&g_session.iface);

    g_session.ep_in = {};
    g_session.ep_out = {};
    g_session.iface = {};
    g_session.has_partial = false;
    g_session.has_partial64 = false;
    g_session.has_prop_value = false;
    g_stream = {};
}

bool IsEndpointHaltedLocked(UsbHsClientEpSession* ep) {
    u32 transferred{};
    const auto rc = usbHsIfCtrlXfer(&g_session.iface,
        USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT,
        USB_REQUEST_GET_STATUS, 0, ep->desc.bEndpointAddress,
        sizeof(u16), g_ctrl_buf, &transferred);
    if (R_FAILED(rc)) {
        return false;
    }

    u16 status{};
    std::memcpy(&status, g_ctrl_buf, sizeof(status));
    return status & 1;
}

Result ClearEndpointHaltLocked(UsbHsClientEpSession* ep) {
    u32 transferred{};
    return usbHsIfCtrlXfer(&g_session.iface,
        USB_ENDPOINT_OUT | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT,
        USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
        ep->desc.bEndpointAddress, 0, nullptr, &transferred);
}

// Tells the responder to drop the in-flight transaction, so both sides agree
// the bus is idle again.
Result CancelTransactionLocked(u32 transaction_id) {
    struct Payload {
        u16 code;
        u32 transaction_id;
    } NX_PACKED;

    const Payload payload{MTP_CANCELLATION_CODE, transaction_id};
    std::memcpy(g_ctrl_buf, &payload, sizeof(payload));

    u32 transferred{};
    return usbHsIfCtrlXfer(&g_session.iface,
        USB_ENDPOINT_OUT | USB_REQUEST_TYPE_CLASS | USB_RECIPIENT_INTERFACE,
        MTP_REQ_CANCEL, 0, g_session.iface.inf.inf.interface_desc.bInterfaceNumber,
        sizeof(payload), g_ctrl_buf, &transferred);
}

// A phone stalls its bulk endpoint whenever it wants out of a transfer -- its
// MTP daemon hits an internal timeout when we throttle the data phase to
// whatever the nand can absorb. That is a normal, recoverable USB condition:
// cancel the transaction, clear the halts, carry on. Tearing the interface
// down instead (what this used to do) made the phone drop off the bus
// entirely, which is why every install died a second or two in.
bool RecoverLinkLocked(u32 transaction_id, const char* why) {
    if (!g_session.connected) {
        return false;
    }

    log_write("[MTP_HOST] recovering link: %s\n", why);

    if (transaction_id) {
        CancelTransactionLocked(transaction_id);
    }

    bool ok = true;
    for (auto* ep : {&g_session.ep_in, &g_session.ep_out}) {
        if (IsEndpointHaltedLocked(ep) && R_FAILED(ClearEndpointHaltLocked(ep))) {
            ok = false;
        }
    }

    if (!ok) {
        CloseUsbLocked("endpoint halt could not be cleared");
        return false;
    }

    g_stream = {};
    return true;
}

// Posts g_xfer_buf to the endpoint and waits for it to complete. `size` must
// be <= XFER_BUF_SIZE; for IN transfers it also has to be at least as large as
// what the device is about to send, otherwise the controller reports babble.
Result PostBuffer(UsbHsClientEpSession* ep, u32 size, u32* out_transferred) {
    if (out_transferred) {
        *out_transferred = 0;
    }

    R_UNLESS(size && size <= XFER_BUF_SIZE, ResultProtocol);

    u32 xfer_id{};
    if (const auto rc = usbHsEpPostBufferAsync(ep, g_xfer_buf, size, 0, &xfer_id); R_FAILED(rc)) {
        log_write("[MTP_HOST] post %u bytes failed: 0x%X\n", size, rc);
        R_THROW(rc);
    }

    auto* evt = usbHsEpGetXferEvent(ep);
    if (const auto rc = eventWait(evt, XFER_TIMEOUT_NS); R_FAILED(rc)) {
        log_write("[MTP_HOST] transfer timed out: 0x%X\n", rc);
        R_THROW(rc);
    }
    eventClear(evt);

    UsbHsXferReport reports[4]{};
    u32 count{};
    if (const auto rc = usbHsEpGetXferReport(ep, reports, std::size(reports), &count); R_FAILED(rc)) {
        log_write("[MTP_HOST] xfer report failed: 0x%X\n", rc);
        R_THROW(rc);
    }

    for (u32 i = 0; i < count; i++) {
        if (reports[i].xferId != xfer_id) {
            continue;
        }

        if (R_FAILED(reports[i].res)) {
            log_write("[MTP_HOST] transfer failed: 0x%X (%u/%u bytes)\n",
                reports[i].res, reports[i].transferredSize, size);
            R_THROW(reports[i].res);
        }

        if (out_transferred) {
            *out_transferred = reports[i].transferredSize;
        }
        R_SUCCEED();
    }

    log_write("[MTP_HOST] no xfer report for id %u (got %u)\n", xfer_id, count);
    R_THROW(ResultTransport);
}

// -------------------------------------------------------------------------
// protocol
// -------------------------------------------------------------------------

// Destination for a data phase. Either grows a vector (datasets) or fills a
// caller supplied buffer (file reads, which must not take an extra copy).
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

    // Anything past the sink's capacity is counted but dropped; callers check
    // Written() against what they asked for.
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

Result SendCommand(u16 code, std::span<const u32> params, u32* out_transaction_id) {
    R_UNLESS(params.size() <= 5, ResultProtocol);

    const u32 length = sizeof(ContainerHeader) + static_cast<u32>(params.size_bytes());

    ContainerHeader hdr{};
    hdr.length = length;
    hdr.type = CONTAINER_COMMAND;
    hdr.code = code;
    hdr.transaction_id = g_session.transaction_id++;
    std::memcpy(g_xfer_buf, &hdr, sizeof(hdr));
    if (!params.empty()) {
        std::memcpy(g_xfer_buf + sizeof(hdr), params.data(), params.size_bytes());
    }

    *out_transaction_id = hdr.transaction_id;

    u32 transferred{};
    R_TRY(PostBuffer(&g_session.ep_out, length, &transferred));
    R_UNLESS(transferred == length, ResultTransport);
    R_SUCCEED();
}

// Reads one container into g_xfer_buf. `post` must cover whatever the device
// is about to send. A zero length transfer is the terminating packet of the
// previous phase and is skipped.
Result ReceiveContainer(u32 post, ContainerHeader* out_hdr, u32* out_transferred) {
    for (int attempt = 0; attempt < 2; attempt++) {
        u32 transferred{};
        R_TRY(PostBuffer(&g_session.ep_in, post, &transferred));

        if (!transferred) {
            continue;
        }

        R_UNLESS(transferred >= sizeof(ContainerHeader), ResultProtocol);
        std::memcpy(out_hdr, g_xfer_buf, sizeof(*out_hdr));
        *out_transferred = transferred;
        R_SUCCEED();
    }

    R_THROW(ResultTransport);
}

Result ReceiveDataPhase(const ContainerHeader& hdr, u32 first_transferred, DataSink* sink) {
    R_UNLESS(hdr.length >= sizeof(ContainerHeader), ResultProtocol);

    const u32 payload_len = hdr.length - sizeof(ContainerHeader);
    if (sink) {
        sink->Reset(payload_len);
    }

    u32 copied = 0;
    if (first_transferred > sizeof(ContainerHeader)) {
        const u32 n = std::min<u32>(payload_len, first_transferred - sizeof(ContainerHeader));
        if (sink) {
            sink->Append(g_xfer_buf + sizeof(ContainerHeader), n);
        }
        copied = n;
    }

    while (copied < payload_len) {
        const u32 want = std::min<u32>(payload_len - copied, XFER_BUF_SIZE);
        u32 transferred{};
        R_TRY(PostBuffer(&g_session.ep_in, AlignUp(want, XFER_ALIGN), &transferred));
        R_UNLESS(transferred, ResultTransport);

        const u32 n = std::min(want, transferred);
        if (sink) {
            sink->Append(g_xfer_buf, n);
        }
        copied += n;
    }

    R_SUCCEED();
}

void AbortStreamLocked();

// Runs one complete MTP transaction. `out_code` receives the responder's reply
// code even when it is an error, so callers can tell "no such object" apart
// from "the link is gone". Any transport level failure kills the session.
Result Transact(u16 op, std::span<const u32> params, DataSink* sink, u16* out_code) {
    if (out_code) {
        *out_code = 0;
    }

    R_UNLESS(g_session.connected, ResultNoSession);

    // a command must not go out while a read stream's data phase is in
    // flight; retire the stream first (this may drop the session).
    AbortStreamLocked();
    R_UNLESS(g_session.connected, ResultNoSession);

    u32 transaction_id{};
    if (const auto rc = SendCommand(op, params, &transaction_id); R_FAILED(rc)) {
        RecoverLinkLocked(transaction_id, "command phase failed");
        R_THROW(rc);
    }

    ContainerHeader hdr{};
    u32 transferred{};
    // The responder answers with either a data phase or, when the operation
    // has no data or failed outright, the response straight away. Post the
    // full buffer since the size is not known until the header arrives.
    if (const auto rc = ReceiveContainer(XFER_BUF_SIZE, &hdr, &transferred); R_FAILED(rc)) {
        RecoverLinkLocked(transaction_id, "data phase failed");
        R_THROW(rc);
    }

    if (hdr.type == CONTAINER_DATA) {
        if (const auto rc = ReceiveDataPhase(hdr, transferred, sink); R_FAILED(rc)) {
            RecoverLinkLocked(transaction_id, "truncated data phase");
            R_THROW(rc);
        }

        if (const auto rc = ReceiveContainer(RESPONSE_POST_SIZE, &hdr, &transferred); R_FAILED(rc)) {
            RecoverLinkLocked(transaction_id, "response phase failed");
            R_THROW(rc);
        }
    }

    if (hdr.type != CONTAINER_RESPONSE) {
        RecoverLinkLocked(transaction_id, "unexpected container type");
        R_THROW(ResultProtocol);
    }

    // A responder that answers the wrong transaction is one phase out of step
    // and everything read after this point would be garbage. Some devices
    // reply with 0, which is tolerated.
    if (hdr.transaction_id && hdr.transaction_id != transaction_id) {
        CloseUsbLocked("response transaction id mismatch");
        R_THROW(ResultProtocol);
    }

    g_session.last_ok_tick = armGetSystemTick();

    if (out_code) {
        *out_code = hdr.code;
    }
    R_UNLESS(hdr.code == RESP_OK, ResultMtpFailed);
    R_SUCCEED();
}

Result TransactData(u16 op, std::span<const u32> params, std::vector<u8>* out, u16* out_code = nullptr) {
    DataSink sink{out};
    return Transact(op, params, &sink, out_code);
}

Result TransactNoData(u16 op, std::span<const u32> params, u16* out_code = nullptr) {
    return Transact(op, params, nullptr, out_code);
}

// -------------------------------------------------------------------------
// streaming reads (see ReadStream above)
// -------------------------------------------------------------------------

// Reads the closing response container once a stream's data phase is done.
Result FinishStreamLocked() {
    ContainerHeader hdr{};
    u32 transferred{};
    R_TRY(ReceiveContainer(RESPONSE_POST_SIZE, &hdr, &transferred));
    R_UNLESS(hdr.type == CONTAINER_RESPONSE, ResultProtocol);
    g_session.last_ok_tick = armGetSystemTick();
    R_UNLESS(hdr.code == RESP_OK, ResultMtpFailed);
    R_SUCCEED();
}

// Pulls up to `want` payload bytes from the active stream into dst (nullptr
// discards). Reads the closing response as soon as the data phase drains, so
// an inactive stream never leaves anything in flight. A transport failure
// drops the session.
Result PullStreamLocked(u8* dst, u64 want, u64* out_got) {
    u64 got = 0;
    *out_got = 0;

    if (g_stream.carry_len) {
        const u64 n = std::min<u64>(want, g_stream.carry_len);
        if (dst) {
            std::memcpy(dst, g_stream.carry + g_stream.carry_off, n);
        }
        g_stream.carry_off += n;
        g_stream.carry_len -= n;
        got += n;
    }

    while (got < want && g_stream.remaining) {
        const u64 n = std::min<u64>(std::min<u64>(want - got, g_stream.remaining), MAX_READ_CHUNK);
        const u32 post = AlignUp(static_cast<u32>(n), 512);

        u32 transferred{};
        if (const auto rc = PostBuffer(&g_session.ep_in, post, &transferred); R_FAILED(rc)) {
            RecoverLinkLocked(g_stream.transaction_id, "stream pull failed");
            R_THROW(rc);
        }
        if (!transferred) {
            RecoverLinkLocked(g_stream.transaction_id, "stream pull returned nothing");
            R_THROW(ResultTransport);
        }

        const u64 payload = std::min<u64>(transferred, g_stream.remaining);
        const u64 to_caller = std::min<u64>(payload, want - got);
        if (dst) {
            std::memcpy(dst + got, g_xfer_buf, to_caller);
        }
        got += to_caller;

        if (payload > to_caller) {
            // the post was packet aligned, so at most one packet of overshoot.
            g_stream.carry_len = static_cast<u32>(payload - to_caller);
            g_stream.carry_off = 0;
            std::memcpy(g_stream.carry, g_xfer_buf + to_caller, g_stream.carry_len);
        }

        g_stream.remaining -= payload;

        // a short transfer with payload still owed means the device ended the
        // container early; trust the wire over the header.
        if (transferred < post && g_stream.remaining) {
            g_stream.remaining = 0;
        }
    }

    g_stream.next_offset += got;

    if (!g_stream.remaining && !g_stream.response_done) {
        g_stream.response_done = true;
        if (const auto rc = FinishStreamLocked(); R_FAILED(rc)) {
            RecoverLinkLocked(g_stream.transaction_id, "stream response failed");
            R_THROW(rc);
        }
    }
    if (!g_stream.remaining && !g_stream.carry_len) {
        g_stream.active = false;
    }

    *out_got = got;
    R_SUCCEED();
}

// Retires the active stream before another command may go out. Cheap when it
// already drained; a small remainder is pulled and discarded; a large one is
// cancelled on the control pipe rather than dragged off the bus.
void AbortStreamLocked() {
    if (!g_stream.active) {
        return;
    }

    if (!g_stream.response_done) {
        if (g_stream.remaining > STREAM_DRAIN_LIMIT) {
            RecoverLinkLocked(g_stream.transaction_id, "cancelling large stream");
            g_stream = {};
            return;
        }
        g_stream.carry_len = 0;
        u64 got{};
        if (R_FAILED(PullStreamLocked(nullptr, g_stream.remaining, &got))) {
            return; // the failed pull already recovered or dropped the link
        }
    }

    g_stream = {};
}

// Opens a data phase at `offset`. Small reads (header and metadata parsing
// seek around, then never come back) get an exactly sized stream that always
// completes without an abort; large sequential readers stream the rest of the
// file in a single transaction.
Result StartStreamLocked(u32 handle, u64 offset, u64 file_size, u64 want) {
    R_UNLESS(g_session.connected, ResultNoSession);

    u64 req = file_size - offset;
    if (want < 1024 * 512) {
        req = std::min<u64>(req, std::max<u64>(want, 1024 * 64));
    }
    // Cap the transaction: a completed data phase is cleaner than a cancelled
    // one, and the phone stalls the endpoint if we throttle a huge one to the
    // speed the nand can absorb. 32 MiB still means ~64 posts per command.
    req = std::min<u64>(req, 32 * 1024 * 1024);

    u16 op = OP_GET_PARTIAL_OBJECT_64;
    u32 params[4]{handle, static_cast<u32>(offset), static_cast<u32>(offset >> 32), static_cast<u32>(req)};
    size_t param_count = 4;

    if (!g_session.has_partial64) {
        // 32 bit offsets only; the caller verified some partial op exists.
        R_UNLESS(offset < SIZE_NEEDS_64BIT, ResultProtocol);
        req = std::min<u64>(req, SIZE_NEEDS_64BIT - offset);
        op = OP_GET_PARTIAL_OBJECT;
        params[1] = static_cast<u32>(offset);
        params[2] = static_cast<u32>(req);
        param_count = 3;
    }

    u32 transaction_id{};
    if (const auto rc = SendCommand(op, {params, param_count}, &transaction_id); R_FAILED(rc)) {
        RecoverLinkLocked(transaction_id, "stream command failed");
        R_THROW(rc);
    }

    // the first packet carries the container header plus the first payload
    // bytes; anything past the header goes to the carry buffer.
    ContainerHeader hdr{};
    u32 transferred{};
    if (const auto rc = ReceiveContainer(512, &hdr, &transferred); R_FAILED(rc)) {
        RecoverLinkLocked(transaction_id, "stream data phase failed");
        R_THROW(rc);
    }

    if (hdr.type == CONTAINER_RESPONSE) {
        // no data phase: the device rejected the request (stale handle, ...).
        g_session.last_ok_tick = armGetSystemTick();
        R_THROW(ResultMtpFailed);
    }
    if (hdr.type != CONTAINER_DATA || hdr.length < sizeof(hdr)) {
        RecoverLinkLocked(transaction_id, "stream got bad container");
        R_THROW(ResultProtocol);
    }

    g_stream.active = true;
    g_stream.response_done = false;
    g_stream.handle = handle;
    g_stream.transaction_id = transaction_id;
    g_stream.next_offset = offset;
    g_stream.remaining = hdr.length - sizeof(hdr); // device may grant less than req
    g_stream.carry_off = 0;
    g_stream.carry_len = 0;

    if (transferred > sizeof(hdr)) {
        g_stream.carry_len = std::min<u64>(transferred - sizeof(hdr), g_stream.remaining);
        std::memcpy(g_stream.carry, g_xfer_buf + sizeof(hdr), g_stream.carry_len);
        g_stream.remaining -= g_stream.carry_len;
    }

    R_SUCCEED();
}

// -------------------------------------------------------------------------
// dataset parsing
// -------------------------------------------------------------------------

bool ParseObjectInfo(std::span<const u8> data, u32 handle, MtpObject* out) {
    Reader r{data};

    u16 format{};
    u32 compressed_size{};

    r.Skip(4);                  // StorageID
    r.Read(&format);
    r.Skip(2);                  // ProtectionStatus
    r.Read(&compressed_size);
    r.Skip(2 + 4 + 4 + 4);      // Thumb{Format,CompressedSize,PixWidth,PixHeight}
    r.Skip(4 + 4 + 4);          // Image{PixWidth,PixHeight,BitDepth}
    r.Skip(4);                  // ParentObject
    r.Skip(2 + 4 + 4);          // Association{Type,Desc}, SequenceNumber

    std::string filename;
    r.ReadString(&filename);

    if (!r.Ok()) {
        return false;
    }

    out->handle = handle;
    out->format = format;
    out->size = compressed_size;
    out->is_dir = format == FORMAT_ASSOCIATION;
    out->filename = std::move(filename);

    if (out->filename.empty()) {
        char fallback[32];
        std::snprintf(fallback, sizeof(fallback), "object_%08x", handle);
        out->filename = fallback;
    }

    return true;
}

// ObjectInfo tops out at 4GiB, so anything at the cap needs the real size from
// the object property. Leaves the size alone when the device cannot answer.
void ResolveLargeSize(MtpObject* obj) {
    if (obj->is_dir || obj->size != SIZE_NEEDS_64BIT || !g_session.has_prop_value) {
        return;
    }

    const u32 params[]{obj->handle, PROP_OBJECT_SIZE};
    std::vector<u8> data;
    if (R_FAILED(TransactData(OP_GET_OBJECT_PROP_VALUE, params, &data))) {
        return;
    }

    u64 size{};
    Reader r{data};
    if (r.Read(&size) && r.Ok()) {
        obj->size = size;
    }
}

// DeviceInfo tells us which read operation to use. Everything else in the
// dataset is skipped past; the strings are variable length so they cannot be
// jumped over by offset.
Result QueryDeviceCapabilities() {
    std::vector<u8> data;
    R_TRY(TransactData(OP_GET_DEVICE_INFO, {}, &data));

    Reader r{data};
    r.Skip(2);              // StandardVersion
    r.Skip(4);              // VendorExtensionID
    r.Skip(2);              // VendorExtensionVersion
    r.ReadString(nullptr);  // VendorExtensionDesc
    r.Skip(2);              // FunctionalMode

    std::vector<u16> ops;
    r.ReadArray(&ops);
    R_UNLESS(r.Ok(), ResultProtocol);

    const auto has = [&ops](u16 op) {
        return std::ranges::find(ops, op) != ops.end();
    };

    g_session.has_partial = has(OP_GET_PARTIAL_OBJECT);
    g_session.has_partial64 = has(OP_GET_PARTIAL_OBJECT_64);
    g_session.has_prop_value = has(OP_GET_OBJECT_PROP_VALUE);

    log_write("[MTP_HOST] capabilities: partial=%d partial64=%d prop_value=%d\n",
        g_session.has_partial, g_session.has_partial64, g_session.has_prop_value);

    R_SUCCEED();
}

// -------------------------------------------------------------------------
// mount bookkeeping
// -------------------------------------------------------------------------

// A devoptab mount outlives the USB session on purpose. The file browser hands
// raw Device pointers to every open DIR and FILE, so unmounting while the user
// is inside the phone is a use-after-free -- which is what turned a flaky
// cable into a crash. On reconnect the existing device is rebound to the new
// storage instead.
//
// The browser does unmount everything when its menu is destroyed, so `device`
// is only safe to touch while devoptab still knows about `config.url`.
struct MountRecord {
    common::MountConfig config{};
    MtpMountDevice* device{};
};

// Guards g_mounts only. Deliberately not g_mutex: this is held across
// MountNetworkDevice2, which takes the devoptab rwlock, so it sits outside
// that lock while g_mutex sits inside it.
Mutex g_mount_mutex{};
std::vector<MountRecord> g_mounts{};

MtpMountDevice* LiveDevice(const MountRecord& rec) {
    return common::IsNetworkDeviceMounted(rec.config.url) ? rec.device : nullptr;
}

// -------------------------------------------------------------------------
// path helpers
// -------------------------------------------------------------------------

// "mtp0:/Android/data/" -> "Android/data", "mtp0:/" -> "".
std::string NormalisePath(const char* path) {
    if (!path) {
        return {};
    }

    std::string out = path;
    if (const auto colon = out.find(':'); colon != std::string::npos) {
        out.erase(0, colon + 1);
    }

    // collapse repeated slashes and drop "." components.
    std::string cleaned;
    cleaned.reserve(out.size());
    for (size_t i = 0; i < out.size();) {
        if (out[i] == '/') {
            i++;
            continue;
        }

        const auto end = out.find('/', i);
        const auto component = out.substr(i, end == std::string::npos ? std::string::npos : end - i);
        if (component != ".") {
            if (!cleaned.empty()) {
                cleaned += '/';
            }
            cleaned += component;
        }

        if (end == std::string::npos) {
            break;
        }
        i = end + 1;
    }

    return cleaned;
}

void SplitPath(const std::string& path, std::string* parent, std::string* name) {
    const auto slash = path.rfind('/');
    if (slash == std::string::npos) {
        parent->clear();
        *name = path;
    } else {
        *parent = path.substr(0, slash);
        *name = path.substr(slash + 1);
    }
}

} // namespace

// -------------------------------------------------------------------------
// MtpMountDevice
// -------------------------------------------------------------------------

MtpMountDevice::MtpMountDevice(const common::MountConfig& config, u32 storage_id, u64 capacity, u64 free_space)
: MountDevice{config}
, m_storage_id{storage_id}
, m_capacity{capacity}
, m_free_space{free_space} {
}

void MtpMountDevice::Rebind(u32 storage_id, u64 capacity, u64 free_space) {
    SCOPED_MUTEX(&g_mutex);
    m_storage_id = storage_id;
    m_capacity = capacity;
    m_free_space = free_space;
    m_generation = g_session.generation;
    DropCaches();
}

void MtpMountDevice::DropCaches() {
    m_dir_cache.clear();
    m_obj_cache.clear();
}

bool MtpMountDevice::Mount() {
    return true;
}

void MtpMountDevice::SyncGenerationLocked() {
    if (m_generation != g_session.generation) {
        DropCaches();
        m_generation = g_session.generation;
    }
}

bool MtpMountDevice::RefreshFileLocked(MtpFileHandle* file) {
    SyncGenerationLocked();
    if (file->generation == m_generation) {
        return true;
    }

    // the session reconnected since this handle was resolved. MTP object
    // handles are per session, so look the path up again on the new one.
    MtpObject obj{};
    if (!LookupLocked(file->path, &obj) || obj.is_dir) {
        return false;
    }

    file->object_handle = obj.handle;
    file->generation = m_generation;
    return true;
}

bool MtpMountDevice::Lookup(const std::string& path, MtpObject* out) {
    SCOPED_MUTEX(&g_mutex);
    SyncGenerationLocked();
    return LookupLocked(path, out);
}

bool MtpMountDevice::List(const std::string& path, std::vector<MtpObject>* out) {
    SCOPED_MUTEX(&g_mutex);
    SyncGenerationLocked();
    return ListLocked(path, out);
}

// EIO tells the browser the phone went away, ENOENT that it simply has no such
// file. Reporting the latter for a dead link is what made a dropped cable look
// like an empty device.
int MtpMountDevice::LookupErrno() const {
    SCOPED_MUTEX(&g_mutex);
    return g_session.connected ? ENOENT : EIO;
}

bool MtpMountDevice::LookupLocked(const std::string& path, MtpObject* out) {
    if (path.empty()) {
        *out = MtpObject{.handle = HANDLE_ROOT, .is_dir = true};
        return true;
    }

    if (const auto it = m_obj_cache.find(path); it != m_obj_cache.end()) {
        *out = it->second;
        return true;
    }

    // Listing the parent caches every sibling, so the entry we want is present
    // afterwards unless it genuinely does not exist.
    std::string parent, name;
    SplitPath(path, &parent, &name);
    if (!ListLocked(parent, nullptr)) {
        return false;
    }

    const auto it = m_obj_cache.find(path);
    if (it == m_obj_cache.end()) {
        return false;
    }

    *out = it->second;
    return true;
}

bool MtpMountDevice::ListLocked(const std::string& path, std::vector<MtpObject>* out) {
    if (const auto it = m_dir_cache.find(path); it != m_dir_cache.end()) {
        if (out) {
            *out = it->second;
        }
        return true;
    }

    MtpObject dir{};
    if (!LookupLocked(path, &dir) || !dir.is_dir) {
        return false;
    }

    if (!EnsureSessionLocked()) {
        return false;
    }

    const u32 handle_params[]{m_storage_id, 0, dir.handle};
    std::vector<u8> data;
    if (R_FAILED(TransactData(OP_GET_OBJECT_HANDLES, handle_params, &data))) {
        // One shot at bringing a dropped link back: phones renegotiate USB on
        // screen lock, which kills the session mid-browse. After a real
        // re-enumeration dir.handle may be stale, in which case this fails
        // again and the next visit re-resolves from the (dropped) caches.
        if (!EnsureSessionLocked() ||
            R_FAILED(TransactData(OP_GET_OBJECT_HANDLES, handle_params, &data))) {
            return false;
        }
    }

    std::vector<u32> handles;
    Reader r{data};
    if (!r.ReadArray(&handles) || !r.Ok()) {
        log_write("[MTP_HOST] malformed GetObjectHandles reply for '%s'\n", path.c_str());
        return false;
    }

    std::vector<MtpObject> entries;
    entries.reserve(handles.size());

    for (const auto handle : handles) {
        const u32 info_params[]{handle};
        std::vector<u8> info;
        if (R_FAILED(TransactData(OP_GET_OBJECT_INFO, info_params, &info))) {
            // The link is gone; a partial listing would look like a phone that
            // lost half its files, so give up on the whole directory.
            if (!g_session.connected) {
                return false;
            }
            continue;
        }

        MtpObject obj{};
        if (!ParseObjectInfo(info, handle, &obj)) {
            continue;
        }

        ResolveLargeSize(&obj);
        entries.push_back(std::move(obj));
    }

    // cap the cache: a deep browse should not grow it without bound.
    if (m_dir_cache.size() >= 64) {
        DropCaches();
    }

    for (const auto& e : entries) {
        m_obj_cache[path.empty() ? e.filename : path + "/" + e.filename] = e;
    }
    const auto& cached = (m_dir_cache[path] = std::move(entries));

    log_write("[MTP_HOST] listed '%s': %zu entries\n", path.c_str(), cached.size());

    if (out) {
        *out = cached;
    }
    return true;
}

int MtpMountDevice::devoptab_diropen(void* fd, const char *path) {
    // devoptab hands us calloc'd storage, so the vector member needs its
    // constructor run before anything touches it.
    auto* dir = new (fd) MtpDirHandle();

    if (!List(NormalisePath(path), &dir->entries)) {
        // devoptab_common frees this allocation without routing back through
        // dirclose, so the vector has to be destroyed here or it leaks.
        const auto err = LookupErrno();
        dir->~MtpDirHandle();
        return -err;
    }

    dir->index = 0;
    return 0;
}

int MtpMountDevice::devoptab_dirreset(void* fd) {
    static_cast<MtpDirHandle*>(fd)->index = 0;
    return 0;
}

int MtpMountDevice::devoptab_dirnext(void* fd, char *filename, struct stat *filestat) {
    auto* dir = static_cast<MtpDirHandle*>(fd);
    if (dir->index >= dir->entries.size()) {
        return -ENOENT;
    }

    const auto& entry = dir->entries[dir->index++];
    std::snprintf(filename, NAME_MAX + 1, "%s", entry.filename.c_str());

    if (filestat) {
        // newlib turns st_mode into dirent::d_type, so getting this right is
        // what keeps the browser from dropping every row as DT_UNKNOWN.
        std::memset(filestat, 0, sizeof(*filestat));
        filestat->st_nlink = 1;
        filestat->st_size = entry.size;
        filestat->st_blksize = STDIO_BLOCK_SIZE;
        filestat->st_mode = entry.is_dir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    }

    return 0;
}

int MtpMountDevice::devoptab_dirclose(void* fd) {
    static_cast<MtpDirHandle*>(fd)->~MtpDirHandle();
    return 0;
}

int MtpMountDevice::devoptab_lstat(const char *path, struct stat *st) {
    MtpObject obj{};
    if (!Lookup(NormalisePath(path), &obj)) {
        return -LookupErrno();
    }

    std::memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_blksize = STDIO_BLOCK_SIZE;
    if (obj.is_dir) {
        st->st_mode = S_IFDIR | 0555;
    } else {
        st->st_mode = S_IFREG | 0444;
        st->st_size = obj.size;
    }

    return 0;
}

int MtpMountDevice::devoptab_open(void *fileStruct, const char *path, int flags, int mode) {
    SCOPED_MUTEX(&g_mutex);
    SyncGenerationLocked();

    const auto norm = NormalisePath(path);
    MtpObject obj{};
    if (!LookupLocked(norm, &obj)) {
        // g_mutex is held, so LookupErrno() (which takes it) would deadlock.
        return g_session.connected ? -ENOENT : -EIO;
    }

    if (obj.is_dir) {
        return -EISDIR;
    }

    // calloc'd storage: run the constructor only on the success path, the
    // common wrapper frees the allocation without calling close on failure.
    auto* file = new (fileStruct) MtpFileHandle();
    file->object_handle = obj.handle;
    file->size = obj.size;
    file->generation = m_generation;
    file->path = norm;
    return 0;
}

int MtpMountDevice::devoptab_close(void *fd) {
    static_cast<MtpFileHandle*>(fd)->~MtpFileHandle();
    return 0;
}

int MtpMountDevice::devoptab_fstat(void *fd, struct stat *st) {
    const auto* file = static_cast<MtpFileHandle*>(fd);
    std::memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_mode = S_IFREG | 0444;
    st->st_size = file->size;
    st->st_blksize = STDIO_BLOCK_SIZE;
    return 0;
}

ssize_t MtpMountDevice::devoptab_seek(void *fd, off_t pos, int dir) {
    auto* file = static_cast<MtpFileHandle*>(fd);

    s64 target{};
    switch (dir) {
        case SEEK_SET: target = pos; break;
        case SEEK_CUR: target = static_cast<s64>(file->offset) + pos; break;
        case SEEK_END: target = static_cast<s64>(file->size) + pos; break;
        default: return -EINVAL;
    }

    if (target < 0) {
        return -EINVAL;
    }

    file->offset = target;
    return static_cast<ssize_t>(file->offset);
}

ssize_t MtpMountDevice::devoptab_read(void *fd, char *ptr, size_t len) {
    auto* file = static_cast<MtpFileHandle*>(fd);
    if (!file->object_handle) {
        return -EBADF;
    }

    SCOPED_MUTEX(&g_mutex);
    if (!EnsureSessionLocked() || !RefreshFileLocked(file)) {
        return -EIO;
    }

    if (file->offset >= file->size) {
        return 0;
    }

    const u64 want = std::min<u64>(len, file->size - file->offset);

    if (!g_session.has_partial64 && !g_session.has_partial) {
        // No partial read support at all. Whole-object reads are only viable
        // for something that fits in the transfer buffer; anything else has
        // no sane access path.
        if (!file->offset && file->size <= MAX_READ_CHUNK) {
            DataSink sink{ptr, want};
            const u32 params[]{file->object_handle};
            if (R_FAILED(Transact(OP_GET_OBJECT, params, &sink, nullptr))) {
                return -EIO;
            }
            const auto got = std::min<u64>(sink.Written(), want);
            file->offset += got;
            return static_cast<ssize_t>(got);
        }
        log_write("[MTP_HOST] device cannot serve partial reads for a %llu byte object\n",
            static_cast<unsigned long long>(file->size));
        return -ENOTSUP;
    }

    u64 done = 0;
    bool retried = false;

    // Callers up the stack (yati's nsp/nca parsers) read headers at exact
    // offsets and cannot cope with a short read, so serve the full request.
    while (done < want) {
        const u64 offset = file->offset + done;
        Result rc{};

        // line the stream up with this read: reuse it when it sits at (or
        // shortly before) the wanted offset, retire it otherwise.
        if (g_stream.active) {
            const u64 avail = g_stream.remaining + g_stream.carry_len;
            const bool usable = g_stream.handle == file->object_handle &&
                offset >= g_stream.next_offset &&
                offset - g_stream.next_offset <= std::min<u64>(avail, STREAM_DRAIN_LIMIT);

            if (!usable) {
                AbortStreamLocked();
            } else if (offset > g_stream.next_offset) {
                u64 skipped{};
                rc = PullStreamLocked(nullptr, offset - g_stream.next_offset, &skipped);
            }
        }

        if (R_SUCCEEDED(rc) && (!g_stream.active || g_stream.next_offset != offset)) {
            AbortStreamLocked();
            rc = StartStreamLocked(file->object_handle, offset, file->size, want - done);
        }

        u64 got{};
        if (R_SUCCEEDED(rc)) {
            rc = PullStreamLocked(reinterpret_cast<u8*>(ptr) + done, want - done, &got);
        }

        if (R_FAILED(rc)) {
            // one reconnect attempt, otherwise a phone that blinked
            // mid-transfer aborts the whole install. RefreshFileLocked
            // re-resolves the handle -- the old one died with the session.
            if (!retried && EnsureSessionLocked() && RefreshFileLocked(file)) {
                retried = true;
                continue;
            }
            file->offset += done;
            return done ? static_cast<ssize_t>(done) : -EIO;
        }

        if (!got) {
            break; // device granted less than requested and the stream ended
        }
        done += got;
    }

    file->offset += done;
    return static_cast<ssize_t>(done);
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

// -------------------------------------------------------------------------
// session management
// -------------------------------------------------------------------------

namespace {

struct StorageEntry {
    u32 id{};
    u64 capacity{};
    u64 free_space{};
    std::string label{};
};

// Picks the first bulk endpoint in each direction. MTP interfaces also expose
// an interrupt IN endpoint for events, so the descriptors cannot be indexed
// blindly.
const usb_endpoint_descriptor* FindBulkEndpoint(const usb_endpoint_descriptor* descs, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!descs[i].bLength || !descs[i].wMaxPacketSize) {
            continue;
        }
        if ((descs[i].bmAttributes & USB_TRANSFER_TYPE_MASK) == USB_TRANSFER_TYPE_BULK) {
            return &descs[i];
        }
    }
    return nullptr;
}

Result OpenSessionOnInterface(const UsbHsInterface& iface) {
    UsbHsInterface copy = iface;
    R_TRY(usbHsAcquireUsbIf(&g_session.iface, &copy));

    auto close_iface = [] { usbHsIfClose(&g_session.iface); g_session.iface = {}; };

    const auto& inf = g_session.iface.inf.inf;
    const auto* in_desc = FindBulkEndpoint(inf.input_endpoint_descs, std::size(inf.input_endpoint_descs));
    const auto* out_desc = FindBulkEndpoint(inf.output_endpoint_descs, std::size(inf.output_endpoint_descs));

    if (!in_desc || !out_desc) {
        log_write("[MTP_HOST] interface has no bulk endpoint pair\n");
        close_iface();
        R_THROW(ResultProtocol);
    }

    // maxXferSize has to cover the biggest post we will ever make, otherwise
    // usb:hs rejects the transfer.
    const auto rc_out = usbHsIfOpenUsbEp(&g_session.iface, &g_session.ep_out, 1, XFER_BUF_SIZE,
        const_cast<usb_endpoint_descriptor*>(out_desc));
    const auto rc_in = usbHsIfOpenUsbEp(&g_session.iface, &g_session.ep_in, 1, XFER_BUF_SIZE,
        const_cast<usb_endpoint_descriptor*>(in_desc));

    if (R_FAILED(rc_out) || R_FAILED(rc_in)) {
        log_write("[MTP_HOST] opening endpoints failed (out=0x%X in=0x%X)\n", rc_out, rc_in);
        if (R_SUCCEEDED(rc_out)) {
            usbHsEpClose(&g_session.ep_out);
        }
        if (R_SUCCEEDED(rc_in)) {
            usbHsEpClose(&g_session.ep_in);
        }
        g_session.ep_in = {};
        g_session.ep_out = {};
        close_iface();
        R_THROW(R_FAILED(rc_out) ? rc_out : rc_in);
    }

    g_session.connected = true;
    g_session.transaction_id = 1;

    const u32 session_id[]{1};
    if (const auto rc = TransactNoData(OP_OPEN_SESSION, session_id); R_FAILED(rc)) {
        log_write("[MTP_HOST] OpenSession failed: 0x%X\n", rc);
        CloseUsbLocked("OpenSession failed");
        R_THROW(rc);
    }

    if (R_FAILED(QueryDeviceCapabilities())) {
        // Losing the link here is fatal, but a responder that merely refuses
        // GetDeviceInfo is still worth mounting: assume the partial read every
        // real implementation supports and let it fail per request if not.
        R_UNLESS(g_session.connected, ResultTransport);
        log_write("[MTP_HOST] DeviceInfo unavailable, assuming GetPartialObject\n");
        g_session.has_partial = true;
    }

    log_write("[MTP_HOST] session open (vid=0x%04x pid=0x%04x)\n",
        iface.device_desc.idVendor, iface.device_desc.idProduct);
    R_SUCCEED();
}

// Finds an MTP responder on the bus and opens a session on it. usb:hs filters
// on the interface descriptor, so the still image / MTP triple is enough to
// skip past adb, audio and every other interface a phone exposes -- talking
// MTP to one of those used to leave the endpoints in a state that took the
// system usb service down with it.
Result ConnectLocked() {
    // usbHsInitialize refcounts, and nothing here ever calls usbHsExit -- the
    // service stays up for the life of the app and libusbhsfs holds it too --
    // so initialising once keeps the count from creeping up per reconnect.
    static bool usbhs_ready{};
    if (!usbhs_ready) {
        R_TRY(usbHsInitialize());
        usbhs_ready = true;
    }

    UsbHsInterfaceFilter filter{};
    filter.Flags = UsbHsInterfaceFilterFlags_bInterfaceClass
                 | UsbHsInterfaceFilterFlags_bInterfaceSubClass
                 | UsbHsInterfaceFilterFlags_bInterfaceProtocol;
    filter.bInterfaceClass = USB_CLASS_IMAGE;
    filter.bInterfaceSubClass = 0x01; // still image capture
    filter.bInterfaceProtocol = 0x01; // picture transfer protocol

    UsbHsInterface interfaces[4]{};
    s32 total{};

    // usb:hs only lists interfaces nobody has acquired, so an interface we just
    // released after a link failure can take a moment to reappear. Without the
    // second look the phone would vanish from the browser for one refresh and
    // come back on the next, which is exactly how the flapping looked.
    for (int attempt = 0; attempt < 2 && total <= 0; attempt++) {
        if (attempt) {
            svcSleepThread(100000000ULL);
        }
        R_TRY(usbHsQueryAvailableInterfaces(&filter, interfaces, sizeof(interfaces), &total));
    }

    if (total <= 0) {
        log_write("[MTP_HOST] no MTP interface on the bus\n");
        R_THROW(ResultMtpFailed);
    }

    for (s32 i = 0; i < total; i++) {
        if (R_SUCCEEDED(OpenSessionOnInterface(interfaces[i]))) {
            g_session.generation++;
            R_SUCCEED();
        }
    }

    R_THROW(ResultMtpFailed);
}

bool EnsureSessionLocked() {
    // ponytail: assumes storage ids survive re-enumeration (true on Android:
    // the id encodes storage type + index). If a device hands out fresh ids,
    // add a storage re-resolve here.
    return g_session.connected || R_SUCCEEDED(ConnectLocked());
}

Result ListStoragesLocked(std::vector<StorageEntry>& out) {
    out.clear();

    std::vector<u8> data;
    R_TRY(TransactData(OP_GET_STORAGE_IDS, {}, &data));

    std::vector<u32> ids;
    Reader ids_reader{data};
    R_UNLESS(ids_reader.ReadArray(&ids) && ids_reader.Ok(), ResultProtocol);

    for (size_t i = 0; i < ids.size(); i++) {
        const u32 params[]{ids[i]};
        std::vector<u8> info;
        if (R_FAILED(TransactData(OP_GET_STORAGE_INFO, params, &info))) {
            if (!g_session.connected) {
                R_THROW(ResultTransport);
            }
            continue;
        }

        StorageEntry entry{};
        entry.id = ids[i];

        Reader r{info};
        r.Skip(2 + 2 + 2);  // StorageType, FilesystemType, AccessCapability
        r.Read(&entry.capacity);
        r.Read(&entry.free_space);
        r.Skip(4);          // FreeSpaceInObjects
        r.ReadString(&entry.label);

        if (!r.Ok() || entry.label.empty()) {
            entry.label = ids.size() > 1
                ? "Phone Storage " + std::to_string(i + 1)
                : "Phone Storage";
        }

        out.push_back(std::move(entry));
    }

    R_UNLESS(!out.empty(), ResultMtpFailed);
    R_SUCCEED();
}

// A cheap "is the phone still there" check. GetStorageIDs returns a handful of
// bytes, unlike the DeviceInfo dataset the old probe pulled on every listing.
// Recent successful traffic is taken as proof of life so simply opening the
// root of the browser does not cost a round trip.
bool IsSessionAliveLocked() {
    if (!g_session.connected) {
        return false;
    }

    if (MsSince(g_session.last_ok_tick) < 2000) {
        return true;
    }

    std::vector<u8> data;
    return R_SUCCEEDED(TransactData(OP_GET_STORAGE_IDS, {}, &data));
}

} // namespace

auto ScanAndMountMtpDevices() -> common::MountConfigs {
    // Phase 1 talks to the device. g_mutex must not be held past this point:
    // MountNetworkDevice2 takes the devoptab rwlock for write, and devoptab
    // callbacks take that same lock before reaching g_mutex.
    std::vector<StorageEntry> storages;
    {
        SCOPED_MUTEX(&g_mutex);

        if (!IsSessionAliveLocked()) {
            CloseUsbLocked("stale session");
            if (R_FAILED(ConnectLocked())) {
                return {};
            }
        }

        if (R_FAILED(ListStoragesLocked(storages))) {
            CloseUsbLocked("no usable storage");
            return {};
        }
    }

    // Phase 2 (re)registers the devoptab mounts. A mount that is still alive
    // is rebound rather than replaced so open handles stay valid.
    SCOPED_MUTEX(&g_mount_mutex);

    common::MountConfigs out;
    out.reserve(storages.size());

    for (size_t i = 0; i < storages.size(); i++) {
        const auto& storage = storages[i];

        char mount_name[16];
        std::snprintf(mount_name, sizeof(mount_name), "mtp%zu", i);

        common::MountConfig config{};
        config.name = storage.label;
        config.url = std::string{mount_name} + ":/";
        config.read_only = true;
        // A per file lstat is answered from the listing cache and costs
        // nothing, but a child count means listing that directory over USB.
        // Doing that for every visible row is what buried the responder in
        // hundreds of requests and made the whole mount look empty.
        config.no_stat_file = false;
        config.no_stat_dir = true;

        const auto it = std::ranges::find_if(g_mounts, [&config](const auto& rec) {
            return rec.config.url == config.url;
        });

        if (it != g_mounts.end()) {
            if (auto* device = LiveDevice(*it)) {
                device->Rebind(storage.id, storage.capacity, storage.free_space);
                it->config = config;
                out.push_back(config);
                continue;
            }
            g_mounts.erase(it);
        }

        auto device = std::make_unique<MtpMountDevice>(config, storage.id, storage.capacity, storage.free_space);
        auto* device_ptr = device.get();

        if (!common::MountNetworkDevice2(std::move(device), config, sizeof(MtpFileHandle), sizeof(MtpDirHandle), mount_name, mount_name)) {
            log_write("[MTP_HOST] failed to mount %s\n", mount_name);
            continue;
        }

        log_write("[MTP_HOST] mounted %s as '%s'\n", mount_name, storage.label.c_str());
        g_mounts.push_back(MountRecord{.config = config, .device = device_ptr});
        out.push_back(config);
    }

    return out;
}

void CloseMtpSession() {
    {
        SCOPED_MUTEX(&g_mutex);
        if (g_session.connected) {
            TransactNoData(OP_CLOSE_SESSION, {});
            CloseUsbLocked("shutting down");
        }
    }

    SCOPED_MUTEX(&g_mount_mutex);

    // The devoptab entries are left alone on purpose; the file browser removes
    // them from its own destructor, by which point nothing holds a handle.
    g_mounts.clear();
}

} // namespace sphaira::devoptab::mtp
