#include "ui/menus/filebrowser.hpp"
#include "archive_extract_plan.hpp"
#include "text_helper.hpp"
#include "path_util.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "ui/menus/filebrowser_forwarder.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/menus/file_viewer.hpp"
#include "ui/menus/appstore.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "utils/devoptab_smb2.hpp"
#include "utils/utils.hpp"
#include "log.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "swkbd.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "haze_helper.hpp"
#include "ui/menus/filebrowser/filebrowser_internal.hpp"

#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <ranges>
#include <optional>
#include <algorithm>

namespace sphaira::ui::menu::filebrowser {
using namespace detail;

void FsView::DisplayOptions() {
    if (m_menu->IsFolderPicker()) {
        DisplayPickerOptions();
        return;
    }

    auto options = std::make_unique<Sidebar>("File Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    const auto is_root = m_fs_entry.type == FsType::Root;

    // at the root, sources can be managed in place, with the same options
    // as Settings -> Sources.
    if (is_root) {
        if (m_entries_current.size() && GetEntry().virtual_target_entry.type == FsType::Network) {
            const auto loc_name = GetEntry().GetName();
            const auto find_location = [loc_name]() -> std::optional<location::Entry> {
                const auto locations = location::Load();
                const auto it = std::ranges::find_if(locations, [&](const auto& e){ return e.name == loc_name; });
                if (it == locations.end()) {
                    return std::nullopt;
                }
                return *it;
            };

            options->Add<SidebarEntryCallback>("Edit Source"_i18n, [loc_name](){
                App::Push<settings::SourceEditMenu>(loc_name);
            }, true, "Configure connection settings."_i18n);

            options->Add<SidebarEntryCallback>("Test Connection"_i18n, [find_location](){
                const auto loc = find_location();
                if (!loc) {
                    return;
                }
                App::Push<ProgressBox>(0, "Testing Connection..."_i18n, loc->name, [loc](auto pbox) -> Result {
                    return settings::TestLocationConnection(*loc);
                }, [loc](Result rc) {
                    SetSourceConnectionStatus(loc->url, R_SUCCEEDED(rc));
                    if (R_SUCCEEDED(rc)) {
                        App::Notify("Connection test successful!"_i18n);
                    } else {
                        App::Push<OptionBox>("Connection test failed!"_i18n + "\n" + settings::ConnectionFailureText(rc), "OK"_i18n);
                    }
                });
            }, true, "Test connection with current settings."_i18n);

            options->Add<SidebarEntryCallback>("Rename Source"_i18n, [this, find_location](){
                const auto loc = find_location();
                if (!loc) {
                    return;
                }
                std::string out;
                if (R_SUCCEEDED(swkbd::ShowText(out, "Rename Network Location"_i18n.c_str(), loc->name.c_str())) && !out.empty() && out != loc->name) {
                    location::Remove(loc->name);
                    location::Entry new_loc = *loc;
                    new_loc.name = out;
                    location::Add(new_loc);
                    App::Notify("Location renamed successfully!"_i18n);
                    App::PopToMenu();
                    SortAndFindLastFile(true);
                }
            }, true, "Rename this network location."_i18n);

            options->Add<SidebarEntryCallback>("Properties"_i18n, [find_location](){
                const auto loc = find_location();
                if (!loc) {
                    return;
                }
                std::string props = "Name: "_i18n + loc->name + "\n";
                std::string proto = loc->protocol;
                if (proto.empty()) {
                    if (loc->IsSmb()) proto = "smb";
                    else if (loc->IsNfs()) proto = "nfs";
                    else if (loc->url.starts_with("ftp://")) proto = "ftp";
                    else if (loc->url.starts_with("http://") || loc->url.starts_with("https://")) proto = "webdav"; // fallback
                    else if (loc->url.starts_with("webdav://") || loc->url.starts_with("webdavs://")) proto = "webdav";
                }
                props += "Protocol: "_i18n + proto + "\n";
                props += "URL: "_i18n + loc->url + "\n";
                if (!loc->user.empty()) {
                    props += "Username: "_i18n + loc->user + "\n";
                }
                if (loc->port) {
                    props += "Port: "_i18n + std::to_string(loc->port) + "\n";
                }
                App::Push<OptionBox>(props, "OK"_i18n);
            }, true, "View network location properties."_i18n);

            options->Add<SidebarEntryCallback>("Delete Source"_i18n, [this, find_location](){
                App::Push<OptionBox>(
                    "Delete this network location?"_i18n,
                    "No"_i18n, "Yes"_i18n, 0, [this, find_location](auto op_delete_idx) {
                        if (op_delete_idx && *op_delete_idx) {
                            const auto loc = find_location();
                            if (!loc) {
                                return;
                            }
                            if (loc->name == App::GetWebdavUrlName()) {
                                App::SetWebdavUrl("");
                            }
                            location::Remove(loc->name);
                            App::Notify("Location deleted successfully!"_i18n);
                            App::PopToMenu();
                            SortAndFindLastFile(true);
                        }
                    }
                );
            }, true, "Delete this network location."_i18n);
        }

        options->Add<SidebarEntryCallback>("Add network location"_i18n, [this](){
            AddNetworkLocationInteractive([this](){
                SortAndFindLastFile(true);
            });
        }, "Configure a new network location (supported protocols: SMB, NFS, WebDAV, FTP, HTTP)."_i18n);
    }

    // returns true if all entries match the ext array.
    const auto check_all_ext = [this](auto& exts){
        const auto entries = GetSelectedEntries();
        if (entries.empty()) {
            return false;
        }

        for (auto&e : entries) {
            if (!e.IsFile() || !path::IsAnyOfIC(e.GetExtension(), exts)) {
                return false;
            }
        }
        return true;
    };

    if (m_entries_current.size()) {
        if (check_all_ext(INSTALL_EXTENSIONS)) {
            auto entry = options->Add<SidebarEntryCallback>("Install"_i18n, [this](){
                InstallFiles();
            }, "Install the selected NSP/XCI file(s) to the console."_i18n);
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);
        }
#if ENABLE_NETWORK_INSTALL
        if (!GetRecursiveInstallTargets().empty()) {
            auto entry = options->Add<SidebarEntryCallback>("Install recursively"_i18n, [this](){ InstallFolderRecursively(); },
                "Scan selected folder(s) recursively for packages and open the install queue."_i18n);
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);
        }
#endif
    }

