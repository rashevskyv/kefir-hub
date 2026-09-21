#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "image.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "dumper.hpp"
#include "path_util.hpp"

#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/file_picker.hpp"

#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"

#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_slot_backend.hpp"

#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"

#include "haze_helper.hpp"

#include <utility>
#include <cstring>
#include <algorithm>
#include <set>
#include <map>
#include <unordered_map>
#include <limits>
#include <unistd.h>
#include <minIni.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::ui::menu::save {
namespace {

auto ProbeWebdavLocation(const location::Entry& loc) -> Result {
    curl::Api api(CURL_LOCATION_TO_API(loc));
    const auto result = curl::Probe(api, curl::ProbeType::Webdav);
    log_write("[SYNC] WebDAV probe for %s: success=%d code=%ld\n", loc.name.c_str(), result.success, result.code);
    if (!result.success) {
        R_THROW(Result_SaveSyncFailed);
    }
    R_SUCCEED();
}

auto DeleteLiveSaveEntry(const Entry& e) -> Result {
    if (e.save_data_id == 0) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    const auto space_id = static_cast<FsSaveDataSpaceId>(e.save_data_space_id);
    const Result rc = fsDeleteSaveDataFileSystemBySaveDataSpaceId(space_id, e.save_data_id);
    log_write("[SAVE] fsDeleteSaveDataFileSystemBySaveDataSpaceId(0x%x, 0x%016lX): 0x%x\n", space_id, e.save_data_id, rc);
    return rc;
}

} // namespace

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

auto Menu::DeleteSavesOn(ProgressBox* pbox, std::vector<Entry> entries) -> Result {
    for (size_t i = 0; i < entries.size(); i++) {
        R_TRY(pbox->ShouldExitResult());
        auto& e = entries[i];
        detail::LoadControlEntry(e);
        pbox->SetTitle(e.GetName());
        pbox->UpdateTransfer(i + 1, entries.size());
        pbox->SetActionName("Deleting save data..."_i18n);

        R_TRY(DeleteLiveSaveEntry(e));
    }
    R_SUCCEED();
}

void Menu::BackupSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    App::Push<ProgressBox>(0, "Backup"_i18n, "", [this, entries, location, backup_root](auto pbox) mutable -> Result {
        for (auto& e : entries) {
            // the entry may not have loaded yet.
            detail::LoadControlEntry(e);
            R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));
        }
        R_SUCCEED();
    }, [this, entries, location, backup_root](Result rc){
        App::PushErrorBox(rc, "Backup failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Backup successful!"_i18n);

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
                    App::Push<ProgressBox>(0, "Auto-syncing saves..."_i18n, "", [this, entries, loc, location, backup_root](auto pbox) mutable -> Result {
                        // the bar runs on synthetic per-file units, so a byte-rate
                        // readout would be nonsense - show only percentage/ETA.
                        pbox->SetHideSpeed(true);
                        R_TRY(ProbeWebdavLocation(loc));
                        // scan the same fs the backup was just written to - a
                        // backup made to a stdio location (usb hdd) must not
                        // fall back to scanning the sd card, as that would
                        // silently upload a stale (or no) archive.
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
                            fs::FsPath latest_path;
                            if (FindLatestBackupPath(fs.get(), e, backup_root, latest_path)) {
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

    // dbi-format backups (sphaira now writes these as well). source 0: wins
    // ties against sphaira-format archives sharing the same timestamp, same
    // as the old single-best FindLatestBackupPath did.
    if (!IsSystemLikeSave(e.save_data_type)) {
        for (const auto& path : CollectDbiBackups(fs, e)) {
            if (DbiBackupMatchesEntry(path, e)) {
                const auto name = std::strrchr(path.s, '/');
                offer(ParseBackupNameTimestamp(name ? name + 1 : path.s), 0, path);
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

bool Menu::FindLatestBackupPath(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root, fs::FsPath& path_out) const {
    const auto all = CollectBackups(fs, e, backup_root);
    if (all.empty()) {
        return false;
    }

    path_out = all.front().path;
    return true;
}

void Menu::RestoreSaves(std::vector<Entry> entries) {
    RestoreSaves(std::move(entries), MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT);
}

void Menu::RestoreSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    RestoreSaves(entries, entries, location, backup_root);
}

void Menu::RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    if (sources.size() != targets.size()) {
        App::Push<OptionBox>("Source and target count mismatch."_i18n, "OK"_i18n);
        return;
    }

    for (const auto& dst : targets) {
        if (dst.is_backup || (dst.save_data_id == 0 && !dst.is_planned_create)) {
            App::Push<OptionBox>("Cannot restore directly to backup or invalid save target."_i18n, "OK"_i18n);
            return;
        }
    }

    for (const auto& src : sources) {
        if (src.backup_members.empty()) {
            App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
            return;
        }
    }

    if (haze::IsRunning()) {
        App::Push<OptionBox>("MTP is currently active. Please close the running game and disable MTP before restoring save data."_i18n, "OK"_i18n);
        return;
    }

    const std::string prompt = "Restore selected saves?"_i18n + "\n\n" + "A safety recovery backup will be created on SD before overwriting.\nPlease close the running game and disable MTP."_i18n;

    App::Push<OptionBox>(prompt, "No"_i18n, "Yes"_i18n, 0, [this, sources = std::move(sources), targets = std::move(targets), location, backup_root](auto op_index) mutable {
        if (!op_index || *op_index != 1) return;

        auto restored = std::make_shared<size_t>(0);
        auto skipped = std::make_shared<size_t>(0);
        auto recovery_paths = std::make_shared<std::vector<fs::FsPath>>();
        auto last_mutation_started = std::make_shared<bool>(false);
        auto last_item_is_raw = std::make_shared<bool>(false);
        auto last_item_created_slot_retained = std::make_shared<bool>(false);

        App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, sources = std::move(sources), targets = std::move(targets), location, backup_root, restored, skipped, recovery_paths, last_mutation_started, last_item_is_raw, last_item_created_slot_retained](auto pbox) mutable -> Result {
            fs::FsStdio stdio_fs;
            fs::FsNativeSd sd_fs;

            for (size_t i = 0; i < sources.size(); i++) {
                auto& src = sources[i];
                auto& dst = targets[i];
                detail::LoadControlEntry(dst);
                pbox->SetTitle(dst.GetName());
                if (dst.image) {
                    pbox->SetImage(dst.image);
                } else if (auto data = title::Get(dst.application_id); data && !data->icon.empty()) {
                    pbox->SetImageDataConst(data->icon);
                } else {
                    pbox->SetImage(0);
                }
                pbox->UpdateTransfer(i + 1, sources.size());

                const fs::FsPath file_path = src.backup_members.front().path;
                fs::Fs* probe_fs = file_path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

                const bool is_raw = IsDisaSaveFile(probe_fs, file_path);
                *last_item_is_raw = is_raw;
                *last_mutation_started = false;
                *last_item_created_slot_retained = false;

                if (is_raw) {
                    return Result_RawSaveRestoreUnsupported;
                }

                const char* filename = std::strrchr(file_path.s, '/');
                filename = filename ? filename + 1 : file_path.s;
                BackupArchiveInfo check_info{};
                if (!InspectBackupArchive(probe_fs, file_path, filename, src.dbi_game_dir, check_info) ||
                    BackupGroupKey(check_info) != BackupGroupKey(src)) {
                    log_write("Backup archive reinspection failed or identity mismatch for %s\n", file_path.s);
                    return FsError_PathNotFound;
                }

                *last_mutation_started = false;
                pbox->SetActionName("Restore"_i18n);
                fs::FsPath item_recovery_path;
                bool item_mutation_started = false;
                bool item_created_slot_retained = false;
                const Result restore_rc = RestoreSaveInternal(pbox, dst, file_path, &item_recovery_path, &item_mutation_started, &item_created_slot_retained);
                if (!item_recovery_path.empty()) {
                    recovery_paths->push_back(item_recovery_path);
                }
                if (R_FAILED(restore_rc)) {
                    *last_mutation_started = item_mutation_started;
                    *last_item_created_slot_retained = item_created_slot_retained;
                    return restore_rc;
                }
                (*restored)++;
            }

            R_SUCCEED();
        }, [restored, skipped, recovery_paths, last_mutation_started, last_item_is_raw, last_item_created_slot_retained](Result rc){
            if (R_FAILED(rc)) {
                if (*last_item_created_slot_retained) {
                    App::Push<OptionBox>("Save slot was created, but restore did not finish.\nThe save slot remains available for retry or manual management.\nNo safety recovery archive was created because the slot was newly created."_i18n, "OK"_i18n);
                } else if (*last_item_is_raw) {
                    App::Push<OptionBox>(save::GetRawRestoreUnsupportedMessage(), "OK"_i18n);
                } else {
                    App::PushErrorBox(rc, "Restore failed!"_i18n);
                }
            } else {
                if (*restored) {
                    App::Notify("Restore successful!"_i18n);
                } else {
                    App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
                }

                if (*skipped) {
                    App::Notify(std::to_string(*skipped) + " saves skipped");
                }
            }

            if (!recovery_paths->empty()) {
                std::string prefix;
                if (R_SUCCEEDED(rc)) {
                    prefix = (recovery_paths->size() == 1)
                        ? "Restore completed.\nSafety recovery archive:\n"_i18n
                        : "Restore completed.\nSafety recovery archive(s):\n"_i18n;
                } else if (*last_item_is_raw) {
                    prefix = "Restore stopped.\nSafety recovery archive(s) retained:\n"_i18n;
                } else if (!*last_item_is_raw && *last_mutation_started) {
                    prefix = (recovery_paths->size() == 1)
                        ? "Restore stopped: target save may have changed and restored contents are unverified.\nSafety recovery archive retained:\n"_i18n
                        : "Restore stopped: current target save may have changed and restored contents are unverified.\nSafety recovery archive(s) retained:\n"_i18n;
                } else {
                    prefix = (recovery_paths->size() == 1)
                        ? "Restore stopped before target save was modified.\nSafety recovery archive retained:\n"_i18n
                        : "Restore stopped before current target save was modified.\nSafety recovery archive(s) retained:\n"_i18n;
                }
                std::string rec_msg = prefix;
                for (const auto& rp : *recovery_paths) {
                    rec_msg += rp.s;
                    rec_msg += "\n";
                }
                rec_msg += "\n" + "Manual recovery: open File Browser -> select recovery.zip -> Restore to confirmed target slot."_i18n;
                App::Push<OptionBox>(rec_msg, "OK"_i18n);
            } else if (R_FAILED(rc)) {
                if (!*last_item_is_raw && !*last_mutation_started && !*last_item_created_slot_retained) {
                    App::Push<OptionBox>("Restore stopped before current target save was modified."_i18n, "OK"_i18n);
                }
            }
        });
    });
}

void Menu::DeleteSaves(std::vector<Entry> entries) {
    if (entries.empty()) {
        return;
    }

    auto deleted_count = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Deleting saves..."_i18n, "", [this, entries, deleted_count](auto pbox) mutable -> Result {
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        for (size_t i = 0; i < entries.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            auto& e = entries[i];
            detail::LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            if (e.image) {
                pbox->SetImage(e.image);
            } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }
            pbox->UpdateTransfer(i + 1, entries.size());

            if (e.is_backup) {
                pbox->SetActionName("Deleting backup files..."_i18n);
                const auto backups = CollectBackups(&sd_fs, e, backup_root);
                for (const auto& b : backups) {
                    R_TRY(pbox->ShouldExitResult());
                    R_TRY(sd_fs.DeleteFile(b.path));
                    (*deleted_count)++;
                }

                // Also clean up empty game directories in DBI and dumps
                if (!IsSystemLikeSave(e.save_data_type)) {
                    const auto dbi_game_dir = fs::AppendPath(sd_fs.Root(), fs::AppendPath(fs::FsPath{DBI_SAVES_PATH}, BuildDbiGameFolderName(e)));
                    sd_fs.DeleteDirectory(dbi_game_dir);
                }
                const auto sphaira_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, false, backup_root));
                sd_fs.DeleteDirectory(sphaira_dir);
                const auto sphaira_id_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, true, backup_root));
                sd_fs.DeleteDirectory(sphaira_id_dir);

                // Custom search paths clean up
                for (const auto& custom_path_str : GetBackupSearchPaths()) {
                    const fs::FsPath custom_root{custom_path_str};
                    const auto custom_sphaira_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, false, custom_root));
                    sd_fs.DeleteDirectory(custom_sphaira_dir);
                    const auto custom_sphaira_id_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, true, custom_root));
                    sd_fs.DeleteDirectory(custom_sphaira_id_dir);
                }
            } else {
                pbox->SetActionName("Deleting save data..."_i18n);
                R_TRY(DeleteLiveSaveEntry(e));
                (*deleted_count)++;
            }
        }
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Delete failed!"_i18n);
        } else {
            App::Notify("Delete successful!"_i18n);
        }

        ClearSelection();
        ScanHomebrew();
    });
}



