#include "location.hpp"
#include "fs.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"
#include "utils/devoptab_mtp.hpp"
#include "haze_helper.hpp"

#include <cstring>
#include <minIni.h>
#include <string>
#include <usbhsfs.h>

namespace sphaira::location {
namespace {

void EnsureUsbHsFsInitialized() {
    static bool initialized = false;

    if (haze::IsRunning()) {
        log_write("[USB_HOST] haze MTP server running; stopping haze to switch USB port to Host mode\n");
        haze::Exit();
        initialized = false;
    }

    if (!initialized) {
        usbHsFsSetFileSystemMountFlags(App::GetWriteProtect() ? UsbHsFsMountFlags_ReadOnly : 0);
        if (R_SUCCEEDED(usbHsFsInitialize(1))) {
            initialized = true;
            log_write("[USBHSFS] dynamically initialized usbHsFs Host interface\n");
            svcSleepThread(300000000ULL);
        }
    }
}

} // namespace

void Add(const Entry& e) {
    if (e.name.empty()) {
        return;
    }

    ini_puts(e.name.c_str(), "url", e.url.c_str(), paths::LOCATIONS.c_str());
    ini_puts(e.name.c_str(), "user", e.user.c_str(), paths::LOCATIONS.c_str());
    ini_puts(e.name.c_str(), "pass", e.pass.c_str(), paths::LOCATIONS.c_str());
    ini_puts(e.name.c_str(), "bearer", e.bearer.c_str(), paths::LOCATIONS.c_str());
    ini_puts(e.name.c_str(), "pub_key", e.pub_key.c_str(), paths::LOCATIONS.c_str());
    ini_puts(e.name.c_str(), "priv_key", e.priv_key.c_str(), paths::LOCATIONS.c_str());
    ini_putl(e.name.c_str(), "port", e.port, paths::LOCATIONS.c_str());
    ini_puts(e.name.c_str(), "protocol", e.protocol.c_str(), paths::LOCATIONS.c_str());
}

void Remove(const std::string& name) {
    if (name.empty()) {
        return;
    }
    ini_puts(name.c_str(), nullptr, nullptr, paths::LOCATIONS.c_str());
}

auto Load() -> Entries {
    Entries out{};

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto e = static_cast<Entries*>(UserData);

        if (!Section || !Key || !Value) {
            return 1;
        }

        // add new entry if use section changed.
        if (e->empty() || std::strcmp(Section, e->back().name.c_str())) {
            e->emplace_back(Section);
        }

        if (!std::strcmp(Key, "url")) {
            e->back().url = Value;
        } else if (!std::strcmp(Key, "user")) {
            e->back().user = Value;
        } else if (!std::strcmp(Key, "pass")) {
            e->back().pass = Value;
        } else if (!std::strcmp(Key, "bearer")) {
            e->back().bearer = Value;
        } else if (!std::strcmp(Key, "pub_key")) {
            e->back().pub_key = Value;
        } else if (!std::strcmp(Key, "priv_key")) {
            e->back().priv_key = Value;
        } else if (!std::strcmp(Key, "port")) {
            e->back().port = std::atoi(Value);
        } else if (!std::strcmp(Key, "protocol")) {
            e->back().protocol = Value;
        }

        return 1;
    };

    ini_browse(cb, &out, paths::LOCATIONS.c_str());

    return out;
}

auto GetStdio(bool write) -> StdioEntries {
    EnsureUsbHsFsInitialized();

    static UsbHsFsDevice devices[0x20];
    const auto count = usbHsFsListMountedDevices(devices, std::size(devices));
    log_write("[USBHSFS] got connected: %u\n", usbHsFsGetPhysicalDeviceCount());
    log_write("[USBHSFS] got count: %u\n", count);

    StdioEntries out{};

    for (s32 i = 0; i < count; i++) {
        const auto& e = devices[i];

        if (write && (e.write_protect || (e.flags & UsbHsFsMountFlags_ReadOnly))) {
            log_write("[USBHSFS] skipping write protect\n");
            continue;
        }

        // what identifies a drive to the user is the brand, the model and the
        // size -- the mount point (ums0:) means nothing to them. Some drives
        // already carry the brand inside the model string, so don't repeat it.
        const std::string brand = e.manufacturer;
        const std::string model = e.product_name;
        std::string label = model;
        if (!brand.empty() && !model.starts_with(brand)) {
            label = model.empty() ? brand : brand + " " + model;
        }
        if (label.empty()) {
            label = e.name;
        }

        const bool read_only = e.write_protect || (e.flags & UsbHsFsMountFlags_ReadOnly);

        const std::string suffix = read_only ? " [" + "read-only"_i18n + "]" : std::string{};
        const auto capacity = utils::formatSizeStorage(e.capacity);

        char display_name[0x100];
        std::snprintf(display_name, sizeof(display_name), "%s — %s (%s)%s",
            label.c_str(), capacity.c_str(), LIBUSBHSFS_FS_TYPE_STR(e.fs_type), suffix.c_str());

        u32 flags{};
        if (read_only) {
            flags |= ui::menu::filebrowser::FsEntryFlag_ReadOnly;
        }
        out.emplace_back(e.name, display_name, flags);
        log_write("\t[USBHSFS] %s name: %s serial: %s man: %s\n", e.name, e.product_name, e.serial_number, e.manufacturer);
    }

    return out;
}

auto GetMtpHostDevices(bool write) -> StdioEntries {
    if (write) {
        // MTP Host mounts are read-only
        return {};
    }

    EnsureUsbHsFsInitialized();

    const auto mtp_configs = devoptab::mtp::ScanAndMountMtpDevices();
    StdioEntries out{};

    for (const auto& config : mtp_configs) {
        std::string mount_name = config.url;
        if (mount_name.back() == '/') mount_name.pop_back();

        std::string display_name = "MTP: " + config.name;
        u32 flags = ui::menu::filebrowser::FsEntryFlag_ReadOnly;

        out.emplace_back(mount_name, display_name, flags);
    }

    return out;
}

} // namespace sphaira::location
