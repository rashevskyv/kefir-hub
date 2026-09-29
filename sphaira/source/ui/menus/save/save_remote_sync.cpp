#include "save_remote_sync.hpp"
#include "ui/menus/save_menu.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/filebrowser.hpp"
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstring>

namespace sphaira::ui::menu::save {

auto ProbeWebdavLocation(const location::Entry& loc) -> Result {
    curl::Api api(CURL_LOCATION_TO_API(loc));
    const auto result = curl::Probe(api, curl::ProbeType::Webdav);
    log_write("[SYNC] WebDAV probe for %s: success=%d code=%ld\n", loc.name.c_str(), result.success, result.code);
    if (!result.success) {
        R_THROW(Result_SaveSyncFailed);
    }
    R_SUCCEED();
}

namespace {

auto DownloadOneBackupFile(fs::Fs* fs, ProgressBox* pbox, const location::Entry& loc, const Entry& e, const std::string& remote_rel, const std::string& name, const fs::FsPath& local_path, s64 unit_index, s64 total_units) -> Result {
    fs::FsPath local_file;
    if (!IsSystemLikeSave(e.save_data_type) && IsDbiBackupName(e, name.c_str()) && ParseDbiBackupNameTimestamp(name)) {
        fs::FsPath dbi_dir;
        std::snprintf(dbi_dir, sizeof(dbi_dir), "%s/%s/%.8s",
            DBI_SAVES_PATH, BuildDbiGameFolderName(e).s, name.c_str() + 19);
        local_file = fs::AppendPath(fs::AppendPath(fs->Root(), dbi_dir), name);
    } else {
        local_file = fs::AppendPath(local_path, name);
    }
    fs->CreateDirectoryRecursivelyWithPath(local_file);

    const auto temp_file = local_file + ".temp";

    curl::Api api(CURL_LOCATION_TO_API(loc));
    api.SetOption(curl::Url{loc.url + "/" + remote_rel + "/" + name});
    api.SetOption(curl::Path{temp_file});
    api.SetOption(MakeAggregateProgressCb(pbox, false, unit_index, total_units));

    auto res = curl::ToFile(api);
    if (!res.success) {
        log_write("[SYNC] failed to download: %s\n", name.c_str());
        fs->DeleteFile(temp_file);
        R_THROW(Result_SaveSyncFailed);
    }

    fs->DeleteFile(local_file);
    R_TRY(fs->RenameFile(temp_file, local_file));
    R_SUCCEED();
}

} // namespace

Result Menu::DownloadRemoteBackupsForEntry(ProgressBox* pbox, const location::Entry& loc, const dump::DumpLocation& location, Entry e, const fs::FsPath& backup_root, std::vector<std::string>* out_downloaded) const {
    R_TRY(ProbeWebdavLocation(loc));
    const auto fs = MakeFsForLocation(location);

    detail::LoadControlEntry(e);
    pbox->SetTitle(e.GetName());

    const auto local_base = BuildSaveBasePath(e, false, backup_root);
    const auto local_path = fs::AppendPath(fs->Root(), local_base);

    // archive names already present locally (sphaira base folder + dbi layout).
    std::set<std::string> local_names;
    filebrowser::FsDirCollection local_col{};
    filebrowser::FsView::get_collection(fs.get(), local_path, "", local_col, true, false, false);
    for (const auto& f : local_col.files) {
        local_names.insert(f.name);
    }
    if (!IsSystemLikeSave(e.save_data_type)) {
        for (const auto& p : CollectDbiBackups(fs.get(), e, backup_root)) {
            const auto name = std::strrchr(p.s, '/');
            local_names.insert(name ? name + 1 : p.s);
        }
    }

    const auto remote_rel = "sphaira-saves/" + BuildSaveBasePath(e, false, "").toString();
    pbox->NewTransfer("Listing remote files..."_i18n);
    const auto remote_files = curl::ListWebdav(loc.url, loc.user, loc.pass, remote_rel, loc.bearer, loc.pub_key, loc.priv_key, loc.port);

    std::vector<std::string> missing;
    for (const auto& f : remote_files) {
        if (!f.ends_with(".zip")) {
            continue;
        }
        if (!local_names.contains(f)) {
            missing.emplace_back(f);
        }
    }

    if (missing.empty()) {
        R_SUCCEED();
    }

    pbox->NewTransfer("WebDAV → SD"_i18n);
    pbox->UpdateTransfer(0, static_cast<s64>(missing.size()) * SYNC_PROGRESS_SCALE);
    for (size_t i = 0; i < missing.size(); i++) {
        R_TRY(pbox->ShouldExitResult());

        const auto& name = missing[i];
        pbox->SetActionName("WebDAV → SD"_i18n + ": " + name);

        R_TRY(DownloadOneBackupFile(fs.get(), pbox, loc, e, remote_rel, name, local_path,
            static_cast<s64>(i), static_cast<s64>(missing.size()) * SYNC_PROGRESS_SCALE));

        out_downloaded->emplace_back(name);
        pbox->UpdateTransfer(static_cast<s64>(i + 1) * SYNC_PROGRESS_SCALE, static_cast<s64>(missing.size()) * SYNC_PROGRESS_SCALE);
    }

    R_SUCCEED();
}

void Menu::SyncSavesRemote() {
    const auto webdav_locations = GetWebdavLocations();

    if (webdav_locations.empty()) {
        App::Push<OptionBox>("No WebDAV network location configured for sync. Add one in settings."_i18n, "OK"_i18n);
        return;
    }

    const auto seeds = ExpandGameGroups(GetSelectedEntries());
    if (seeds.empty()) {
        App::Push<OptionBox>("No saves selected for sync."_i18n, "OK"_i18n);
        return;
    }

    // no confirmation popup: the entry's tooltip in Save Options already
    // explains exactly what the sync does.
    if (webdav_locations.size() == 1) {
        SyncSavesRemoteWithLocation(webdav_locations.front());
    } else {
        PopupList::Items items;
        for (const auto& loc : webdav_locations) {
            items.emplace_back(loc.name);
        }
        App::Push<PopupList>("Select Sync Location"_i18n, items, [this, webdav_locations](auto op_index) {
            if (op_index) {
                SyncSavesRemoteWithLocation(webdav_locations[*op_index]);
            }
        });
    }
}

void Menu::SyncSavesRemoteWithLocation(const location::Entry& loc) {
    const auto seeds = ExpandGameGroups(GetSelectedEntries());
    if (seeds.empty()) {
        return;
    }

    // number of failed transfers, shared between the worker and the completion
    // callback so partial failures can be reported without a field on Menu.
    auto failed_count = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Syncing saves..."_i18n, "", [this, seeds, loc, failed_count](auto pbox) mutable -> Result {
        // the bar runs on synthetic per-file units, so a byte-rate readout would
        // be nonsense - show only percentage/ETA.
        pbox->SetHideSpeed(true);
        R_TRY(ProbeWebdavLocation(loc));
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        // names of archives whose transfer failed. a single failed file no
        // longer aborts the whole sync - the rest of the plan is still tried
        // and the failures are summarised at the end.
        std::vector<std::string> failed;

        // full sync plan, built before any transfer starts. uploads run as one
        // phase with its own counter, downloads follow as a second phase.
        struct UploadItem {
            size_t entry_index;
            std::string name;
            fs::FsPath path;
            std::string remote_rel;
        };
        struct DownloadItem {
            size_t entry_index;
            std::string name;
            std::string remote_rel;
            fs::FsPath local_path;
        };
        std::vector<UploadItem> uploads;
        std::vector<DownloadItem> downloads;

        const auto set_entry_visuals = [pbox](const Entry& e) {
            pbox->SetTitle(e.GetName());
            if (e.image) {
                pbox->SetImage(e.image);
            } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }
        };

