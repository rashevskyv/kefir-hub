// Homebrew menu folders. They are virtual: a folder is a tag kept in homebrew_folders.txt (see
// homebrew_vfolders.hpp), an app can be in several, and nothing on the SD card moves, because many apps
// look for their files at a fixed place under /switch.

#include "app.hpp"
#include "app_paths.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "swkbd.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/homebrew_internal.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/forwarder_editor.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"

#include <cstdio>

namespace sphaira::ui::menu::homebrew {

namespace {

const auto FOLDERS_FILE = paths::DATA_ROOT + "/homebrew_folders.txt";

auto FolderName(const hb_folders::Store& s, u32 id) -> std::string {
    if (id == hb_folders::ROOT) {
        return "Homebrew"_i18n;
    }
    const auto f = hb_folders::FindFolder(s, id);
    return f ? f->name : std::string{};
}

// a name for a new or renamed folder; says why when it cannot be used
auto CheckName(const hb_folders::Store& s, const std::string& name, u32 self) -> bool {
    if (!hb_folders::IsValidName(name)) {
        App::Notify("This name cannot be used."_i18n);
        return false;
    }
    if (const auto f = hb_folders::FindFolderByName(s, name); f && f->id != self) {
        App::Notify("A folder with this name already exists."_i18n);
        return false;
    }
    return true;
}

} // namespace

auto FolderIconPath(u32 id) -> fs::FsPath {
    fs::FsPath out{};
    std::snprintf(out, sizeof(out), "%s/homebrew_folders/%u.jpg", paths::DATA_ROOT.c_str(), id);
    return out;
}

void LoadFolderIcon(NVGcontext* vg, NroEntry& e) {
    e.icon_size = 0; // one try only
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(FolderIconPath(e.folder_id), data))) {
        return;
    }
    const auto image = ImageLoadIcon(data);
    if (!image.data.empty() && image.w == 256 && image.h == 256) {
        e.image = nvgCreateImageRGBA(vg, image.w, image.h, 0, image.data.data());
    }
}

void Menu::LoadFolders() {
    std::vector<u8> data;
    m_store = {};
    if (R_SUCCEEDED(fs::FsNativeSd().read_entire_file(fs::FsPath{FOLDERS_FILE}, data))) {
        m_store = hb_folders::Parse(std::string_view{reinterpret_cast<const char*>(data.data()), data.size()});
    }
}

void Menu::SaveFolders() {
    const auto text = hb_folders::Format(m_store);
    const auto rc = fs::FsNativeSd().write_entire_file(fs::FsPath{FOLDERS_FILE}, std::vector<u8>(text.begin(), text.end()));
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to save the folders"_i18n);
    }
}

