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

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

void Menu::OnUsbMountRemoved(std::string_view mount) {
    auto matches = [&](FsView* v) {
        if (!v) {
            return false;
        }
        const auto& e = v->GetFsEntry();
        if (e.type != FsType::Stdio) {
            return false;
        }
        const auto root = e.root.toString();
        return root == mount || root.starts_with(std::string(mount));
    };
    if (matches(view) || matches(view_left.get()) || matches(view_right.get())) {
        SetPop();
    }
}

Menu::Menu(u32 flags, const ::sphaira::location::Entry* launch_location) : MenuBase{"FileBrowser"_i18n, flags} {
    SetAction(Button::START, Action{"Options"_i18n, [this](){
        if (App::GetApp()->m_controller.GotHeld(Button::R2) && !IsFolderPicker()) {
            view->DisplayAdvancedOptions();
        } else {
            view->DisplayOptions();
        }
    }});

    SetAction(Button::L3, Action{"Split"_i18n, [this](){
        SetSplitScreen(IsSplitScreen() ^ 1);
    }});

    if (!IsTab()) {
        SetAction(Button::SELECT, Action{"Close"_i18n, [this](){
            PromptIfShouldExit();
        }});
    }

    view_left = std::make_unique<FsView>(this, ViewSide::Left);
    view = view_left.get();
    ueventCreate(&g_change_uevent, true);

    if (launch_location) {
        const auto loc = *launch_location;
        m_pending_launch_connect = [this, loc]() {
            ConnectToLocation(loc);
        };
    }
}

Menu::Menu(u32 flags, const FsEntry& initial_entry, const fs::FsPath& initial_path)
: Menu{flags, nullptr} {
    // replace the default SD mount with the requested one (e.g. a component's
    // content). Scan runs on focus, so this is safe during construction.
    view->SetFs(initial_path, initial_entry);
    // SetFs is a no-op when the requested mount is the one already open (the
    // default sd card), which would leave this overload sitting at the root -
    // it always means "start at initial_path", so set it unconditionally.
    view->m_path = initial_path;
}

Menu::~Menu() {
#ifdef BUILD_SMB2
    if (g_smb2fs) {
        delete g_smb2fs;
        g_smb2fs = nullptr;
    }
#endif
    devoptab::UmountAllNeworkDevices();
}

void Menu::SetFolderPicker(FolderPickCallback cb, std::string title, std::string confirm, std::string create_name) {
    m_on_folder_picked = std::move(cb);
    m_folder_pick_confirm = confirm.empty() ? "Install firmware from this folder?"_i18n : std::move(confirm);
    m_picker_create_name = std::move(create_name);
    SetTitle(title.empty() ? "Select firmware folder"_i18n : std::move(title));
    view->RemoveAction(Button::X);
    view->RemoveAction(Button::Y);
}

void Menu::ConfirmFolderPick(const fs::FsPath& folder) {
    if (!m_on_folder_picked) {
        return;
    }

    const std::string path_str = folder.s[0] ? folder.s : "/";
    std::string prompt = m_folder_pick_confirm;
    if (m_folder_pick_confirm == "Install firmware from this folder?"_i18n && path::EqualsIC(text_helper::GetExtension(folder.s), "zip")) {
        prompt = "Install firmware from this archive?"_i18n;
    }
    App::Push<OptionBox>(
        prompt + "\n\n" + path_str,
        "Cancel"_i18n, "Select"_i18n, 1,
        [this, folder](auto op_index) {
            if (op_index && *op_index == 1 && m_on_folder_picked) {
                // hand the folder back to the caller, then close the picker.
                // the caller acts on regaining focus (nothing is pushed over
                // this soon-to-be-popped browser).
                m_on_folder_picked(folder);
                SetPop();
            }
        });
}

