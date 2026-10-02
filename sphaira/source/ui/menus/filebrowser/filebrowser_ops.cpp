#include "ui/menus/filebrowser.hpp"
#include "path_util.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "download.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#if ENABLE_NETWORK_INSTALL
#include "ui/menus/dbi_menu.hpp"
#endif
#include "ui/error_box.hpp"
#include "log.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "nro.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "web.hpp"
#include "yati/yati.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "title_info.hpp"
#include "haze_helper.hpp"
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <memory>
#include <ranges>
#include <algorithm>
#include <vector>
#include <string>

namespace sphaira::ui::menu::filebrowser {

using namespace detail;

void FsView::RestoreSaveFile(const FileEntry& entry) {
    const auto file_path = GetNewPath(entry);
    const bool is_dir = entry.IsDir();
    const bool is_disa = !is_dir && save::IsDisaSaveFile(m_fs.get(), file_path);
    const bool is_zip = !is_dir && std::string_view{entry.name}.ends_with(".zip");

    if (is_dir) {
        if (!IsSd()) {
            App::Push<OptionBox>("Not a valid save backup directory."_i18n, "OK"_i18n);
            return;
        }
    } else if (is_disa) {
        App::Push<OptionBox>(save::GetRawRestoreUnsupportedMessage(), "OK"_i18n);
        return;
    } else if (!is_zip) {
        App::Push<OptionBox>("Not a valid save backup file."_i18n, "OK"_i18n);
        return;
    }

    if (!is_disa && !haze::ReleaseSaveMounts()) { // only while the PC has a save open over MTP
        App::Push<OptionBox>("MTP is currently active. Please close the running game and disable MTP before restoring save data."_i18n, "OK"_i18n);
        return;
    }

    struct SaveOption {
        save::Entry entry;
        std::string label;
    };
    std::vector<SaveOption> save_options;

    const auto accounts = App::GetAccountList();
    std::set<std::string> seen_keys;

    const auto add_candidates = [&](const std::vector<FsSaveDataInfo>& infos, const std::string& acc_name) {
        for (const auto& info : infos) {
            const auto key = save::SaveEntryKey(info);
            if (seen_keys.insert(key).second) {
                save::Entry se;
                static_cast<FsSaveDataInfo&>(se) = info;

                auto data = title::Get(se.application_id);
                std::string game_name = (data && data->lang.name[0] != '\0') ? data->lang.name : (se.system_save_data_id ? "System" : "Unknown");

                char id_str[64];
                if (se.save_data_index != 0 || se.save_data_rank != 0) {
                    std::snprintf(id_str, sizeof(id_str), " [idx:%u rk:%u sp:%u %016lX]",
                        se.save_data_index, se.save_data_rank, se.save_data_space_id, se.save_data_id);
                } else {
                    std::snprintf(id_str, sizeof(id_str), " [sp:%u %016lX]",
                        se.save_data_space_id, se.save_data_id);
                }
                save_options.push_back({se, game_name + " (" + acc_name + ")" + id_str});
            }
        }
    };

    for (const auto& acc : accounts) {
        const auto infos = save::DiscoverSaveDataInfo(&acc.uid, FsSaveDataType_Account);
        add_candidates(infos, acc.nickname);
    }

    constexpr u8 NON_ACCOUNT_TYPES[] = {
        FsSaveDataType_Bcat,
        FsSaveDataType_Device,
        FsSaveDataType_Temporary,
        FsSaveDataType_Cache,
        FsSaveDataType_System,
        FsSaveDataType_SystemBcat,
    };
    for (const auto type : NON_ACCOUNT_TYPES) {
        const auto infos = save::DiscoverSaveDataInfo(nullptr, type);
        add_candidates(infos, save::GetSaveTypeLabel(type));
    }

    if (save_options.empty()) {
        App::Push<OptionBox>("No existing save slots found on console."_i18n, "OK"_i18n);
        return;
    }

    const auto execute_restore = [this, file_path, is_dir, is_disa](save::Entry se, const std::string& label) {
        if (se.save_data_id == 0 || se.is_backup) {
            return;
        }

        const std::string prompt = "Restore save data to\n"_i18n + label + "?\n\n" + "A safety recovery backup will be created on SD before overwriting.\nPlease close the running game and disable MTP."_i18n;

        App::Push<OptionBox>(prompt, "No"_i18n, "Yes"_i18n, 0, [this, file_path, is_dir, is_disa, se](auto op_index) {
            if (!op_index || *op_index != 1) return;

            auto recovery_path = std::make_shared<fs::FsPath>();
            auto mutation_started = std::make_shared<bool>(false);

            App::Push<ProgressBox>(0, "Restoring save..."_i18n, "", [this, file_path, is_dir, is_disa, se, recovery_path, mutation_started](auto pbox) -> Result {
                if (is_dir) {
                    return save::RestoreSaveFolder(pbox, se, file_path, recovery_path.get(), mutation_started.get());
                } else {
                    return save::RestoreSaveZip(pbox, se, file_path, recovery_path.get(), mutation_started.get());
                }
            }, [recovery_path, mutation_started, is_disa](Result rc) {
                if (rc == Result_TransferCancelled) {
                    App::Notify("Restore cancelled."_i18n); // the user stopped it: not an error
                } else if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Save restore failed!"_i18n);
                } else {
                    App::Notify("Save restored successfully!"_i18n);
                }

                if (!is_disa) {
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
        });
    };

    PopupList::Items items;
    for (const auto& opt : save_options) {
        items.emplace_back(opt.label);
    }
    App::Push<PopupList>("Select Target Save"_i18n, items, [save_options, execute_restore](auto op_index) {
        if (op_index && *op_index < save_options.size()) {
            execute_restore(save_options[*op_index].entry, save_options[*op_index].label);
        }
    });
}

void FsView::UnzipFiles(fs::FsPath dir_path) {
    const auto targets = GetSelectedEntries();

    // set to current path.
    if (dir_path.empty()) {
        dir_path = m_path;
    }

    App::Push<ui::ProgressBox>(0, "Extracting "_i18n, "", [this, dir_path, targets](auto pbox) -> Result {
        const auto is_hdd_fs = m_fs->Root().starts_with("ums");

        for (auto& e : targets) {
            pbox->SetTitle(e.GetName());
            const auto zip_out = GetNewPath(e);
            R_TRY(thread::TransferUnzipAll(pbox, zip_out, m_fs.get(), dir_path, nullptr, is_hdd_fs ? thread::Mode::SingleThreaded : thread::Mode::SingleThreadedIfSmaller));
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Extract failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Extract success!"_i18n);
        }

        Scan(m_path);
        log_write("did extract\n");
    });
}

void FsView::ZipFiles(fs::FsPath zip_out) {
    const auto targets = GetSelectedEntries();

    // set to current path.
    if (zip_out.empty()) {
        if (std::size(targets) == 1) {
            const auto name = targets[0].name;
            const auto ext = std::strrchr(targets[0].name, '.');
            fs::FsPath file_path;
            if (!ext) {
                std::snprintf(file_path, sizeof(file_path), "%s.zip", name);
            } else {
                std::snprintf(file_path, sizeof(file_path), "%.*s.zip", (int)(ext - name), name);
            }
            zip_out = fs::AppendPath(m_path, file_path);
            log_write("zip out: %s name: %s file_path: %s\n", zip_out.s, name, file_path.s);
        } else {
            // loop until we find an unused file name.
            for (u64 i = 0; ; i++) {
                fs::FsPath file_path = "Archive.zip";
                if (i) {
                    std::snprintf(file_path, sizeof(file_path), "Archive (%zu).zip", i);
                }

                zip_out = fs::AppendPath(m_path, file_path);
                if (!m_fs->FileExists(zip_out)) {
                    break;
                }
            }
        }
    } else {
        if (!std::string_view(zip_out).ends_with(".zip")) {
            zip_out += ".zip";
        }
    }

    App::Push<ui::ProgressBox>(0, "Compressing "_i18n, "", [this, zip_out, targets](auto pbox) -> Result {
        const auto t = std::time(NULL);
        const auto tm = std::localtime(&t);
        const auto is_hdd_fs = m_fs->Root().starts_with("ums");

        // pre-calculate the time rather than calculate it in the loop.
        zip_fileinfo zip_info{};
        zip_info.tmz_date.tm_sec = tm->tm_sec;
        zip_info.tmz_date.tm_min = tm->tm_min;
        zip_info.tmz_date.tm_hour = tm->tm_hour;
        zip_info.tmz_date.tm_mday = tm->tm_mday;
        zip_info.tmz_date.tm_mon = tm->tm_mon;
        zip_info.tmz_date.tm_year = tm->tm_year;

        zlib_filefunc64_def file_func;
        mz::FileFuncStdio(&file_func);

        auto zfile = zipOpen2_64(zip_out, APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zfile, Result_ZipOpen2_64);
        ON_SCOPE_EXIT(zipClose(zfile, "sphaira v" APP_VERSION_HASH));

        const auto zip_add = [&](const fs::FsPath& file_path) -> Result {
            // the file name needs to be relative to the current directory.
            const char* file_name_in_zip = file_path.s + std::strlen(m_path);

            // strip root path (/ or ums0:)
            if (!std::strncmp(file_name_in_zip, m_fs->Root(), std::strlen(m_fs->Root()))) {
                file_name_in_zip += std::strlen(m_fs->Root());
            }

            // root paths are banned in zips, they will warn when extracting otherwise.
            while (file_name_in_zip[0] == '/') {
                file_name_in_zip++;
            }

            pbox->NewTransfer(file_name_in_zip);

            if (ZIP_OK != zipOpenNewFileInZip(zfile, file_name_in_zip, &zip_info, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION)) {
                log_write("failed to add zip for %s\n", file_path.s);
                R_THROW(Result_ZipOpenNewFileInZip);
            }
            ON_SCOPE_EXIT(zipCloseFileInZip(zfile));

            return thread::TransferZip(pbox, zfile, m_fs.get(), file_path, nullptr, is_hdd_fs ? thread::Mode::SingleThreaded : thread::Mode::SingleThreadedIfSmaller);
        };

        for (auto& e : targets) {
            pbox->SetTitle(e.GetName());
            if (e.IsFile()) {
                const auto file_path = GetNewPath(e);
                R_TRY(zip_add(file_path));
            } else {
                FsDirCollections collections;
                get_collections(GetNewPath(e), e.name, collections);

                for (const auto& collection : collections) {
                    for (const auto& file : collection.files) {
                        const auto file_path = fs::AppendPath(collection.path, file.name);
                        R_TRY(zip_add(file_path));
                    }
                }
            }
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Compress failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Compress success!"_i18n);
        }

        Scan(m_path);
        log_write("did compress\n");
    });
}

void FsView::UploadFiles() {
    const auto targets = GetSelectedEntries();

    const auto network_locations = location::Load();
    if (network_locations.empty()) {
        App::Notify("No network locations configured! Add one in Settings."_i18n);
        return;
    }

    PopupList::Items items;
    for (const auto&p : network_locations) {
        items.emplace_back(p.name);
    }

    App::Push<PopupList>(
        "Select network location"_i18n, items, [this, network_locations](auto op_index){
            if (!op_index) {
                return;
            }

            const auto loc = network_locations[*op_index];
            App::Push<ProgressBox>(0, "Uploading"_i18n, "", [this, loc](auto pbox) -> Result {
                auto targets = GetSelectedEntries();
                const auto is_file_based_emummc = App::IsFileBaseEmummc();

                const auto file_add = [&](s64 file_size, const fs::FsPath& file_path, const char* name) -> Result {
                    // the file name needs to be relative to the current directory.
                    const auto relative_file_name = file_path.s + std::strlen(m_path);
                    pbox->SetTitle(name);
                    pbox->NewTransfer(relative_file_name);

                    fs::File f;
                    R_TRY(m_fs->OpenFile(file_path, FsOpenMode_Read, &f));

                    return thread::TransferPull(pbox, file_size,
                        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                            const auto rc = f.Read(off, data, size, FsReadOption_None, bytes_read);
                            if (m_fs->IsNative() && is_file_based_emummc) {
                                svcSleepThread(2e+6); // 2ms
                            }
                            return rc;
                        },
                        [&](thread::PullCallback pull) -> Result {
                            s64 offset{};
                            const auto result = curl::Api().FromMemory(
                                CURL_LOCATION_TO_API(loc),
                                curl::OnProgress{pbox->OnDownloadProgressCallback()},
                                curl::UploadInfo{
                                    relative_file_name, file_size,
                                    [&](void *ptr, size_t size) -> size_t {
                                        // curl will request past the size of the file, causing an error.
                                        if (offset >= file_size) {
                                            log_write("finished file upload\n");
                                            return 0;
                                        }

                                        u64 bytes_read{};
                                        if (R_FAILED(pull(ptr, size, &bytes_read))) {
                                            log_write("failed to read in custom callback: %zd size: %zd\n", offset, size);
                                            return 0;
                                        }

                                        offset += bytes_read;
                                        return bytes_read;
                                    }
                                }
                            );

                            R_UNLESS(result.success, Result_FileBrowserFailedUpload);
                            R_SUCCEED();
                        }
                    );
                };

                for (auto& e : targets) {
                    if (e.IsFile()) {
                        const auto file_path = GetNewPath(e);
                        R_TRY(file_add(e.file_size, file_path, e.GetName().c_str()));
                    } else {
                        FsDirCollections collections;
                        get_collections(GetNewPath(e), e.name, collections, true);

                        for (const auto& collection : collections) {
                            for (const auto& file : collection.files) {
                                const auto file_path = fs::AppendPath(collection.path, file.name);
                                R_TRY(file_add(file.file_size, file_path, file.name));
                            }
                        }
                    }
                }

                R_SUCCEED();
            }, [this](Result rc){
                App::PushErrorBox(rc, "Failed to upload files"_i18n);
                m_menu->ResetSelection();

                if (R_SUCCEEDED(rc)) {
                    App::Notify("Upload successful!"_i18n);
                    log_write("Upload successfull!!!\n");
                } else {
                    App::Notify("Upload failed!"_i18n);
                    log_write("Upload failed!!!\n");
                }
            });
        }
    );
}

void FsView::ShareFolder() {
    if (!IsSd()) {
        App::Notify("Only microSD folders can be shared"_i18n);
        return;
    }

    const auto targets = GetMountTargets();
    App::SetMountedFolders(targets);

    WebShareResult result;
    if (const auto rc = WebShareFolder(targets.front(), result); R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to start folder server"_i18n);
        return;
    }

    // the server may already be up -- started from Tools, or by an earlier
    // mount. it now picks the new mount up on its next request, so all that is
    // left is to say so: pushing a second progress box would give two owners of
    // one server, and whichever was dismissed first would stop it under the
    // other.
    if (WebGetProgressBox()) {
        nvgDeleteImage(App::GetVg(), result.qr_image);
        App::Notify("Mounted over HTTP: "_i18n + result.url);
        return;
    }

    WebPushServerProgressBox(result.url, result.qr_image, "StartWebServer"_i18n);
}

} // namespace sphaira::ui::menu::filebrowser
