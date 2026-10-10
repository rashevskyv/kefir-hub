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

// the file browser entry for a saved network source.
static FsEntry NetworkFsEntry(const location::Entry& e) {
    return FsEntry{
        .name = e.name,
        .root = e.IsSmb() ? "smb2:/" : MakeNetworkRoot(e.url),
        .type = FsType::Network,
        .flags = e.IsNfs() ? FsEntryFlag_ReadOnly : FsEntryFlag_None,
        .url = e.url,
        .protocol = e.protocol,
        .user = e.user,
        .pass = e.pass,
        .port = e.port
    };
}

// only touched from the main thread (ui callbacks).
std::unordered_map<std::string, ConnectionStatus> g_source_status;

void SetSourceConnectionStatus(const std::string& url, bool connected) {
    if (url.empty()) {
        return;
    }
    g_source_status[url] = connected ? ConnectionStatus::Connected : ConnectionStatus::Failed;
}

auto GetSourceConnectionStatus(const std::string& url) -> ConnectionStatus {
    const auto it = g_source_status.find(url);
    return it == g_source_status.end() ? ConnectionStatus::Unknown : it->second;
}

void FsView::ConnectToLocation(const FsEntry& target_entry) {
    const auto target_url_str = target_entry.url.toString();
    const auto target_proto_str = target_entry.protocol.toString();
    std::string proto = (target_url_str.starts_with("smb://") || target_proto_str == "smb") ? "SMB" :
                        ((target_url_str.starts_with("nfs://") || target_proto_str == "nfs") ? "NFS" : "Network Storage");
    std::string msg = "Connecting to " + proto + "...";

    App::Push<ProgressBox>(0, msg, target_entry.name, [this, target_entry](auto pbox) -> Result {
        const auto url_str = target_entry.url.toString();
        const auto proto_str = target_entry.protocol.toString();
        if (url_str.starts_with("smb://") || proto_str == "smb") {
#ifdef BUILD_SMB2
            if (g_smb2fs) {
                if (g_smb2fs->GetConnectUrl() == url_str) {
                    R_SUCCEED();
                }
                delete g_smb2fs;
                g_smb2fs = nullptr;
            }
            std::string server, share;
            ParseSmbUrl(url_str, server, share);
            g_smb2fs = new CSMB2FS(server, target_entry.user.toString(), target_entry.pass.toString(), share, "smb2", "smb2");
            if (g_smb2fs->RegisterFilesystem_v2()) {
                R_SUCCEED();
            } else {
                delete g_smb2fs;
                g_smb2fs = nullptr;
                R_THROW(Result_SmbConnectionFailed);
            }
#else
            R_THROW(Result_SmbNotSupported);
#endif
        } else if (url_str.starts_with("nfs://") || proto_str == "nfs") {
            if (!sphaira::nfs::ValidateUrl(url_str)) {
                log_write("[FILEBROWSER] invalid NFS URL format\n");
                R_THROW(0xCCCC);
            }

            if (devoptab::common::IsNetworkDeviceMounted(url_str)) {
                R_SUCCEED();
            }

            sphaira::devoptab::common::MountConfig config{
                .name = target_entry.name.toString(),
                .url = url_str,
                .user = "",
                .pass = "",
                .port = target_entry.port,
                .read_only = true
            };

            const auto dev_name = MakeNetworkDeviceName(url_str);
            const auto mount_name = MakeNetworkRoot(url_str);

            if (sphaira::devoptab::nfs::Mount(config, dev_name.c_str(), mount_name.c_str())) {
                R_SUCCEED();
            } else {
                R_THROW(0xCCCC);
            }
        } else {
            curl::Api api{
                curl::Url{url_str},
                curl::UserPass{target_entry.user.toString(), target_entry.pass.toString()},
                curl::Port{target_entry.port},
                curl::StopToken{pbox->GetToken()}, // Cancel joins this thread: the probe must abort
            };
            auto probe_type = curl::ProbeType::Http;
            if (proto_str == "webdav" ||
                url_str.starts_with("webdav://") || url_str.starts_with("webdavs://")) {
                probe_type = curl::ProbeType::Webdav;
            } else if (url_str.starts_with("ftp://") || url_str.starts_with("ftps://")) {
                probe_type = curl::ProbeType::Ftp;
            }

            const auto probe = curl::Probe(api, probe_type);
            log_write("[FILEBROWSER] source probe: success=%d code=%ld url=%s\n", probe.success, probe.code, target_entry.url.s);
            if (!probe.success) {
                R_THROW(0xCCCC);
            }

            if (devoptab::common::IsNetworkDeviceMounted(url_str)) {
                R_SUCCEED();
            }

            sphaira::devoptab::common::MountConfig config{
                .name = target_entry.name.toString(),
                .url = url_str,
                .user = target_entry.user.toString(),
                .pass = target_entry.pass.toString(),
                .port = target_entry.port,
                .read_only = target_entry.IsReadOnly()
            };
            auto device = std::make_unique<sphaira::devoptab::common::MountCurlDevice>(config);
            const auto dev_name = MakeNetworkDeviceName(url_str);
            const auto mount_name = MakeNetworkRoot(url_str);

            if (sphaira::devoptab::common::MountNetworkDevice2(std::move(device), config, sizeof(sphaira::devoptab::common::CurlFileState), sizeof(sphaira::devoptab::common::CurlDirState), dev_name.c_str(), mount_name.c_str())) {
                R_SUCCEED();
            } else {
                R_THROW(0xCCCC);
            }
        }
        R_SUCCEED();
    }, [this, target_entry](Result rc) {
        SetSourceConnectionStatus(target_entry.url.toString(), R_SUCCEEDED(rc));

        // reflect the outcome on the root view badges, if we are still there.
        if (m_fs_entry.type == FsType::Root) {
            for (auto& e : m_entries) {
                if (e.virtual_target_entry.type == FsType::Network && IsSameNetworkLocation(e.virtual_target_entry, target_entry)) {
                    e.connection_status = R_FAILED(rc) ? ConnectionStatus::Failed : ConnectionStatus::Connected;
                }
            }
        }

        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Failed to connect to network storage!"_i18n + "\n" +
                "Check that the server is powered on and reachable on your network."_i18n, "OK"_i18n);
        } else {
            SetFs(target_entry.root, target_entry);
        }
    });
}

