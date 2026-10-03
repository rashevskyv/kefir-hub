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
#include "web.hpp"
#include "ui/menus/filebrowser/filebrowser_internal.hpp"

#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <optional>
#include <algorithm>

namespace sphaira::ui::menu::filebrowser {
using namespace detail;

void FsView::ShowNoLauncherHint() {
    const auto ext = GetEntry().GetExtension();
    auto message = "No launcher is set up for this file"_i18n;
    if (!ext.empty()) {
        message += " (." + ext + ")";
    }

    if (GetRomDatabaseFromPath(m_path).empty()) {
        message += ".\n\n";
        message += "Emulator cores are only offered when the file sits in a folder named after its system, for example /roms/snes or /roms/segacd. Rename the folder and try again."_i18n;
    } else {
        message += ".\n\n";
        message += "The folder is recognised, but no installed core claims this extension. Install a core that supports it."_i18n;
    }

    App::Push<OptionBox>(message, "OK"_i18n);
}

void FsView::InstallForwarder() {
    if (path::EqualsIC(GetEntry().GetExtension(), "nro")) {
        if (R_FAILED(homebrew::Menu::InstallHomebrewFromPath(GetNewPathCurrent()))) {
            log_write("failed to create forwarder\n");
        }
        return;
    }

    const auto assoc_list = m_menu->FindFileAssocFor();
    if (assoc_list.empty()) {
        log_write("failed to find assoc for: %s ext: %s\n", GetEntry().name, GetEntry().GetExtension().c_str());
        ShowNoLauncherHint();
        return;
    }

    PopupList::Items items;
    for (const auto&p : assoc_list) {
        items.emplace_back(MakeLauncherLabel(p));
    }

    const auto title = std::string{"Select launcher for: "_i18n} + GetEntry().name;
    App::Push<PopupList>(
        title, items, [this, assoc_list](auto op_index){
            if (op_index) {
                const auto assoc = assoc_list[*op_index];
                ShowRomForwarderEditor(assoc, GetRomDatabaseFromPath(m_path), GetEntry(), GetNewPathCurrent());
            } else {
                log_write("pressed B to skip launch...\n");
            }
        }
    );
}

void FsView::OpenImageViewer() {
    std::vector<fs::FsPath> paths;
    s64 image_index{};

    for (u32 i = 0; i < m_entries_current.size(); i++) {
        const auto& entry = GetEntry(i);
        if (!entry.IsFile() || !path::IsAnyOfIC(entry.GetExtension(), IMAGE_EXTENSIONS)) {
            continue;
        }

        if (static_cast<s64>(i) == m_index) {
            image_index = static_cast<s64>(paths.size());
        }

        paths.emplace_back(GetNewPath(i));
    }

    App::Push<fileview::Menu>(GetNewPathCurrent(), std::move(paths), image_index);
}

void FsView::DisplayHash(hash::Type type) {
    // hack because we cannot share output between threaded calls...
    static std::string hash_out;
    hash_out.clear();

    App::Push<ProgressBox>(0, "Hashing"_i18n, GetEntry().name, [this, type](auto pbox) -> Result {
        const auto full_path = GetNewPathCurrent();
        pbox->NewTransfer(full_path);
        R_TRY(hash::Hash(pbox, type, m_fs.get(), full_path, hash_out));

        R_SUCCEED();
    }, [this, type](Result rc){
        App::PushErrorBox(rc, "Failed to hash file..."_i18n);

        if (R_SUCCEEDED(rc)) {
            char buf[0x100];
            // std::snprintf(buf, sizeof(buf), "%s\n%s\n%s", hash::GetTypeStr(type), hash_out.c_str(), GetEntry().GetName());
            std::snprintf(buf, sizeof(buf), "%s\n%s", hash::GetTypeStr(type), hash_out.c_str());
            App::Push<OptionBox>(buf, "OK"_i18n);
        }
    });
}

void FsView::DisplayPickerOptions() {
    auto options = std::make_unique<Sidebar>("File Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    auto create_folder_entry = options->Add<SidebarEntryCallback>("Create Folder"_i18n, [this](){
        std::string out;
        if (R_SUCCEEDED(swkbd::ShowText(out, "Set Folder Name"_i18n.c_str(), m_menu->m_picker_create_name.c_str())) && !out.empty()) {
            App::PopToMenu();

            fs::FsPath full_path;
            if (out.starts_with(m_fs_entry.root.s)) {
                full_path = out;
            } else {
                full_path = fs::AppendPath(m_path, out);
            }

            if (R_SUCCEEDED(m_fs->CreateDirectoryRecursively(full_path))) {
                log_write("created dir: %s\n", full_path.s);
                Scan(m_path);
            } else {
                log_write("failed to create dir: %s\n", full_path.s);
            }
        }
    });
    create_folder_entry->Depends([this](){ return !IsReadOnly(m_path); }, "Folder is read-only"_i18n);

    options->Add<SidebarEntryCallback>("Close picker"_i18n, [this](){
        App::PopToMenu();
        m_menu->PromptIfShouldExit();
    });
}


