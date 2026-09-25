#include "utils/devoptab_mtp_internal.hpp"

namespace sphaira::devoptab::mtp {
namespace {

static MtpMountDevice* LiveDevice(const MountRecord& rec) {
    return common::IsNetworkDeviceMounted(rec.config.url) ? rec.device : nullptr;
}

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

    // maxXferSize is what usb:hs reserves per urb, not a ceiling on a post:
    // libusbhsfs passes wMaxPacketSize here and then posts 8 MiB buffers
    // through it all day. Reserving a whole MiB per endpoint instead was this
    // code's one unexplained deviation from the driver that is known to work
    // on this hardware -- and usb:hs is shared with libusbhsfs, which this app
    // also has running.
    const auto rc_out = usbHsIfOpenUsbEp(&g_session.iface, &g_session.ep_out, 1, out_desc->wMaxPacketSize,
        const_cast<usb_endpoint_descriptor*>(out_desc));
    const auto rc_in = usbHsIfOpenUsbEp(&g_session.iface, &g_session.ep_in, 1, in_desc->wMaxPacketSize,
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
    // a fresh session must not inherit the previous one's recovery
    // bookkeeping: the stale tick made the give-up guard fire on the very
    // first hiccup of a reconnect (its OpenSession post), killing a link
    // that had not even had its one allowed recovery yet.
    g_session.last_recover_tick = 0;
    // 512 at high speed, 1024 once the link comes up as USB 3.0. Posting a
    // size that is not a multiple of this leaves the controller without room
    // for the tail of the final packet.
    g_session.packet_size = in_desc->wMaxPacketSize;
    log_write("[MTP_HOST] bulk in max packet: %u\n", g_session.packet_size);

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

} // namespace

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
            g_stats = {armGetSystemTick(), 0, 0};
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
