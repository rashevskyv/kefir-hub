#include "utils/devoptab_mtp_internal.hpp"

namespace sphaira::devoptab::mtp {

ReadStream g_stream{};

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
        RecoverLinkLocked("command phase failed");
        R_THROW(rc);
    }

    ContainerHeader hdr{};
    u32 transferred{};
    // The responder answers with either a data phase or, when the operation
    // has no data or failed outright, the response straight away. Post the
    // full buffer since the size is not known until the header arrives.
    if (const auto rc = ReceiveContainer(XFER_BUF_SIZE, &hdr, &transferred); R_FAILED(rc)) {
        RecoverLinkLocked("data phase failed");
        R_THROW(rc);
    }

    if (hdr.type == CONTAINER_DATA) {
        if (const auto rc = ReceiveDataPhase(hdr, transferred, sink); R_FAILED(rc)) {
            RecoverLinkLocked("truncated data phase");
            R_THROW(rc);
        }

        if (const auto rc = ReceiveContainer(RESPONSE_POST_SIZE, &hdr, &transferred); R_FAILED(rc)) {
            RecoverLinkLocked("response phase failed");
            R_THROW(rc);
        }
    }

    if (hdr.type != CONTAINER_RESPONSE) {
        RecoverLinkLocked("unexpected container type");
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

Result TransactData(u16 op, std::span<const u32> params, std::vector<u8>* out, u16* out_code) {
    DataSink sink{out};
    return Transact(op, params, &sink, out_code);
}

Result TransactNoData(u16 op, std::span<const u32> params, u16* out_code) {
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
        const u32 post = AlignUp(static_cast<u32>(n), g_session.packet_size);

        u32 transferred{};
        if (const auto rc = PostBuffer(&g_session.ep_in, post, &transferred); R_FAILED(rc)) {
            RecoverLinkLocked("stream pull failed");
            R_THROW(rc);
        }
        if (!transferred) {
            RecoverLinkLocked("stream pull returned nothing");
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
            // clamped anyway: a device that returns more than it was asked for
            // must not be able to write past this buffer.
            g_stream.carry_len = static_cast<u32>(std::min<u64>(payload - to_caller, sizeof(g_stream.carry)));
            g_stream.carry_off = 0;
            std::memcpy(g_stream.carry, g_xfer_buf + to_caller, g_stream.carry_len);
        }

        g_stream.remaining -= payload;
        g_stats.bytes += payload;

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
            RecoverLinkLocked("stream response failed");
            R_THROW(rc);
        }
    }
    if (!g_stream.remaining && !g_stream.carry_len) {
        g_stream.active = false;
    }

    *out_got = got;
    R_SUCCEED();
}

// Retires the active stream before another command may go out. The leftover
// payload is always read off the bus and dropped -- never cancelled. A cancel
// is the one operation this phone does not survive, and reading is something
// the link demonstrably does at ~20 MB/s, so the wait is bounded by the
// stream cap. StartStreamLocked keeps that leftover small by not requesting
// more than a caller has shown it will use.
void AbortStreamLocked() {
    if (!g_stream.active) {
        return;
    }

    if (!g_stream.response_done) {
        g_stream.carry_len = 0;
        u64 got{};
        if (R_FAILED(PullStreamLocked(nullptr, g_stream.remaining, &got))) {
            return; // the failed pull already recovered or dropped the link
        }
    }

    g_stream = {};
}

// Opens a data phase at `offset` for exactly what the caller asked for (with
// a small floor, so a header probe does not cost a round trip per field).
//
// It used to request far more and feed later reads from the open phase, which
// is fine while the reader keeps up and fatal once it does not: yati's read
// thread stalls whenever the nand write side falls behind, and a data phase
// left half consumed across that stall gets its endpoint stalled by the
// responder. The 14:16 log shows exactly that -- 68 MiB at full speed, then
// 2140-0301 the moment the pipeline backed up. A phase that always completes
// inside one read call cannot be caught mid-transfer; the bus idling *between*
// transactions is ordinary MTP and the phone is happy to sit there for
// minutes.
Result StartStreamLocked(u32 handle, u64 offset, u64 file_size, u64 want) {
    R_UNLESS(g_session.connected, ResultNoSession);

    u64 req = std::min<u64>(file_size - offset, std::max<u64>(want, 1024 * 64));

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
        RecoverLinkLocked("stream command failed");
        R_THROW(rc);
    }

    // the first packet carries the container header plus the first payload
    // bytes; anything past the header goes to the carry buffer.
    ContainerHeader hdr{};
    u32 transferred{};
    if (const auto rc = ReceiveContainer(XFER_ALIGN, &hdr, &transferred); R_FAILED(rc)) {
        RecoverLinkLocked("stream data phase failed");
        R_THROW(rc);
    }

    if (hdr.type == CONTAINER_RESPONSE) {
        // no data phase: the device rejected the request (stale handle, ...).
        g_session.last_ok_tick = armGetSystemTick();
        R_THROW(ResultMtpFailed);
    }
    if (hdr.type != CONTAINER_DATA || hdr.length < sizeof(hdr)) {
        RecoverLinkLocked("stream got bad container");
        R_THROW(ResultProtocol);
    }

    g_stream.active = true;
    g_stream.response_done = false;
    g_stream.handle = handle;
    g_stream.next_offset = offset;
    g_stream.remaining = hdr.length - sizeof(hdr); // device may grant less than req
    g_stream.carry_off = 0;
    g_stream.carry_len = 0;

    if (transferred > sizeof(hdr)) {
        g_stream.carry_len = std::min<u64>(std::min<u64>(transferred - sizeof(hdr), g_stream.remaining),
            sizeof(g_stream.carry));
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

} // namespace sphaira::devoptab::mtp
