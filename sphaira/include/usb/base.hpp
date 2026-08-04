#pragma once

#include <vector>
#include <string>
#include <memory>
#include <switch.h>

namespace sphaira::usb {

// true if rc means the usb link itself went away mid-transfer -- the host
// re-enumerated the device, the cable was disturbed, the bus was reset -- as
// opposed to a protocol or filesystem level problem. Once one of these shows
// up, every following transfer on the same session fails the same way until
// the link is re-established, so callers must stop and reconnect rather than
// carry on to the next item.
bool IsLinkError(Result rc);

struct Base {
    Base(u64 transfer_timeout);
    virtual ~Base();

    // sets up usb.
    virtual Result Init() = 0;

    // returns 0 if usb is connected to a device.
    virtual Result IsUsbConnected(u64 timeout) = 0;

    // transfers a chunk of data, check out_size_transferred for how much was transferred.
    Result TransferPacketImpl(bool read, void *page, u32 remaining, u32 size, u32 *out_size_transferred, u64 timeout);
    Result TransferPacketImpl(bool read, void *page, u32 remaining, u32 size, u32 *out_size_transferred) {
        return TransferPacketImpl(read, page, remaining, size, out_size_transferred, m_transfer_timeout);
    }

    // transfers all data. zlt only applies to writes: it decides whether a
    // size landing exactly on a multiple of the endpoint packet size gets a
    // zero length packet appended to mark the end. Pass false for protocols
    // built on fixed size blocks (goldleaf) -- the host asks for exactly that
    // many bytes, so the terminator would be left in the pipe and come back
    // as an empty reply on its next read.
    Result TransferAll(bool read, void *data, u32 size, u64 timeout, bool zlt = true);
    Result TransferAll(bool read, void *data, u32 size) {
        return TransferAll(read, data, size, m_transfer_timeout);
    }

    // returns the cancel event.
    auto GetCancelEvent() {
        return &m_uevent;
    }

    // cancels an in progress transfer.
    void Cancel() {
        ueventSignal(GetCancelEvent());
    }

    auto GetTransferTimeout() const {
        return m_transfer_timeout;
    }

protected:
    enum UsbSessionEndpoint {
        UsbSessionEndpoint_In = 0,
        UsbSessionEndpoint_Out = 1,
    };

    virtual Event *GetCompletionEvent(UsbSessionEndpoint ep) = 0;
    virtual Result WaitTransferCompletion(UsbSessionEndpoint ep, u64 timeout) = 0;
    virtual Result TransferAsync(UsbSessionEndpoint ep, void *buffer, u32 remaining, u32 size, u32 *out_xfer_id) = 0;
    virtual Result GetTransferResult(UsbSessionEndpoint ep, u32 xfer_id, u32 *out_requested_size, u32 *out_transferred_size) = 0;

private:
    u64 m_transfer_timeout{};
    UEvent m_uevent{};
    std::unique_ptr<u8*> m_aligned{};
};

} // namespace sphaira::usb