void Menu::AddSelectedEntries(SelectedType type) {
    auto entries = view->GetSelectedEntries();
    if (entries.empty()) {
        return;
    }

    // an archive/content copy owns a fresh read-only fs so it survives the
    // source view being navigated away / unmounted (e.g. leaving the mount to
    // paste elsewhere).
    std::shared_ptr<fs::Fs> owned_src_fs;
    const auto& e = view->GetFsEntry();
    if (e.type == FsType::Archive) {
        owned_src_fs = std::make_shared<fs::FsZip>(e.root);
    } else if (e.type == FsType::Content) {
        owned_src_fs = std::make_shared<fs::FsNcm>(e.content_app_id, e.content_meta_type, e.content_storage_id);
    }

    m_selected.Add(view, type, entries, view->m_path, owned_src_fs);
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (auto* usb_evt = usbHsFsGetStatusChangeUserEvent(); usb_evt && R_SUCCEEDED(waitSingle(waiterForUEvent(usb_evt), 0))) {
        ueventSignal(&g_change_uevent);
    }

    if (R_SUCCEEDED(waitSingle(waiterForUEvent(&g_change_uevent), 0))) {
        if (IsSplitScreen()) {
            view_left->SortAndFindLastFile(true);
            view_right->SortAndFindLastFile(true);
        } else {
            view->SortAndFindLastFile(true);
        }
    }

    // workaround the buttons not being display properly.
    // basically, inherit all actions from the view, draw them,
    // then restore state after.
    // ponytail: the restore erases them again, so the hint row is re-measured
    // twice a frame here while every other menu caches it. Hoist the inherit to
    // when the view's action set actually changes if it ever shows up in a
    // profile.
    const auto view_actions = view->GetActions();
    SetActions(view_actions);
    ON_SCOPE_EXIT(RemoveActions(view_actions));

    MenuBase::Update(controller, touch);
    view->Update(controller, touch);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    // see Menu::Update().
    const auto view_actions = view->GetActions();
    SetActions(view_actions);
    ON_SCOPE_EXIT(RemoveActions(view_actions));

    MenuBase::Draw(vg, theme);

    if (IsSplitScreen()) {
        view_left->Draw(vg, theme);
        view_right->Draw(vg, theme);

        if (view == view_left.get()) {
            gfx::drawRect(vg, view_right->GetPos(), theme->GetColour(ThemeEntryID_FOCUS), 5);
        } else {
            gfx::drawRect(vg, view_left->GetPos(), theme->GetColour(ThemeEntryID_FOCUS), 5);
        }

        gfx::drawRect(vg, SCREEN_WIDTH/2, GetY(), 1, GetH(), theme->GetColour(ThemeEntryID_LINE));
    } else {
        view->Draw(vg, theme);
    }
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (m_pending_launch_connect) {
        auto action = std::move(m_pending_launch_connect);
        m_pending_launch_connect = nullptr;
        action();
        return;
    }

    if (IsSplitScreen()) {
        view_left->OnFocusGained();
        view_right->OnFocusGained();
    } else {
        view->OnFocusGained();
    }

    if (!m_loaded_assoc_entries) {
        m_loaded_assoc_entries = true;
        log_write("loading assoc entries\n");
        LoadAssocEntries();
    }
}

