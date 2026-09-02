#include "ui/menus/filebrowser.hpp"
#include "text_helper.hpp"
#include "path_util.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "ui/menus/filebrowser_forwarder.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/menus/file_viewer.hpp"
#include "ui/menus/theme_creator.hpp"
#include "ui/menus/appstore.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/uninstaller_menu.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "title_info.hpp"
#include "utils/devoptab_smb2.hpp"
#include "utils/devoptab_curl_device.hpp"
#include "utils/nfs_url.hpp"
#include "utils/utils.hpp"
#include "app_paths.hpp"

#include "log.hpp"
#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "fs.hpp"
#include "fs_zip.hpp"
#include "fs_ncm.hpp"
#include "haze_helper.hpp"
#include "ftpsrv_helper.hpp"
#include "nacp_util.hpp"
#include "nro.hpp"
#include "defines.hpp"
#include "image.hpp"
#include "download.hpp"
#include "owo.hpp"
#include "swkbd.hpp"
#include "i18n.hpp"
#include "hasher.hpp"
#include "location.hpp"
#include "evman.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "web.hpp"

#include "yati/yati.hpp"
#include "yati/source/file.hpp"

#include <minIni.h>
#include <usbhsfs.h>
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include <dirent.h>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <string>
#include <string_view>
#include <ctime>
#include <span>
#include <utility>
#include <ranges>
#include <expected>
#include <memory>
#include <optional>
#include <unordered_map>
#include <limits>
#include <algorithm>
#include "ui/menus/filebrowser/filebrowser_internal.hpp"

