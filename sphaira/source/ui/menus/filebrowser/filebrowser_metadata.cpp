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

void FsView::LoadTitleLabels() {
    std::string_view path{m_path.s};
    if (path.ends_with('/')) {
        path.remove_suffix(1);
    }

    if (!IsSd()) {
        return;
    }

    if (path::EqualsIC(path, "/atmosphere/contents")) {
        for (auto& e : m_entries) {
            const auto id = e.IsDir() ? path::ParseTitleIdName(e.name) : 0;
            if (!id) {
                continue;
            }

            // sysmodules name themselves; games need the control nacp, which is slow
            // enough to read that it happens on the title:: thread instead.
            e.title_label = hats::GetModuleName(id);
            if (e.title_label.empty()) {
                if (!m_title_service) {
                    m_title_service = R_SUCCEEDED(title::Init());
                }
                if (m_title_service) {
                    title::PushAsync(id);
                    e.title_id = id;
                }
            }
        }
    }

    if (path::EqualsIC(path, paths::DATA_ROOT)) {
        for (auto& e : m_entries) {
            if (!e.IsDir()) {
                continue;
            }

            if (path::EqualsIC(e.name, "account_link_rollback")) {
                e.title_label = "Safety snapshots created before changing Nintendo Account link data"_i18n;
            } else if (path::EqualsIC(e.name, "account_save_dump")) {
                e.title_label = "Legacy read-only dump of account save 0010"_i18n;
            } else if (path::EqualsIC(e.name, "restore_pending")) {
                e.title_label = "Staged account restore and rollback state"_i18n;
            } else if (path::EqualsIC(e.name, "account_backups")) {
                e.title_label = "KefirHub account backup library"_i18n;
            } else if (path::EqualsIC(e.name, "user_packs")) {
                e.title_label = "Legacy KefirHub account backup library"_i18n;
            } else if (path::EqualsIC(e.name, "playtime_pending")) {
                e.title_label = "Files awaiting TegraExplorer play time restoration"_i18n;
            } else if (path::EqualsIC(e.name, "nand_transfer")) {
                e.title_label = "Temporary files and scripts for NAND transfer"_i18n;
            } else if (path::EqualsIC(e.name, "assoc")) {
                e.title_label = "File type associations"_i18n;
            } else if (path::EqualsIC(e.name, "themes")) {
                e.title_label = "Custom user interface themes"_i18n;
            } else if (path::EqualsIC(e.name, "github")) {
                e.title_label = "Downloaded GitHub packages and repositories"_i18n;
            } else if (path::EqualsIC(e.name, "i18n")) {
                e.title_label = "Custom interface translations"_i18n;
            } else if (path::EqualsIC(e.name, "downloads")) {
                e.title_label = "Downloaded files and updates"_i18n;
            } else if (path::EqualsIC(e.name, "packages")) {
                e.title_label = "Package definitions and metadata"_i18n;
            } else if (path::EqualsIC(e.name, "logo")) {
                e.title_label = "Custom startup logo and animation"_i18n;
            } else if (path::EqualsIC(e.name, "cache")) {
                e.title_label = "Application cache files"_i18n;
            } else if (path::EqualsIC(e.name, "avatars")) {
                e.title_label = "Custom user avatar images"_i18n;
            }
        }
    }

    for (auto& e : m_entries) {
        if (e.IsFile() && path::EqualsIC(e.GetExtension(), "bin")) {
            e.title_label = IdentifyPayload(m_fs.get(), GetNewPath(e));
        }
    }
}

auto FsView::GetTitleLabel(FileEntry& e) -> std::string {
    if (e.title_id) {
        if (auto data = title::GetAsync(e.title_id); data && data->status != title::NacpLoadStatus::Progress) {
            if (data->status == title::NacpLoadStatus::Loaded && data->lang.name[0] && !title::IsPlaceholderName(data->lang.name)) {
                e.title_label = data->lang.name;
            }
            e.title_id = 0; // resolved, or never will be: stop polling
        }
    }

    return e.title_label;
}