auto Menu::FindFileAssocFor() -> std::vector<FileAssocEntry> {
    // only support roms in correctly named folders, sorry!
    const auto db_indexs = GetRomDatabaseFromPath(view->m_path);
    const auto& entry = view->GetEntry();
    const auto extension = entry.GetExtension();
    const auto internal_extension = entry.GetInternalExtension();
    if (extension.empty() && internal_extension.empty()) {
        // log_write("failed to get extension for db: %s path: %s\n", database_entry.c_str(), m_path);
        return {};
    }

    std::vector<FileAssocEntry> out_entries;
    if (!db_indexs.empty()) {
        // if database isn't empty, then we are in a valid folder
        // search for an entry that matches the db and ext
        for (const auto& assoc : m_assoc_entries) {
            for (const auto& assoc_db : assoc.database) {
                // if (assoc_db == PATHS[db_idx].folder || assoc_db == PATHS[db_idx].database) {
                for (auto db_idx : db_indexs) {
                    if (PATHS[db_idx].IsDatabase(assoc_db)) {
                        if (assoc.IsExtension(extension, internal_extension)) {
                            out_entries.emplace_back(assoc);
                            goto jump;
                        }
                    }
                }
            }
            jump:
        }
    } else {
        // otherwise, if not in a valid folder, find an entry that doesn't
        // use a database, ie, not a emulator.
        // this is because media players and hbmenu can launch from anywhere
        // and the extension is enough info to know what type of file it is.
        // whereas with roms, a .iso can be used for multiple systems, so it needs
        // to be in the correct folder, ie psx, to know what system that .iso is for.
        for (const auto& assoc : m_assoc_entries) {
            if (assoc.database.empty()) {
                if (assoc.IsExtension(extension, internal_extension)) {
                    log_write("found ext: %s\n", assoc.path.s);
                    out_entries.emplace_back(assoc);
                }
            }
        }
    }

    enum class LauncherGroup {
        RetroArch = 0,
        TICO = 1,
        Other = 2,
    };

    auto GetLauncherGroup = [](std::string_view path) -> LauncherGroup {
        if (path.starts_with('/')) {
            path.remove_prefix(1);
        }
        const auto slash = path.find('/');
        const auto root = (slash != std::string_view::npos) ? path.substr(0, slash) : path;
        if (path::EqualsIC(root, "retroarch")) {
            return LauncherGroup::RetroArch;
        }
        if (path::EqualsIC(root, "tico")) {
            return LauncherGroup::TICO;
        }
        return LauncherGroup::Other;
    };

    std::ranges::stable_sort(out_entries, [&](const FileAssocEntry& a, const FileAssocEntry& b) {
        const auto group_a = GetLauncherGroup(a.path.s);
        const auto group_b = GetLauncherGroup(b.path.s);
        if (group_a != group_b) {
            return group_a < group_b;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return out_entries;
}

void Menu::LoadAssocEntriesPath(const fs::FsPath& path) {
    auto dir = opendir(path);
    if (!dir) {
        return;
    }
    ON_SCOPE_EXIT(closedir(dir));

    while (auto d = readdir(dir)) {
        if (d->d_name[0] == '.') {
            continue;
        }

        if (d->d_type != DT_REG) {
            continue;
        }

        const auto ext = std::strrchr(d->d_name, '.');
        if (!ext || strcasecmp(ext, ".ini")) {
            continue;
        }

        const auto full_path = GetNewPath(path, d->d_name);
        FileAssocEntry assoc{};

        ini_browse([](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) {
            auto assoc = static_cast<FileAssocEntry*>(UserData);
            if (!std::strcmp(Key, "path")) {
                assoc->path = Value;
            } else if (!std::strcmp(Key, "name")) {
                assoc->name = Value;
            } else if (!std::strcmp(Key, "argument")) {
                assoc->argument = Value;
            } else if (!std::strcmp(Key, "supported_extensions") || !std::strcmp(Key, "extensions")) {
                for (const auto& p : std::views::split(std::string_view{Value}, '|')) {
                    if (p.empty()) {
                        continue;
                    }
                    assoc->ext.emplace_back(p.data(), p.size());
                }
            } else if (!std::strcmp(Key, "database")) {
                for (const auto& p : std::views::split(std::string_view{Value}, '|')) {
                    if (p.empty()) {
                        continue;
                    }
                    assoc->database.emplace_back(p.data(), p.size());
                }
            } else if (!std::strcmp(Key, "use_base_name")) {
                if (!std::strcmp(Value, "true") || !std::strcmp(Value, "1")) {
                    assoc->use_base_name = true;
                }
            }
            return 1;
        }, &assoc, full_path);

        if (assoc.ext.empty()) {
            continue;
        }

        if (assoc.name.empty()) {
            assoc.name.assign(d->d_name, ext - d->d_name);
        }

        // if path isn't empty, check if the file exists
        bool file_exists{};
        if (!assoc.path.empty()) {
            file_exists = view->m_fs->FileExists(assoc.path);
        } else {
            const auto nro_name = assoc.name + ".nro";
            for (const auto& nro : homebrew::GetNroEntries()) {
                const auto len = std::strlen(nro.path);
                if (len < nro_name.length()) {
                    continue;
                }
                if (!strcasecmp(nro.path + len - nro_name.length(), nro_name.c_str())) {
                    assoc.path = nro.path;
                    file_exists = true;
                    break;
                }
            }
        }

        // after all of that, the file doesn't exist :(
        if (!file_exists) {
            // log_write("removing: %s\n", assoc.name.c_str());
            continue;
        }

        // log_write("\tpath: %s\n", assoc.path.s);
        // log_write("\tname: %s\n", assoc.name.c_str());
        // for (const auto& ext : assoc.ext) {
        //     log_write("\t\text: %s\n", ext.c_str());
        // }
        // for (const auto& db : assoc.database) {
        //     log_write("\t\tdb: %s\n", db.c_str());
        // }

        m_assoc_entries.emplace_back(assoc);
    }
}

static size_t CountAssocEntriesPath(const fs::FsPath& path) {
    auto dir = opendir(path);
    if (!dir) {
        return 0;
    }
    ON_SCOPE_EXIT(closedir(dir));

    size_t count = 0;
    while (auto d = readdir(dir)) {
        if (d->d_name[0] == '.') {
            continue;
        }

        if (d->d_type != DT_REG) {
            continue;
        }

        const auto ext = std::strrchr(d->d_name, '.');
        if (!ext || strcasecmp(ext, ".ini")) {
            continue;
        }

        count++;
    }

    return count;
}

void Menu::LoadAssocEntries() {
    size_t count = 0;
    const bool romfs_ok = R_SUCCEEDED(romfsInit());
    if (romfs_ok) {
        count += CountAssocEntriesPath("romfs:/assoc/");
    }
    count += CountAssocEntriesPath(paths::ASSOC);

    m_assoc_entries.reserve(count);

    // load from romfs first
    if (romfs_ok) {
        LoadAssocEntriesPath("romfs:/assoc/");
        romfsExit();
    }
    // then load custom entries
    LoadAssocEntriesPath(paths::ASSOC);
}

void Menu::UpdateSubheading() {
    const auto index = view->m_entries_current.empty() ? 0 : view->m_index + 1;
    std::string text = std::to_string(index) + " / " + std::to_string(view->m_entries_current.size());

    if (view->m_selected_count) {
        u64 selected_size{};
        size_t selected_files{};
        size_t pending_files{};
        for (const auto& entry : view->m_entries) {
            if (!entry.selected || !entry.IsFile()) {
                continue;
            }
            selected_files++;
            if (!entry.metadata_loaded) {
                pending_files++;
                continue;
            }
            const auto size = entry.file_size > 0 ? static_cast<u64>(entry.file_size) : 0;
            selected_size = size > UINT64_MAX - selected_size ? UINT64_MAX : selected_size + size;
        }

        // shown at the top next to the title: count above, size below. The
        // bottom sub heading is left with just the position, as the size was
        // hidden behind the button hints there.
        std::string size_text;
        if (selected_files) {
            size_text = utils::formatSizeStorage(selected_size);
            if (pending_files) {
                size_text += " + ...";
            }
        }
        this->SetTitleStats("Selected"_i18n + ": " + std::to_string(view->m_selected_count), std::move(size_text));
    } else {
        this->SetTitleStats({}, {});
    }

    this->SetSubHeading(std::move(text));
}

void Menu::SetSplitScreen(bool enable) {
    if (m_split_screen != enable) {
        m_split_screen = enable;

        if (m_split_screen) {
            const auto change_view = [this](FsView* new_view){
                if (view != new_view) {
                    view->OnFocusLost();
                    view = new_view;
                    view->OnFocusGained();
                    SetTitleSubHeading(view->m_path, true);
                    UpdateSubheading();
                }
            };

            // load second screen as a copy of the left side.
            view->SetSide(ViewSide::Left);
            view_right = std::make_unique<FsView>(this, view->m_path, view->GetFsEntry(), ViewSide::Right);
            change_view(view_right.get());

            SetAction(Button::LEFT, Action{[this, change_view](){
                change_view(view_left.get());
            }});
            SetAction(Button::RIGHT, Action{[this, change_view](){
                change_view(view_right.get());
            }});
        } else {
            if (view == view_right.get()) {
                view_left = std::move(view_right);
            }

            view_right = {};
            view = view_left.get();
            view->SetSide(ViewSide::Left);

            RemoveAction(Button::LEFT);
            RemoveAction(Button::RIGHT);
            ResetSelection();
        }
    }
}

void Menu::RefreshViews() {
    ResetSelection();

    if (IsSplitScreen()) {
        view_left->Scan(view_left->m_path);
        view_right->Scan(view_right->m_path);
    } else {
        view->Scan(view->m_path);
    }
}

void Menu::PromptIfShouldExit() {
    if (IsTab()) {
        return;
    }

    SetPop();
}
} // namespace sphaira::ui::menu::filebrowser
