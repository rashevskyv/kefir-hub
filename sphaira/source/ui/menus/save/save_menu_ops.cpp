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

#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"

#include <utility>
#include <cstring>
#include <algorithm>
#include <set>
#include <minIni.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::ui::menu::save {
namespace {

constexpr u32 NX_SAVE_META_MAGIC = 0x4A4B5356; // JKSV
constexpr u32 NX_SAVE_META_VERSION = 1;
constexpr const char* NX_SAVE_META_NAME = ".nx_save_meta.bin";

// https://github.com/J-D-K/JKSV/issues/264#issuecomment-2618962807
struct NXSaveMeta {
    u32 magic{}; // NX_SAVE_META_MAGIC
    u32 version{}; // NX_SAVE_META_VERSION
    FsSaveDataAttribute attr{}; // FsSaveDataExtraData::attr
    u64 owner_id{}; // FsSaveDataExtraData::owner_id
    u64 timestamp{}; // FsSaveDataExtraData::timestamp
    u32 flags{}; // FsSaveDataExtraData::flags
    u32 unk_x54{}; // FsSaveDataExtraData::unk_x54
    s64 data_size{}; // FsSaveDataExtraData::data_size
    s64 journal_size{}; // FsSaveDataExtraData::journal_size
    u64 commit_id{}; // FsSaveDataExtraData::commit_id
    u64 raw_size{}; // FsSaveDataInfo::size
};
static_assert(sizeof(NXSaveMeta) == 128);

auto ProbeWebdavLocation(const location::Entry& loc) -> Result {
    curl::Api api(CURL_LOCATION_TO_API(loc));
    const auto result = curl::Probe(api, curl::ProbeType::Webdav);
    log_write("[SYNC] WebDAV probe for %s: success=%d code=%ld\n", loc.name.c_str(), result.success, result.code);
    if (!result.success) {
        R_THROW(Result_SaveSyncFailed);
    }
    R_SUCCEED();
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

void Menu::BackupSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    App::Push<ProgressBox>(0, "Backup"_i18n, "", [this, entries, location, backup_root](auto pbox) mutable -> Result {
        for (auto& e : entries) {
            // the entry may not have loaded yet.
            detail::LoadControlEntry(e);
            R_TRY(BackupSaveInternal(pbox, location, e, m_compress_save_backup.Get(), false, backup_root));
        }
        R_SUCCEED();
    }, [this, entries, location, backup_root](Result rc){
        App::PushErrorBox(rc, "Backup failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Backup successful!"_i18n);

            if (m_save_autosync.Get()) {
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
    auto restored = std::make_shared<size_t>(0);
    auto skipped = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, entries, location, backup_root, restored, skipped](auto pbox) mutable -> Result {
        const auto fs = MakeFsForLocation(location);

        for (auto& e : entries) {
            detail::LoadControlEntry(e);

            fs::FsPath file_path;
            if (!FindLatestBackupPath(fs.get(), e, backup_root, file_path)) {
                (*skipped)++;
                continue;
            }

            if (m_auto_backup_on_restore.Get()) {
                pbox->SetActionName("Auto backup"_i18n);
                R_TRY(BackupSaveInternal(pbox, location, e, m_compress_save_backup.Get(), true, backup_root));
            }

            pbox->SetActionName("Restore"_i18n);
            R_TRY(RestoreSaveInternal(pbox, e, file_path));
            (*restored)++;
        }

        R_SUCCEED();
    }, [restored, skipped](Result rc){
        App::PushErrorBox(rc, "Restore failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            if (*restored) {
                App::Notify("Restore successful!"_i18n);
            } else {
                App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
            }

            if (*skipped) {
                App::Notify(std::to_string(*skipped) + " saves skipped");
            }
        }
    });
}

void Menu::DeleteSaves(std::vector<Entry> entries) {
    if (entries.empty()) {
        return;
    }

    auto deleted_count = std::make_shared<size_t>(0);
    auto failed_count = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Deleting saves..."_i18n, "", [this, entries, deleted_count, failed_count](auto pbox) mutable -> Result {
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
                    sd_fs.DeleteFile(b.path);
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

                (*deleted_count)++;
            } else {
                pbox->SetActionName("Deleting save data..."_i18n);
                const auto space_id = static_cast<FsSaveDataSpaceId>(
                    IsSystemLikeSave(e.save_data_type) ? FsSaveDataSpaceId_System :
                    e.save_data_space_id ? e.save_data_space_id : FsSaveDataSpaceId_User
                );

                Result rc = 0;
                if (e.save_data_id != 0) {
                    rc = fsDeleteSaveDataFileSystemBySaveDataSpaceId(space_id, e.save_data_id);
                    log_write("[SAVE] fsDeleteSaveDataFileSystemBySaveDataSpaceId(0x%x, 0x%016lX): 0x%x\n", space_id, e.save_data_id, rc);
                }

                if (e.save_data_id == 0 || R_FAILED(rc)) {
                    FsSaveDataAttribute attr{};
                    attr.application_id = e.application_id;
                    attr.uid = e.uid;
                    attr.system_save_data_id = e.system_save_data_id;
                    attr.save_data_type = e.save_data_type;
                    attr.save_data_rank = e.save_data_rank;
                    attr.save_data_index = e.save_data_index;

                    Result rc2 = fsDeleteSaveDataFileSystemBySaveDataAttribute(space_id, &attr);
                    log_write("[SAVE] fsDeleteSaveDataFileSystemBySaveDataAttribute: 0x%x\n", rc2);
                    if (R_SUCCEEDED(rc2)) {
                        rc = 0;
                    }
                }

                if (R_SUCCEEDED(rc)) {
                    (*deleted_count)++;
                } else {
                    (*failed_count)++;
                }
            }
        }
        R_SUCCEED();
    }, [this, deleted_count, failed_count](Result rc) {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Delete failed!"_i18n);
        } else if (*failed_count > 0 && *deleted_count == 0) {
            App::Push<OptionBox>("Failed to delete save data."_i18n, "OK"_i18n);
        } else {
            App::Notify("Delete successful!"_i18n);
        }

        ClearSelection();
        ScanHomebrew();
    });
}

