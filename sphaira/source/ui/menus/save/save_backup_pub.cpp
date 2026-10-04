#include "ui/menus/save_menu.hpp"
#include "save_backup_writer.hpp"
#include "save_remote_sync.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "image.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
#include "ui/menus/filebrowser.hpp"
#include "path_util.hpp"
#include <utility>
#include <cstring>
#include <algorithm>
#include <set>
#include <ctime>

namespace sphaira::ui::menu::save {

void Menu::BackupSaves(std::vector<std::reference_wrapper<Entry>>& entries) {
    std::vector<Entry> copy;
    for (const auto& e : entries) {
        copy.emplace_back(e.get());
    }
    BackupSaves(std::move(copy));
}

void Menu::BackupSaves(std::vector<Entry> entries) {
    BackupSaves(std::move(entries), MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT);
}

auto Menu::BackupSavesOn(ProgressBox* pbox, std::vector<Entry> entries, const fs::FsPath& backup_root) -> Result {
    const auto location = MakeSdCardDumpLocation();
    for (auto& e : entries) {
        R_TRY(pbox->ShouldExitResult());
        detail::LoadControlEntry(e);
        R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));
    }
    R_SUCCEED();
}

void Menu::BackupSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    auto created_paths = std::make_shared<std::vector<fs::FsPath>>(entries.size());
    auto up_to_date = std::make_shared<size_t>(0);
    App::Push<ProgressBox>(0, "Backup"_i18n, "", [this, entries, location, backup_root, created_paths, up_to_date](auto pbox) mutable -> Result {
        // a save unchanged since its newest backup would only be written again, byte for byte.
        fs::FsStdio stdio_fs;
        fs::FsNativeSd sd_fs;
        fs::Fs* probe_fs = (location.entry.type == dump::DumpLocationType_Stdio || backup_root.starts_with("ums"))
            ? static_cast<fs::Fs*>(&stdio_fs)
            : static_cast<fs::Fs*>(&sd_fs);
        const bool can_probe = location.entry.type == dump::DumpLocationType_SdCard || location.entry.type == dump::DumpLocationType_Stdio;
        for (size_t i = 0; i < entries.size(); i++) {
            auto& e = entries[i];
            // the entry may not have loaded yet.
            detail::LoadControlEntry(e);
            if (can_probe && IsBackupUpToDate(probe_fs, e, backup_root)) {
                (*up_to_date)++;
                continue;
            }
            R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root, &(*created_paths)[i]));
        }
        R_SUCCEED();
    }, [this, entries, location, created_paths, up_to_date](Result rc){
        App::PushErrorBox(rc, "Backup failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            // empty and unchanged saves are skipped, so a successful run may have written nothing.
            const auto created = std::ranges::count_if(*created_paths, [](const auto& p) { return !p.empty(); });
            if (*up_to_date && !created) {
                App::Push<OptionBox>("All selected saves are already up to date."_i18n, "OK"_i18n);
            } else if (*up_to_date) {
                App::Push<OptionBox>(std::to_string(created) + " " + "backup(s) created, "_i18n +
                    std::to_string(*up_to_date) + " " + "already up to date."_i18n, "OK"_i18n);
            } else {
                App::Notify(created ? "Backup successful!"_i18n : "No save data found for this title"_i18n);
            }

            if (App::GetSaveAutosync()) {
                const auto webdav_locations = GetWebdavLocations();
                if (!webdav_locations.empty()) {
                    location::Entry target_loc = webdav_locations.front();
                    const auto active_name = App::GetWebdavUrlName();
                    for (const auto& l : webdav_locations) {
                        if (l.name == active_name) {
                            target_loc = l;
                            break;
                        }
                    }
                    const auto loc = target_loc;
                    App::Push<ProgressBox>(0, "Auto-syncing saves..."_i18n, "", [this, entries, loc, location, created_paths](auto pbox) mutable -> Result {
                        // the bar runs on synthetic per-file units, so a byte-rate
                        // readout would be nonsense - show only percentage/ETA.
                        pbox->SetHideSpeed(true);
                        R_TRY(ProbeWebdavLocation(loc));
                        // Read from the storage that received this backup.
                        const auto fs = MakeFsForLocation(location);
                        const auto total_units = static_cast<s64>(entries.size()) * SYNC_PROGRESS_SCALE;
                        if (total_units) {
                            // one transfer for the whole batch: NewTransfer resets the
                            // bar to zero, so calling it per file made the bar jump.
                            pbox->NewTransfer("Local → WebDAV"_i18n);
                            pbox->UpdateTransfer(0, total_units);
                        }

                        for (size_t i = 0; i < entries.size(); i++) {
                            R_TRY(pbox->ShouldExitResult());

                            auto& e = entries[i];
                            detail::LoadControlEntry(e);
                            const auto& latest_path = (*created_paths)[i];
                            if (!latest_path.empty()) {
                                std::string latest_path_str = latest_path.toString();
                                size_t last_slash = latest_path_str.find_last_of('/');
                                std::string filename = (last_slash != std::string::npos) ? latest_path_str.substr(last_slash + 1) : latest_path_str;
                                const auto remote_rel = "sphaira-saves/" + BuildSaveBasePath(e, false, "").toString();
                                const auto remote_name = remote_rel + "/" + filename;
                                pbox->SetActionName("Uploading: "_i18n + filename);

                                curl::ApiResult res{};
                                if (location.entry.type == dump::DumpLocationType_SdCard) {
                                    curl::Api api(CURL_LOCATION_TO_API(loc));
                                    api.SetUpload(true);
                                    api.SetOption(curl::Path{latest_path});
                                    api.SetOption(curl::UploadInfo{remote_name});
                                    api.SetOption(MakeAggregateProgressCb(pbox, true, static_cast<s64>(i), total_units));

                                    res = curl::FromFile(api);
                                } else {
                                    // stdio location (e.g. usb hdd): curl::Path uploads
                                    // always open the file via the native sd fs, so a
                                    // ums0:/ path would fail to open. stream the file
                                    // through the location's own fs instead.
                                    fs::File file;
                                    if (R_FAILED(fs->OpenFile(latest_path, FsOpenMode_Read, &file))) {
                                        log_write("[SYNC] auto-sync failed to open: %s\n", latest_path.s);
                                        R_THROW(Result_SaveSyncFailed);
                                    }

                                    s64 file_size{};
                                    R_TRY(file.GetSize(&file_size));

                                    // the file (and offset) must outlive the curl call;
                                    // the call is synchronous, so by-reference capture
                                    // is safe here.
                                    s64 offset{};
                                    curl::Api api(CURL_LOCATION_TO_API(loc));
                                    api.SetUpload(true);
                                    api.SetOption(curl::UploadInfo{remote_name, file_size,
                                        [&](void* ptr, size_t size) -> size_t {
                                            // curl will request past the end of the file,
                                            // returning 0 there ends the upload normally.
                                            if (offset >= file_size) {
                                                return 0;
                                            }

                                            u64 bytes_read{};
                                            if (R_FAILED(file.Read(offset, ptr, size, FsReadOption_None, &bytes_read))) {
                                                log_write("[SYNC] auto-sync failed to read: %s at offset: %zd\n", latest_path.s, offset);
                                                return 0;
                                            }

                                            offset += static_cast<s64>(bytes_read);
                                            return bytes_read;
                                        }});
                                    api.SetOption(MakeAggregateProgressCb(pbox, true, static_cast<s64>(i), total_units));

                                    res = curl::FromMemory(api);
                                }

                                if (!res.success) {
                                    log_write("[SYNC] auto-sync failed to upload: %s (HTTP %ld)\n", filename.c_str(), res.code);
                                    R_THROW(Result_SaveSyncFailed);
                                }
                            }

                            pbox->UpdateTransfer(static_cast<s64>(i + 1) * SYNC_PROGRESS_SCALE, total_units);
                        }
                        R_SUCCEED();
                    }, [](Result rc){
                        if (R_FAILED(rc)) {
                            App::PushErrorBox(rc, "Auto-sync failed!"_i18n);
                        } else {
                            App::Notify("Auto-sync successful!"_i18n);
                        }
                    });
                }
            }
        }
    });
}