namespace sphaira::ui::menu::filebrowser {
using namespace detail;

void FsView::MountCurrentOverMtp() {
    // one pinned storage per target, each with a factory that recreates its fs
    // on demand (MTP restarts may recreate it) and a base path to root it at.
    std::vector<haze::PinnedMount> mounts;
    std::vector<fs::FsPath> targets;
    const auto& e = m_fs_entry;

    if (e.type == FsType::Content) {
        const auto app_id = e.content_app_id;
        const auto meta_type = e.content_meta_type;
        const auto storage_id = e.content_storage_id;
        mounts.push_back({[app_id, meta_type, storage_id]{ return std::make_unique<fs::FsNcm>(app_id, meta_type, storage_id); },
            std::string(e.name) + " (content)", {}, {}});
    } else if (e.type == FsType::Archive) {
        const fs::FsPath zip_path = e.root;
        mounts.push_back({[zip_path]{ return std::make_unique<fs::FsZip>(zip_path); },
            std::string(e.name) + " (archive)", {}, {}});
    } else if (IsSd()) {
        targets = GetMountTargets();
        for (const auto& target : targets) {
            const char* leaf = std::strrchr(target.s, '/');
            mounts.push_back({[]{ return std::make_unique<fs::FsNativeSd>(true); },
                (leaf && leaf[1]) ? (leaf + 1) : "microSD", target.toString(), {}});
        }
    } else {
        App::Notify("This source cannot be shared over MTP"_i18n);
        return;
    }

    if (sphaira::haze::MountFs(std::move(mounts))) {
        // real microSD folders become *the* mounts, so they also show up over
        // FTP and HTTP. content / archive mounts are MTP-only (the other two
        // serve the card directly and cannot read a virtual fs).
        if (IsSd()) {
            App::SetMountedFolders(targets);
        }
        App::Notify("Mounted over MTP: "_i18n + haze::GetPinnedName());
    } else {
        App::Push<OptionBox>("Failed to start MTP!"_i18n, "OK"_i18n);
    }
}

void FsView::ShareCurrentFolder() {
    // MTP can also share virtual mounts (content / archive); FTP and HTTP serve
    // real microSD folders only.
    const bool can_mtp = IsSd() || m_fs_entry.type == FsType::Content || m_fs_entry.type == FsType::Archive;
    const bool can_net = IsSd();

    // the popup used to tick its first row unconditionally, which read as
    // "already mounted over MTP" whatever was actually going on. show the state
    // instead: each transport is labelled with the folder it is currently
    // serving, and the tick only lands on a transport that really is serving
    // one (no tick at all when nothing is, or when several are).
    const auto mount_name = [](const fs::FsPath& p) -> std::string {
        if (p.empty()) {
            return {};
        }
        const char* leaf = std::strrchr(p.s, '/');
        return (leaf && leaf[1]) ? (leaf + 1) : p.toString();
    };

    const auto join = [](const auto& parts, auto to_name) -> std::string {
        std::string out;
        for (const auto& p : parts) {
            const auto name = to_name(p);
            if (name.empty()) {
                continue;
            }
            if (!out.empty()) {
                out += ", ";
            }
            out += name;
        }
        return out;
    };

    // MTP names its own pinned storages (it can also hold a content / archive
    // mount, which never becomes a global mount); FTP names its root devices.
    // HTTP has no names of its own -- it just lists the global mounts.
    const auto mounted = App::GetMountedFolders();
    const std::string mtp_on = haze::GetPinnedName();
    const std::string ftp_on = ftpsrv::IsRunning()
        ? join(ftpsrv::GetFtpMountedNames(), [](const std::string& n){ return n; })
        : std::string{};
    const std::string http_on = WebShareIsRunning() ? join(mounted, mount_name) : std::string{};

    PopupList::Items items;
    std::vector<int> actions; // 0 = MTP, 1 = FTP, 2 = HTTP.
    s64 active_index = -1;
    s64 active_count = 0;

    const auto add = [&](const char* label, int action, const std::string& serving) {
        if (!serving.empty()) {
            active_index = (s64)items.size();
            active_count++;
            items.emplace_back(std::string{label} + ": " + serving);
        } else {
            items.emplace_back(label);
        }
        actions.push_back(action);
    };

    if (can_mtp) { add("MTP", 0, mtp_on); }
    if (can_net) { add("FTP", 1, ftp_on); }
    if (can_net) { add("HTTP", 2, http_on); }

    if (items.empty()) {
        App::Notify("This source cannot be shared"_i18n);
        return;
    }

    // name the target in the title: "Mount" acts on the highlighted folder, and
    // the popup is the last chance to notice it is not the one you meant.
    const auto title = "Mount over..."_i18n + " (" + join(GetMountTargets(), mount_name) + ")";

    auto popup = std::make_unique<PopupList>(title, items, [this, actions](std::optional<s64> op_index){
        if (!op_index || *op_index < 0 || *op_index >= (s64)actions.size()) {
            return;
        }
        switch (actions[*op_index]) {
            case 0: MountCurrentOverMtp(); break;
            case 1: ShareCurrentOverFtp(); break;
            case 2: ShareFolder(); break;
        }
    }, active_count == 1 ? active_index : 0);

    if (active_count != 1) {
        popup->SetMenuStyle(true);
    }

    App::Push(std::move(popup));
}

void FsView::ShareCurrentOverFtp() {
    if (!IsSd()) {
        App::Notify("Only microSD folders can be shared over FTP"_i18n);
        return;
    }

    App::SetMountedFolders(GetMountTargets());

    if (!App::GetFtpEnable()) {
        App::SetFtpEnable(true);
    } else if (!ftpsrv::IsRunning()) {
        // the setting says on but the server is not up (it failed to start at
        // boot, say). SetFtpEnable() would see no change and do nothing, so the
        // mount would sit there with nothing serving it.
        ftpsrv::Init();
    }

    if (!ftpsrv::IsRunning()) {
        App::Push<OptionBox>("Failed to start FTP!"_i18n, "OK"_i18n);
        return;
    }

    u32 ip = 0;
    nifmGetCurrentIpAddress(&ip);
    if (ip) {
        char buf[128];
        std::string mname;
        for (const auto& n : ftpsrv::GetFtpMountedNames()) {
            if (!mname.empty()) mname += ", ";
            mname += n;
        }
        if (!mname.empty()) {
            std::snprintf(buf, sizeof(buf), "ftp://%u.%u.%u.%u:%u (%s)", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, (unsigned)App::GetFtpPort(), mname.c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "ftp://%u.%u.%u.%u:%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, (unsigned)App::GetFtpPort());
        }
        App::Notify(std::string("FTP: ") + buf);
    } else {
        App::Notify("FTP enabled (no network connection)"_i18n);
    }
}

void FsView::MountUsbStorage() {
    if (!App::GetHddEnable()) {
        App::Push<OptionBox>("USB storage is turned off in Settings, under Sources."_i18n, "OK"_i18n);
        return;
    }

    // mtp and usb host storage cannot both own the usb port, so at boot
    // usbhsfs is skipped entirely when mtp is on. Say so instead of reporting
    // "no drive found", which would send the user looking at the cable.
    if (haze::IsRunning()) {
        haze::Exit();
    }

    usbHsFsSetFileSystemMountFlags(App::GetWriteProtect() ? UsbHsFsMountFlags_ReadOnly : 0);
    // a no-op when the stack is already up; this is the path that recovers the
    // case where it was never started at boot.
    usbHsFsInitialize(1);

    const auto devices = location::GetStdio(false);
    if (devices.empty()) {
        App::Push<OptionBox>("No USB drive found.\nCheck that it has power and is formatted as FAT32, exFAT or NTFS."_i18n, "OK"_i18n);
        return;
    }

    // open the drive straight away: mounting it was the point.
    const auto& e = devices.front();
    FsEntry entry{};
    std::strcpy(entry.name, e.name.c_str());
    std::strcpy(entry.root, e.mount.c_str());
    entry.type = FsType::Stdio;
    entry.flags = e.flags;

    App::PopToMenu();
    App::Notify("Mounted"_i18n + ": " + e.name);
    SetFs(entry.root, entry);
}
} // namespace sphaira::ui::menu::filebrowser