void Menu::RestoreSavesPicked(Entry e, const Entry& group, const dump::DumpLocation& location, const fs::FsPath& backup_root, fs::FsPath chosen) {
    const bool in_retained = std::ranges::any_of(group.backup_members, [&](const auto& m) {
        return m.path == chosen;
    });
    if (!in_retained) {
        App::Push<OptionBox>("Selected backup archive has changed or is no longer available."_i18n, "OK"_i18n);
        return;
    }

    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = chosen.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);
    const bool is_raw = IsDisaSaveFile(probe_fs, chosen);

    if (is_raw) {
        App::Push<OptionBox>(save::GetRawRestoreUnsupportedMessage(), "OK"_i18n);
        return;
    }

    const char* filename = std::strrchr(chosen.s, '/');
    filename = filename ? filename + 1 : chosen.s;
    BackupArchiveInfo check_info{};
    if (!InspectBackupArchive(probe_fs, chosen, filename, group.dbi_game_dir, check_info)) {
        App::Push<OptionBox>("Selected backup archive has changed or is no longer available."_i18n, "OK"_i18n);
        return;
    }

    if (BackupGroupKey(check_info) != BackupGroupKey(group)) {
        App::Push<OptionBox>("Selected backup archive has changed or is no longer available."_i18n, "OK"_i18n);
        return;
    }

    if (haze::IsRunning()) {
        App::Push<OptionBox>("MTP is currently active. Please close the running game and disable MTP before restoring save data."_i18n, "OK"_i18n);
        return;
    }

    const std::string prompt = "Restore save data to\n"_i18n + (e.GetName() ? std::string(e.GetName()) : "") + "?\n\n" + "A safety recovery backup will be created on SD before overwriting.\nPlease close the running game and disable MTP."_i18n;

    auto start_restore = [this, e = std::move(e), location, backup_root, chosen = std::move(chosen), is_raw]() mutable {
        auto recovery_path = std::make_shared<fs::FsPath>();
        auto mutation_started = std::make_shared<bool>(false);
        auto created_slot_retained = std::make_shared<bool>(false);

        App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, e, location, backup_root, chosen, recovery_path, mutation_started, created_slot_retained, is_raw](auto pbox) mutable -> Result {
            detail::LoadControlEntry(e);

            pbox->SetActionName("Restore"_i18n);
            R_TRY(RestoreSaveInternal(pbox, e, chosen, recovery_path.get(), mutation_started.get(), created_slot_retained.get()));
            R_SUCCEED();
        }, [recovery_path, mutation_started, created_slot_retained, is_raw](Result rc){
            if (R_FAILED(rc)) {
                if (*created_slot_retained) {
                    App::Push<OptionBox>("Save slot was created, but restore did not finish.\nThe save slot remains available for retry or manual management.\nNo safety recovery archive was created because the slot was newly created."_i18n, "OK"_i18n);
                    return;
                }
                App::PushErrorBox(rc, "Restore failed!"_i18n);
            } else {
                App::Notify("Restore successful!"_i18n);
            }

            if (!is_raw && !*created_slot_retained) {
                if (!recovery_path->empty()) {
                    std::string prefix;
                    if (R_SUCCEEDED(rc)) {
                        prefix = "Restore completed.\nSafety recovery archive:\n"_i18n;
                    } else if (*mutation_started) {
                        prefix = "Restore stopped: target save may have changed and restored contents are unverified.\nSafety recovery archive retained:\n"_i18n;
                    } else {
                        prefix = "Restore stopped before target save was modified.\nSafety recovery archive retained:\n"_i18n;
                    }
                    const std::string msg = prefix + recovery_path->toString() + "\n\n" + "Manual recovery: open File Browser -> select recovery.zip -> Restore to confirmed target slot."_i18n;
                    App::Push<OptionBox>(msg, "OK"_i18n);
                } else if (R_FAILED(rc)) {
                    if (!*mutation_started) {
                        App::Push<OptionBox>("Restore stopped before target save was modified."_i18n, "OK"_i18n);
                    }
                }
            }
        });
    };

    if (e.is_planned_create) {
        start_restore();
        return;
    }

    App::Push<OptionBox>(prompt, "No"_i18n, "Yes"_i18n, 0, [start_restore](auto op_index) mutable {
        if (!op_index || *op_index != 1) return;
        start_restore();
    });
}

