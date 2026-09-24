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
        SetAction(Button::SELECT, Action{App::HandleMinus});
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