        for (size_t i = 0; i < seeds.size(); i++) {
            R_TRY(pbox->ShouldExitResult());

            const auto& e = seeds[i];
            set_entry_visuals(e);

            const auto local_base = BuildSaveBasePath(e, false, backup_root);
            const auto local_path = fs::AppendPath(sd_fs.Root(), local_base);

            filebrowser::FsDirCollection local_col{};
            filebrowser::FsView::get_collection(&sd_fs, local_path, "", local_col, true, false, false);

            // name -> full local path, dbi-format backups included.
            std::vector<std::pair<std::string, fs::FsPath>> local_files;
            for (const auto& f : local_col.files) {
                local_files.emplace_back(f.name, fs::AppendPath(local_col.path, f.name));
            }
            if (!IsSystemLikeSave(e.save_data_type)) {
                for (const auto& p : CollectDbiBackups(&sd_fs, e, backup_root)) {
                    const auto name = std::strrchr(p.s, '/');
                    local_files.emplace_back(name ? name + 1 : p.s, p);
                }
            }

            const auto remote_rel = "sphaira-saves/" + BuildSaveBasePath(e, false, "").toString();
            pbox->NewTransfer("Listing remote files..."_i18n);
            const auto remote_files = curl::ListWebdav(loc.url, loc.user, loc.pass, remote_rel, loc.bearer, loc.pub_key, loc.priv_key, loc.port);

            for (const auto& [fname, fpath] : local_files) {
                if (!fname.ends_with(".zip")) continue;
                if (std::ranges::find(remote_files, fname) != remote_files.end()) continue;
                if (std::ranges::find_if(uploads, [&](const auto& u){ return u.entry_index == i && u.name == fname; }) != uploads.end()) continue;
                uploads.emplace_back(i, fname, fpath, remote_rel);
            }

            for (const auto& f : remote_files) {
                if (!f.ends_with(".zip")) continue;
                const auto found = std::ranges::find_if(local_files, [&](const auto& l){ return l.first == f; }) != local_files.end();
                if (!found) {
                    downloads.emplace_back(i, f, remote_rel, local_path);
                }
            }
        }