void Menu::StartRestore(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    // multi-select keeps the existing "latest of each" behaviour; the backup
    // picker (and thus the remote pre-download) is only for a single save.
    if (entries.size() != 1) {
        RestoreSaves(std::move(entries), location, backup_root);
        return;
    }

    Entry e = entries.front();

    if (!m_restore_include_remote.Get()) {
        ShowRestorePicker(std::move(e), location, backup_root, {});
        return;
    }

    const auto webdav_locations = GetWebdavLocations();
    if (webdav_locations.empty()) {
        // toggle is on but nothing is configured: fall back to the local
        // picker silently. Save Options -> Sync with remote already surfaces
        // the "add a WebDAV location" warning for this same condition; a
        // second OptionBox here only ended up hidden behind the picker we'd
        // push right after it.
        ShowRestorePicker(std::move(e), location, backup_root, {});
        return;
    }

    // download-only sync for this save, then show the picker with anything that
    // was pulled from the remote flagged. the picker is shown from the done
    // callback so it runs on the UI thread after the transfer finishes.
    const auto run = [this, e, location, backup_root](const location::Entry& loc) {
        auto downloaded = std::make_shared<std::vector<std::string>>();
        App::Push<ProgressBox>(0, "Syncing saves..."_i18n, "",
            [this, e, loc, location, backup_root, downloaded](auto pbox) mutable -> Result {
                pbox->SetHideSpeed(true);
                return DownloadRemoteBackupsForEntry(pbox, loc, location, e, backup_root, downloaded.get());
            },
            [this, e, location, backup_root, downloaded](Result rc) mutable {
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Sync failed!"_i18n);
                }
                // show the picker regardless: on failure fall back to whatever
                // backups are already on the console.
                ShowRestorePicker(std::move(e), location, backup_root, std::move(*downloaded));
            });
    };

    if (webdav_locations.size() == 1) {
        run(webdav_locations.front());
    } else {
        PopupList::Items items;
        for (const auto& loc : webdav_locations) {
            std::string proto = loc.protocol;
            if (proto.empty()) {
                if (loc.url.starts_with("webdav://") || loc.url.starts_with("webdavs://")) proto = "webdav";
                else if (loc.url.starts_with("http://") || loc.url.starts_with("https://")) proto = "webdav";
            }
            std::string proto_upper = proto;
            std::transform(proto_upper.begin(), proto_upper.end(), proto_upper.begin(), ::toupper);
            items.emplace_back(loc.name + " (" + proto_upper + ")");
        }
        App::Push<PopupList>("Select Sync Location"_i18n, items, [webdav_locations, run](auto op_index) {
            if (op_index) {
                run(webdav_locations[*op_index]);
            }
        });
    }
}