void FsView::ShowSourcePicker() {
    auto options = std::make_unique<Sidebar>("Sources"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items mount_items;
    std::vector<FsEntry> fs_entries;

    const auto stdio_locations = location::GetStdio(false);
    for (const auto& e: stdio_locations) {
        u32 flags{};
        if (e.flags & FsEntryFlag_ReadOnly) {
            flags |= FsEntryFlag_ReadOnly;
        }

        fs_entries.emplace_back(e.name, e.mount, FsType::Stdio, flags);
        mount_items.push_back(e.name);
    }

    const auto mtp_locations = location::GetMtpHostDevices(false);
    for (const auto& e: mtp_locations) {
        fs_entries.emplace_back(e.name, e.mount, FsType::Stdio, e.flags);
        mount_items.push_back(e.name);
    }

    for (const auto& e: FS_ENTRIES) {
        fs_entries.emplace_back(e);
        mount_items.push_back(i18n::get(e.name));
    }

    const auto network_locations = location::Load();
    for (const auto& e: network_locations) {
        if (e.IsNfs() && !sphaira::nfs::ValidateUrl(e.url)) {
            continue;
        }

        fs_entries.emplace_back(NetworkFsEntry(e));

        std::string proto = e.protocol;
        if (proto.empty()) {
            if (e.IsSmb()) proto = "smb";
            else if (e.IsNfs()) proto = "nfs";
            else if (e.url.starts_with("ftp://") || e.url.starts_with("ftps://")) proto = "ftp";
            else if (e.url.starts_with("http://") || e.url.starts_with("https://")) proto = "http";
            else proto = "webdav";
        }
        std::string proto_upper = proto;
        std::transform(proto_upper.begin(), proto_upper.end(), proto_upper.begin(), ::toupper);

        // "FTP (FTP)" said the same thing twice: the protocol only when the name does not already say it.
        std::string name_upper = e.name;
        std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);
        mount_items.push_back(name_upper.starts_with(proto_upper) ? e.name : e.name + " (" + proto_upper + ")");
    }

    s64 current_index = 0;
    for (size_t i = 0; i < fs_entries.size(); ++i) {
        bool is_current = false;
        if (m_fs_entry.type == fs_entries[i].type && m_fs_entry.root == fs_entries[i].root) {
            if (m_fs_entry.type != FsType::Network || IsSameNetworkLocation(m_fs_entry, fs_entries[i])) {
                is_current = true;
            }
        }
        if (is_current) {
            current_index = i;
        }
    }

    options->Add<SidebarEntryArray>("Mount"_i18n, mount_items, [this, fs_entries](s64& index_out){
        App::PopToMenu();
        auto target_entry = fs_entries[index_out];
        if (target_entry.type == FsType::Network) {
            for (const auto& e : location::Load()) {
                if (e.name == target_entry.name.toString()) {
                    target_entry = NetworkFsEntry(e);
                    break;
                }
            }
            FsView* other_view = (this == m_menu->view_left.get()) ? m_menu->view_right.get() : m_menu->view_left.get();
            if (other_view && other_view->m_fs_entry.type == FsType::Network && !IsSameNetworkLocation(other_view->m_fs_entry, target_entry)) {
                other_view->SetFs("/", FS_ENTRY_DEFAULT);
            }
            if (m_fs_entry.type == FsType::Network && !IsSameNetworkLocation(m_fs_entry, target_entry)) {
                SetFs("/", FS_ENTRY_DEFAULT);
            }

            ConnectToLocation(target_entry);
        } else {
            SetFs(target_entry.root, target_entry);
        }
    }, current_index, "Switch the file source to a different storage or mount point."_i18n);

    options->Add<SidebarEntryCallback>("Mount USB drive"_i18n, [this](){
        MountUsbStorage();
    }, "Bring up a connected USB drive and open it."_i18n);

    options->Add<SidebarEntryCallback>("Add network location"_i18n, [this](){
        // close the panels under the new source's form: B from the form lands in the file
        // browser, whose source list already has the new source (a re-pushed Sources panel
        // was a second, stale copy on top of the first).
        AddNetworkLocationInteractive([this](){
            App::PopToMenu();
            SortAndFindLastFile(true);
        });
    }, "Configure a new network location (supported protocols: SMB, NFS, WebDAV, FTP, HTTP)."_i18n);
}