// downloads one missing backup archive from WebDAV into the correct local
// layout (dbi-named backups go into the dbi date folder so CollectBackups/DBI
// find them, everything else lands directly under local_path) via a .temp
// file + rename. shared by the restore-time download-only sync
// (DownloadRemoteBackupsForEntry) and the download phase of the two-way
// Sync with remote (SyncSavesRemoteWithLocation) - both used to carry their
// own copy of this dance and had started to drift.
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
        for (const auto& p : CollectDbiBackups(fs.get(), e)) {
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

namespace {

struct RecoveryStreamContext {
    std::FILE* fp = nullptr;
    bool write_failed = false;
    bool flush_failed = false;
    bool sync_failed = false;
    bool close_failed = false;
};

static voidpf RecoveryOpen(voidpf opaque, const void* filename, int mode) {
    auto* ctx = static_cast<RecoveryStreamContext*>(opaque);
    const char* mode_fopen = nullptr;
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ) {
        mode_fopen = "rb";
    } else if (mode & ZLIB_FILEFUNC_MODE_EXISTING) {
        mode_fopen = "r+b";
    } else if (mode & ZLIB_FILEFUNC_MODE_CREATE) {
        mode_fopen = "wb";
    } else {
        return nullptr;
    }
    auto f = std::fopen(static_cast<const char*>(filename), mode_fopen);
    if (f) {
        std::setvbuf(f, nullptr, _IOFBF, 1024 * 512);
        if (ctx) {
            ctx->fp = f;
        }
    }
    return f;
}

static ZPOS64_T RecoveryTell(voidpf /*opaque*/, voidpf stream) {
    return std::ftell(static_cast<std::FILE*>(stream));
}

static long RecoverySeek(voidpf /*opaque*/, voidpf stream, ZPOS64_T offset, int origin) {
    return std::fseek(static_cast<std::FILE*>(stream), offset, origin);
}

static uLong RecoveryRead(voidpf /*opaque*/, voidpf stream, void* buf, uLong size) {
    return std::fread(buf, 1, size, static_cast<std::FILE*>(stream));
}

static uLong RecoveryWrite(voidpf opaque, voidpf stream, const void* buf, uLong size) {
    auto* ctx = static_cast<RecoveryStreamContext*>(opaque);
    auto file = static_cast<std::FILE*>(stream);
    const auto written = std::fwrite(buf, 1, size, file);
    if (written != size && ctx) {
        ctx->write_failed = true;
    }
    return written;
}

static int RecoveryClose(voidpf opaque, voidpf stream) {
    auto* ctx = static_cast<RecoveryStreamContext*>(opaque);
    auto file = static_cast<std::FILE*>(stream);
    if (!file) {
        return 0;
    }
    if (std::fflush(file) != 0) {
        if (ctx) ctx->flush_failed = true;
    }
    const int fd = fileno(file);
    if (fd == -1) {
        if (ctx) ctx->sync_failed = true;
    } else if (fsync(fd) != 0) {
        if (ctx) ctx->sync_failed = true;
    }
    const int rc = std::fclose(file);
    if (rc != 0) {
        if (ctx) ctx->close_failed = true;
    }
    if (ctx && ctx->fp == file) {
        ctx->fp = nullptr;
    }
    return rc;
}

static int RecoveryError(voidpf /*opaque*/, voidpf stream) {
    auto file = static_cast<std::FILE*>(stream);
    if (file) {
        return std::ferror(file);
    }
    return 0;
}

static bool IsInvalidSavePathChar(char c) {
    const auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20) {
        return true;
    }
    switch (c) {
        case ':': case '*': case '?': case '"':
        case '<': case '>': case '|': case '\\':
            return true;
        default:
            return false;
    }
}

static Result WriteSaveBackupZip(
    ProgressBox* pbox,
    fs::Fs* target_fs,
    const fs::FsPath& temp_path,
    fs::Fs* save_fs,
    const Entry& e,
    const FsSaveDataExtraData& extra,
    const filebrowser::FsDirCollections& collections,
    const std::string& account_name,
    bool dbi_format,
    bool compressed,
    bool recovery_mode = false,
    bool checked_stream = false) {

    const auto t = (extra.timestamp != 0) ? static_cast<time_t>(extra.timestamp) : std::time(nullptr);
    const auto tm = std::localtime(&t);

    zip_fileinfo zip_info_default{};
    zip_info_default.tmz_date.tm_sec = tm->tm_sec;
    zip_info_default.tmz_date.tm_min = tm->tm_min;
    zip_info_default.tmz_date.tm_hour = tm->tm_hour;
    zip_info_default.tmz_date.tm_mday = tm->tm_mday;
    zip_info_default.tmz_date.tm_mon = tm->tm_mon;
    zip_info_default.tmz_date.tm_year = tm->tm_year;

    const bool use_checked_stream = recovery_mode || checked_stream;
    const auto file_download = use_checked_stream || App::IsApplet() || e.size >= 1024ULL * 1024ULL * 1024ULL;

    RecoveryStreamContext rec_ctx{};
    mz::MzMem mz_mem{};
    zlib_filefunc64_def file_func{};
    if (use_checked_stream) {
        file_func.zopen64_file = RecoveryOpen;
        file_func.zread_file = RecoveryRead;
        file_func.zwrite_file = RecoveryWrite;
        file_func.ztell64_file = RecoveryTell;
        file_func.zseek64_file = RecoverySeek;
        file_func.zclose_file = RecoveryClose;
        file_func.zerror_file = RecoveryError;
        file_func.opaque = &rec_ctx;
    } else if (!file_download) {
        mz::FileFuncMem(&mz_mem, &file_func);
    } else {
        mz::FileFuncStdio(&file_func);
    }

    {
        auto zfile = zipOpen2_64(temp_path, APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zfile, Result_ZipOpen2_64);
        bool zip_archive_open = true;
        ON_SCOPE_EXIT({
            if (zip_archive_open && zfile) {
                zipClose(zfile, nullptr);
            }
        });

        // add save meta (sphaira format only, dbi stores its own meta below).
        if (!dbi_format) {
            const NXSaveMeta meta{
                .magic = NX_SAVE_META_MAGIC,
                .version = NX_SAVE_META_VERSION,
                .attr = extra.attr,
                .owner_id = extra.owner_id,
                .timestamp = extra.timestamp,
                .flags = extra.flags,
                .unk_x54 = extra.unk_x54,
                .data_size = extra.data_size,
                .journal_size = extra.journal_size,
                .commit_id = extra.commit_id,
                .raw_size = e.size,
            };

            R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, NX_SAVE_META_NAME, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
            bool meta_open = true;
            ON_SCOPE_EXIT({
                if (meta_open && zfile) {
                    zipCloseFileInZip(zfile);
                }
            });
            R_UNLESS(ZIP_OK == zipWriteInFileInZip(zfile, &meta, sizeof(meta)), Result_ZipWriteInFileInZip);
            meta_open = false;
            R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
        }

        // dbi stores explicit directory entries with absolute paths.
        if (dbi_format) {
            for (const auto& collection : collections) {
                if (collection.path == "/") {
                    continue;
                }

                fs::FsPath dir_name;
                std::snprintf(dir_name, sizeof(dir_name), "%s/", collection.path.s);
                R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, dir_name, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
                bool dir_entry_open = true;
                ON_SCOPE_EXIT({
                    if (dir_entry_open && zfile) {
                        zipCloseFileInZip(zfile);
                    }
                });
                dir_entry_open = false;
                R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
            }
        } else if (recovery_mode) {
            for (const auto& collection : collections) {
                if (collection.path == "/") {
                    continue;
                }
                const char* rel_dir = collection.path.s;
                while (*rel_dir == '/') {
                    rel_dir++;
                }
                if (*rel_dir == '\0') {
                    continue;
                }
                fs::FsPath dir_name;
                std::snprintf(dir_name, sizeof(dir_name), "%s/", rel_dir);
                R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, dir_name, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
                bool dir_entry_open = true;
                ON_SCOPE_EXIT({
                    if (dir_entry_open && zfile) {
                        zipCloseFileInZip(zfile);
                    }
                });
                dir_entry_open = false;
                R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
            }
        }

        const auto zip_add = [&](const fs::FsPath& file_path) -> Result {
            const char* file_name_in_zip = file_path.s;

            // strip root path (/ or ums0:)
            if (!std::strncmp(file_name_in_zip, save_fs->Root(), std::strlen(save_fs->Root()))) {
                file_name_in_zip += std::strlen(save_fs->Root());
            }

            // root paths are banned in zips, they will warn when extracting otherwise.
            while (file_name_in_zip[0] == '/') {
                file_name_in_zip++;
            }

            // dbi stores entries with absolute paths.
            fs::FsPath dbi_name;
            if (dbi_format) {
                std::snprintf(dbi_name, sizeof(dbi_name), "/%s", file_name_in_zip);
                file_name_in_zip = dbi_name.s;
            }

            pbox->NewTransfer(file_name_in_zip);

            const auto level = compressed ? Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION;
            if (ZIP_OK != zipOpenNewFileInZip(zfile, file_name_in_zip, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, level)) {
                log_write("failed to add zip for %s\n", file_path.s);
                R_THROW(Result_ZipOpenNewFileInZip);
            }
            bool file_in_zip_open = true;
            ON_SCOPE_EXIT({
                if (file_in_zip_open && zfile) {
                    zipCloseFileInZip(zfile);
                }
            });

            R_TRY(thread::TransferZip(pbox, zfile, save_fs, file_path));
            file_in_zip_open = false;
            R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
            R_SUCCEED();
        };

        // loop through every save file and store to zip.
        for (const auto& collection : collections) {
            for (const auto& file : collection.files) {
                const auto file_path = fs::AppendPath(collection.path, file.name);
                R_TRY(zip_add(file_path));
            }
        }

        // add the dbi meta entries last, matching real dbi backups.
        if (dbi_format) {
            const auto write_meta_file = [&](const char* name, const void* data, size_t size) -> Result {
                R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, name, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
                bool meta_open = true;
                ON_SCOPE_EXIT({
                    if (meta_open && zfile) {
                        zipCloseFileInZip(zfile);
                    }
                });
                R_UNLESS(ZIP_OK == zipWriteInFileInZip(zfile, data, size), Result_ZipWriteInFileInZip);
                meta_open = false;
                R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
                R_SUCCEED();
            };

            const auto account = e.save_data_type == FsSaveDataType_Account
                ? account_name
                : std::string{GetSaveTypeLabel(e.save_data_type)};

            const char* space = "User";
            switch (e.save_data_space_id) {
                case FsSaveDataSpaceId_System:
                case FsSaveDataSpaceId_SdSystem:
                case FsSaveDataSpaceId_ProperSystem:
                    space = "System";
                    break;
                case FsSaveDataSpaceId_Temporary:
                    space = "Temporary";
                    break;
            }

            const auto now_dbi = std::time(nullptr);
            const auto now_tm = *std::localtime(&now_dbi);

            char info[0x400];
            std::snprintf(info, sizeof(info),
                "TitleId=%016lX\n"
                "TitleName=%s\n"
                "BackupDate=%04d-%02d-%02d %02d:%02d:%02d\n"
                "Account=%s\n"
                "Space=%s",
                e.application_id,
                e.GetName(),
                now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec,
                account.c_str(),
                space);

            R_TRY(write_meta_file(DBI_SAVE_INFO_NAME, info, std::strlen(info)));
            R_TRY(write_meta_file(DBI_SAVE_EXTRA_NAME, &extra, sizeof(extra)));
        }

        zip_archive_open = false;
        R_UNLESS(ZIP_OK == zipClose(zfile, "sphaira v" APP_VERSION_HASH), Result_ZipWriteInFileInZip);
    }

    if (use_checked_stream) {
        R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
        R_UNLESS(!rec_ctx.flush_failed, Result_FsUnknownStdioError);
        R_UNLESS(!rec_ctx.sync_failed, Result_FsUnknownStdioError);
        R_UNLESS(!rec_ctx.close_failed, Result_FsUnknownStdioError);
        R_UNLESS(rec_ctx.fp == nullptr, Result_FsUnknownStdioError);

        const auto sdmc_rc = fsdevCommitDevice("sdmc");
        R_TRY(sdmc_rc);
        fs::FsNativeSd sd_fs;
        R_TRY(sd_fs.GetFsOpenResult());
        R_TRY(sd_fs.Commit());
    } else if (!file_download && target_fs) {
        // if we dumped the save to ram, flush the data to file.
        const auto is_file_based_emummc = App::IsFileBaseEmummc();
        pbox->NewTransfer("Flushing zip to file");
        R_TRY(target_fs->CreateFile(temp_path, mz_mem.buf.size(), 0));

        fs::File file;
        R_TRY(target_fs->OpenFile(temp_path, FsOpenMode_Write, &file));

        R_TRY(thread::Transfer(pbox, mz_mem.buf.size(),
            [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                size = std::min<s64>(size, mz_mem.buf.size() - off);
                std::memcpy(data, mz_mem.buf.data() + off, size);
                *bytes_read = size;
                R_SUCCEED();
            },
            [&](const void* data, s64 off, s64 size) -> Result {
                const auto rc = file.Write(off, data, size, FsWriteOption_None);
                if (is_file_based_emummc) {
                    svcSleepThread(2e+6); // 2ms
                }
                return rc;
            }
        ));
    }

    R_SUCCEED();
}