auto Menu::CollectBackups(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root) const -> std::vector<BackupCandidate> {
    // every restorable archive across all backup formats/locations. an archive
    // whose name doesn't parse to a timestamp (ts == 0, e.g. renamed by hand or
    // by another tool) is still kept - it can't be dated or tie-broken, but it
    // must stay restorable, so it's simply sorted to the back. the same file
    // discovered twice (e.g. through the id-path and name-path scans) is only
    // kept once.
    std::vector<BackupCandidate> out;
    std::set<std::string> seen;

    const auto offer = [&](u64 ts, int source, const fs::FsPath& path) {
        if (!seen.insert(path.toString()).second) {
            return;
        }
        out.emplace_back(BackupCandidate{ts, path, source});
    };

    // DBI-format backups: prefer the selected root when timestamps match.
    if (!IsSystemLikeSave(e.save_data_type)) {
        const auto target_root = fs::AppendPath(fs->Root(), backup_root);
        for (const auto& path : CollectDbiBackups(fs, e, backup_root)) {
            if (DbiBackupMatchesEntry(path, e)) {
                const auto name = std::strrchr(path.s, '/');
                const bool is_target_root = !backup_root.empty() && sphaira::path::IsSubpathOf(path.s, target_root.s);
                offer(ParseBackupNameTimestamp(name ? name + 1 : path.s), is_target_root ? 0 : 5, path);
            }
        }
    }

    // sphaira backups: new structure (name, title id), then legacy (name, title id).
    for (auto i = 0; i < 4; i++) {
        const bool legacy = i >= 2;
        const bool force_id_path = i % 2 != 0;
        const auto base_path = legacy
            ? BuildSaveBasePathLegacy(e, force_id_path, backup_root)
            : BuildSaveBasePath(e, force_id_path, backup_root);
        const auto save_path = fs::AppendPath(fs->Root(), base_path);

        filebrowser::FsDirCollection collection{};
        filebrowser::FsView::get_collection(fs, save_path, "", collection, true, false, false);

        for (const auto& p : collection.files) {
            const auto view = std::string_view{p.name};
            const auto full_path = fs::AppendPath(collection.path, p.name);
            if (view.ends_with(".zip")) {
                offer(ParseBackupNameTimestamp(view), 1 + i, full_path);
            } else if (IsRawSaveCandidate(fs, full_path, view)) {
                offer(ParseBackupNameTimestamp(view), 1 + i, full_path);
            }
        }
    }

    // custom backup search paths configured in settings
    for (const auto& custom_path_str : GetBackupSearchPaths()) {
        const fs::FsPath custom_root{custom_path_str};
        for (auto i = 0; i < 4; i++) {
            const bool legacy = i >= 2;
            const bool force_id_path = i % 2 != 0;
            const auto base_path = legacy
                ? BuildSaveBasePathLegacy(e, force_id_path, custom_root)
                : BuildSaveBasePath(e, force_id_path, custom_root);
            const auto save_path = fs::AppendPath(fs->Root(), base_path);

            filebrowser::FsDirCollection collection{};
            filebrowser::FsView::get_collection(fs, save_path, "", collection, true, false, false);

            for (const auto& p : collection.files) {
                const auto view = std::string_view{p.name};
                const auto full_path = fs::AppendPath(collection.path, p.name);
                if (view.ends_with(".zip")) {
                    offer(ParseBackupNameTimestamp(view), 10 + i, full_path);
                } else if (IsRawSaveCandidate(fs, full_path, view)) {
                    offer(ParseBackupNameTimestamp(view), 10 + i, full_path);
                }
            }
        }
    }

    // Newest first; equal timestamps are broken by source and then path.  The
    // path key matters for hand-renamed archives: they all have ts == 0 and can
    // otherwise still be reordered arbitrarily when they share a source.
    std::ranges::sort(out, [](const BackupCandidate& a, const BackupCandidate& b) {
        if (a.ts != b.ts) {
            return a.ts > b.ts;
        }
        if (a.source != b.source) {
            return a.source < b.source;
        }
        return a.path.toString() < b.path.toString();
    });
    return out;
}