        // phase 1: local -> WebDAV.
        if (!uploads.empty()) {
            pbox->NewTransfer("Local → WebDAV"_i18n);
            pbox->UpdateTransfer(0, static_cast<s64>(uploads.size()) * SYNC_PROGRESS_SCALE);
        }
        for (size_t i = 0; i < uploads.size(); i++) {
            R_TRY(pbox->ShouldExitResult());

            const auto& u = uploads[i];
            set_entry_visuals(seeds[u.entry_index]);
            pbox->SetActionName("Local → WebDAV"_i18n + ": " + u.name);

            curl::Api api(CURL_LOCATION_TO_API(loc));
            api.SetUpload(true);
            api.SetOption(curl::Path{u.path});
            api.SetOption(curl::UploadInfo{u.remote_rel + "/" + u.name});
            api.SetOption(MakeAggregateProgressCb(pbox, true, static_cast<s64>(i), static_cast<s64>(uploads.size()) * SYNC_PROGRESS_SCALE));

            auto res = curl::FromFile(api);
            if (!res.success) {
                // a user cancel also fails the transfer (the progress callback
                // returns false) - that must still abort the whole sync.
                R_TRY(pbox->ShouldExitResult());
                // otherwise keep going with the next file; the failure is
                // reported in the final summary.
                log_write("[SYNC] failed to upload: %s (HTTP %ld)\n", u.name.c_str(), res.code);
                failed.emplace_back(u.name);
            }

            // the file counts as processed either way so the bar can't stall.
            pbox->UpdateTransfer(static_cast<s64>(i + 1) * SYNC_PROGRESS_SCALE, static_cast<s64>(uploads.size()) * SYNC_PROGRESS_SCALE);
        }

        // phase 2: WebDAV -> SD, only after every upload has finished. its own
        // separate bar with the same smooth, non-resetting behaviour.
        if (!downloads.empty()) {
            pbox->NewTransfer("WebDAV → SD"_i18n);
            pbox->UpdateTransfer(0, static_cast<s64>(downloads.size()) * SYNC_PROGRESS_SCALE);
        }
        for (size_t i = 0; i < downloads.size(); i++) {
            R_TRY(pbox->ShouldExitResult());

            const auto& d = downloads[i];
            const auto& e = seeds[d.entry_index];
            set_entry_visuals(e);
            pbox->SetActionName("WebDAV → SD"_i18n + ": " + d.name);

            const auto rc = DownloadOneBackupFile(&sd_fs, pbox, loc, e, d.remote_rel, d.name, d.local_path,
                static_cast<s64>(i), static_cast<s64>(downloads.size()) * SYNC_PROGRESS_SCALE);
            if (R_FAILED(rc)) {
                // a user cancel also fails the transfer (the progress callback
                // returns false) - that must still abort the whole sync, and it
                // must report Result_TransferCancelled (not the transfer's own
                // failure code), same as the upload phase above.
                R_TRY(pbox->ShouldExitResult());
                // DownloadOneBackupFile already logged the failure.
                failed.emplace_back(d.name);
            }

            // the file counts as processed either way so the bar can't stall.
            pbox->UpdateTransfer(static_cast<s64>(i + 1) * SYNC_PROGRESS_SCALE, static_cast<s64>(downloads.size()) * SYNC_PROGRESS_SCALE);
        }

        if (!failed.empty()) {
            for (const auto& name : failed) {
                log_write("[SYNC] failed transfer: %s\n", name.c_str());
            }
            *failed_count = failed.size();
            R_THROW(Result_SaveSyncFailed);
        }

        R_SUCCEED();
    }, [failed_count](Result rc){
        if (R_FAILED(rc)) {
            // partial failure: everything else was still transferred, so show
            // a summary instead of the bare error box. any other failure
            // (cancel, listing error, ...) keeps the old error box.
            if (rc == Result_SaveSyncFailed && *failed_count) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Sync finished with %zu failed transfers. See log for details."_i18n.c_str(), *failed_count);
                App::Push<OptionBox>(buf, "OK"_i18n);
            } else {
                App::PushErrorBox(rc, "Sync failed!"_i18n);
            }
        } else {
            App::Notify("Sync successful!"_i18n);
        }
    });
}

} // namespace sphaira::ui::menu::save