struct SaveReaderContext {
    zlib_filefunc64_def base_funcs{};
    bool io_error{false};
    bool close_error{false};

    [[nodiscard]] bool HasError() const {
        return io_error || close_error;
    }

    void InitFileFunc(zlib_filefunc64_def* funcs) {
        mz::FileFuncStdio(&base_funcs);
        *funcs = base_funcs;
        funcs->opaque = this;
        funcs->zopen64_file = [](voidpf opaque, const void* filename, int mode) -> voidpf {
            auto self = static_cast<SaveReaderContext*>(opaque);
            return self->base_funcs.zopen64_file(self->base_funcs.opaque, filename, mode);
        };
        funcs->zread_file = [](voidpf opaque, voidpf stream, void* buf, uLong size) -> uLong {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.zread_file(self->base_funcs.opaque, stream, buf, size);
            if (res < size) {
                if (self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream)) {
                    self->io_error = true;
                }
            }
            return res;
        };
        funcs->zseek64_file = [](voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) -> long {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.zseek64_file(self->base_funcs.opaque, stream, offset, origin);
            if (res != 0) {
                self->io_error = true;
            }
            return res;
        };
        funcs->ztell64_file = [](voidpf opaque, voidpf stream) -> ZPOS64_T {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.ztell64_file(self->base_funcs.opaque, stream);
            if (res == static_cast<ZPOS64_T>(-1)) {
                self->io_error = true;
            } else if (self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream)) {
                self->io_error = true;
            }
            return res;
        };
        funcs->zclose_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = 0;
            if (self->base_funcs.zclose_file) {
                res = self->base_funcs.zclose_file(self->base_funcs.opaque, stream);
                if (res != 0) {
                    self->close_error = true;
                }
            }
            return res;
        };
        funcs->zerror_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = 0;
            if (self->base_funcs.zerror_file) {
                res = self->base_funcs.zerror_file(self->base_funcs.opaque, stream);
                if (res != 0) {
                    self->io_error = true;
                }
            }
            return res;
        };
    }
};

struct FolderSourceEntry {
    fs::FsPath full_path;
    s64 size{0};
};

static Result CheckedJoinPath(fs::FsPath& out, std::string_view base, std::string_view rel) {
    size_t base_len = base.size();
    while (base_len > 0 && base[base_len - 1] == '/') {
        base_len--;
    }
    size_t rel_len = rel.size();
    while (rel_len > 0 && rel[0] == '/') {
        rel.remove_prefix(1);
        rel_len--;
    }
    const size_t total_len = base_len + 1 + rel_len;
    if (total_len >= sizeof(fs::FsPath)) {
        return FsError_TooLongPath;
    }
    char buf[sizeof(fs::FsPath)];
    const int n = std::snprintf(buf, sizeof(buf), "%.*s/%.*s",
        static_cast<int>(base_len), base.data(),
        static_cast<int>(rel_len), rel.data());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        return FsError_TooLongPath;
    }
    out = buf;
    return 0;
}

static bool IsStagingParentOrRelated(std::string_view canonical_path) {
    if (canonical_path == "/" || canonical_path.empty()) {
        return true;
    }

    std::vector<std::string_view> comps;
    size_t start = 0;
    while (start < canonical_path.size()) {
        if (canonical_path[start] == '/') {
            start++;
            continue;
        }
        size_t end = canonical_path.find('/', start);
        if (end == std::string_view::npos) {
            end = canonical_path.size();
        }
        comps.push_back(canonical_path.substr(start, end - start));
        start = end;
    }

    if (comps.empty()) {
        return true;
    }

    for (const auto& c : comps) {
        if (c == "." || c == "..") {
            return true;
        }
    }

    const auto equals_ic = [](std::string_view a, const char* b) {
        const size_t b_len = std::strlen(b);
        return a.size() == b_len && !strncasecmp(a.data(), b, b_len);
    };

    if (comps.size() == 1) {
        if (equals_ic(comps[0], "dumps")) {
            return true;
        }
        return false;
    }

    if (equals_ic(comps[0], "dumps") && equals_ic(comps[1], "save-import")) {
        return true;
    }

    return false;
}