auto Menu::BuildSavePath(const Entry& e, bool is_auto, const fs::FsPath& backup_root) const -> fs::FsPath {
    const auto t = std::time(NULL);
    const auto tm = std::localtime(&t);
    const auto base = BuildSaveBasePath(e, false, backup_root);

    char time[64];
    std::snprintf(time, sizeof(time), "%u.%02u.%02u @ %02u.%02u.%02u", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

    fs::FsPath path;
    if (e.save_data_type == FsSaveDataType_Account) {
        const auto account = GetAccountName(e.uid);

        fs::FsPath name_buf;
        if (is_auto) {
            std::snprintf(name_buf, sizeof(name_buf), "AUTO - %s", account.c_str());
        } else {
            std::snprintf(name_buf, sizeof(name_buf), "%s", account.c_str());
        }

        title::utilsReplaceIllegalCharacters(name_buf, true);
        std::snprintf(path, sizeof(path), "%s/%s - %s.zip", base.s, name_buf.s, time);
    } else {
        std::snprintf(path, sizeof(path), "%s/%s.zip", base.s, time);
    }

    return path;
}

Result Menu::BackupSaveInternal(ProgressBox* pbox, const dump::DumpLocation& location, const Entry& e, bool compressed, bool is_auto, const fs::FsPath& backup_root, fs::FsPath* out_path) const {
    if (out_path) *out_path = {};
    if (e.save_data_id == 0) {
        return 0;
    }

    const auto fs = MakeFsForLocation(location);

    pbox->SetTitle(e.GetName());
    if (e.image) {
        pbox->SetImage(e.image);
    } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
        pbox->SetImageDataConst(data->icon);
    } else {
        pbox->SetImage(0);
    }

    const auto save_data_space_id = (FsSaveDataSpaceId)e.save_data_space_id;

    // try and get the journal and data size.
    FsSaveDataExtraData extra{};
    R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), save_data_space_id, e.save_data_id));

    FsSaveDataAttribute attr{};
    attr.application_id = e.application_id;
    attr.uid = e.uid;
    attr.system_save_data_id = e.system_save_data_id;
    attr.save_data_type = e.save_data_type;
    attr.save_data_rank = e.save_data_rank;
    attr.save_data_index = e.save_data_index;

    if (extra.attr.application_id != attr.application_id ||
        extra.attr.uid.uid[0] != attr.uid.uid[0] ||
        extra.attr.uid.uid[1] != attr.uid.uid[1] ||
        extra.attr.system_save_data_id != attr.system_save_data_id ||
        extra.attr.save_data_type != attr.save_data_type ||
        extra.attr.save_data_rank != attr.save_data_rank ||
        extra.attr.save_data_index != attr.save_data_index) {
        return FsError_PathNotFound;
    }

    // try and open the save file system
    fs::FsNativeSave save_fs{(FsSaveDataType)e.save_data_type, save_data_space_id, &attr, true};
    R_TRY(save_fs.GetFsOpenResult());

    // get a list of collections.
    filebrowser::FsDirCollections collections;
    R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", collections));

    // the save file may be empty, this isn't an error, but we exit early: a
    // metadata-only archive is hidden by the backup library and refused on restore.
    // (the root collection always exists, so look at what it holds.)
    const bool has_payload = std::ranges::any_of(collections, [](const auto& c) {
        return !c.files.empty() || !c.dirs.empty();
    });
    R_UNLESS(has_payload, 0x0);

    // non-system saves are written in the dbi backup format so that DBI can
    // restore them and vice versa. system saves keep the sphaira format.
    const auto dbi_format = !IsSystemLikeSave(e.save_data_type);

    const auto now = std::time(NULL);
    const auto now_tm = *std::localtime(&now);

    const auto path = dbi_format
        ? fs::AppendPath(fs->Root(), BuildDbiSavePath(e, now_tm, backup_root))
        : fs::AppendPath(fs->Root(), BuildSavePath(e, is_auto, backup_root));
    const bool is_sd = (location.entry.type == dump::DumpLocationType_SdCard);
    if (is_sd) {
        fs::FsNativeSd sd_fs;
        R_TRY(sd_fs.GetFsOpenResult());

        FsDirEntryType final_entry_type{};
        const auto pre_probe_rc = sd_fs.GetEntryType(path, &final_entry_type);
        if (R_SUCCEEDED(pre_probe_rc)) {
            return FsError_PathAlreadyExists;
        }
        if (pre_probe_rc != FsError_PathNotFound) {
            return pre_probe_rc;
        }

        const auto parent_rc = sd_fs.CreateDirectoryRecursivelyWithPath(path);
        if (R_FAILED(parent_rc) && parent_rc != FsError_PathAlreadyExists) {
            return parent_rc;
        }

        fs::FsPath stage_dir;
        const int stage_n = std::snprintf(stage_dir, sizeof(stage_dir), "%s.stage", path.s);
        R_UNLESS(stage_n > 0 && static_cast<size_t>(stage_n) < sizeof(stage_dir), FsError_TooLongPath);

        fs::FsPath owned_temp_path;
        const int temp_n = std::snprintf(owned_temp_path, sizeof(owned_temp_path), "%s/backup.zip.temp", stage_dir.s);
        R_UNLESS(temp_n > 0 && static_cast<size_t>(temp_n) < sizeof(owned_temp_path), FsError_TooLongPath);

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        bool owned_stage_created = false;
        enum class BackupPubState {
            Unpublished,
            Renamed,
            Published
        };
        BackupPubState pub_state = BackupPubState::Unpublished;

        ON_SCOPE_EXIT({
            if (pub_state == BackupPubState::Unpublished) {
                if (owned_stage_created) {
                    sd_fs.DeleteFile(owned_temp_path);
                    sd_fs.DeleteDirectory(stage_dir);
                }
            } else if (pub_state == BackupPubState::Renamed) {
                sd_fs.DeleteFile(path);
                if (owned_stage_created) {
                    sd_fs.DeleteDirectory(stage_dir);
                }
            } else if (pub_state == BackupPubState::Published) {
                if (owned_stage_created) {
                    sd_fs.DeleteDirectory(stage_dir);
                }
            }
        });

        const auto stage_prim_rc = fsFsCreateDirectory(&sd_fs.m_fs, stage_dir);
        if (R_FAILED(stage_prim_rc)) {
            return stage_prim_rc;
        }
        owned_stage_created = true;

        const auto stage_commit_rc = sd_fs.Commit();
        if (R_FAILED(stage_commit_rc)) {
            return stage_commit_rc;
        }

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        R_TRY(WriteSaveBackupZip(pbox, &sd_fs, owned_temp_path, &save_fs, e, extra, collections, GetAccountName(e.uid), dbi_format, compressed, false, true));

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        const auto post_probe_rc = sd_fs.GetEntryType(path, &final_entry_type);
        if (R_SUCCEEDED(post_probe_rc)) {
            return FsError_PathAlreadyExists;
        }
        if (post_probe_rc != FsError_PathNotFound) {
            return post_probe_rc;
        }

        const auto rename_rc = fsFsRenameFile(&sd_fs.m_fs, owned_temp_path, path);
        if (R_FAILED(rename_rc)) {
            return rename_rc;
        }
        pub_state = BackupPubState::Renamed;

        const auto sdmc_rc = fsdevCommitDevice("sdmc");
        if (R_FAILED(sdmc_rc)) {
            return sdmc_rc;
        }

        const auto sd_commit_rc = sd_fs.Commit();
        if (R_FAILED(sd_commit_rc)) {
            return sd_commit_rc;
        }

        pub_state = BackupPubState::Published;
        if (out_path) *out_path = path;
        R_SUCCEED();
    } else {
        const auto temp_path = path + ".temp";

        fs->CreateDirectoryRecursivelyWithPath(temp_path);
        ON_SCOPE_EXIT(fs->DeleteFile(temp_path));

        R_TRY(WriteSaveBackupZip(pbox, fs.get(), temp_path, &save_fs, e, extra, collections, GetAccountName(e.uid), dbi_format, compressed, false, false));

        fs->DeleteFile(path);
        R_TRY(fs->RenameFile(temp_path, path));

        if (out_path) *out_path = path;
        R_SUCCEED();
    }
}

} // namespace sphaira::ui::menu::save