void Menu::ShowRestorePicker(Entry e, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::vector<std::string> remote_names) {
    detail::LoadControlEntry(e);

    // CollectBackups scans every backup directory/format and, for Account/Cache
    // saves, opens every matching dbi zip to verify it belongs to this entry -
    // real directory/zip I/O that must not run on the render thread. Run it in
    // a ProgressBox worker like every other backup/restore scan, and build the
    // actual popup from the done callback (UI thread) once it's finished.
    auto candidates = std::make_shared<std::vector<BackupCandidate>>();
    App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, e, location, backup_root, candidates](auto pbox) mutable -> Result {
        pbox->SetTitle(e.GetName());
        const auto fs = MakeFsForLocation(location);
        *candidates = CollectBackups(fs.get(), e, backup_root);
        R_SUCCEED();
    }, [this, e, location, backup_root, remote_names, candidates](Result rc) mutable {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Restore failed!"_i18n);
            return;
        }
        ShowRestorePickerPopup(std::move(e), location, backup_root, std::move(remote_names), std::move(*candidates));
    });
}

void Menu::ShowRestorePickerPopup(Entry e, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::vector<std::string> remote_names, std::vector<BackupCandidate> candidates) {
    if (candidates.empty()) {
        App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
        return;
    }

    // exactly one archive: nothing to choose, restore it straight away.
    if (candidates.size() == 1) {
        RestoreSavesPicked(std::move(e), location, backup_root, candidates.front().path);
        return;
    }

    const std::set<std::string> remote_set{remote_names.begin(), remote_names.end()};

    // ts == 0 means the name didn't parse to a date (renamed by hand or by
    // another tool) - show the raw file name instead of a bogus all-zero date
    // so the archive stays pickable rather than silently unlisted.
    const auto label_for = [](const BackupCandidate& c) -> std::string {
        if (c.ts == 0) {
            const auto name = std::strrchr(c.path.s, '/');
            return name ? name + 1 : c.path.s;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04u.%02u.%02u  %02u:%02u:%02u",
            (u32)(c.ts / 10000000000ULL), (u32)(c.ts / 100000000ULL % 100), (u32)(c.ts / 1000000ULL % 100),
            (u32)(c.ts / 10000ULL % 100), (u32)(c.ts / 100ULL % 100), (u32)(c.ts % 100));
        return buf;
    };

    PopupList::Items items;
    std::vector<bool> markers;
    for (const auto& c : candidates) {
        const auto name = std::strrchr(c.path.s, '/');
        const std::string base = name ? name + 1 : c.path.s;

        items.emplace_back(label_for(c));
        markers.emplace_back(remote_set.contains(base));
    }

    const bool any_remote = std::ranges::any_of(markers, [](bool b){ return b; });

    auto popup = std::make_unique<PopupList>("Select backup"_i18n, items,
        [this, e, location, backup_root, candidates](auto op_index) mutable {
            if (op_index) {
                RestoreSavesPicked(std::move(e), location, backup_root, candidates[*op_index].path);
            }
        });
    if (any_remote) {
        popup->SetRemoteMarkers(std::move(markers));
    }
    App::Push(std::move(popup));
}

void Menu::RestoreSavesPicked(Entry e, const dump::DumpLocation& location, const fs::FsPath& backup_root, fs::FsPath chosen) {
    App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, e, location, backup_root, chosen](auto pbox) mutable -> Result {
        detail::LoadControlEntry(e);

        if (m_auto_backup_on_restore.Get()) {
            pbox->SetActionName("Auto backup"_i18n);
            R_TRY(BackupSaveInternal(pbox, location, e, m_compress_save_backup.Get(), true, backup_root));
        }

        pbox->SetActionName("Restore"_i18n);
        R_TRY(RestoreSaveInternal(pbox, e, chosen));
        R_SUCCEED();
    }, [](Result rc){
        App::PushErrorBox(rc, "Restore failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Restore successful!"_i18n);
        }
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

