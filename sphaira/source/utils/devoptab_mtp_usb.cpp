#include "utils/devoptab_mtp_internal.hpp"

namespace sphaira::devoptab::mtp {

Session g_session{};
Mutex g_mutex{};
LinkStats g_stats{};
alignas(0x1000) u8 g_xfer_buf[XFER_BUF_SIZE];
alignas(XFER_ALIGN) u8 g_ctrl_buf[XFER_ALIGN];

u64 MsSince(u64 tick) {
    if (!tick) return 0;
    return armTicksToNs(armGetSystemTick() - tick) / 1000000ULL;
}

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

Result GetEndpointStatusLocked(UsbHsClientEpSession* ep, bool* out_halted) {
    u32 transferred{};
    R_TRY(usbHsIfCtrlXfer(&g_session.iface,
        u8(USB_ENDPOINT_IN) | u8(USB_REQUEST_TYPE_STANDARD) | u8(USB_RECIPIENT_ENDPOINT),
        USB_REQUEST_GET_STATUS, 0, ep->desc.bEndpointAddress,
        sizeof(u16), g_ctrl_buf, &transferred));

    u16 status{};
    std::memcpy(&status, g_ctrl_buf, sizeof(status));
    *out_halted = status & 1;
    R_SUCCEED();
}

Result ClearEndpointHaltLocked(UsbHsClientEpSession* ep) {
    u32 transferred{};
    return usbHsIfCtrlXfer(&g_session.iface,
        u8(USB_ENDPOINT_OUT) | u8(USB_REQUEST_TYPE_STANDARD) | u8(USB_RECIPIENT_ENDPOINT),
        USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
        ep->desc.bEndpointAddress, 0, nullptr, &transferred);
}

// Clears a stalled bulk pipe after a transfer error, the standard USB way
// (this is what libusbhsfs does too). There is deliberately no MTP Cancel
// Request here: on the tested phone the class-specific cancel wedges the bulk
// out pipe for good -- every command after one returned 2140-0301 and no
// amount of halt clearing brought it back. Reconnecting does work, so a
// recovery that does not take escalates to that instead of retrying forever.
bool RecoverLinkLocked(const char* why) {
    if (!g_session.connected) {
        return false;
    }

    log_write("[MTP_HOST] recovering link: %s (after %u commands, %llu MiB, %llu ms)\n",
        why, g_stats.commands, (unsigned long long)(g_stats.bytes >> 20),
        (unsigned long long)MsSince(g_stats.tick));

    if (g_session.last_recover_tick > g_session.last_ok_tick) {
        CloseUsbLocked("link did not come back after recovery");
        return false;
    }
    g_session.last_recover_tick = armGetSystemTick();

    bool ok = true;
    for (auto* ep : {&g_session.ep_in, &g_session.ep_out}) {
        bool halted{};
        if (const auto rc = GetEndpointStatusLocked(ep, &halted); R_FAILED(rc)) {
            log_write("[MTP_HOST] GET_STATUS ep 0x%02X failed: 0x%X\n", ep->desc.bEndpointAddress, rc);
            ok = false;
        } else if (halted && R_FAILED(ClearEndpointHaltLocked(ep))) {
            log_write("[MTP_HOST] CLEAR_HALT ep 0x%02X failed\n", ep->desc.bEndpointAddress);
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

// One attempt at posting g_xfer_buf. See PostBuffer for the retry around it.
Result PostBufferOnce(UsbHsClientEpSession* ep, u32 size, u32* out_transferred) {
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

// Posts g_xfer_buf to the endpoint and waits for it to complete. `size` must
// be <= XFER_BUF_SIZE; for IN transfers it also has to be at least as large as
// what the device is about to send, otherwise the controller reports babble.
//
// A failed transfer is retried once, the way libusbhsfs does it: query the
// endpoint on the control pipe, clear the halt if one is set, then retry
// EVEN WHEN THE ENDPOINT REPORTS HEALTHY. Every stall in the 16:29 log was
// the first post after the bus sat idle for a second or two (yati parsing,
// placeholder creation, the write side backing up), failing 2140-0301 with
// no halt set -- the .376 retry only fired on a halt, so it never fired at
// all. The status round trip is itself what brings the idle link back.
Result PostBuffer(UsbHsClientEpSession* ep, u32 size, u32* out_transferred) {
    if (out_transferred) {
        *out_transferred = 0;
    }

    R_UNLESS(size && size <= XFER_BUF_SIZE, ResultProtocol);

    const auto rc = PostBufferOnce(ep, size, out_transferred);
    if (R_SUCCEEDED(rc)) {
        return rc;
    }

    bool halted{};
    if (R_FAILED(GetEndpointStatusLocked(ep, &halted))) {
        R_THROW(rc);
    }
    if (halted && R_FAILED(ClearEndpointHaltLocked(ep))) {
        R_THROW(rc);
    }

    return PostBufferOnce(ep, size, out_transferred);
}

// -------------------------------------------------------------------------
// protocol
// -------------------------------------------------------------------------



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
    g_stats.commands++;

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


} // namespace sphaira::devoptab::mtp