    if (m_entries_current.size() && !m_selected_count && GetEntry().IsFile()) {
        const auto new_path = GetNewPathCurrent();
        const auto name = std::string_view{GetEntry().name};
        if (name.ends_with(".disa") || name.ends_with(".bin") || name.ends_with(".zip") || name.size() == 16 || save::IsDisaSaveFile(m_fs.get(), new_path)) {
            options->Add<SidebarEntryCallback>("Restore save data"_i18n, [this](){
                RestoreSaveFile(GetEntry());
            }, "Restore this save data file to the console."_i18n);
        }
    }

    if (IsSd() && m_entries_current.size() && !m_selected_count && !IsParentEntry(m_index) && GetEntry().IsDir()) {
        options->Add<SidebarEntryCallback>("Restore save data"_i18n, [this](){
            RestoreSaveFile(GetEntry());
        }, "Restore this save backup directory to the console."_i18n);
    }

    if (m_entries_current.size() && !m_selected_count && GetEntry().IsFile()) {
        options->Add<SidebarEntryCallback>("View as text"_i18n, [this](){
            App::Push<fileview::Menu>(m_fs.get(), GetNewPathCurrent(), fileview::TextMode::View, !IsReadOnly(GetNewPathCurrent()));
        }, "Open the selected file in read-only text view mode."_i18n);

        if (text_helper::IsTextFile(GetEntry().GetName())) {
            auto edit_entry = options->Add<SidebarEntryCallback>("Edit"_i18n, [this](){
                App::Push<fileview::Menu>(m_fs.get(), GetNewPathCurrent(), fileview::TextMode::Edit, true);
            }, "Open the selected file in text editor mode."_i18n);
            auto remote_entry = options->Add<SidebarEntryCallback>("Edit on PC / phone"_i18n, [this](){
                auto menu = std::make_unique<fileview::Menu>(m_fs.get(), GetNewPathCurrent(), fileview::TextMode::Edit, true);
                menu->QueueRemoteEdit();
                App::Push(std::move(menu));
            }, "Open this file in a browser, edit it there, then Save to send it back."_i18n);
            const auto can_edit = [this](){
                return !IsReadOnly(GetNewPathCurrent()) && GetEntry().file_size <= 4 * 1024 * 1024;
            };
            const auto edit_reason = IsReadOnly(GetNewPathCurrent()) ? "File is read-only"_i18n : "File is too large to edit"_i18n;
            edit_entry->Depends(can_edit, edit_reason);
            remote_entry->Depends(can_edit, edit_reason);
        }
    }

