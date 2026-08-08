#if ENABLE_NETWORK_INSTALL

#include "usb/usbds.hpp"
#include "log.hpp"
#include "defines.hpp"
#include <algorithm>
#include <ranges>
#include <cstring>

auto GetUsbDsStateStr(UsbState state) -> const char* {
    switch (state) {
        case UsbState_Detached: return "Detached";
        case UsbState_Attached: return "Attached";
        case UsbState_Powered: return "Powered";
        case UsbState_Default: return "Default";
        case UsbState_Address: return "Address";
        case UsbState_Configured: return "Configured";
        case UsbState_Suspended: return "Suspended";
    }

    return "Unknown";
}

auto GetUsbDsSpeedStr(UsbDeviceSpeed speed) -> const char* {
    // todo: remove this cast when libnx pr is merged.
    switch ((u32)speed) {
        case UsbDeviceSpeed_None: return "None";
        case UsbDeviceSpeed_Low: return "USB 1.0 Low Speed";
        case UsbDeviceSpeed_Full: return "USB 1.1 Full Speed";
        case UsbDeviceSpeed_High: return "USB 2.0 High Speed";
        case UsbDeviceSpeed_Super: return "USB 3.0 Super Speed";
    }

    return "Unknown";
}

namespace sphaira::usb {
namespace {

constexpr u16 DEVICE_SPEED[] = {
    [UsbDeviceSpeed_None] = 0x0,
    [UsbDeviceSpeed_Low] = 0x0,
    [UsbDeviceSpeed_Full] = 0x40,
    [UsbDeviceSpeed_High] = 0x200,
    [UsbDeviceSpeed_Super] = 0x400,
};

// how long the device stays detached during a forced re-attach. The host has
// to actually notice the disconnect (windows debounces port changes for
// ~100ms) or it keeps the stale enumeration and never re-enumerates us.
constexpr u64 DETACH_SETTLE_NS = 1e+9; // 1 second.

// how long to sit on a silent bus before forcing another attach.
constexpr u64 REATTACH_INTERVAL_NS = 1e+9 * 3; // 3 seconds.

} // namespace

UsbDs::~UsbDs() {
    usbDsExit();
}

Result UsbDs::Init() {
    log_write("doing USB init\n");
    R_TRY(usbDsInitialize());

    // whoever had the port before us (a previous install session, MTP, another
    // homebrew, or the sysmodule itself after booting with the cable in) may
    // have left the device attached. usbDsEnable() on an already attached
    // device is not a transition, so the host would never re-enumerate us --
    // start from a known detached state instead. Reattach() below handles the
    // case where this is still not enough for the host to notice.
    usbDsDisable();

    static SetSysSerialNumber serial_number{};
    R_TRY(setsysInitialize());
    ON_SCOPE_EXIT(setsysExit());
    R_TRY(setsysGetSerialNumber(&serial_number));

    u8 iManufacturer, iProduct, iSerialNumber;
    static constexpr u16 supported_langs[1] = {0x0409};
    // Send language descriptor
    R_TRY(usbDsAddUsbLanguageStringDescriptor(nullptr, supported_langs, std::size(supported_langs)));
    // Send manufacturer
    R_TRY(usbDsAddUsbStringDescriptor(&iManufacturer, "Nintendo"));
    // Send product
    R_TRY(usbDsAddUsbStringDescriptor(&iProduct, "Nintendo Switch"));
    // Send serial number
    R_TRY(usbDsAddUsbStringDescriptor(&iSerialNumber, serial_number.number));

    // Send device descriptors
    struct usb_device_descriptor device_descriptor = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0110,
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,
        .bMaxPacketSize0 = 0x40,
        .idVendor = 0x057e,
        .idProduct = 0x3000,
        .bcdDevice = 0x0100,
        .iManufacturer = iManufacturer,
        .iProduct = iProduct,
        .iSerialNumber = iSerialNumber,
        .bNumConfigurations = 0x01
    };