void FsView::DisplayAdvancedOptions() {
    auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (IsSd()) {
        options->Add<SidebarEntryCallback>("StartWebServer"_i18n, [this](){
            ShareFolder();
        }, "Share the current folder via the built-in web server."_i18n);
    }

    if (IsSd() && !m_selected_count && !m_entries_current.empty() && !IsParentEntry(m_index) && GetEntry().IsDir()) {
        const auto path = GetNewPathCurrent();
        if (path != "/" && path != "/switch") {
            if (homebrew::IsSearchPath(path)) {
                options->Add<SidebarEntryCallback>("Delete Homebrew Search Paths"_i18n, [path](){
                    const auto prompt = "Remove Homebrew Search Path?"_i18n + "\n\n" + path.toString();
                    App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [path](auto index){
                        if (!index || *index != 1) {
                            return;
                        }

                        if (homebrew::RemoveSearchPath(path)) {
                            App::PopToMenu();
                            App::Notify("Homebrew search path removed."_i18n);
                        } else {
                            App::Notify("Failed to remove Homebrew search path"_i18n);
                        }
                    });
                });
            } else {
                options->Add<SidebarEntryCallback>("Add to Homebrew Search Paths"_i18n, [path](){
                    if (homebrew::AddSearchPath(path)) {
                        App::PopToMenu();
                        App::Notify("Homebrew search path added."_i18n);
                    } else {
                        App::Notify("Failed to add Homebrew search path"_i18n);
                    }
                });
            }
        }
    }

    auto create_file_entry = options->Add<SidebarEntryCallback>("Create File"_i18n, [this](){
        std::string out;
        if (R_SUCCEEDED(swkbd::ShowText(out, "Set File Name"_i18n.c_str(), fs::AppendPath(m_path, ""))) && !out.empty()) {
            App::PopToMenu();

            fs::FsPath full_path;
            if (out.starts_with(m_fs_entry.root.s)) {
                full_path = out;
            } else {
                full_path = fs::AppendPath(m_path, out);
            }

            m_fs->CreateDirectoryRecursivelyWithPath(full_path);
            if (R_SUCCEEDED(m_fs->CreateFile(full_path, 0, 0))) {
                log_write("created file: %s\n", full_path.s);
                Scan(m_path);
            } else {
                log_write("failed to create file: %s\n", full_path.s);
            }
        }
    });
    create_file_entry->Depends([this](){ return !IsReadOnly(m_path); }, "Folder is read-only"_i18n);

    auto create_folder_entry = options->Add<SidebarEntryCallback>("Create Folder"_i18n, [this](){
        std::string out;
        if (R_SUCCEEDED(swkbd::ShowText(out, "Set Folder Name"_i18n.c_str(), fs::AppendPath(m_path, ""))) && !out.empty()) {
            App::PopToMenu();

            fs::FsPath full_path;
            if (out.starts_with(m_fs_entry.root.s)) {
                full_path = out;
            } else {
                full_path = fs::AppendPath(m_path, out);
            }

            if (R_SUCCEEDED(m_fs->CreateDirectoryRecursively(full_path))) {
                log_write("created dir: %s\n", full_path.s);
                Scan(m_path);
            } else {
                log_write("failed to create dir: %s\n", full_path.s);
            }
        }
    });
    create_folder_entry->Depends([this](){ return !IsReadOnly(m_path); }, "Folder is read-only"_i18n);

    if (m_entries_current.size() && !m_selected_count && GetEntry().IsFile()) {
        options->Add<SidebarEntryCallback>("View as hex"_i18n, [this](){
            App::Push<fileview::Menu>(m_fs.get(), GetNewPathCurrent(), fileview::TextMode::Hex);
        }, "Show the raw bytes of the selected file."_i18n);
        if (IsSd() && path::IsAnyOfIC(GetEntry().GetExtension(), IMAGE_EXTENSIONS)) {
            options->Add<SidebarEntryCallback>("View Image"_i18n, [this](){
                OpenImageViewer();
            }, "Open the selected image in the built-in viewer."_i18n);
            auto theme_entry = options->Add<SidebarEntryCallback>("Create Switch Theme"_i18n, [this](){
                App::Push<theme_creator::Menu>(GetNewPathCurrent());
            }, "Use the selected image to create a custom Switch theme."_i18n);
            theme_entry->SetHasSubmenu(true);
        }
    }

    if (m_entries_current.size()) {
        options->Add<SidebarEntryCallback>("Upload to network location"_i18n, [this](){
            UploadFiles();
        }, "Upload the selected file(s) to a configured network storage."_i18n);
    }

    if (m_entries_current.size() && !m_selected_count && GetEntry().IsFile()) {
        auto hash_entry = options->Add<SidebarEntryCallback>("Hash"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Hash Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            options->Add<SidebarEntryCallback>("CRC32"_i18n, [this](){
                DisplayHash(hash::Type::Crc32);
            }, "Calculate and display the CRC32 hash of the selected file."_i18n);
            options->Add<SidebarEntryCallback>("MD5"_i18n, [this](){
                DisplayHash(hash::Type::Md5);
            }, "Calculate and display the MD5 hash of the selected file."_i18n);
            options->Add<SidebarEntryCallback>("SHA1"_i18n, [this](){
                DisplayHash(hash::Type::Sha1);
            }, "Calculate and display the SHA1 hash of the selected file."_i18n);
            options->Add<SidebarEntryCallback>("SHA256"_i18n, [this](){
                DisplayHash(hash::Type::Sha256);
            }, "Calculate and display the SHA256 hash of the selected file."_i18n);
        }, "Calculate a checksum hash for the selected file."_i18n);
        hash_entry->SetHasSubmenu(true);
    }

    options->Add<SidebarEntryBool>("Ignore read only"_i18n, m_menu->m_ignore_read_only.Get(), [this](bool& v_out){
        m_menu->m_ignore_read_only.Set(v_out);
        m_fs->SetIgnoreReadOnly(v_out);
    }, "Allow modifying files and folders that are marked as read-only."_i18n);
}

} // namespace sphaira::ui::menu::filebrowser