void Menu::ConnectToLocation(const ::sphaira::location::Entry& e) {
    if (e.IsNfs() && !sphaira::nfs::ValidateUrl(e.url)) {
        log_write("[FILEBROWSER] ConnectToLocation rejected invalid NFS URL\n");
        App::Push<OptionBox>("Failed to connect to network storage!"_i18n + "\n" +
            "Check that the server is powered on and reachable on your network."_i18n, "OK"_i18n);
        return;
    }

    const auto root_p = e.IsSmb() ? std::string{"smb2:/"} : MakeNetworkRoot(e.url);
    FsEntry target_entry{};
    std::strncpy(target_entry.name, e.name.c_str(), sizeof(target_entry.name) - 1);
    target_entry.name[sizeof(target_entry.name) - 1] = '\0';
    std::strncpy(target_entry.root, root_p.c_str(), sizeof(target_entry.root) - 1);
    target_entry.root[sizeof(target_entry.root) - 1] = '\0';
    target_entry.type = FsType::Network;
    target_entry.flags = e.IsNfs() ? FsEntryFlag_ReadOnly : FsEntryFlag_None;
    std::strncpy(target_entry.url, e.url.c_str(), sizeof(target_entry.url) - 1);
    target_entry.url[sizeof(target_entry.url) - 1] = '\0';
    std::strncpy(target_entry.protocol, e.protocol.c_str(), sizeof(target_entry.protocol) - 1);
    target_entry.protocol[sizeof(target_entry.protocol) - 1] = '\0';
    std::strncpy(target_entry.user, e.user.c_str(), sizeof(target_entry.user) - 1);
    target_entry.user[sizeof(target_entry.user) - 1] = '\0';
    std::strncpy(target_entry.pass, e.pass.c_str(), sizeof(target_entry.pass) - 1);
    target_entry.pass[sizeof(target_entry.pass) - 1] = '\0';
    target_entry.port = e.port;
    view->ConnectToLocation(target_entry);
}

// Protocol first, then one form with every field (name, address, login, Test Connection): the old
// flow asked only for a name and left the user to find the empty source in the list and edit it.
void AddNetworkLocationInteractive(std::function<void()> on_success) {
    PopupList::Items protocols = {"Samba (SMB)", "NFS", "WebDAV", "FTP", "HTTP"};
    App::Push<PopupList>("Select Protocol"_i18n, protocols, [on_success](std::optional<s64> op_proto) {
        if (!op_proto || *op_proto < 0 || *op_proto >= 5) return;
        constexpr std::array values{"smb", "nfs", "webdav", "ftp", "http"};
        constexpr std::array labels{"SMB", "NFS", "WebDAV", "FTP", "HTTP"};
        const auto proto = *op_proto;

        // a free default name; the form has a Name row to change it.
        const auto locations = location::Load();
        const auto taken = [&](const std::string& n) {
            return std::ranges::any_of(locations, [&](const auto& l){ return l.name == n; });
        };
        std::string name = labels[proto];
        for (int n = 2; taken(name); n++) {
            name = std::string{labels[proto]} + " (" + std::to_string(n) + ")";
        }

        location::Entry e;
        e.name = name;
        e.protocol = values[proto];
        e.url = std::string{values[proto]} + "://";
        e.port = (proto == 3) ? 21 : 0;
        location::Add(e);

        App::Pop();
        evman::push(evman::FunctionalEventData{[on_success, name]() {
            if (on_success) {
                on_success();
            }
            App::Push<settings::SourceEditMenu>(name);
        }});
    });
}
} // namespace sphaira::ui::menu::filebrowser