void Menu::BuildView() {
    const auto vg = App::GetVg();
    for (auto& f : m_folders) {
        FreeEntry(vg, f);
    }
    m_folders.clear();
    m_folder_previews.clear();
    for (auto& index : m_entries_index) {
        index.clear();
    }

    // the folder was deleted: back to the top level
    if (m_folder != hb_folders::ROOT && !hb_folders::FindFolder(m_store, m_folder)) {
        m_folder = hb_folders::ROOT;
    }

    std::vector<std::string> paths;
    paths.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        paths.emplace_back(e.path.s);
    }
    const auto view = hb_folders::BuildView(m_store, paths, m_folder);

    fs::FsNativeSd fs;
    for (size_t j = 0; j < view.folders.size(); j++) {
        NroEntry e{};
        e.is_dir = true;
        e.folder_id = view.folders[j];
        std::snprintf(e.nacp.lang.name, sizeof(e.nacp.lang.name), "%s", FolderName(m_store, e.folder_id).c_str());
        e.size = static_cast<s64>(view.counts[j]);
        e.has_star = false;
        e.icon_size = fs.FileExists(FolderIconPath(e.folder_id)) ? 1 : 0; // loaded when the row is drawn
        m_folders.emplace_back(std::move(e));
        m_folder_previews.emplace_back(view.previews[j].begin(), view.previews[j].end());
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

void Menu::Enter(u32 folder, u32 focus) {
    // the same folder again (after a change): keep the place in the list
    const auto keep = folder == m_folder ? m_index : 0;
    if (folder != m_folder) {
        ClearSelection();
    }
    m_folder = folder;
    BuildView();
    Sort();
    SetIndex(keep);

    if (focus != hb_folders::ROOT) {
        for (s64 i = 0; i < static_cast<s64>(m_entries_current.size()); i++) {
            const auto& e = GetEntry(i);
            if (e.is_dir && e.folder_id == focus) {
                SetIndex(i);
                m_list->EnsureVisible(i, m_entries_current.size());
                break;
            }
        }
    }
}

// A opens a folder and starts an app; B goes back to the top level before it leaves the Hub.
void Menu::UpdateActions() {
    const bool folder = IsFolderFocused();
    const bool at_root = m_folder == hb_folders::ROOT;

    SetAction(Button::A, Action{folder ? "Open"_i18n : "Launch"_i18n, [this](){
        if (m_entries_current.empty()) {
            return;
        }
        auto& e = GetEntry();
        if (e.is_dir) {
            Enter(e.folder_id);
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
        } else if (m_folder != hb_folders::ROOT) {
            Enter(hb_folders::ROOT, m_folder);
        } else {
            App::Exit();
        }
    }});
}

void Menu::AddFolderOptions(Sidebar* options) {
    const auto id = GetEntry().folder_id;
    const bool has_icon = GetEntry().icon_size || GetEntry().image;

    options->Add<SidebarEntryHeader>("THIS FOLDER"_i18n);

    options->Add<SidebarEntryCallback>("Open"_i18n, [this, id](){
        Enter(id);
    }, true, "Show the apps in this folder."_i18n);

    options->Add<SidebarEntryCallback>("Rename"_i18n, [this, id](){
        RenameFolder(id);
    }, true, "Rename this folder. Files on the SD card do not change."_i18n)->SetIcon(ActionIcon::Edit);

    options->Add<SidebarEntryCallback>("Edit name and icon"_i18n, [this, id](){
        EditFolder(id);
    }, true, "Change the name and the picture of this folder."_i18n)->SetIcon(ActionIcon::Edit);

    if (has_icon) {
        options->Add<SidebarEntryCallback>("Remove icon"_i18n, [this, id](){
            fs::FsNativeSd().DeleteFile(FolderIconPath(id));
            Enter(m_folder);
        }, true, "Show the standard folder picture again."_i18n);
    }

    options->Add<SidebarEntryCallback>("Delete"_i18n, [this, id](){
        DeleteFolder(id);
    }, true, "Delete this folder. The apps in it stay in Homebrew."_i18n)->SetIcon(ActionIcon::Delete);

    AddNewFolderOption(options);
}

void Menu::AddNewFolderOption(Sidebar* options) {
    if (m_folder == hb_folders::ROOT) {
        options->Add<SidebarEntryCallback>("New folder"_i18n, [this](){
            NewFolder();
        }, true, "Make a folder in Homebrew. It only groups apps here; files on the SD card do not move."_i18n)->SetIcon(ActionIcon::FolderNew);
    }
}

void Menu::AddAppFolderOptions(Sidebar* options) {
    options->Add<SidebarEntryHeader>("FOLDERS"_i18n);

    options->Add<SidebarEntryCallback>("Copy to folder"_i18n, [this](){
        PickFolder("Copy to folder"_i18n, m_folder, [this](u32 to){ PutInFolder(to, false); });
    }, true, "Also show the selected apps in another folder. They stay here too."_i18n)->SetIcon(ActionIcon::FolderCopy);

    options->Add<SidebarEntryCallback>("Move to folder"_i18n, [this](){
        PickFolder("Move to folder"_i18n, m_folder, [this](u32 to){ PutInFolder(to, true); });
    }, true, "Show the selected apps in another folder instead of this one."_i18n)->SetIcon(ActionIcon::FolderMove);

    if (m_folder != hb_folders::ROOT) {
        options->Add<SidebarEntryCallback>("Remove from this folder"_i18n, [this](){
            for (const auto& e : GetSelectedEntries()) {
                hb_folders::Remove(m_store, e.path.s, m_folder);
            }
            SaveFolders();
            ClearSelection();
            Enter(m_folder);
        }, true, "The app stays in its other folders. An app in no folder goes back to Homebrew."_i18n)->SetIcon(ActionIcon::FolderOut);
    }
    AddNewFolderOption(options);
}

void Menu::PickFolder(const std::string& title, u32 skip, std::function<void(u32)> on_pick) {
    std::vector<u32> ids;
    PopupList::Items items;
    if (skip != hb_folders::ROOT) {
        ids.push_back(hb_folders::ROOT);
        items.push_back("Homebrew"_i18n);
    }
    for (const auto& f : m_store.folders) {
        if (f.id != skip) {
            ids.push_back(f.id);
            items.push_back(f.name);
        }
    }
    items.push_back("New folder"_i18n);

    auto popup = std::make_unique<PopupList>(title, items, [this, ids, on_pick](auto op_index){
        if (!op_index) {
            return;
        }
        const auto i = static_cast<size_t>(*op_index);
        if (i < ids.size()) {
            on_pick(ids[i]);
        } else {
            NewFolder(on_pick);
        }
    });
    popup->SetMenuStyle(true); // a choice of where to put the apps, not a current value
    App::Push(std::move(popup));
}

void Menu::NewFolder(std::function<void(u32)> on_created) {
    std::string name;
    if (R_FAILED(swkbd::ShowText(name, "New folder"_i18n.c_str(), "")) || name.empty()) {
        return;
    }
    if (!CheckName(m_store, name, hb_folders::ROOT)) {
        return;
    }
    const auto id = hb_folders::CreateFolder(m_store, name);
    SaveFolders();
    if (on_created) {
        on_created(id);
    } else {
        Enter(m_folder);
    }
}

void Menu::RenameFolder(u32 id) {
    std::string name;
    if (R_FAILED(swkbd::ShowText(name, "Set Folder Name"_i18n.c_str(), FolderName(m_store, id).c_str())) || name.empty()) {
        return;
    }
    if (CheckName(m_store, name, id) && hb_folders::RenameFolder(m_store, id, name)) {
        SaveFolders();
        Enter(m_folder);
    }
}

void Menu::EditFolder(u32 id) {
    forwarder::Config editor{};
    editor.values.title = FolderName(m_store, id);
    fs::FsNativeSd().read_entire_file(FolderIconPath(id), editor.values.icon);
    editor.steam_query = editor.values.title;
    editor.screen_title = "Edit name and icon"_i18n;
    editor.title_label = "Name"_i18n;
    editor.submit_label = "Save Changes"_i18n;
    editor.show_forwarder_options = false;
    editor.on_create = [weak_alive = std::weak_ptr<bool>(m_alive), this, id](const forwarder::Values& values) {
        const auto alive = weak_alive.lock();
        if (!alive || !*alive) {
            return false;
        }
        if (values.title != FolderName(m_store, id)) {
            if (!CheckName(m_store, values.title, id) || !hb_folders::RenameFolder(m_store, id, values.title)) {
                return false;
            }
            SaveFolders();
        }
        if (!values.icon.empty()) {
            fs::FsNativeSd fs;
            fs.CreateDirectory(fs::FsPath{paths::DATA_ROOT + "/homebrew_folders"});
            const auto rc = fs.write_entire_file(FolderIconPath(id), values.icon);
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to save the icon"_i18n);
                return false;
            }
        }
        Enter(m_folder);
        return true;
    };

    forwarder::Show(std::move(editor));
}

void Menu::DeleteFolder(u32 id) {
    const auto text = "Delete this folder? The apps in it stay in Homebrew."_i18n + "\n\n" + FolderName(m_store, id);
    App::Push<OptionBox>(text, "Back"_i18n, "Delete"_i18n, 0, [this, id](auto op_index){
        if (!op_index || !*op_index) {
            return;
        }
        hb_folders::DeleteFolder(m_store, id);
        SaveFolders();
        fs::FsNativeSd().DeleteFile(FolderIconPath(id));
        Enter(hb_folders::ROOT);
    });
}

void Menu::PutInFolder(u32 to, bool move) {
    const auto targets = GetSelectedEntries();
    if (targets.empty()) {
        return;
    }
    for (const auto& e : targets) {
        if (move) {
            hb_folders::Move(m_store, e.path.s, m_folder, to);
        } else {
            hb_folders::Add(m_store, e.path.s, to);
        }
    }
    SaveFolders();
    App::Notify((move ? "Moved to "_i18n : "Copied to "_i18n) + FolderName(m_store, to));
    ClearSelection();
    Enter(m_folder);
}

} // namespace sphaira::ui::menu::homebrew