void FsView::QueueRemoteMetadata() {
    if (m_metadata_paused || m_fs->IsNative() || m_fs_entry.type == FsType::Root || !m_metadata_thread_created) {
        return;
    }

    // position of each entry in the sorted view, entries hidden from the
    // current view are fetched last.
    std::vector<s64> view_position(m_entries.size(), -1);
    for (size_t i = 0; i < m_entries_current.size(); i++) {
        view_position[m_entries_current[i]] = static_cast<s64>(i);
    }

    mutexLock(&m_metadata_mutex);
    for (size_t i = 0; i < m_entries.size(); i++) {
        // never queue metadata for a synthetic row -- there is no such path.
        if (m_menu->IsFolderPicker() && i == m_picker_entry_index) {
            continue;
        }
        if (m_has_parent_entry && i == m_parent_entry_index) {
            continue;
        }
        auto& entry = m_entries[i];
        // mounts that charge a round trip per stat (MTP) opt out entirely, so
        // browsing a folder costs one listing rather than one per row.
        if (entry.IsDir() ? m_fs_entry.NoStatDir() : m_fs_entry.NoStatFile()) {
            entry.metadata_loaded = true;
            continue;
        }
        const auto wanted = entry.IsDir() || (entry.IsFile() && !entry.metadata_loaded);
        if (!wanted) {
            continue;
        }
        const auto pos = view_position[i];
        m_metadata_jobs.push_back(MetadataJob{
            .generation = m_metadata_generation,
            .entry_index = i,
            .view_index = pos >= 0 ? pos : static_cast<s64>(100000 + i),
            .path = GetNewPath(entry),
            .is_dir = entry.IsDir(),
        });
    }
    m_metadata_focus = m_index;
    condvarWakeOne(&m_metadata_cond);
    mutexUnlock(&m_metadata_mutex);
}

void FsView::PauseRemoteMetadata() {
    if (m_fs->IsNative() || !m_metadata_thread_created) {
        return;
    }

    m_metadata_paused = true;
    mutexLock(&m_metadata_mutex);
    m_metadata_generation++;
    m_metadata_jobs.clear();
    m_metadata_updates.clear();
    mutexUnlock(&m_metadata_mutex);

    // Wait for at most the one request which was already in flight. Once this
    // lock is acquired, the worker has no queued work left and the installer
    // can use the remote filesystem without competing metadata requests.
    mutexLock(&m_metadata_io_mutex);
    mutexUnlock(&m_metadata_io_mutex);
}

void FsView::MetadataThreadFunction() {
    for (;;) {
        mutexLock(&m_metadata_mutex);
        while (m_metadata_jobs.empty() && !m_metadata_thread_exit) {
            condvarWait(&m_metadata_cond, &m_metadata_mutex);
        }
        if (m_metadata_thread_exit) {
            mutexUnlock(&m_metadata_mutex);
            return;
        }
        // fetch whatever is nearest the cursor first: the visible screen,
        // then one screen above/below, then the rest. Directory counts are
        // slower, so within the same area file sizes win.
        size_t best = 0;
        s64 best_score = std::numeric_limits<s64>::max();
        for (size_t i = 0; i < m_metadata_jobs.size(); i++) {
            const auto& j = m_metadata_jobs[i];
            const auto score = std::abs(j.view_index - m_metadata_focus) + (j.is_dir ? 24 : 0);
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
        auto job = std::move(m_metadata_jobs[best]);
        m_metadata_jobs[best] = std::move(m_metadata_jobs.back());
        m_metadata_jobs.pop_back();
        mutexUnlock(&m_metadata_mutex);

        MetadataUpdate update{
            .generation = job.generation,
            .entry_index = job.entry_index,
        };
        Result rc{};
        mutexLock(&m_metadata_io_mutex);
        if (job.is_dir) {
            rc = m_fs->DirGetEntryCount(job.path, &update.file_count, &update.dir_count);
        } else {
            rc = m_fs->FileGetSizeAndTimestamp(job.path, &update.timestamp, &update.file_size);
        }
        mutexUnlock(&m_metadata_io_mutex);
        update.success = R_SUCCEEDED(rc);

        mutexLock(&m_metadata_mutex);
        if (job.generation == m_metadata_generation) {
            m_metadata_updates.emplace_back(std::move(update));
        }
        mutexUnlock(&m_metadata_mutex);
    }
}

void FsView::ApplyRemoteMetadata() {
    std::vector<MetadataUpdate> updates;
    mutexLock(&m_metadata_mutex);
    std::swap(updates, m_metadata_updates);
    mutexUnlock(&m_metadata_mutex);

    bool selected_size_changed{};
    for (const auto& update : updates) {
        if (update.generation != m_metadata_generation || update.entry_index >= m_entries.size()) {
            continue;
        }
        auto& entry = m_entries[update.entry_index];
        selected_size_changed |= entry.selected && entry.IsFile();
        entry.metadata_loaded = true;
        entry.metadata_failed = !update.success;
        if (!update.success) {
            continue;
        }
        if (entry.IsDir()) {
            entry.file_count = update.file_count;
            entry.dir_count = update.dir_count;
        } else {
            entry.file_size = update.file_size;
            entry.time_stamp = update.timestamp;
        }
    }
    if (selected_size_changed) {
        m_menu->UpdateSubheading();
    }
}
} // namespace sphaira::ui::menu::filebrowser
