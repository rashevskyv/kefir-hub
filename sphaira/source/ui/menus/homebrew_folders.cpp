// Homebrew menu folders: the rows of one folder of /switch, going in and out, and the folder actions.
// A folder row is a real folder on the card; renaming, deleting and its icon change that folder.

#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "swkbd.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/homebrew_internal.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/forwarder_editor.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"

#include <cstdio>

namespace sphaira::ui::menu::homebrew {

void LoadFolderIcon(NVGcontext* vg, NroEntry& e) {
    e.icon_size = 0; // one try only
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(FolderIconPath(e.path), data))) {
        return;
    }
    const auto image = ImageLoadIcon(data);
    if (!image.data.empty() && image.w == 256 && image.h == 256) {
        e.image = nvgCreateImageRGBA(vg, image.w, image.h, 0, image.data.data());
    }
}

void Menu::BuildView() {
    const auto vg = App::GetVg();
    for (auto& f : m_folders) {
        FreeEntry(vg, f);
    }
    m_folders.clear();
    for (auto& index : m_entries_index) {
        index.clear();
    }

    // the folder was renamed or deleted: start again from the top
    if (m_cwd != homebrew_tree::ROOT && !fs::DirExists(fs::FsPath{m_cwd})) {
        m_cwd = homebrew_tree::ROOT;
    }

    std::vector<std::string> paths;
    paths.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        paths.emplace_back(e.path.s);
    }
    const auto view = homebrew_tree::BuildView(paths, m_cwd);

    fs::FsNativeSd fs;
    for (const auto& f : view.folders) {
        NroEntry e{};
        e.is_dir = true;
        e.path = fs::FsPath{f.path};
        std::snprintf(e.nacp.lang.name, sizeof(e.nacp.lang.name), "%s", f.name.c_str());
        e.size = static_cast<s64>(f.apps);
        e.has_star = false;
        e.icon_size = fs.FileExists(FolderIconPath(e.path)) ? 1 : 0; // loaded when the row is drawn
        m_folders.emplace_back(std::move(e));
    }

    for (auto& index : m_entries_index) {
        index.reserve(view.apps.size() + m_folders.size());
    }
    for (const auto i : view.apps) {
        m_entries_index[Filter_All].emplace_back(i);
        if (!m_entries[i].hbini.hidden) {
            m_entries_index[Filter_HideHidden].emplace_back(i);
        }
    }
    for (u32 j = 0; j < m_folders.size(); j++) {
        const auto i = static_cast<u32>(m_entries.size()) + j;
        m_entries_index[Filter_All].emplace_back(i);
        m_entries_index[Filter_HideHidden].emplace_back(i);
    }
}

void Menu::Enter(const std::string& folder, const std::string& focus) {
    m_cwd = folder;
    ClearSelection();
    BuildView();
    Sort();
    SetIndex(0);

    for (s64 i = 0; i < static_cast<s64>(m_entries_current.size()); i++) {
        if (focus == GetEntry(i).path.s) {
            SetIndex(i);
            m_list->EnsureVisible(i, m_entries_current.size());
            break;
        }
    }
}

// A opens a folder and starts an app; B goes up a folder before it leaves the Hub.
void Menu::UpdateActions() {
    const bool folder = IsFolderFocused();
    const bool at_root = m_cwd == homebrew_tree::ROOT;

    SetAction(Button::A, Action{folder ? "Open"_i18n : "Launch"_i18n, [this](){
        if (m_entries_current.empty()) {
            return;
        }
        auto& e = GetEntry();
        if (e.is_dir) {
            Enter(e.path.toString());
        } else if (!g_kefir_updater_notice_ack.Get() && IsKefirUpdaterEntry(e)) {
            ShowKefirUpdaterRemovedDialog();
        } else if (OfferDeleteHubCopy(e)) {
            // asked instead of starting a second Kefir Hub
        } else {
            nro_launch(e.path);
        }
    }});

    SetAction(Button::B, Action{at_root ? "Exit"_i18n : "Back"_i18n, [this](){
        if (m_selected_count) {
            ClearSelection();
        } else if (m_cwd != homebrew_tree::ROOT) {
            Enter(homebrew_tree::Parent(m_cwd), m_cwd);
        } else {
            App::Exit();
        }
    }});
}