static Result StageBackupFolderToZip(
    ProgressBox* pbox,
    fs::FsNativeSd& sd_fs,
    const fs::FsPath& owned_stage_zip,
    const std::map<std::string, FolderSourceEntry>& source_files,
    const std::set<std::string>& source_dirs) {

    RecoveryStreamContext rec_ctx{};
    zlib_filefunc64_def file_func{};
    file_func.zopen64_file = RecoveryOpen;
    file_func.zread_file = RecoveryRead;
    file_func.zwrite_file = RecoveryWrite;
    file_func.ztell64_file = RecoveryTell;
    file_func.zseek64_file = RecoverySeek;
    file_func.zclose_file = RecoveryClose;
    file_func.zerror_file = RecoveryError;
    file_func.opaque = &rec_ctx;

    auto zfile = zipOpen2_64(owned_stage_zip.s, APPEND_STATUS_CREATE, NULL, &file_func);
    R_UNLESS(zfile, Result_ZipOpenNewFileInZip);
    bool zfile_open = true;
    ON_SCOPE_EXIT({
        if (zfile_open && zfile) {
            zipClose(zfile, NULL);
        }
    });

    const auto t = std::time(nullptr);
    const auto tm = std::localtime(&t);
    zip_fileinfo zip_info_default{};
    if (tm) {
        zip_info_default.tmz_date.tm_sec = tm->tm_sec;
        zip_info_default.tmz_date.tm_min = tm->tm_min;
        zip_info_default.tmz_date.tm_hour = tm->tm_hour;
        zip_info_default.tmz_date.tm_mday = tm->tm_mday;
        zip_info_default.tmz_date.tm_mon = tm->tm_mon;
        zip_info_default.tmz_date.tm_year = tm->tm_year;
    }

    for (const auto& dir_key : source_dirs) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }
        R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, dir_key.c_str(), &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
        bool dir_open = true;
        ON_SCOPE_EXIT({
            if (dir_open && zfile) {
                zipCloseFileInZip(zfile);
            }
        });
        dir_open = false;
        R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
        R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
    }

    std::vector<u8> buffer(64 * 1024);
    for (const auto& [file_key, file_info] : source_files) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        FsDirEntryType cur_type{};
        R_TRY(sd_fs.GetEntryType(file_info.full_path, &cur_type));
        R_UNLESS(cur_type == FsDirEntryType_File, FsError_TargetLocked);

        fs::File src_file;
        R_TRY(sd_fs.OpenFile(file_info.full_path, FsOpenMode_Read, &src_file));
        s64 cur_size = 0;
        R_TRY(src_file.GetSize(&cur_size));
        R_UNLESS(cur_size == file_info.size, FsError_TargetLocked);

        const auto level = Z_DEFAULT_COMPRESSION;
        R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, file_key.c_str(), &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, level), Result_ZipOpenNewFileInZip);
        bool file_open = true;
        ON_SCOPE_EXIT({
            if (file_open && zfile) {
                zipCloseFileInZip(zfile);
            }
        });

        if (file_info.size > 0) {
            s64 remaining = file_info.size;
            s64 offset = 0;
            while (remaining > 0) {
                if (pbox) {
                    R_TRY(pbox->ShouldExitResult());
                }
                const u64 chunk_to_read = std::min<u64>(remaining, buffer.size());
                u64 chunk_read_accum = 0;
                while (chunk_read_accum < chunk_to_read) {
                    if (pbox) {
                        R_TRY(pbox->ShouldExitResult());
                    }
                    u64 read_once = 0;
                    R_TRY(src_file.Read(offset + chunk_read_accum, buffer.data() + chunk_read_accum, chunk_to_read - chunk_read_accum, FsReadOption_None, &read_once));
                    if (read_once == 0 || read_once > (chunk_to_read - chunk_read_accum)) {
                        return FsError_InvalidSize;
                    }
                    chunk_read_accum += read_once;
                }

                const int write_rc = zipWriteInFileInZip(zfile, buffer.data(), chunk_read_accum);
                R_UNLESS(write_rc == ZIP_OK, Result_ZipWriteInFileInZip);
                R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);

                offset += chunk_read_accum;
                remaining -= chunk_read_accum;
            }
        }

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        s64 final_size = 0;
        R_TRY(src_file.GetSize(&final_size));
        R_UNLESS(final_size == file_info.size, FsError_TargetLocked);

        u8 trailing_buf[1]{};
        u64 trailing_read = 0;
        const auto trailing_rc = src_file.Read(file_info.size, trailing_buf, sizeof(trailing_buf), FsReadOption_None, &trailing_read);
        R_TRY(trailing_rc);
        R_UNLESS(trailing_read == 0, FsError_InvalidSize);

        src_file.Close();
        file_open = false;
        R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
        R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
    }

    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    zfile_open = false;
    const int close_rc = zipClose(zfile, NULL);
    R_UNLESS(close_rc == ZIP_OK, Result_ZipWriteInFileInZip);
    R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
    R_UNLESS(!rec_ctx.flush_failed, Result_FsUnknownStdioError);
    R_UNLESS(!rec_ctx.sync_failed, Result_FsUnknownStdioError);
    R_UNLESS(!rec_ctx.close_failed, Result_FsUnknownStdioError);
    R_UNLESS(rec_ctx.fp == nullptr, Result_FsUnknownStdioError);

    const auto sdmc_rc = fsdevCommitDevice("sdmc");
    R_TRY(sdmc_rc);

    const auto sd_commit_rc = sd_fs.Commit();
    if (R_FAILED(sd_commit_rc)) {
        return sd_commit_rc;
    }

    R_SUCCEED();
}

} // namespace

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty, bool* out_created_slot_retained);

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool* out_created_slot_retained) {
    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, false, out_created_slot_retained);
}

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started) {
    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, false, nullptr);
}

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty) {
    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, allow_empty, nullptr);
}

Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty, bool* out_created_slot_retained) {
    if (out_mutation_started) {
        *out_mutation_started = false;
    }
    if (out_created_slot_retained) {
        *out_created_slot_retained = false;
    }
    if (haze::IsRunning()) {
        return FsError_TargetLocked;
    }
    R_UNLESS(!e.is_backup, FsError_PathNotFound);
    R_UNLESS(e.save_data_id != 0 || e.is_planned_create, FsError_PathNotFound);

    SaveReaderContext source_reader_ctx;
    zlib_filefunc64_def file_func;
    source_reader_ctx.InitFileFunc(&file_func);

    auto zfile = unzOpen2_64(path, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    bool source_reader_open = true;
    ON_SCOPE_EXIT({
        if (source_reader_open && zfile) {
            unzClose(zfile);
        }
    });
    log_write("opened zip\n");

    const auto save_filter = [](const fs::FsPath& name, fs::FsPath& /*path*/) -> bool {
        // skip restoring the reserved meta files (sphaira and dbi).
        if (IsSaveReservedMetadataRoot(name.s)) {
            log_write("skipping meta\n");
            return false;
        }

        // restore everything else.
        return true;
    };

    thread::UnzipPayloadSummary summary{};
    thread::UnzipPayloadInventory source_inventory{};
    pbox->NewTransfer("Validating save..."_i18n);
    R_TRY(thread::TransferUnzipPreflight(pbox, zfile, "/", save_filter, true, &summary, &source_inventory, allow_empty));
    log_write("save preflight payload: %lld bytes, %lld files, %lld dirs\n",
        static_cast<long long>(summary.file_bytes),
        static_cast<long long>(summary.file_count),
        static_cast<long long>(summary.directory_count));

    DecodedSaveMetadata archive_meta{};
    Result meta_rc = 0;
    const auto meta_status = ReadArchiveSaveMetadata(zfile, pbox, archive_meta, &meta_rc);
    if (meta_status == ArchiveMetaStatus::Invalid) {
        R_THROW(R_FAILED(meta_rc) ? meta_rc : Result_UnzOpen2_64);
    }

    Entry target_entry = e;
    bool was_newly_created = false;
    if (e.is_planned_create) {
        R_TRY(pbox->ShouldExitResult());
        const auto create_res = CreateSaveDataChecked(e.creation_request, [pbox]() {
            return pbox && R_FAILED(pbox->ShouldExitResult());
        });
        if (create_res.create_succeeded) {
            if (out_mutation_started) *out_mutation_started = true;
            if (out_created_slot_retained) *out_created_slot_retained = true;
        }
        if (!create_res.verified) {
            return R_FAILED(create_res.rc) ? create_res.rc : FsError_PathNotFound;
        }
        was_newly_created = true;
        static_cast<FsSaveDataInfo&>(target_entry) = create_res.verified_info;
        target_entry.is_planned_create = false;
    }

    if (target_entry.save_data_id == 0) {
        return FsError_PathNotFound;
    }

    R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);

    FsSaveDataAttribute attr{};
    attr.application_id = target_entry.application_id;
    attr.uid = target_entry.uid;
    attr.system_save_data_id = target_entry.system_save_data_id;
    attr.save_data_type = target_entry.save_data_type;
    attr.save_data_rank = target_entry.save_data_rank;
    attr.save_data_index = target_entry.save_data_index;

    const auto save_data_space_id = static_cast<FsSaveDataSpaceId>(target_entry.save_data_space_id);
    const u64 target_save_data_id = target_entry.save_data_id;

    // Check if save filesystem already exists
    bool save_exists = false;
    {
        fs::FsNativeSave check_save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, false};
        const auto check_rc = check_save_fs.GetFsOpenResult();
        if (target_entry.save_data_id != 0) {
            R_TRY(check_rc);
            save_exists = true;
        } else {
            save_exists = R_SUCCEEDED(check_rc);
        }
    }
    R_UNLESS(save_exists, FsError_PathNotFound);

    FsSaveDataExtraData live{};
    R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&live, sizeof(live), save_data_space_id, target_save_data_id));

    if (live.attr.application_id != attr.application_id ||
        live.attr.uid.uid[0] != attr.uid.uid[0] ||
        live.attr.uid.uid[1] != attr.uid.uid[1] ||
        live.attr.system_save_data_id != attr.system_save_data_id ||
        live.attr.save_data_type != attr.save_data_type ||
        live.attr.save_data_rank != attr.save_data_rank ||
        live.attr.save_data_index != attr.save_data_index) {
        return FsError_PathNotFound;
    }

    if (live.data_size <= 0 || live.journal_size < 0) {
        return FsError_InvalidSize;
    }

    // ponytail: payload bytes are a rejection lower bound; implicit
    // directories, allocation, and journal overhead are not accounted for;
    // verified sizing/growth remains queued.
    if (summary.file_bytes > live.data_size) {
        return FsError_InvalidSize;
    }

    // Lexical RW scope: open the save file system for writing
    {
        fs::FsNativeSave save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, false};
        R_TRY(save_fs.GetFsOpenResult());

        filebrowser::FsDirCollections collections;
        if (target_entry.save_data_id != 0 && !was_newly_created) {
            filebrowser::FsDirCollections live_collections;
            R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", live_collections, true));

            // Validate live entries against reserved metadata and invalid characters
            for (const auto& col : live_collections) {
                if (col.path != "/") {
                    for (const char c : std::string_view{col.path.s}) {
                        if (IsInvalidSavePathChar(c)) {
                            return FsError_InvalidCharacter;
                        }
                    }
                }
                for (const auto& f : col.files) {
                    if (col.path == "/") {
                        if (!strcasecmp(f.name, NX_SAVE_META_NAME) ||
                            !strcasecmp(f.name, DBI_SAVE_INFO_NAME) ||
                            !strcasecmp(f.name, DBI_SAVE_EXTRA_NAME)) {
                            return FsError_PathAlreadyExists;
                        }
                    }
                    for (const char c : std::string_view{f.name}) {
                        if (IsInvalidSavePathChar(c)) {
                            return FsError_InvalidCharacter;
                        }
                    }
                }
            }

            // Build live inventory maps for bijection verification
            struct LiveFileInfo {
                fs::FsPath full_fs_path;
                s64 size = 0;
                bool verified = false;
            };
            std::map<std::string, LiveFileInfo> live_files;
            std::map<std::string, bool> live_dirs;
            s64 live_bytes_sum = 0;

            for (const auto& col : live_collections) {
                if (col.path != "/") {
                    const char* r = col.path.s;
                    while (*r == '/') r++;
                    if (*r != '\0') {
                        std::string dir_key{r};
                        if (!dir_key.ends_with('/')) dir_key += '/';
                        auto [dit, dinserted] = live_dirs.try_emplace(dir_key, false);
                        R_UNLESS(dinserted, FsError_PathAlreadyExists);
                    }
                }
                for (const auto& lf : col.files) {
                    R_UNLESS(lf.file_size >= 0, FsError_InvalidSize);
                    R_UNLESS(std::numeric_limits<s64>::max() - live_bytes_sum >= lf.file_size, FsError_InvalidSize);
                    fs::FsPath full_path = fs::AppendPath(col.path, lf.name);
                    const char* rf = full_path.s;
                    while (*rf == '/') rf++;
                    auto [fit, finserted] = live_files.try_emplace(std::string{rf}, LiveFileInfo{full_path, lf.file_size, false});
                    R_UNLESS(finserted, FsError_PathAlreadyExists);
                    live_bytes_sum += lf.file_size;
                }
            }

            R_UNLESS(live_dirs.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);
            R_UNLESS(live_files.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);

            // Collision-safe directory reservation on SD
            fs::FsNativeSd sd_fs;
            R_TRY(sd_fs.GetFsOpenResult());
            const auto rec_parent_rc = sd_fs.CreateDirectoryRecursively("/dumps/recovery");
            if (R_FAILED(rec_parent_rc) && rec_parent_rc != FsError_PathAlreadyExists) {
                return rec_parent_rc;
            }

            const auto now = std::time(nullptr);
            const auto tm = *std::localtime(&now);
            fs::FsPath owned_dir;
            bool reserved = false;
            for (u32 counter = 0; counter < 1000; counter++) {
                R_TRY(pbox->ShouldExitResult());
                char dir_buf[sizeof(fs::FsPath)];
                const int n = std::snprintf(dir_buf, sizeof(dir_buf), "/dumps/recovery/%04d%02d%02d_%02d%02d%02d_%016lX_%03u",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    e.save_data_id, counter);
                R_UNLESS(n > 0 && static_cast<size_t>(n) < sizeof(dir_buf), FsError_TooLongPath);

                const auto create_rc = sd_fs.CreateDirectory(dir_buf);
                if (R_SUCCEEDED(create_rc)) {
                    owned_dir = dir_buf;
                    reserved = true;
                    break;
                }
                if (create_rc != FsError_PathAlreadyExists) {
                    return create_rc;
                }
            }
            R_UNLESS(reserved, FsError_PathAlreadyExists);

            const fs::FsPath recovery_temp_path = owned_dir + "/recovery.zip.temp";
            const fs::FsPath recovery_final_path = owned_dir + "/recovery.zip";

            enum class RecoveryPubState {
                Unpublished,
                Renamed,
                Published
            };
            RecoveryPubState pub_state = RecoveryPubState::Unpublished;

            ON_SCOPE_EXIT({
                if (pub_state == RecoveryPubState::Unpublished) {
                    sd_fs.DeleteFile(recovery_temp_path);
                    sd_fs.DeleteDirectory(owned_dir);
                } else if (pub_state == RecoveryPubState::Renamed) {
                    sd_fs.DeleteFile(recovery_final_path);
                    sd_fs.DeleteDirectory(owned_dir);
                }
            });

            pbox->NewTransfer("Creating recovery backup..."_i18n);
            R_TRY(pbox->ShouldExitResult());
            R_TRY(WriteSaveBackupZip(pbox, &sd_fs, recovery_temp_path, &save_fs, e, live, live_collections, {}, false, true, true));

            // Reopen recovery backup and preflight
            SaveReaderContext rec_reader_ctx;
            zlib_filefunc64_def rec_file_func;
            rec_reader_ctx.InitFileFunc(&rec_file_func);
            auto rec_zfile = unzOpen2_64(recovery_temp_path, &rec_file_func);
            R_UNLESS(rec_zfile, Result_UnzOpen2_64);
            bool rec_reader_open = true;
            ON_SCOPE_EXIT({
                if (rec_reader_open && rec_zfile) {
                    unzClose(rec_zfile);
                }
            });

            thread::UnzipPayloadSummary rec_summary{};
            thread::UnzipPayloadInventory rec_inventory{};
            pbox->NewTransfer("Validating recovery backup..."_i18n);
            R_TRY(thread::TransferUnzipPreflight(pbox, rec_zfile, "/", save_filter, true, &rec_summary, &rec_inventory, true));

            const s64 live_dir_count = static_cast<s64>(live_dirs.size());
            const s64 live_file_count = static_cast<s64>(live_files.size());
            R_UNLESS(rec_summary.directory_count == live_dir_count, FsError_PathNotFound);
            R_UNLESS(rec_summary.file_count == live_file_count, FsError_PathNotFound);
            R_UNLESS(rec_summary.file_bytes == live_bytes_sum, FsError_InvalidSize);

            // Verified recovery archive check against native held mount
            R_TRY(thread::VerifyArchiveAgainstNative(pbox, rec_zfile, &save_fs, "/", rec_inventory, save_filter, true));

            DecodedSaveMetadata rec_meta{};
            Result rec_meta_rc = 0;
            const auto rec_meta_status = ReadArchiveSaveMetadata(rec_zfile, pbox, rec_meta, &rec_meta_rc);
            R_UNLESS(rec_meta_status == ArchiveMetaStatus::Valid, R_FAILED(rec_meta_rc) ? rec_meta_rc : Result_UnzOpen2_64);

            // Explicit checked close of candidate recovery reader BEFORE re-enumeration/publication
            rec_reader_open = false;
            R_UNLESS(UNZ_OK == unzClose(rec_zfile), Result_UnzOpen2_64);
            R_UNLESS(!rec_reader_ctx.HasError(), Result_UnzOpen2_64);

            // Re-enumerate live inventory from save_fs immediately before clear
            filebrowser::FsDirCollections current_collections;
            R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", current_collections, true));

            std::map<std::string, s64> curr_files;
            std::set<std::string> curr_dirs;
            for (const auto& col : current_collections) {
                if (col.path != "/") {
                    const char* r = col.path.s;
                    while (*r == '/') r++;
                    if (*r != '\0') {
                        std::string dir_key{r};
                        if (!dir_key.ends_with('/')) dir_key += '/';
                        auto [dit, dinserted] = curr_dirs.insert(dir_key);
                        R_UNLESS(dinserted, FsError_TargetLocked);
                    }
                }
                for (const auto& cf : col.files) {
                    R_UNLESS(cf.file_size >= 0, FsError_TargetLocked);
                    fs::FsPath c_full = fs::AppendPath(col.path, cf.name);
                    const char* rf = c_full.s;
                    while (*rf == '/') rf++;
                    auto [fit, finserted] = curr_files.try_emplace(std::string{rf}, cf.file_size);
                    R_UNLESS(finserted, FsError_TargetLocked);
                }
            }

            R_UNLESS(curr_dirs.size() == live_dirs.size(), FsError_TargetLocked);
            for (const auto& [d, _] : live_dirs) {
                R_UNLESS(curr_dirs.contains(d), FsError_TargetLocked);
            }
            R_UNLESS(curr_files.size() == live_files.size(), FsError_TargetLocked);
            for (const auto& [f, info] : live_files) {
                auto it = curr_files.find(f);
                R_UNLESS(it != curr_files.end(), FsError_TargetLocked);
                R_UNLESS(it->second == info.size, FsError_TargetLocked);
            }

            // Publish validated recovery archive using native primitive to separate rename from commit
            const auto rename_rc = fsFsRenameFile(&sd_fs.m_fs, recovery_temp_path, recovery_final_path);
            R_TRY(rename_rc);
            pub_state = RecoveryPubState::Renamed;

            const auto sdmc_rc = fsdevCommitDevice("sdmc");
            if (R_FAILED(sdmc_rc)) {
                return sdmc_rc;
            }

            const auto sd_commit_rc = sd_fs.Commit();
            if (R_FAILED(sd_commit_rc)) {
                return sd_commit_rc;
            }

            pub_state = RecoveryPubState::Published;
            if (out_recovery_path) {
                *out_recovery_path = recovery_final_path;
            }

            collections = std::move(current_collections);
        } else if (!was_newly_created) {
            R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", collections));
        }

        R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);
        R_TRY(pbox->ShouldExitResult());

        // Conservative mutation flag: mark mutation started immediately before clear
        if (out_mutation_started) {
            *out_mutation_started = true;
        }

        // ponytail: held mount and byte comparison verify correspondence at check time, but do not provide snapshot isolation against concurrent external mutation; true atomic save transactions require filesystem-level snapshot support.
        if (!was_newly_created) {
            R_TRY(filebrowser::FsView::DeleteAllCollections(pbox, &save_fs, collections));
            R_TRY(save_fs.Commit());
        }

        log_write("opened save file\n");
        // restore save data from zip.
        pbox->NewTransfer("Restoring save..."_i18n);
        const s64 target_journal_size = live.journal_size;
        if (!allow_empty || !source_inventory.files.empty() || !source_inventory.directories.empty()) {
            R_TRY(thread::TransferUnzipAll(pbox, zfile, &save_fs, "/", save_filter, thread::Mode::SingleThreadedIfSmaller, true, true, target_journal_size));
        }

        R_TRY(save_fs.Commit());
        log_write("finished save restore commit\n");
    } // End lexical RW scope: save_fs is destroyed here!

    // Reread live extra data to verify save identity after restore
    if (target_entry.save_data_id != 0) {
        FsSaveDataExtraData post_live{};
        R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&post_live, sizeof(post_live), save_data_space_id, target_save_data_id));
        if (post_live.attr.application_id != attr.application_id ||
            post_live.attr.uid.uid[0] != attr.uid.uid[0] ||
            post_live.attr.uid.uid[1] != attr.uid.uid[1] ||
            post_live.attr.system_save_data_id != attr.system_save_data_id ||
            post_live.attr.save_data_type != attr.save_data_type ||
            post_live.attr.save_data_rank != attr.save_data_rank ||
            post_live.attr.save_data_index != attr.save_data_index) {
            return FsError_PathNotFound;
        }
        if (post_live.data_size <= 0 || post_live.journal_size < 0) {
            return FsError_InvalidSize;
        }
    }

    // Open fresh read-only mount to verify restored save contents
    {
        pbox->NewTransfer("Verifying restored save..."_i18n);
        fs::FsNativeSave ro_save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, true};
        R_TRY(ro_save_fs.GetFsOpenResult());
        if (allow_empty) {
            R_TRY(thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, "/", source_inventory, save_filter, true, allow_empty));
        } else {
            R_TRY(thread::VerifyArchiveAgainstNative(pbox, zfile, &ro_save_fs, "/", source_inventory, save_filter, true));
        }
    }

    // Explicit checked close of source reader
    source_reader_open = false;
    R_UNLESS(UNZ_OK == unzClose(zfile), Result_UnzOpen2_64);
    R_UNLESS(!source_reader_ctx.HasError(), Result_UnzOpen2_64);

    log_write("finished save restore\n");
    R_SUCCEED();
}