    if (IsSd() && m_entries_current.size() && !m_selected_count) {
        if (GetEntry().IsFile() && (path::EqualsIC(GetEntry().GetExtension(), "nro") || !m_menu->FindFileAssocFor().empty())) {
            auto entry = options->Add<SidebarEntryCallback>("Install Forwarder"_i18n, [this](){;
                InstallForwarder();
            }, "Install a forwarder shortcut for this file."_i18n);
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);
        }
        if (GetEntry().IsFile() && path::EqualsIC(GetEntry().GetExtension(), "bin")) {
            auto launch_entry = options->Add<SidebarEntryCallback>("Launch payload"_i18n, [this](){
                const auto path = GetNewPathCurrent();
                const auto entry_name = GetEntry().GetName();
                App::Push<OptionBox>("Reboot to payload "_i18n + entry_name + "?\n\n" + path.s,
                    "Cancel"_i18n, "Reboot"_i18n, 1, [path](auto op_index) {
                        if (op_index && *op_index == 1) {
                            fs::FsPath launch_path = path;
                            const bool prepared = !utils::isTegraExplorerPayload(path) ||
                                utils::ensureTegraExplorerPayload(launch_path, path);
                            if (!prepared || !utils::rebootToPayload(launch_path)) {
                                App::Push<OptionBox>("Failed to prepare payload launch!"_i18n + "\n" +
                                    "Hekate payload API is not available or payload is invalid."_i18n, "OK"_i18n);
                            }
                        }
                    });
            }, "Reboot console into this payload via Hekate."_i18n);
            launch_entry->SetIcon(ActionIcon::Launch);
        }
    }

    if ((IsSd() || m_fs_entry.type == FsType::Network) && m_entries_current.size() && !m_selected_count) {
        if (check_all_ext(VIDEO_EXTENSIONS) || check_all_ext(AUDIO_EXTENSIONS)) {
            options->Add<SidebarEntryCallback>("Play with NXMP"_i18n, [this](){
                if (HasNxmp()) {
                    std::string play_url;
                    if (m_fs_entry.type == FsType::Network) {
                        std::string raw_url = m_fs_entry.url.toString();
                        std::string user = m_fs_entry.user.toString();
                        std::string pass = m_fs_entry.pass.toString();
#ifdef BUILD_SMB2
                        std::string server;
                        std::string share;
                        ParseSmbUrl(raw_url, server, share);
                        raw_url = "smb://";
                        if (!user.empty()) {
                            std::string creds = UrlEncode(user);
                            if (!pass.empty()) {
                                creds += ":" + UrlEncode(pass);
                            }
                            creds += "@";
                            raw_url += creds;
                        }
                        raw_url += server;
                        if (!share.empty()) {
                            raw_url += "/" + UrlEncode(share, true);
                        }
                        std::string rel_path = GetNewPathCurrent().toString();
                        if (rel_path.starts_with("smb2:")) {
                            rel_path = rel_path.substr(5);
                        }
                        play_url = raw_url + UrlEncode(rel_path, true);
#else
                        if (!user.empty()) {
                            std::string creds = user;
                            if (!pass.empty()) {
                                creds += ":" + pass;
                            }
                            creds += "@";
                            raw_url.insert(6, creds);
                        }
                        std::string rel_path = GetNewPathCurrent().toString();
                        play_url = raw_url + rel_path;
#endif
                        nro_launch(GetNxmpPath(), nro_add_arg(play_url));
                    } else {
                        play_url = GetNewPathCurrent().toString();
                        nro_launch(GetNxmpPath(), nro_add_arg_file(play_url));
                    }
                } else {
                    App::Push<OptionBox>(
                        "NXMP not found, open AppStore to install?"_i18n,
                        "No"_i18n, "Yes"_i18n, 1, [](auto op_index){
                            if (op_index && *op_index) {
                                App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
                            }
                        }
                    );
                }
            }, "Play the selected media file using NXMP player."_i18n);
        }
    }

    if (!is_root && !m_menu->m_selected.Empty() && (m_menu->m_selected.Type() == SelectedType::Cut || m_menu->m_selected.Type() == SelectedType::Copy)) {
        auto paste_entry = options->Add<SidebarEntryCallback>("Paste"_i18n, [this](){
            if (HasPasteConflicts()) {
                App::Push<OptionBox>(
                    "One or more files already exist in the destination. Replace existing files?"_i18n,
                    "Cancel"_i18n, "Replace"_i18n, 0, [this](auto replace_op){
                        if (replace_op && *replace_op) {
                            App::PopToMenu();
                            OnPasteCallback(true);
                        }
                    }
                );
            } else {
                App::PopToMenu();
                OnPasteCallback(false);
            }
        }, "Paste the clipboard contents into the current folder."_i18n);
        paste_entry->SetIcon(ActionIcon::Paste);
        paste_entry->Depends([this](){ return !IsReadOnly(m_path); }, "Destination folder is read-only"_i18n);
    }

    if (!is_root && m_entries_current.size()) {
        auto cut_entry = options->Add<SidebarEntryCallback>("Cut"_i18n, [this](){
            m_menu->AddSelectedEntries(SelectedType::Cut);
        }, true, "Move the selected files to the clipboard."_i18n);
        cut_entry->SetIcon(ActionIcon::Cut);
        cut_entry->Depends([this](){ return !AnySelectedReadOnly(); }, "Cannot cut read-only files"_i18n);

        auto copy_entry = options->Add<SidebarEntryCallback>("Copy"_i18n, [this](){
            m_menu->AddSelectedEntries(SelectedType::Copy);
        }, true, "Copy the selected files to the clipboard."_i18n);
        copy_entry->SetIcon(ActionIcon::Copy);
    }

    if (!is_root && m_entries_current.size()) {
        auto delete_entry = options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
            m_menu->AddSelectedEntries(SelectedType::Delete);

            log_write("clicked on delete\n");
            App::Push<OptionBox>(
                "Delete Selected files?"_i18n, "No"_i18n, "Yes"_i18n, 0, [this](auto op_index){
                    if (op_index && *op_index) {
                        App::PopToMenu();
                        OnDeleteCallback();
                    }
                }
            );
            log_write("pushed delete\n");
        }, "Permanently delete the selected file(s) or folder(s)."_i18n);
        delete_entry->SetIcon(ActionIcon::Delete);
        delete_entry->Depends([this](){ return !AnySelectedReadOnly(); }, "Cannot delete read-only files"_i18n);
    }

    if (!is_root && m_entries_current.size() && !m_selected_count) {
        auto rename_entry = options->Add<SidebarEntryCallback>("Rename"_i18n, [this](){
            std::string out;
            const auto& entry = GetEntry();
            const auto name = entry.GetName();
            if (R_SUCCEEDED(swkbd::ShowText(out, "Set New File Name"_i18n.c_str(), name.c_str())) && !out.empty() && out != name) {
                App::PopToMenu();

                const auto src_path = GetNewPath(entry);
                const auto dst_path = GetNewPath(m_path, out);

                Result rc;
                if (entry.IsFile()) {
                    rc = m_fs->RenameFile(src_path, dst_path);
                } else {
                    rc = m_fs->RenameDirectory(src_path, dst_path);
                }

                if (R_SUCCEEDED(rc)) {
                    Scan(m_path);
                } else {
                    const auto msg = std::string("Failed to rename file: ") + entry.name;
                    App::PushErrorBox(rc, msg);
                }
            }
        }, "Rename the selected file or folder."_i18n);
        rename_entry->SetIcon(ActionIcon::Edit);
        rename_entry->Depends([this](){ return !AnySelectedReadOnly(); }, "Cannot rename read-only files"_i18n);
    }

    auto view_entry = options->Add<SidebarEntryCallback>("View"_i18n, [this](){
        auto options = std::make_unique<Sidebar>("View Options"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        SidebarEntryArray::Items layout_items;
        layout_items.push_back("List"_i18n);
        layout_items.push_back("Icon"_i18n);
        options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
            m_menu->SetIconLayout(index_out);
        }, m_menu->IsIconLayout() ? 1 : 0, "Icon shows file thumbnails and folder previews."_i18n)->SetIcon(ActionIcon::Layout);

        SidebarEntryArray::Items sort_items;
        sort_items.push_back("Size"_i18n);
        sort_items.push_back("Alphabetical"_i18n);

        SidebarEntryArray::Items order_items;
        order_items.push_back("Descending"_i18n);
        order_items.push_back("Ascending"_i18n);

        options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
            m_menu->m_sort.Set(index_out);
            SortAndFindLastFile();
        }, m_menu->m_sort.Get(), "Select which field to sort files and folders by."_i18n)->SetIcon(ActionIcon::Sort);

        options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
            m_menu->m_order.Set(index_out);
            SortAndFindLastFile();
        }, m_menu->m_order.Get(), "Sort entries from largest to smallest or A to Z."_i18n)->SetIcon(ActionIcon::Sort);

        options->Add<SidebarEntryBool>("Show Hidden"_i18n, m_menu->m_show_hidden.Get(), [this](bool& v_out){
            m_menu->m_show_hidden.Set(v_out);
            SortAndFindLastFile();
        }, "Show files and folders that start with a dot (hidden)."_i18n)->SetIcon(ActionIcon::Toggle);

        options->Add<SidebarEntryBool>("Folders First"_i18n, m_menu->m_folders_first.Get(), [this](bool& v_out){
            m_menu->m_folders_first.Set(v_out);
            SortAndFindLastFile();
        }, "Place folders before files in the listing."_i18n)->SetIcon(ActionIcon::Folder);

        options->Add<SidebarEntryBool>("Hidden Last"_i18n, m_menu->m_hidden_last.Get(), [this](bool& v_out){
            m_menu->m_hidden_last.Set(v_out);
            SortAndFindLastFile();
        }, "Push hidden entries to the bottom of the listing."_i18n)->SetIcon(ActionIcon::Sort);
    }, "Change display order and visibility settings for files."_i18n);
    view_entry->SetIcon(ActionIcon::Layout);
    view_entry->SetHasSubmenu(true);

    // extract writes next to the .zip through the SD filesystem: a zip opened from a network share has no such folder.
    if (m_fs_entry.type == FsType::Archive && m_entries_current.size() && m_archive_return_entry.type == FsType::Sd) {
        auto extract_sel = options->Add<SidebarEntryCallback>("Extract selection"_i18n, [this](){
            const auto targets = GetSelectedEntries();
            const auto src_path = m_path;               // current dir inside the archive
            const auto dst_dir = m_archive_return_path; // SD folder the .zip lives in
            App::Push<ProgressBox>(0, "Extracting"_i18n, "", [this, targets, src_path, dst_dir](auto pbox) -> Result {
                auto src_fs = m_fs.get();
                auto dst_fs = std::make_unique<fs::FsNativeSd>(true);

                FsDirCollections collections;
                for (const auto& p : targets) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());
                    if (p.IsDir()) {
                        const auto full = GetNewPath(src_path, p.name);
                        pbox->NewTransfer("Scanning "_i18n + full);
                        R_TRY(get_collections(src_fs, full, p.name, collections));
                    }
                }

                for (const auto& p : targets) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());
                    const auto src = GetNewPath(src_path, p.name);
                    const auto dst = GetNewPath(dst_dir, p.name);
                    if (p.IsDir()) {
                        pbox->SetTitle(p.name);
                        pbox->NewTransfer("Creating "_i18n + dst);
                        dst_fs->CreateDirectory(dst);
                    } else {
                        pbox->SetTitle(p.name);
                        pbox->NewTransfer("Extracting "_i18n + src);
                        R_TRY(pbox->CopyFile(src_fs, dst_fs.get(), src, dst));
                    }
                }

                for (const auto& c : collections) {
                    const auto base_dst = GetNewPath(dst_dir, c.parent_name);
                    for (const auto& p : c.dirs) {
                        pbox->Yield();
                        R_TRY(pbox->ShouldExitResult());
                        dst_fs->CreateDirectory(GetNewPath(base_dst, p.name));
                    }
                    for (const auto& p : c.files) {
                        pbox->Yield();
                        R_TRY(pbox->ShouldExitResult());
                        const auto src = GetNewPath(c.path, p.name);
                        const auto dst = GetNewPath(base_dst, p.name);
                        pbox->SetTitle(p.name);
                        pbox->NewTransfer("Extracting "_i18n + src);
                        R_TRY(pbox->CopyFile(src_fs, dst_fs.get(), src, dst));
                    }
                }
                R_SUCCEED();
            }, [this](Result rc){
                App::PushErrorBox(rc, "Extract failed!"_i18n);
                if (R_SUCCEEDED(rc)) {
                    App::Notify("Extract success!"_i18n);
                }
            });
        }, "Copy the selected files and folders out of the archive to where the .zip lives."_i18n);
        extract_sel->SetHasSubmenu(false);
        extract_sel->SetIcon(ThemeEntryID_ICON_ZIP);
    }

    if (!is_root && m_fs_entry.type != FsType::Archive && m_entries_current.size()) {
        if (check_all_ext(ZIP_EXTENSIONS) || check_all_ext(archive::EXTENSIONS)) {
            auto extract_entry = options->Add<SidebarEntryCallback>("Extract"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Extract Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                auto here_entry = options->Add<SidebarEntryCallback>("Extract here"_i18n, [this](){
                    UnzipFiles("");
                }, "Extract the archive contents into the current folder."_i18n);
                here_entry->SetIcon(ThemeEntryID_ICON_ZIP);

                auto root_entry = options->Add<SidebarEntryCallback>("Extract to root"_i18n, [this](){
                    App::Push<OptionBox>("Are you sure you want to extract to root?"_i18n,
                        "No"_i18n, "Yes"_i18n, 0, [this](auto op_index){
                        if (op_index && *op_index) {
                            UnzipFiles(m_fs->Root());
                        }
                    });
                }, "Extract the archive contents to the root of this storage."_i18n);
                root_entry->SetIcon(ThemeEntryID_ICON_ZIP);

                auto to_entry = options->Add<SidebarEntryCallback>("Extract to..."_i18n, [this](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Enter the path to the folder to extract into", fs::AppendPath(m_path, ""))) && !out.empty()) {
                        UnzipFiles(out);
                    }
                }, "Extract the archive to a custom path you specify."_i18n);
                to_entry->SetIcon(ThemeEntryID_ICON_ZIP);
            }, "Extract the contents of the selected archive (zip, rar, 7z, tar, gz, xz)."_i18n);
            extract_entry->SetHasSubmenu(true);
            extract_entry->SetIcon(ThemeEntryID_ICON_ZIP);
        }

        if (!check_all_ext(ZIP_EXTENSIONS) || m_selected_count) {
            auto compress_entry = options->Add<SidebarEntryCallback>("Compress to zip"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Compress Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                auto comp_here = options->Add<SidebarEntryCallback>("Compress"_i18n, [this](){
                    ZipFiles("");
                }, "Compress the selected file(s) into a zip in the current folder."_i18n);
                comp_here->SetIcon(ThemeEntryID_ICON_ZIP);

                auto comp_to = options->Add<SidebarEntryCallback>("Compress to..."_i18n, [this](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Enter the path to the folder to extract into", m_path)) && !out.empty()) {
                        ZipFiles(out);
                    }
                }, "Compress the selected file(s) to a custom output path."_i18n);
                comp_to->SetIcon(ThemeEntryID_ICON_ZIP);
            }, "Compress the selected file(s) into a ZIP archive."_i18n);
            compress_entry->SetHasSubmenu(true);
            compress_entry->SetIcon(ThemeEntryID_ICON_ZIP);
        }
    }

    // expose the current folder or virtual mount (content / archive) to a PC
    // over MTP, FTP or HTTP (chosen from a popup).
    if (!is_root && (IsSd() || m_fs_entry.type == FsType::Content || m_fs_entry.type == FsType::Archive)) {
        options->Add<SidebarEntryCallback>("Mount"_i18n, [this](){
            ShareCurrentFolder();
        }, "Expose the selected folder to a PC over MTP, FTP or HTTP."_i18n);
    }
    // one entry for the one mount: it is what FTP exposes as a root device, what
    // the web root page lists next to the card, and what MTP pinned. leaving
    // only "Unmount MTP" here would strand the other two with no way back to a
    // plain card listing.
    if (sphaira::haze::HasPinned() || !App::GetMountedFolders().empty()) {
        options->Add<SidebarEntryCallback>("Unmount"_i18n, [](){
            sphaira::haze::UnmountPinned();
            App::SetMountedFolders({});
            App::Notify("Unmounted"_i18n);
        }, "Stop sharing the mounted folder over MTP, FTP and HTTP."_i18n);
    }

    auto source_entry = options->Add<SidebarEntryCallback>("Sources"_i18n, [this](){
        ShowSourcePicker();
    }, "Quickly switch this pane's file source."_i18n);
    source_entry->SetHasSubmenu(true);

    auto adv_entry = options->Add<SidebarEntryCallback>("Advanced"_i18n, [this](){
        DisplayAdvancedOptions();
    }, "Access file browser advanced tools."_i18n);
    adv_entry->SetHasSubmenu(true);
}

} // namespace sphaira::ui::menu::filebrowser
