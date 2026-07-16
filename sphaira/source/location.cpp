#include "location.hpp"
#include "fs.hpp"
#include "app.hpp"

#include <cstring>
#include <minIni.h>
#include <usbhsfs.h>

namespace sphaira::location {
namespace {

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
    if (!App::GetHddEnable()) {
        log_write("[USBHSFS] not enabled\n");
        return {};
    }

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

        char display_name[0x100];
        std::snprintf(display_name, sizeof(display_name), "%s (%s - %s - %zu GB)", e.name, LIBUSBHSFS_FS_TYPE_STR(e.fs_type), e.product_name, e.capacity / 1024 / 1024 / 1024);

        u32 flags{};
        if (e.write_protect || (e.flags & UsbHsFsMountFlags_ReadOnly)) {
            flags |= ui::menu::filebrowser::FsEntryFlag_ReadOnly;
        }
        out.emplace_back(e.name, display_name, flags);
        log_write("\t[USBHSFS] %s name: %s serial: %s man: %s\n", e.name, e.product_name, e.serial_number, e.manufacturer);
    }

    return out;
}

} // namespace sphaira::location