Result Menu::RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path) const {
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
        log_write("restoring raw DISA save: %s\n", path.s);
        fs::File src_file;
        R_TRY(probe_fs->OpenFile(path, FsOpenMode_Read, &src_file));
        s64 src_size{};
        R_TRY(src_file.GetSize(&src_size));

        const auto partition_id = IsSystemLikeSave(e.save_data_type) ? FsBisPartitionId_System : FsBisPartitionId_User;
        fs::FsNativeBis bis_fs{partition_id};
        R_TRY(bis_fs.GetFsOpenResult());

        char target_path[64];
        std::snprintf(target_path, sizeof(target_path), "/save/%016lX", e.save_data_id);

        bis_fs.DeleteFile(target_path);
        R_TRY(bis_fs.CreateFile(target_path, src_size, 0));

        fs::File dst_file;
        R_TRY(bis_fs.OpenFile(target_path, FsOpenMode_Write, &dst_file));

        pbox->NewTransfer("Restoring raw save..."_i18n);
        pbox->UpdateTransfer(0, src_size);

        R_TRY(thread::Transfer(pbox, src_size,
            [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                return src_file.Read(off, data, size, FsReadOption_None, bytes_read);
            },
            [&](const void* data, s64 off, s64 size) -> Result {
                return dst_file.Write(off, data, size, FsWriteOption_None);
            }
        ));

        R_TRY(bis_fs.Commit());
        log_write("finished raw save restore\n");
        R_SUCCEED();
    }

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(path, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));
    log_write("opened zip\n");

    std::optional<NXSaveMeta> meta{};

    // get manifest
    if (UNZ_END_OF_LIST_OF_FILE != unzLocateFile(zfile, NX_SAVE_META_NAME, 0)) {
        log_write("found meta file\n");
        if (UNZ_OK == unzOpenCurrentFile(zfile)) {
            log_write("opened meta file\n");
            ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

            NXSaveMeta temp_meta;
            const auto len = unzReadCurrentFile(zfile, &temp_meta, sizeof(temp_meta));
            if (len == sizeof(temp_meta) && temp_meta.magic == NX_SAVE_META_MAGIC && temp_meta.version == NX_SAVE_META_VERSION) {
                meta = temp_meta;
                log_write("loaded meta!\n");
            }
        }
    }

    // dbi backups store the raw FsSaveDataExtraData instead of the sphaira meta.
    std::optional<FsSaveDataExtraData> dbi_extra{};
    if (!meta.has_value() && UNZ_END_OF_LIST_OF_FILE != unzLocateFile(zfile, DBI_SAVE_EXTRA_NAME, 2)) {
        if (UNZ_OK == unzOpenCurrentFile(zfile)) {
            ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

            FsSaveDataExtraData temp{};
            if (sizeof(temp) == unzReadCurrentFile(zfile, &temp, sizeof(temp))) {
                dbi_extra = temp;
                log_write("loaded dbi save extra data\n");
            }
        }
    }

    FsSaveDataAttribute attr{};
    attr.application_id = e.application_id;
    attr.uid = e.uid;
    attr.system_save_data_id = e.system_save_data_id;
    attr.save_data_type = e.save_data_type;
    attr.save_data_rank = e.save_data_rank;
    attr.save_data_index = e.save_data_index;

    s64 data_size = 0;
    s64 journal_size = 0;
    u64 owner_id = 0;
    u32 flags = 0;

    if (meta.has_value()) {
        if (!attr.application_id) attr.application_id = meta->attr.application_id;
        if (!attr.system_save_data_id) attr.system_save_data_id = meta->attr.system_save_data_id;
        if (!attr.save_data_type) attr.save_data_type = meta->attr.save_data_type;
        if (attr.uid.uid[0] == 0 && attr.uid.uid[1] == 0) attr.uid = meta->attr.uid;
        data_size = meta->data_size;
        journal_size = meta->journal_size;
        owner_id = meta->owner_id;
        flags = meta->flags;
    } else if (dbi_extra.has_value()) {
        if (!attr.application_id) attr.application_id = dbi_extra->attr.application_id;
        if (!attr.system_save_data_id) attr.system_save_data_id = dbi_extra->attr.system_save_data_id;
        if (!attr.save_data_type) attr.save_data_type = dbi_extra->attr.save_data_type;
        if (attr.uid.uid[0] == 0 && attr.uid.uid[1] == 0) attr.uid = dbi_extra->attr.uid;
        data_size = dbi_extra->data_size;
        journal_size = dbi_extra->journal_size;
        owner_id = dbi_extra->owner_id;
        flags = dbi_extra->flags;
    }

    const auto save_data_space_id = static_cast<FsSaveDataSpaceId>(
        IsSystemLikeSave(attr.save_data_type) ? FsSaveDataSpaceId_System :
        e.save_data_space_id ? e.save_data_space_id : FsSaveDataSpaceId_User
    );

    // Check if save filesystem already exists or needs to be created
    fs::FsNativeSave check_save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, false};
    if (R_FAILED(check_save_fs.GetFsOpenResult())) {
        log_write("save filesystem does not exist or cannot be opened, creating save...\n");

        FsSaveDataCreationInfo creation_info{};
        creation_info.save_data_size = data_size > 0 ? data_size : 0x200000;
        creation_info.journal_size = journal_size > 0 ? journal_size : 0x200000;
        creation_info.available_size = 0x4000;
        creation_info.owner_id = owner_id ? owner_id : (attr.application_id ? attr.application_id : 0);
        creation_info.flags = flags;
        creation_info.save_data_space_id = save_data_space_id;

        FsSaveDataMetaInfo meta_info{};
        meta_info.size = sizeof(FsSaveDataMetaInfo);
        meta_info.type = FsSaveDataMetaType_None;

        Result create_rc = 0;
        if (IsSystemLikeSave(attr.save_data_type)) {
            create_rc = fsCreateSaveDataFileSystemBySystemSaveDataId(&attr, &creation_info);
        } else {
            create_rc = fsCreateSaveDataFileSystem(&attr, &creation_info, &meta_info);
        }
        log_write("fsCreateSaveDataFileSystem result: 0x%x\n", create_rc);
        if (R_FAILED(create_rc)) {
            R_TRY(create_rc);
        }
    } else {
        if (e.save_data_id != 0) {
            if (data_size > 0 && journal_size > 0) {
                log_write("extending save file\n");
                fsExtendSaveDataFileSystem(save_data_space_id, e.save_data_id, data_size, journal_size);
                log_write("extended save file\n");
            } else {
                FsSaveDataExtraData extra{};
                if (R_SUCCEEDED(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), save_data_space_id, e.save_data_id)) && extra.journal_size > 0) {
                    log_write("doing manual meta parse\n");
                    s64 total_size = 0;
                    unz_global_info64 ginfo{};
                    if (UNZ_OK == unzGetGlobalInfo64(zfile, &ginfo) && UNZ_OK == unzGoToFirstFile(zfile)) {
                        for (s64 i = 0; i < ginfo.number_entry; i++) {
                            if (i > 0 && UNZ_OK != unzGoToNextFile(zfile)) break;
                            unz_file_info64 info{};
                            fs::FsPath name{};
                            if (UNZ_OK == unzGetCurrentFileInfo64(zfile, &info, name, sizeof(name), 0, 0, 0, 0)) {
                                if (name != NX_SAVE_META_NAME && strcasecmp(name.s, DBI_SAVE_INFO_NAME) && strcasecmp(name.s, DBI_SAVE_EXTRA_NAME)) {
                                    total_size += info.uncompressed_size;
                                }
                            }
                        }
                        const auto rounded_size = total_size + (total_size % extra.journal_size);
                        log_write("extending manual meta parse\n");
                        fsExtendSaveDataFileSystem(save_data_space_id, e.save_data_id, rounded_size, extra.journal_size);
                        log_write("extended manual meta parse\n");
                    }
                }
            }
        }
    }

    // open the save file system for writing
    fs::FsNativeSave save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, false};
    R_TRY(save_fs.GetFsOpenResult());

    // delete all files in save.
    filebrowser::FsDirCollections collections;
    R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", collections));
    R_TRY(filebrowser::FsView::DeleteAllCollections(pbox, &save_fs, collections));

    log_write("opened save file\n");
    // restore save data from zip.
    pbox->NewTransfer("Restoring save..."_i18n);
    R_TRY(thread::TransferUnzipAll(pbox, zfile, &save_fs, "/", [&](const fs::FsPath& name, fs::FsPath& path) -> bool {
        // skip restoring the meta files (sphaira and dbi).
        if (name == NX_SAVE_META_NAME || !strcasecmp(name.s, DBI_SAVE_INFO_NAME) || !strcasecmp(name.s, DBI_SAVE_EXTRA_NAME)) {
            log_write("skipping meta\n");
            return false;
        }

        // restore everything else.
        log_write("restoring: %s\n", path.s);
        return true;
    }));

    R_TRY(save_fs.Commit());
    log_write("finished save restore\n");
    R_SUCCEED();
}