void Menu::AddFolderOptions(Sidebar* options) {
    const auto folder = GetEntry();

    options->Add<SidebarEntryHeader>("THIS FOLDER"_i18n);

    options->Add<SidebarEntryCallback>("Open"_i18n, [this, folder](){
        Enter(folder.path.toString());
    }, true, "Show the apps in this folder."_i18n);

    options->Add<SidebarEntryCallback>("Rename"_i18n, [this, folder](){
        RenameFolder(folder);
    }, true, "Rename the folder on the SD card."_i18n)->SetIcon(ActionIcon::Edit);

    options->Add<SidebarEntryCallback>("Set icon"_i18n, [this, folder](){
        SetFolderIcon(folder);
    }, true, "Pick a picture for this folder. It is saved in the folder as a hidden file."_i18n);

    if (folder.icon_size || folder.image) {
        options->Add<SidebarEntryCallback>("Remove icon"_i18n, [this, folder](){
            fs::FsNativeSd().DeleteFile(FolderIconPath(folder.path));
            SortAndFindLastFile(true);
        }, true, "Show the standard folder picture again."_i18n);
    }

    options->Add<SidebarEntryCallback>("Delete"_i18n, [this, folder](){
        DeleteFolder(folder);
    }, true, "Permanently delete the folder and everything in it."_i18n)->SetIcon(ActionIcon::Delete);
}

void Menu::RenameFolder(const NroEntry& folder) {
    const auto [parent, name] = files_transfer::SplitPath(folder.path.s);

    std::string out;
    if (R_FAILED(swkbd::ShowText(out, "Set Folder Name"_i18n.c_str(), name.c_str())) || out.empty() || out == name) {
        return;
    }
    if (!files_transfer::IsSafeName(out)) {
        App::Notify("This name cannot be used."_i18n);
        return;
    }

    const fs::FsPath dst{(parent == "/" ? std::string{} : parent) + "/" + out};
    const auto rc = fs::FsNativeSd().RenameDirectory(folder.path, dst);
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to rename the folder"_i18n);
        return;
    }
    SortAndFindLastFile(true);
}

void Menu::SetFolderIcon(const NroEntry& folder) {
    const auto path = folder.path;
    const auto name = files_transfer::SplitPath(path.s).second;

    forwarder::Config editor{};
    editor.values.title = name;
    fs::FsNativeSd().read_entire_file(FolderIconPath(path), editor.values.icon);
    editor.steam_query = name;
    editor.screen_title = "Set folder icon"_i18n;
    editor.submit_label = "Save Changes"_i18n;
    editor.on_create = [weak_alive = std::weak_ptr<bool>(m_alive), this, path](const forwarder::Values& values) {
        const auto alive = weak_alive.lock();
        if (!alive || !*alive) {
            return false;
        }
        if (!values.icon.empty()) {
            const auto rc = fs::FsNativeSd().write_entire_file(FolderIconPath(path), values.icon);
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to save the icon"_i18n);
                return false;
            }
        }
        SortAndFindLastFile(true);
        return true;
    };

    forwarder::Show(std::move(editor));
}

void Menu::DeleteFolder(const NroEntry& folder) {
    const auto path = folder.path;
    const auto text = "Delete this folder and everything in it?"_i18n + "\n\n"
        + files_transfer::SplitPath(path.s).second + ": " + std::to_string(folder.size) + " " + "apps"_i18n;

    App::Push<OptionBox>(text, "Back"_i18n, "Delete"_i18n, 0, [this, path](auto op_index){
        if (!op_index || !*op_index) {
            return;
        }
        App::Push<ProgressBox>(0, "Deleting"_i18n, files_transfer::SplitPath(path.s).second, [path](auto) -> Result {
            return fs::FsNativeSd().DeleteDirectoryRecursively(path);
        }, [this](Result rc){
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Delete failed!"_i18n);
            } else {
                App::Notify("Delete successful!"_i18n);
            }
            SortAndFindLastFile(true);
        });
    });
}

} // namespace sphaira::ui::menu::homebrew