    // Full Speed is USB 1.1
    R_TRY(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &device_descriptor));

    // High Speed is USB 2.0
    device_descriptor.bcdUSB = 0x0200;
    R_TRY(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &device_descriptor));

    // Super Speed is USB 3.0
    device_descriptor.bcdUSB = 0x0300;
    // Upgrade packet size to 512
    device_descriptor.bMaxPacketSize0 = 0x09;
    R_TRY(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &device_descriptor));

    // Define Binary Object Store
    const u8 bos[0x16] = {
        0x05,                     // .bLength
        USB_DT_BOS,               // .bDescriptorType
        0x16, 0x00,               // .wTotalLength
        0x02,                     // .bNumDeviceCaps

        // USB 2.0
        0x07,                     // .bLength
        USB_DT_DEVICE_CAPABILITY, // .bDescriptorType
        0x02,                     // .bDevCapabilityType
        0x02, 0x00, 0x00, 0x00,   // dev_capability_data

        // USB 3.0
        0x0A,                     // .bLength
        USB_DT_DEVICE_CAPABILITY, // .bDescriptorType
        0x03,                     /* .bDevCapabilityType */
        0x00,                     /* .bmAttributes */
        0x0E, 0x00,               /* .wSpeedSupported */
        0x03,                     /* .bFunctionalitySupport */
        0x00,                     /* .bU1DevExitLat */
        0x00, 0x00                /* .bU2DevExitLat */
    };

    R_TRY(usbDsSetBinaryObjectStore(bos, sizeof(bos)));

    struct usb_interface_descriptor interface_descriptor = {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = USBDS_DEFAULT_InterfaceNumber, // set below
        .bNumEndpoints = static_cast<u8>(std::size(m_endpoints)),
        .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
        .bInterfaceSubClass = USB_CLASS_VENDOR_SPEC,
        .bInterfaceProtocol = USB_CLASS_VENDOR_SPEC,
    };

    struct usb_endpoint_descriptor endpoint_descriptor_in = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
    };

    struct usb_endpoint_descriptor endpoint_descriptor_out = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_OUT,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
    };

    const struct usb_ss_endpoint_companion_descriptor endpoint_companion = {
        .bLength = sizeof(struct usb_ss_endpoint_companion_descriptor),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F,
        .bmAttributes = 0x00,
        .wBytesPerInterval = 0x00,
    };

    R_TRY(usbDsRegisterInterface(&m_interface));

    interface_descriptor.bInterfaceNumber = m_interface->interface_index;
    endpoint_descriptor_in.bEndpointAddress += interface_descriptor.bInterfaceNumber + 1;
    endpoint_descriptor_out.bEndpointAddress += interface_descriptor.bInterfaceNumber + 1;

    // Full Speed Config
    endpoint_descriptor_in.wMaxPacketSize = DEVICE_SPEED[UsbDeviceSpeed_Full];
    endpoint_descriptor_out.wMaxPacketSize = DEVICE_SPEED[UsbDeviceSpeed_Full];
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Full, &interface_descriptor, USB_DT_INTERFACE_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Full, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Full, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE));

    // High Speed Config
    endpoint_descriptor_in.wMaxPacketSize = DEVICE_SPEED[UsbDeviceSpeed_High];
    endpoint_descriptor_out.wMaxPacketSize = DEVICE_SPEED[UsbDeviceSpeed_High];
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_High, &interface_descriptor, USB_DT_INTERFACE_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_High, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_High, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE));

    // Super Speed Config
    endpoint_descriptor_in.wMaxPacketSize = DEVICE_SPEED[UsbDeviceSpeed_Super];
    endpoint_descriptor_out.wMaxPacketSize = DEVICE_SPEED[UsbDeviceSpeed_Super];
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, &interface_descriptor, USB_DT_INTERFACE_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, &endpoint_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE));
    R_TRY(usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, &endpoint_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE));

    //Setup endpoints.
    R_TRY(usbDsInterface_RegisterEndpoint(m_interface, &m_endpoints[UsbSessionEndpoint_In], endpoint_descriptor_in.bEndpointAddress));
    R_TRY(usbDsInterface_RegisterEndpoint(m_interface, &m_endpoints[UsbSessionEndpoint_Out], endpoint_descriptor_out.bEndpointAddress));

    R_TRY(usbDsInterface_EnableInterface(m_interface));
    R_TRY(usbDsEnable());

    // the state right after enabling says whether the console actually went
    // onto the bus. Detached here means the host never saw the attach, which
    // is the whole "the pc does not see the console" class of report.
    UsbState state{UsbState_Detached};
    usbDsGetState(&state);
    log_write("success USB init (state: %s)\n", GetUsbDsStateStr(state));
    R_SUCCEED();
}

// the host only starts enumerating on a fresh attach. If the cable was already
// plugged in when we enabled the device -- a previous session closed without
// the cable being pulled, MTP handing the port back, the console booted while
// connected -- the attach can go unnoticed and the host keeps its stale view of
// the port: it never talks to us and we sit in Powered/Default forever. That is
// what "unplug the cable and plug it back in" fixes by hand; this does the same
// electrically, by dropping the pullup for long enough for the host to see it.
void UsbDs::Reattach() {
    usbDsDisable();
    svcSleepThread(DETACH_SETTLE_NS);
    // no-op on [11.0.0+], where the device level Enable covers the interface.
    if (m_interface) {
        usbDsInterface_EnableInterface(m_interface);
    }
    const auto rc = usbDsEnable();
    log_write("[USBDS] forced re-attach: 0x%X\n", rc);
}