Result Menu::BackupSaveInternal(ProgressBox* pbox, const dump::DumpLocation& location, const Entry& e, bool compressed, bool is_auto, const fs::FsPath& backup_root) const {
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

    // try and open the save file system
    fs::FsNativeSave save_fs{(FsSaveDataType)e.save_data_type, save_data_space_id, &attr, true};
    R_TRY(save_fs.GetFsOpenResult());

    // get a list of collections.
    filebrowser::FsDirCollections collections;
    R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", collections));

    // the save file may be empty, this isn't an error, but we exit early.
    R_UNLESS(!collections.empty(), 0x0);

    const auto t = (time_t)extra.timestamp;
    const auto tm = std::localtime(&t);

    // pre-calculate the time rather than calculate it in the loop.
    zip_fileinfo zip_info_default{};
    zip_info_default.tmz_date.tm_sec = tm->tm_sec;
    zip_info_default.tmz_date.tm_min = tm->tm_min;
    zip_info_default.tmz_date.tm_hour = tm->tm_hour;
    zip_info_default.tmz_date.tm_mday = tm->tm_mday;
    zip_info_default.tmz_date.tm_mon = tm->tm_mon;
    zip_info_default.tmz_date.tm_year = tm->tm_year;

    // non-system saves are written in the dbi backup format so that DBI can
    // restore them and vice versa. system saves keep the sphaira format.
    const auto dbi_format = !IsSystemLikeSave(e.save_data_type);

    const auto now = std::time(NULL);
    const auto now_tm = *std::localtime(&now);

    const auto path = dbi_format
        ? fs::AppendPath(fs->Root(), BuildDbiSavePath(e, now_tm))
        : fs::AppendPath(fs->Root(), BuildSavePath(e, is_auto, backup_root));
    const auto temp_path = path + ".temp";

    fs->CreateDirectoryRecursivelyWithPath(temp_path);
    ON_SCOPE_EXIT(fs->DeleteFile(temp_path));

    // zip to memory if less than 1GB and not applet mode.
    // TODO: use my mmz code from ftpsrv to stream zip creation.
    // this will allow for zipping to memory and flushing every X bytes
    // such as flushing every 8MB.
    const auto file_download = App::IsApplet() || e.size >= 1024ULL * 1024ULL * 1024ULL;

    mz::MzMem mz_mem{};
    zlib_filefunc64_def file_func;
    if (!file_download) {
        mz::FileFuncMem(&mz_mem, &file_func);
    } else {
        mz::FileFuncStdio(&file_func);
    }

    {
        auto zfile = zipOpen2_64(temp_path, APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zfile, Result_ZipOpen2_64);
        ON_SCOPE_EXIT(zipClose(zfile, "sphaira v" APP_VERSION_HASH));

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
            ON_SCOPE_EXIT(zipCloseFileInZip(zfile));
            R_UNLESS(ZIP_OK == zipWriteInFileInZip(zfile, &meta, sizeof(meta)), Result_ZipWriteInFileInZip);
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
                zipCloseFileInZip(zfile);
            }
        }

        const auto zip_add = [&](const fs::FsPath& file_path) -> Result {
            const char* file_name_in_zip = file_path.s;

            // strip root path (/ or ums0:)
            if (!std::strncmp(file_name_in_zip, save_fs.Root(), std::strlen(save_fs.Root()))) {
                file_name_in_zip += std::strlen(save_fs.Root());
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
            ON_SCOPE_EXIT(zipCloseFileInZip(zfile));

            return thread::TransferZip(pbox, zfile, &save_fs, file_path);
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
                ON_SCOPE_EXIT(zipCloseFileInZip(zfile));
                R_UNLESS(ZIP_OK == zipWriteInFileInZip(zfile, data, size), Result_ZipWriteInFileInZip);
                R_SUCCEED();
            };

            const auto account = e.save_data_type == FsSaveDataType_Account
                ? GetAccountName(e.uid)
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
    }

    // if we dumped the save to ram, flush the data to file.
    const auto is_file_based_emummc = App::IsFileBaseEmummc();
    if (!file_download) {
        pbox->NewTransfer("Flushing zip to file");
        R_TRY(fs->CreateFile(temp_path, mz_mem.buf.size(), 0));

        fs::File file;
        R_TRY(fs->OpenFile(temp_path, FsOpenMode_Write, &file));

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

    fs->DeleteFile(path);
    R_TRY(fs->RenameFile(temp_path, path));

    R_SUCCEED();
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
