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

void FsView::OpenArchive() {
    const auto zip_path = GetNewPathCurrent();

    // probe the archive before switching views, so a corrupt zip reports an
    // error instead of dropping the user into an empty mount.
    auto probe = std::make_unique<fs::FsZip>(zip_path);
    if (R_FAILED(probe->GetFsOpenResult())) {
        App::Push<OptionBox>("Failed to open archive!"_i18n, "OK"_i18n);
        return;
    }
    probe.reset();

    // remember where to return when the user backs out of the archive root.
    m_archive_return_entry = m_fs_entry;
    m_archive_return_path = m_path;

    FsEntry archive_entry{};
    archive_entry.name = GetEntry().name;
    archive_entry.root = zip_path;
    archive_entry.type = FsType::Archive;
    archive_entry.flags = FsEntryFlag_ReadOnly;

    SetFs("/", archive_entry);
}

auto FsView::Scan(const fs::FsPath& new_path, bool is_walk_up) -> Result {
    SCOPED_MUTEX(&m_metadata_io_mutex);
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    log_write("new scan path: %s\n", new_path.s);
    if (!is_walk_up && !m_path.empty() && !m_entries_current.empty()) {
        const LastFile f(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
        m_previous_highlighted_file.emplace_back(f);
    }

    m_path = new_path;
    FreeThumbs();
    mutexLock(&m_metadata_mutex);
    m_metadata_generation++;
    m_metadata_jobs.clear();
    m_metadata_updates.clear();
    mutexUnlock(&m_metadata_mutex);
    m_entries.clear();
    m_index = 0;
    m_list->SetYoff(0);
    m_menu->SetTitleSubHeading(m_path, true);
    m_selected_count = 0;

    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();

    if (m_fs_entry.type == FsType::Root) {
        // catches every other way into the root (startup, the sources picker):
        // with only the card to pick from there is nothing to pick, so drop
        // straight into it.
        if (!HasExtraRootSources()) {
            SetFs("/", FS_ENTRY_DEFAULT);
            R_SUCCEED();
        }

        std::vector<FsDirectoryEntry> dir_entries;

        FsDirectoryEntry sd{};
        std::strcpy(sd.name, "microSD card");
        sd.type = FsDirEntryType_Dir;
        dir_entries.push_back(sd);

        // connected usb mass storage sits directly under the sd card, so a
        // plugged in drive turns up where the user is already looking rather
        // than only in the sources sidebar. Empty when the hdd option is off.
        const auto stdio_locations = location::GetStdio(false);
        for (const auto& e : stdio_locations) {
            FsDirectoryEntry hdd{};
            std::snprintf(hdd.name, sizeof(hdd.name), "%s", e.name.c_str());
            hdd.type = FsDirEntryType_Dir;
            dir_entries.push_back(hdd);
        }

        const auto mtp_locations = location::GetMtpHostDevices(false);
        for (const auto& e : mtp_locations) {
            FsDirectoryEntry mtp_dev{};
            std::snprintf(mtp_dev.name, sizeof(mtp_dev.name), "%s", e.name.c_str());
            mtp_dev.type = FsDirEntryType_Dir;
            dir_entries.push_back(mtp_dev);
        }

        const u32 phys_devices = usbHsFsGetPhysicalDeviceCount();
        if (!mtp_locations.empty()) {
            App::Notify("MTP Host: Connected " + std::to_string(mtp_locations.size()) + " storage(s)");
        } else if (!stdio_locations.empty()) {
            App::Notify("USB Host: Mounted " + std::to_string(stdio_locations.size()) + " drive(s)");
        } else if (phys_devices > 0) {
            App::Notify("USB Host: Device detected! (No FAT32/exFAT volume mounted)");
        } else {
            App::Notify("USB Host: No physical device detected on USB port");
        }

        if (App::GetGodModeEnabled()) {
            FsDirectoryEntry nand{};
            std::strcpy(nand.name, "Image System memory");
            nand.type = FsDirEntryType_Dir;
            dir_entries.push_back(nand);

            FsDirectoryEntry sdimag{};
            std::strcpy(sdimag.name, "Image microSD card");
            sdimag.type = FsDirEntryType_Dir;
            dir_entries.push_back(sdimag);
        }

        const auto network_locations = location::Load();
        for (const auto& e : network_locations) {
            if (e.IsNfs() && !sphaira::nfs::ValidateUrl(e.url)) {
                continue;
            }

            FsDirectoryEntry net{};
            std::strcpy(net.name, e.name.c_str());
            net.type = FsDirEntryType_Dir;
            dir_entries.push_back(net);
        }

        const auto count = dir_entries.size();
        m_entries.reserve(count);
        m_entries_index.reserve(count);
        m_entries_index_hidden.reserve(count);

        u32 i = 0;
        for (const auto& e : dir_entries) {
            m_entries_index_hidden.emplace_back(i);
            m_entries_index.emplace_back(i);

            FileEntry fe{};
            std::strcpy(fe.name, e.name);
            fe.type = e.type;

            if (std::strcmp(e.name, "microSD card") == 0) {
                fe.virtual_target_entry = FS_ENTRY_DEFAULT;
            } else if (std::strcmp(e.name, "Image System memory") == 0) {
                fe.virtual_target_entry.type = FsType::ImageNand;
                std::strcpy(fe.virtual_target_entry.name, "Image System memory");
                std::strcpy(fe.virtual_target_entry.root, "/");
            } else if (std::strcmp(e.name, "Image microSD card") == 0) {
                fe.virtual_target_entry.type = FsType::ImageSd;
                std::strcpy(fe.virtual_target_entry.name, "Image microSD card");
                std::strcpy(fe.virtual_target_entry.root, "/");
            } else if (const auto hdd = std::ranges::find_if(stdio_locations,
                [&e](const auto& loc) { return loc.name == e.name; }); hdd != stdio_locations.end()) {
                fe.virtual_target_entry.type = FsType::Stdio;
                std::strcpy(fe.virtual_target_entry.name, hdd->name.c_str());
                std::strcpy(fe.virtual_target_entry.root, hdd->mount.c_str());
                fe.virtual_target_entry.flags = hdd->flags;
            } else if (const auto mtp = std::ranges::find_if(mtp_locations,
                [&e](const auto& loc) { return loc.name == e.name; }); mtp != mtp_locations.end()) {
                fe.virtual_target_entry.type = FsType::Stdio;
                std::strcpy(fe.virtual_target_entry.name, mtp->name.c_str());
                std::strcpy(fe.virtual_target_entry.root, mtp->mount.c_str());
                fe.virtual_target_entry.flags = mtp->flags;
            } else {
                for (const auto& loc : network_locations) {
                    if (loc.name == e.name) {
                        const auto root_p = loc.IsSmb() ? std::string{"smb2:/"} : MakeNetworkRoot(loc.url);
                        fe.virtual_target_entry.type = FsType::Network;
                        std::strncpy(fe.virtual_target_entry.name, loc.name.c_str(), sizeof(fe.virtual_target_entry.name) - 1);
                        fe.virtual_target_entry.name[sizeof(fe.virtual_target_entry.name) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.root, root_p.c_str(), sizeof(fe.virtual_target_entry.root) - 1);
                        fe.virtual_target_entry.root[sizeof(fe.virtual_target_entry.root) - 1] = '\0';
                        fe.virtual_target_entry.flags = loc.IsNfs() ? FsEntryFlag_ReadOnly : FsEntryFlag_None;
                        std::strncpy(fe.virtual_target_entry.url, loc.url.c_str(), sizeof(fe.virtual_target_entry.url) - 1);
                        fe.virtual_target_entry.url[sizeof(fe.virtual_target_entry.url) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.protocol, loc.protocol.c_str(), sizeof(fe.virtual_target_entry.protocol) - 1);
                        fe.virtual_target_entry.protocol[sizeof(fe.virtual_target_entry.protocol) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.user, loc.user.c_str(), sizeof(fe.virtual_target_entry.user) - 1);
                        fe.virtual_target_entry.user[sizeof(fe.virtual_target_entry.user) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.pass, loc.pass.c_str(), sizeof(fe.virtual_target_entry.pass) - 1);
                        fe.virtual_target_entry.pass[sizeof(fe.virtual_target_entry.pass) - 1] = '\0';
                        fe.virtual_target_entry.port = loc.port;

                        // A registered/mounted devoptab does not prove that the
                        // remote server is currently reachable. Show the result
                        // of the last real probe this session, if any.
                        fe.connection_status = GetSourceConnectionStatus(loc.url);
                        break;
                    }
                }
            }

            m_entries.emplace_back(fe);
            i++;
        }
    } else {
        fs::Dir d;
        R_TRY(m_fs->OpenDirectory(new_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));

        std::vector<FsDirectoryEntry> dir_entries;
        R_TRY(d.ReadAll(dir_entries));

        const auto count = dir_entries.size();
        m_entries.reserve(count);
        m_entries_index.reserve(count);
        m_entries_index_hidden.reserve(count);

        u32 i = 0;
        for (const auto& e : dir_entries) {
            m_entries_index_hidden.emplace_back(i);
            if ('.' != e.name[0]) {
                m_entries_index.emplace_back(i);
            }

            FileEntry fe{};
            std::strcpy(fe.name, e.name);
            fe.type = e.type;
            fe.file_size = e.file_size;
            // a mount that opts out of stat never gets a second pass, so the
            // listing itself is all the metadata there will ever be.
            const auto no_stat = e.type == FsDirEntryType_Dir ? m_fs_entry.NoStatDir() : m_fs_entry.NoStatFile();
            fe.metadata_loaded = m_fs->IsNative() || no_stat || e.file_size > 0;

            m_entries.emplace_back(fe);
            i++;
        }
    }

    // a ".." row, so "up" is something the user can see and point at rather
    // than a button they have to know about. it doubles as the way to say "the
    // folder I am standing in" to Mount. not at the top of the filesystem,
    // where there is nothing above to go to.
    m_has_parent_entry = false;
    if (!m_menu->IsFolderPicker() && m_fs_entry.type != FsType::Root && m_path != m_fs->Root()) {
        FileEntry up{};
        std::strcpy(up.name, "..");
        up.type = FsDirEntryType_Dir;
        up.metadata_loaded = true;
        up.file_count = 0;
        up.dir_count = 0;
        m_parent_entry_index = static_cast<u32>(m_entries.size());
        m_entries.emplace_back(up);
        m_has_parent_entry = true;
    }

    // folder-picker mode: add a synthetic entry that Sort() pins to the top of
    // the listing so every folder offers "select current folder" as row 0.
    if (m_menu->IsFolderPicker()) {
        FileEntry synth{};
        synth.type = FsDirEntryType_Dir;
        synth.metadata_loaded = true;
        synth.file_count = 0;
        synth.dir_count = 0;
        m_picker_entry_index = static_cast<u32>(m_entries.size());
        m_entries.emplace_back(synth);
    }

    m_thumbs.assign(m_entries.size(), 0);
    m_mosaics.assign(m_entries.size(), {});

    Sort();
    LoadTitleLabels();

    // quick check to see if this is an update folder (never in picker mode).
    m_is_update_folder = !m_menu->IsFolderPicker() && R_SUCCEEDED(CheckIfUpdateFolder());

    // start on the first real row, not on ".." -- landing on "go back up" every
    // time you open a folder makes A into a no-op you have to steer around.
    SetIndex(m_has_parent_entry && m_entries_current.size() > 1 ? 1 : 0);
    QueueRemoteMetadata();

    // find previous entry
    if (is_walk_up && !m_previous_highlighted_file.empty()) {
        ON_SCOPE_EXIT(m_previous_highlighted_file.pop_back());
        SetIndexFromLastFile(m_previous_highlighted_file.back());
    }

    R_SUCCEED();
}

// one level up, from B or from the ".." row. leaving the top of a filesystem
// unmounts an archive, drops to the source list when there is more than the
// card to pick from, and otherwise closes the browser.
void FsView::WalkUp() {
    std::string_view view{m_path};
    if (m_fs_entry.type != FsType::Root && view != m_fs->Root()) {
        const auto end = view.find_last_of('/');
        assert(end != view.npos);

        if (end == 0) {
            Scan(m_fs->Root(), true);
        } else {
            Scan(view.substr(0, end), true);
        }
    } else if (m_fs_entry.type == FsType::Archive) {
        // at the archive root: unmount and return to the opening view.
        SetFs(m_archive_return_path, m_archive_return_entry);
    } else if (m_fs_entry.type != FsType::Root && HasExtraRootSources()) {
        FsEntry root_entry{
            .name = "System Root",
            .root = "root:/",
            .type = FsType::Root
        };
        SetFs("root:/", root_entry);
    } else if (!m_menu->IsTab()) {
        m_menu->PromptIfShouldExit();
    }
}

void FsView::Sort() {
    // returns true if lhs should be before rhs
    const auto sort = m_menu->m_sort.Get();
    const auto order = m_menu->m_order.Get();
    const auto folders_first = m_menu->m_folders_first.Get();
    const auto hidden_last = m_menu->m_hidden_last.Get();

    const auto sorter = [this, sort, order, folders_first, hidden_last](u32 _lhs, u32 _rhs) -> bool {
        const auto& lhs = m_entries[_lhs];
        const auto& rhs = m_entries[_rhs];

        if (hidden_last) {
            if (lhs.IsHidden() && !rhs.IsHidden()) {
                return false;
            } else if (!lhs.IsHidden() && rhs.IsHidden()) {
                return true;
            }
        }

        if (folders_first) {
            if (lhs.type == FsDirEntryType_Dir && !(rhs.type == FsDirEntryType_Dir)) { // left is folder
                return true;
            } else if (!(lhs.type == FsDirEntryType_Dir) && rhs.type == FsDirEntryType_Dir) { // right is folder
                return false;
            }
        }

        switch (sort) {
            case SortType_Size: {
                if (lhs.file_size == rhs.file_size) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else if (order == OrderType_Descending) {
                    return lhs.file_size > rhs.file_size;
                } else {
                    return lhs.file_size < rhs.file_size;
                }
            } break;
            case SortType_Alphabetical: {
                if (order == OrderType_Descending) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) > 0;
                }
            } break;
        }

        std::unreachable();
    };

    if (m_menu->m_show_hidden.Get()) {
        m_entries_current = m_entries_index_hidden;
    } else {
        m_entries_current = m_entries_index;
    }

    std::sort(m_entries_current.begin(), m_entries_current.end(), sorter);

    // prepend the pinned synthetic row so it is always first, whatever the
    // sort order: "select current folder" in picker mode, ".." otherwise.
    std::optional<u32> pinned;
    if (m_menu->IsFolderPicker() && m_picker_entry_index < m_entries.size()) {
        pinned = m_picker_entry_index;
    } else if (m_has_parent_entry && m_parent_entry_index < m_entries.size()) {
        pinned = m_parent_entry_index;
    }

    if (pinned) {
        m_pinned_view.assign(1, *pinned);
        m_pinned_view.insert(m_pinned_view.end(), m_entries_current.begin(), m_entries_current.end());
        m_entries_current = m_pinned_view;
    }
}