Result RestoreSaveFolder(ProgressBox* pbox, const Entry& e, const fs::FsPath& folder_path, fs::FsPath* out_recovery_path, bool* out_mutation_started) {
    if (out_mutation_started) {
        *out_mutation_started = false;
    }
    if (haze::IsRunning()) {
        return FsError_TargetLocked;
    }
    R_UNLESS(!e.is_backup && e.save_data_id != 0, FsError_PathNotFound);

    // 1. Length bounds check with strnlen
    const size_t raw_len = strnlen(folder_path.s, sizeof(folder_path.s));
    if (raw_len == 0 || raw_len >= sizeof(folder_path.s)) {
        return FsError_TooLongPath;
    }

    const std::string_view raw_view{folder_path.s, raw_len};

    // 2. Must be absolute SD path starting with '/' and no mount prefixes (colons)
    if (raw_view.front() != '/' || raw_view.find(':') != std::string_view::npos) {
        return FsError_InvalidCharacter;
    }

    // 3. Reject duplicate slashes (//) anywhere in path
    if (raw_view.find("//") != std::string_view::npos) {
        return FsError_InvalidCharacter;
    }

    // 4. Character validation: no control chars, no 0x7F, no backslashes, no invalid save characters
    for (const char c : raw_view) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\' || IsInvalidSavePathChar(c)) {
            return FsError_InvalidCharacter;
        }
    }

    // 5. Canonical browser-compatible trailing-slash handling:
    // Single trailing slash on non-root paths is trimmed to canonical form without altering authority.
    std::string_view canonical_view = raw_view;
    if (canonical_view.size() > 1 && canonical_view.back() == '/') {
        canonical_view.remove_suffix(1);
    }

    if (canonical_view == "/" || canonical_view.empty()) {
        return FsError_PathNotFound;
    }

    // 6. Validate every component in canonical path
    {
        size_t start = 1;
        while (start < canonical_view.size()) {
            size_t end = canonical_view.find('/', start);
            if (end == std::string_view::npos) {
                end = canonical_view.size();
            }
            const std::string_view comp = canonical_view.substr(start, end - start);
            if (comp.empty() || comp == "." || comp == "..") {
                return FsError_InvalidCharacter;
            }
            start = end + 1;
        }
    }

    // 7. Reject SD root and staging ancestry / equality / descendant on canonical path BEFORE creating any directories
    if (IsStagingParentOrRelated(canonical_view)) {
        return FsError_PathAlreadyExists;
    }

    fs::FsPath canonical_folder_path;
    if (canonical_view.size() >= sizeof(canonical_folder_path.s)) {
        return FsError_TooLongPath;
    }
    std::memcpy(canonical_folder_path.s, canonical_view.data(), canonical_view.size());
    canonical_folder_path.s[canonical_view.size()] = '\0';

    // Independent FsNativeSd handle
    fs::FsNativeSd sd_fs;
    R_TRY(sd_fs.GetFsOpenResult());

    // Verify source exists and is a directory
    FsDirEntryType src_type{};
    R_TRY(sd_fs.GetEntryType(canonical_folder_path, &src_type));
    R_UNLESS(src_type == FsDirEntryType_Dir, FsError_PathNotFound);

    // Iterative worklist scan to checked EOF
    std::map<std::string, FolderSourceEntry> source_files;
    std::set<std::string> source_dirs;
    s64 total_source_file_bytes = 0;

    std::vector<std::string> dir_worklist;
    dir_worklist.push_back("");

    while (!dir_worklist.empty()) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        const std::string rel_dir = std::move(dir_worklist.back());
        dir_worklist.pop_back();

        fs::FsPath cur_full_dir;
        if (rel_dir.empty()) {
            cur_full_dir = canonical_folder_path;
        } else {
            R_TRY(CheckedJoinPath(cur_full_dir, canonical_folder_path.s, rel_dir));
        }

        fs::Dir dir;
        R_TRY(sd_fs.OpenDirectory(cur_full_dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir));

        constexpr size_t BATCH_SIZE = 32;
        FsDirectoryEntry batch[BATCH_SIZE];
        s64 read_count = 0;

        while (true) {
            if (pbox) {
                R_TRY(pbox->ShouldExitResult());
            }

            R_TRY(dir.Read(&read_count, BATCH_SIZE, batch));
            if (read_count == 0) {
                break;
            }
            if (read_count < 0 || static_cast<size_t>(read_count) > BATCH_SIZE) {
                return FsError_InvalidSize;
            }

            for (s64 i = 0; i < read_count; i++) {
                const auto& de = batch[i];
                const auto name_len = strnlen(de.name, sizeof(de.name));
                if (name_len == 0 || name_len >= sizeof(de.name)) {
                    return FsError_InvalidCharacter;
                }

                if (!std::strcmp(de.name, ".") || !std::strcmp(de.name, "..")) {
                    continue;
                }

                const std::string_view name_view{de.name, name_len};
                for (size_t c_idx = 0; c_idx < name_len; c_idx++) {
                    const char c = de.name[c_idx];
                    const auto uc = static_cast<unsigned char>(c);
                    if (uc < 0x20 || uc == 0x7F || c == '/' || c == '\\' || c == ':' || IsInvalidSavePathChar(c)) {
                        return FsError_InvalidCharacter;
                    }
                }

                if (de.type != FsDirEntryType_Dir && de.type != FsDirEntryType_File) {
                    return FsError_InvalidCharacter;
                }

                const size_t rel_entry_len = (rel_dir.empty() ? 0 : (rel_dir.size() + 1)) + name_len;
                if (de.type == FsDirEntryType_Dir) {
                    if (rel_entry_len + 1 >= sizeof(fs::FsPath)) {
                        return FsError_TooLongPath;
                    }
                } else {
                    if (rel_entry_len >= sizeof(fs::FsPath)) {
                        return FsError_TooLongPath;
                    }
                }

                const std::string rel_entry = rel_dir.empty() ? std::string(name_view) : (rel_dir + "/" + std::string(name_view));

                fs::FsPath full_entry_path;
                R_TRY(CheckedJoinPath(full_entry_path, cur_full_dir.s, name_view));

                if (de.type == FsDirEntryType_Dir) {
                    if (rel_dir.empty() && IsSaveReservedMetadataRoot(de.name)) {
                        return FsError_PathAlreadyExists;
                    }

                    const std::string dir_key = rel_entry + "/";
                    if (source_files.contains(rel_entry) || source_dirs.contains(dir_key)) {
                        return FsError_PathAlreadyExists;
                    }

                    size_t slash_pos = rel_entry.find('/');
                    while (slash_pos != std::string::npos) {
                        const std::string parent = rel_entry.substr(0, slash_pos);
                        if (source_files.contains(parent)) {
                            return FsError_PathAlreadyExists;
                        }
                        slash_pos = rel_entry.find('/', slash_pos + 1);
                    }

                    source_dirs.insert(dir_key);
                    dir_worklist.push_back(rel_entry);
                } else {
                    if (de.file_size < 0) {
                        return FsError_InvalidSize;
                    }
                    if (std::numeric_limits<s64>::max() - total_source_file_bytes < de.file_size) {
                        return FsError_InvalidSize;
                    }

                    if (source_files.contains(rel_entry) || source_dirs.contains(rel_entry + "/")) {
                        return FsError_PathAlreadyExists;
                    }

                    size_t slash_pos = rel_entry.find('/');
                    while (slash_pos != std::string::npos) {
                        const std::string parent = rel_entry.substr(0, slash_pos);
                        if (source_files.contains(parent)) {
                            return FsError_PathAlreadyExists;
                        }
                        slash_pos = rel_entry.find('/', slash_pos + 1);
                    }

                    source_files.emplace(rel_entry, FolderSourceEntry{full_entry_path, de.file_size});
                    total_source_file_bytes += de.file_size;
                }
            }
        }
    }

    R_UNLESS(source_files.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);
    R_UNLESS(source_dirs.size() <= static_cast<size_t>(std::numeric_limits<s64>::max()), FsError_InvalidSize);

    // Ensure staging parent directory exists
    const auto stage_parent_rc = sd_fs.CreateDirectoryRecursively("/dumps/save-import");
    if (R_FAILED(stage_parent_rc) && stage_parent_rc != FsError_PathAlreadyExists) {
        return stage_parent_rc;
    }

    // Reserve owned directory with checked native primitive and explicit commit
    const auto now = std::time(nullptr);
    const auto tm = *std::localtime(&now);
    fs::FsPath owned_dir;
    bool owned_dir_created = false;
    for (u32 counter = 0; counter < 1000; counter++) {
        R_TRY(pbox->ShouldExitResult());
        char dir_buf[sizeof(fs::FsPath)];
        const int n = std::snprintf(dir_buf, sizeof(dir_buf), "/dumps/save-import/%04d%02d%02d_%02d%02d%02d_%016lX_%03u",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            e.save_data_id, counter);
        R_UNLESS(n > 0 && static_cast<size_t>(n) < sizeof(dir_buf), FsError_TooLongPath);

        const auto prim_rc = fsFsCreateDirectory(&sd_fs.m_fs, dir_buf);
        if (R_SUCCEEDED(prim_rc)) {
            owned_dir = dir_buf;
            owned_dir_created = true;
            const auto commit_rc = sd_fs.Commit();
            if (R_FAILED(commit_rc)) {
                sd_fs.DeleteDirectory(owned_dir);
                return commit_rc;
            }
            break;
        }
        if (prim_rc != FsError_PathAlreadyExists) {
            return prim_rc;
        }
    }
    R_UNLESS(owned_dir_created, FsError_PathAlreadyExists);

    fs::FsPath owned_stage_zip;
    R_TRY(CheckedJoinPath(owned_stage_zip, owned_dir.s, "source.zip.temp"));

    bool stage_published = false;
    ON_SCOPE_EXIT({
        if (!stage_published) {
            if (!owned_stage_zip.empty()) {
                sd_fs.DeleteFile(owned_stage_zip);
            }
            if (owned_dir_created && !owned_dir.empty()) {
                sd_fs.DeleteDirectory(owned_dir);
            }
        }
    });

    // Stage source folder to temporary zip archive
    pbox->NewTransfer("Staging save backup..."_i18n);
    R_TRY(pbox->ShouldExitResult());
    R_TRY(StageBackupFolderToZip(pbox, sd_fs, owned_stage_zip, source_files, source_dirs));

    // Reopen finalized stage with SaveReaderContext for preflight and admission
    SaveReaderContext stage_reader_ctx;
    zlib_filefunc64_def stage_file_func;
    stage_reader_ctx.InitFileFunc(&stage_file_func);
    auto stage_zfile = unzOpen2_64(owned_stage_zip.s, &stage_file_func);
    R_UNLESS(stage_zfile, Result_UnzOpen2_64);
    bool stage_reader_open = true;
    ON_SCOPE_EXIT({
        if (stage_reader_open && stage_zfile) {
            unzClose(stage_zfile);
        }
    });

    const auto save_filter = [](const fs::FsPath& name, fs::FsPath& /*path*/) -> bool {
        if (IsSaveReservedMetadataRoot(name.s)) {
            return false;
        }
        return true;
    };

    thread::UnzipPayloadSummary stage_summary{};
    thread::UnzipPayloadInventory stage_inventory{};
    pbox->NewTransfer("Validating staged save..."_i18n);
    R_TRY(thread::TransferUnzipPreflight(pbox, stage_zfile, "/", save_filter, true, &stage_summary, &stage_inventory, true));

    DecodedSaveMetadata stage_meta{};
    Result stage_meta_rc = 0;
    const auto stage_meta_status = ReadArchiveSaveMetadata(stage_zfile, pbox, stage_meta, &stage_meta_rc);
    if (stage_meta_status == ArchiveMetaStatus::Invalid) {
        R_THROW(R_FAILED(stage_meta_rc) ? stage_meta_rc : Result_UnzOpen2_64);
    }

    // Compare exact staged payload inventory/sizes/dirs to source inventory with ONLY shared reserved ROOT metadata filtering
    size_t expected_payload_file_count = 0;
    for (const auto& [file_key, file_info] : source_files) {
        if (IsSaveReservedMetadataRoot(file_key)) {
            continue;
        }
        expected_payload_file_count++;
        const std::string staged_key = "/" + file_key;
        auto it = stage_inventory.files.find(staged_key);
        R_UNLESS(it != stage_inventory.files.end(), FsError_PathNotFound);
        R_UNLESS(it->second == file_info.size, FsError_InvalidSize);
    }
    R_UNLESS(stage_inventory.files.size() == expected_payload_file_count, FsError_PathNotFound);

    for (const auto& dir_key : source_dirs) {
        std::string dir_no_slash = dir_key;
        if (dir_no_slash.ends_with('/')) {
            dir_no_slash.pop_back();
        }
        const std::string staged_dir_key = "/" + dir_no_slash;
        R_UNLESS(stage_inventory.directories.contains(staged_dir_key), FsError_PathNotFound);
    }
    R_UNLESS(static_cast<s64>(source_dirs.size()) == stage_summary.directory_count, FsError_PathNotFound);

    stage_reader_open = false;
    R_UNLESS(UNZ_OK == unzClose(stage_zfile), Result_UnzOpen2_64);
    R_UNLESS(!stage_reader_ctx.HasError(), Result_UnzOpen2_64);

    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    // Invoke shared RestoreSaveZip for sole destructive restore ownership
    const auto restore_rc = RestoreSaveZip(pbox, e, owned_stage_zip, out_recovery_path, out_mutation_started, true);

    // Exact owned stage cleanup
    const auto del_file_rc = sd_fs.DeleteFile(owned_stage_zip);
    const auto del_dir_rc = sd_fs.DeleteDirectory(owned_dir);
    stage_published = true;

    if (R_FAILED(restore_rc)) {
        return restore_rc;
    }
    if (R_FAILED(del_file_rc)) {
        return del_file_rc;
    }
    if (R_FAILED(del_dir_rc)) {
        return del_dir_rc;
    }

    R_SUCCEED();
}