// the below code is taken from libnx, with the addition of a uevent to cancel.
Result UsbDs::WaitUntilConfigured(u64 timeout) {
    Result rc;
    UsbState state = UsbState_Detached;

    rc = usbDsGetState(&state);
    if (R_FAILED(rc)) return rc;
    if (state == UsbState_Configured) return 0;

    bool has_timeout = timeout != UINT64_MAX;
    u64 deadline = 0;

    const std::array waiters{
        waiterForEvent(usbDsGetStateChangeEvent()),
        waiterForUEvent(GetCancelEvent()),
    };

    if (has_timeout)
        deadline = armGetSystemTick() + armNsToTicks(timeout);

    do {
        if (has_timeout) {
            s64 remaining = deadline - armGetSystemTick();
            timeout = remaining > 0 ? armTicksToNs(remaining) : 0;
        }

        // never block for longer than the re-attach interval, so that a bus
        // the host is ignoring gets kicked instead of hanging here forever.
        const auto wait = std::min<u64>(timeout, REATTACH_INTERVAL_NS);

        s32 idx;
        rc = waitObjects(&idx, waiters.data(), waiters.size(), wait);
        eventClear(usbDsGetStateChangeEvent());

        // check if we got one of the cancel events.
        if (R_SUCCEEDED(rc)) {
            if (waiters[idx].handle == waiterForUEvent(GetCancelEvent()).handle) {
                log_write("got usb cancel event\n");
                rc = Result_UsbCancelled;
                break;
            }
        }

        // nothing at all happened on the bus for a full interval.
        const bool silent = wait == REATTACH_INTERVAL_NS && rc == KERNELRESULT(TimedOut);

        rc = usbDsGetState(&state);

        if (R_SUCCEEDED(rc) && silent && state != UsbState_Configured) {
            log_write("[USBDS] host has not enumerated us (state: %s)\n", GetUsbDsStateStr(state));
            Reattach();
        }
    } while (R_SUCCEEDED(rc) && state != UsbState_Configured && timeout > 0);

    if (R_SUCCEEDED(rc) && state != UsbState_Configured && timeout == 0)
        return KERNELRESULT(TimedOut);

    return rc;
}

Result UsbDs::IsUsbConnected(u64 timeout) {
    const auto rc = WaitUntilConfigured(timeout);
    if (R_FAILED(rc)) {
        m_max_packet_size = 0;
        return rc;
    }

    if (!m_max_packet_size) {
        UsbDeviceSpeed speed;
        R_TRY(GetSpeed(&speed, &m_max_packet_size));
        log_write("[USBDS] speed: %u max_packet: 0x%X\n", speed, m_max_packet_size);
    }

    R_SUCCEED();
}

Result UsbDs::GetSpeed(UsbDeviceSpeed* out, u16* max_packet_size) {
    if (hosversionAtLeast(8,0,0)) {
        R_TRY(usbDsGetSpeed(out));
    } else {
        // assume USB 2.0 speed (likely the case anyway).
        *out = UsbDeviceSpeed_High;
    }

    *max_packet_size = DEVICE_SPEED[*out];
    R_UNLESS(*max_packet_size > 0, Result_UsbDsBadDeviceSpeed);
    R_SUCCEED();
}

Event *UsbDs::GetCompletionEvent(UsbSessionEndpoint ep) {
    return std::addressof(m_endpoints[ep]->CompletionEvent);
}

Result UsbDs::WaitTransferCompletion(UsbSessionEndpoint ep, u64 timeout) {
    const std::array waiters{
        waiterForEvent(GetCompletionEvent(ep)),
        waiterForEvent(usbDsGetStateChangeEvent()),
        waiterForUEvent(GetCancelEvent()),
    };

    s32 idx;
    auto rc = waitObjects(&idx, waiters.data(), waiters.size(), timeout);

    // check if we got one of the cancel events.
    if (R_SUCCEEDED(rc)) {
        if (waiters[idx].handle == waiterForEvent(usbDsGetStateChangeEvent()).handle) {
            log_write("got usbDsGetStateChangeEvent() event\n");
            m_max_packet_size = 0;
            rc = KERNELRESULT(TimedOut);
        } else if (waiters[idx].handle == waiterForUEvent(GetCancelEvent()).handle) {
            log_write("got usb cancel event\n");
            rc = Result_UsbCancelled;
        }
    }

    if (R_FAILED(rc)) {
        usbDsEndpoint_Cancel(m_endpoints[ep]);
        eventClear(GetCompletionEvent(ep));
        eventClear(usbDsGetStateChangeEvent());
    }

    return rc;
}

Result UsbDs::TransferAsync(UsbSessionEndpoint ep, void *buffer, u32 remaining, u32 size, u32 *out_urb_id) {
    if (ep == UsbSessionEndpoint_In) {
        if (size && remaining == size && !(size % (u32)m_max_packet_size)) {
            log_write("[USBDS] SetZlt(true)\n");
            R_TRY(usbDsEndpoint_SetZlt(m_endpoints[ep], true));
        } else {
            R_TRY(usbDsEndpoint_SetZlt(m_endpoints[ep], false));
        }
    }

    return usbDsEndpoint_PostBufferAsync(m_endpoints[ep], buffer, size, out_urb_id);
}

Result UsbDs::GetTransferResult(UsbSessionEndpoint ep, u32 urb_id, u32 *out_requested_size, u32 *out_transferred_size) {
    UsbDsReportData report_data;

    R_TRY(eventClear(GetCompletionEvent(ep)));
    R_TRY(usbDsEndpoint_GetReportData(m_endpoints[ep], std::addressof(report_data)));
    R_TRY(usbDsParseReportData(std::addressof(report_data), urb_id, out_requested_size, out_transferred_size));

    R_SUCCEED();
}

} // namespace sphaira::usb

#endif