void FsView::SortAndFindLastFile(bool scan) {
    std::optional<LastFile> last_file;
    if (!m_path.empty() && !m_entries_current.empty()) {
        last_file = LastFile(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
    }

    if (scan) {
        Scan(m_path);
    } else {
        Sort();
    }

    if (last_file.has_value()) {
        SetIndexFromLastFile(*last_file);
    }
}

void FsView::SetIndexFromLastFile(const LastFile& last_file) {
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        if (last_file.name == GetEntry(i).name) {
            index = i;
            break;
        }
    }
    if (index >= 0) {
        if (index == last_file.index && m_entries_current.size() == last_file.entries_count) {
            m_list->SetYoff(last_file.offset);
            log_write("index is the same as last time\n");
        } else {
            // file position changed!
            log_write("file position changed\n");
            // guesstimate where the position is
            if (index >= 8) {
                m_list->SetYoff(((index - 8) + 1) * m_list->GetMaxY());
            } else {
                m_list->SetYoff(0);
            }
        }
        SetIndex(index);
    }
}

void FsView::SetFs(const fs::FsPath& new_path, const FsEntry& new_entry) {
    if (m_fs && m_fs_entry.root == new_entry.root && m_fs_entry.type == new_entry.type) {
        if (new_entry.type != FsType::Network || IsSameNetworkLocation(m_fs_entry, new_entry)) {
            log_write("same fs, ignoring\n");
            return;
        }
    }

    mutexLock(&m_metadata_io_mutex);

#ifdef BUILD_SMB2
    if (m_fs_entry.type == FsType::Network) {
        g_smb_ref_count--;
        if (g_smb_ref_count <= 0 && g_smb2fs) {
            delete g_smb2fs;
            g_smb2fs = nullptr;
            g_smb_ref_count = 0;
        }
    }
    if (new_entry.type == FsType::Network) {
        g_smb_ref_count++;
    }
#endif

    // m_fs.reset();
    m_path = new_path;
    m_entries.clear();
    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();
    m_entries_current = {};
    m_previous_highlighted_file.clear();
    // keep a copy that owns its own source fs (an archive copy) so it can be
    // pasted after leaving the archive; otherwise clear the pending selection.
    if (!m_menu->m_selected.HasOwnedFs()) {
        m_menu->m_selected.Reset();
    }
    m_selected_count = 0;
    m_fs_entry = new_entry;

    switch (new_entry.type) {
         case FsType::Sd:
            m_fs = std::make_unique<fs::FsNativeSd>(m_menu->m_ignore_read_only.Get());
            break;
        case FsType::ImageNand:
            m_fs = std::make_unique<fs::FsNativeImage>(FsImageDirectoryId_Nand);
            break;
        case FsType::ImageSd:
            m_fs = std::make_unique<fs::FsNativeImage>(FsImageDirectoryId_Sd);
            break;
        case FsType::Stdio:
            m_fs = std::make_unique<fs::FsStdio>(true, new_entry.root);
            break;
        case FsType::Network:
            m_fs = std::make_unique<fs::FsStdio>(true, new_entry.root);
            break;
        case FsType::Root:
            m_fs = std::make_unique<fs::FsStdio>(true, "root:/");
            break;
        case FsType::Archive:
            // new_entry.root holds the absolute path of the .zip to mount.
            m_fs = std::make_unique<fs::FsZip>(new_entry.root);
            break;
        case FsType::Content:
            m_fs = std::make_unique<fs::FsNcm>(new_entry.content_app_id, new_entry.content_meta_type, new_entry.content_storage_id);
            break;
    }

    m_path = new_path.empty() ? m_fs->Root() : new_path;
    mutexUnlock(&m_metadata_io_mutex);

    if (HasFocus()) {
        const auto rc = Scan(m_path);
        // a mounted network share or usb/mtp device can still fail to list
        // (server rejected the request, phone dropped the link). Without this,
        // the user is left staring at a green "Empty..." screen.
        if (R_FAILED(rc) && (m_fs_entry.type == FsType::Network || m_fs_entry.type == FsType::Stdio)) {
            log_write("[FILEBROWSER] listing failed: 0x%X\n", rc);
            if (m_fs_entry.type == FsType::Network) {
                App::Push<OptionBox>("Failed to list network storage!"_i18n + "\n" +
                    "The server is reachable but the listing failed. Check the credentials and the shared folder path."_i18n, "OK"_i18n);
            } else {
                App::PushErrorBox(rc, "Failed to list storage!"_i18n);
            }
            const FsEntry root_entry{
                .name = "System Root",
                .root = "root:/",
                .type = FsType::Root
            };
            SetFs("root:/", root_entry);
        }
    }
}
} // namespace sphaira::ui::menu::filebrowser