Result Menu::RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool* out_created_slot_retained) const {
    if (out_mutation_started) {
        *out_mutation_started = false;
    }
    if (out_created_slot_retained) {
        *out_created_slot_retained = false;
    }
    R_UNLESS(!e.is_backup && (e.save_data_id != 0 || e.is_planned_create), FsError_PathNotFound);
    pbox->SetTitle(e.GetName());
    if (e.image) {
        pbox->SetImage(e.image);
    } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
        pbox->SetImageDataConst(data->icon);
    } else {
        pbox->SetImage(0);
    }

    log_write("restoring save: %s\n", path.s);

    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

    if (IsDisaSaveFile(probe_fs, path)) {
        log_write("refusing unsupported raw DISA save restore: %s\n", path.s);
        return Result_RawSaveRestoreUnsupported;
    }

    return RestoreSaveZip(pbox, e, path, out_recovery_path, out_mutation_started, false, out_created_slot_retained);
}

Result Menu::BackupSaveInternal(ProgressBox* pbox, const dump::DumpLocation& location, const Entry& e, bool compressed, bool is_auto, const fs::FsPath& backup_root) const {
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

    // the save file may be empty, this isn't an error, but we exit early.
    R_UNLESS(!collections.empty(), 0x0);

    // non-system saves are written in the dbi backup format so that DBI can
    // restore them and vice versa. system saves keep the sphaira format.
    const auto dbi_format = !IsSystemLikeSave(e.save_data_type);

    const auto now = std::time(NULL);
    const auto now_tm = *std::localtime(&now);

    const auto dbi_base = (backup_root == "/dumps" || backup_root == DEFAULT_BACKUP_ROOT)
        ? fs::FsPath{DBI_SAVES_PATH}
        : backup_root;

    const auto path = dbi_format
        ? fs::AppendPath(fs->Root(), BuildDbiSavePath(e, now_tm, dbi_base))
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
        R_SUCCEED();
    } else {
        const auto temp_path = path + ".temp";

        fs->CreateDirectoryRecursivelyWithPath(temp_path);
        ON_SCOPE_EXIT(fs->DeleteFile(temp_path));

        R_TRY(WriteSaveBackupZip(pbox, fs.get(), temp_path, &save_fs, e, extra, collections, GetAccountName(e.uid), dbi_format, compressed, false, false));

        fs->DeleteFile(path);
        R_TRY(fs->RenameFile(temp_path, path));

        R_SUCCEED();
    }
}

void Menu::SyncSavesRemote() {
    const auto webdav_locations = GetWebdavLocations();

    if (webdav_locations.empty()) {
        App::Push<OptionBox>("No WebDAV network location configured for sync. Add one in settings."_i18n, "OK"_i18n);
        return;
    }

    const auto seeds = GetSelectedEntries();
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
    const auto seeds = GetSelectedEntries();
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
                for (const auto& p : CollectDbiBackups(&sd_fs, e)) {
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
