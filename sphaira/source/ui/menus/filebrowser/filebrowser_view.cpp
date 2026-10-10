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

FsView::FsView(Menu* menu, const fs::FsPath& path, const FsEntry& entry, ViewSide side) : m_menu{menu}, m_side{side} {
    mutexInit(&m_metadata_mutex);
    mutexInit(&m_metadata_io_mutex);
    condvarInit(&m_metadata_cond);
    if (R_SUCCEEDED(threadCreate(&m_metadata_thread, metadata_thread_func, this, nullptr, 1024 * 32, PRIO_PREEMPTIVE, 1))) {
        if (R_SUCCEEDED(threadStart(&m_metadata_thread))) {
            m_metadata_thread_created = true;
        } else {
            threadClose(&m_metadata_thread);
        }
    }

    this->SetActions(
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){
            ToggleSelection();
        }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){
            InvertSelection();
        }}),
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (m_entries_current.empty()) {
                return;
            }

            // folder-picker mode: row 0 (the synthetic "select current folder"
            // action) commits the current folder; opening a .zip file selects
            // the archive directly; opening any other file commits the current
            // folder; directories keep navigating so the user can drill down.
            if (m_menu->IsFolderPicker()) {
                if (m_index == 0) {
                    m_menu->ConfirmFolderPick(m_path);
                    return;
                }
                const auto& entry = GetEntry();
                if (entry.IsFile()) {
                    if (path::EqualsIC(entry.GetExtension(), "zip")) {
                        m_menu->ConfirmFolderPick(GetNewPathCurrent());
                        return;
                    }
                    m_menu->ConfirmFolderPick(m_path);
                    return;
                }
            }

            if (IsParentEntry(m_index)) {
                WalkUp();
                return;
            }

            if (!m_menu->IsFolderPicker() && IsSd() && m_is_update_folder && m_daybreak_path.has_value()) {
                App::Push<OptionBox>("Open with DayBreak?"_i18n, "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
                    if (op_index && *op_index) {
                        // daybreak uses native fs so do not use nro_add_arg_file
                        // otherwise it'll fail to open the folder...
                        nro_launch(m_daybreak_path.value(), nro_add_arg(m_path));
                    }
                });
                return;
            }

            const auto& entry = GetEntry();

            if (m_fs_entry.type == FsType::Root) {
                if (entry.virtual_target_entry.type == FsType::Network) {
                    ConnectToLocation(entry.virtual_target_entry);
                } else {
                    SetFs(entry.virtual_target_entry.root, entry.virtual_target_entry);
                }
                return;
            }

            if (entry.type == FsDirEntryType_Dir) {
                Scan(GetNewPathCurrent());
            } else {
                // special case for nro
                if (IsSd() && path::EqualsIC(entry.GetExtension(), "nro")) {
                    App::Push<OptionBox>("Launch "_i18n + entry.GetName() + '?',
                        "No"_i18n, "Launch"_i18n, 1, [this](auto op_index){
                            if (op_index && *op_index) {
                                nro_launch(GetNewPathCurrent());
                            }
                        });
                } else if (path::IsAnyOfIC(entry.GetExtension(), INSTALL_EXTENSIONS)) {
                    InstallFiles();
                } else if (IsSd() && path::IsAnyOfIC(entry.GetExtension(), IMAGE_EXTENSIONS)) {
                    OpenImageViewer();
                } else if (path::IsAnyOfIC(entry.GetExtension(), ZIP_EXTENSIONS)) {
                    // browse inside the archive; if the zip is also a ROM (assoc
                    // match), offer both browsing and launching.
                    const auto assoc_list = m_menu->FindFileAssocFor();
                    if (assoc_list.empty()) {
                        OpenArchive();
                    } else {
                        PopupList::Items items;
                        items.emplace_back("Browse archive"_i18n);
                        for (const auto& p : assoc_list) {
                            items.emplace_back(MakeLauncherLabel(p));
                        }
                        const auto title = "Open: "_i18n + entry.GetName();
                        App::Push<PopupList>(title, items, [this, assoc_list](auto op_index){
                            if (!op_index) {
                                return;
                            }
                            if (*op_index == 0) {
                                OpenArchive();
                            } else {
                                const auto& assoc = assoc_list[*op_index - 1];
                                nro_launch(assoc.path, assoc.GetRomArgs(GetNewPathCurrent()));
                            }
                        });
                    }
                } else if (text_helper::IsTextFile(entry.name)) {
                    const auto path = GetNewPathCurrent();
                    const bool writable = !IsReadOnly(path);
                    const bool can_edit = writable && entry.file_size <= 4 * 1024 * 1024;
                    if (!can_edit) {
                        App::Push<fileview::Menu>(m_fs.get(), path, fileview::TextMode::View, writable);
                    } else {
                        PopupList::Items items;
                        items.emplace_back("View as text"_i18n);
                        items.emplace_back("Edit"_i18n);
                        items.emplace_back("Edit on PC / phone"_i18n);
                        auto popup = std::make_unique<PopupList>("Open: "_i18n + entry.GetName(), items, [this, path](auto op_index){
                            if (!op_index) {
                                return;
                            }
                            if (*op_index == 0) {
                                App::Push<fileview::Menu>(m_fs.get(), path, fileview::TextMode::View, true);
                            } else if (*op_index == 1) {
                                App::Push<fileview::Menu>(m_fs.get(), path, fileview::TextMode::Edit, true);
                            } else {
                                auto menu = std::make_unique<fileview::Menu>(m_fs.get(), path, fileview::TextMode::Edit, true);
                                menu->QueueRemoteEdit();
                                App::Push(std::move(menu));
                            }
                        });
                        popup->SetMenuStyle(true);
                        App::Push(std::move(popup));
                    }
                } else if (IsSd()) {
                    const auto assoc_list = m_menu->FindFileAssocFor();
                    if (!assoc_list.empty()) {
                        // for (auto&e : assoc_list) {
                        //     log_write("assoc got: %s\n", e.path.c_str());
                        // }

                        PopupList::Items items;
                        for (const auto&p : assoc_list) {
                            items.emplace_back(MakeLauncherLabel(p));
                        }

                        const auto title = "Launch option for: "_i18n + GetEntry().name;
                        App::Push<PopupList>(
                            title, items, [this, assoc_list](auto op_index){
                                if (op_index) {
                                    log_write("selected: %s\n", assoc_list[*op_index].name.c_str());
                                    const auto& assoc = assoc_list[*op_index];
                                    nro_launch(assoc.path, assoc.GetRomArgs(GetNewPathCurrent()));
                                } else {
                                    log_write("pressed B to skip launch...\n");
                                }
                            }
                        );
                    } else {
                        log_write("assoc list is empty\n");
                    }
                }
            }
        }}),

        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (!m_menu->IsTab() && App::GetApp()->m_controller.GotHeld(Button::R2)) {
                m_menu->PromptIfShouldExit();
                return;
            }

            WalkUp();
        }})
    );

    SetSide(m_side);

    auto buf = path;
    if (path.empty()) {
        ini_gets("paths", "last_path", entry.root, buf, sizeof(buf), App::CONFIG_PATH);
    }

    SetFs(buf, entry);
}

FsView::FsView(Menu* menu, ViewSide side) : FsView{menu, "", FS_ENTRY_DEFAULT, side} {

}


FsView::~FsView() {
    FreeThumbs();
    if (m_title_service) {
        title::Exit();
    }

    if (m_metadata_thread_created) {
        mutexLock(&m_metadata_mutex);
        m_metadata_thread_exit = true;
        condvarWakeAll(&m_metadata_cond);
        mutexUnlock(&m_metadata_mutex);
        threadWaitForExit(&m_metadata_thread);
        threadClose(&m_metadata_thread);
    }

    // don't store mount points for non-sd card paths.
    if (IsSd()) {
        ini_puts("paths", "last_path", m_path, App::CONFIG_PATH);
    }
#ifdef BUILD_SMB2
    if (m_fs_entry.type == FsType::Network) {
        g_smb_ref_count--;
        if (g_smb_ref_count <= 0 && g_smb2fs) {
            delete g_smb2fs;
            g_smb2fs = nullptr;
            g_smb_ref_count = 0;
        }
    }
#endif
}

void FsView::Update(Controller* controller, TouchInfo* touch) {
    ApplyRemoteMetadata();
    m_list->OnUpdate(controller, touch, m_index, m_entries_current.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}



void FsView::OnFocusGained() {
    Widget::OnFocusGained();
    if (m_entries.empty()) {
        const auto rc = Scan(m_path.empty() ? m_fs->Root() : m_path);
        if (R_FAILED(rc) && (m_fs_entry.type == FsType::Network || m_fs_entry.type == FsType::Stdio)) {
            log_write("[FILEBROWSER] listing failed: 0x%X\n", rc);
            if (m_fs_entry.type == FsType::Network) {
                App::Push<OptionBox>("Failed to list network storage!"_i18n + "\n" +
                    "The server is reachable but the listing failed. Check the credentials and the shared folder path."_i18n, "OK"_i18n);
            } else {
                // usb drive / mtp phone stopped answering. Without this the
                // user is left staring at a fake "Empty..." listing.
                App::PushErrorBox(rc, "Failed to list storage!"_i18n);
            }
            const FsEntry root_entry{
                .name = "System Root",
                .root = "root:/",
                .type = FsType::Root
            };
            SetFs("root:/", root_entry);
        }
    } else if (m_fs_entry.type == FsType::Root) {
        // sources may have changed while unfocused -- a network location added
        // or removed, or a usb drive plugged in or pulled. Re-scan so the root
        // reflects what is currently connected, not just re-sort the old list.
        Scan(m_path.empty() ? m_fs->Root() : m_path);
    } else if (m_metadata_paused) {
        m_metadata_paused = false;
        QueueRemoteMetadata();
    }
}



void FsView::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff();
    } else if (!m_entries_current.empty()) {
        // keep one row of context past the cursor visible, so scrolling starts
        // at the second-to-last row rather than when the cursor falls off the
        // edge. also covers the callers that move the index themselves (X
        // toggling selection), which otherwise never touched the scroll offset.
        const s64 count = m_entries_current.size();
        m_list->EnsureVisible(m_index + 1, count);
        m_list->EnsureVisible(m_index - 1, count);
    }

    // let the metadata worker fetch sizes for entries near the cursor first.
    if (m_metadata_thread_created) {
        mutexLock(&m_metadata_mutex);
        m_metadata_focus = m_index;
        mutexUnlock(&m_metadata_mutex);
    }

    if (IsSd() && !m_entries_current.empty() && !GetEntry().checked_internal_extension && path::EqualsIC(GetEntry().GetExtension(), "zip")) {
        GetEntry().checked_internal_extension = true;

        TimeStamp ts;
        fs::FsPath filename_inzip{};
        if (R_SUCCEEDED(mz::PeekFirstFileName(GetFs(), GetNewPathCurrent(), filename_inzip))) {
            if (auto ext = std::strrchr(filename_inzip, '.')) {
                GetEntry().internal_name = filename_inzip.toString();
                GetEntry().internal_extension = ext+1;
            }
            log_write("\tzip, time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
        }
    }

    m_menu->UpdateSubheading();
}

void FsView::ToggleSelection() {
    if (m_entries_current.empty() || IsParentEntry(m_index)) {
        return;
    }

    if (!m_menu->m_selected.Empty()) {
        m_menu->ResetSelection();
    }

    const bool current_is_file = GetEntry().IsFile();
    const bool bulk_select = App::GetApp()->m_controller.GotHeld(Button::R2);
    if (bulk_select) {
        s64 visible_selected_count{};
        for (u32 i = 0; i < m_entries_current.size(); i++) {
            if (GetEntry(i).selected) {
                visible_selected_count++;
            }
        }

        const auto set = visible_selected_count != static_cast<s64>(m_entries_current.size());
        for (u32 i = 0; i < m_entries_current.size(); i++) {
            if (!IsParentEntry(i)) {
                GetEntry(i).selected = set;
            }
        }
    } else {
        GetEntry().selected ^= 1;
    }

    m_selected_count = 0;
    for (const auto& e : m_entries) {
        if (e.selected) {
            m_selected_count++;
        }
    }

    s64 next_index = m_index + 1;
    if (current_is_file) {
        while (next_index < static_cast<s64>(m_entries_current.size()) && !GetEntry(next_index).IsFile()) {
            next_index++;
        }
    }
    if (!bulk_select && next_index < static_cast<s64>(m_entries_current.size())) {
        SetIndex(next_index);
    } else {
        m_menu->UpdateSubheading();
    }
}

void FsView::InvertSelection() {
    if (m_entries_current.empty()) {
        return;
    }

    if (!m_menu->m_selected.Empty()) {
        m_menu->ResetSelection();
    }

    for (u32 i = 0; i < m_entries_current.size(); i++) {
        if (!IsParentEntry(i)) {
            GetEntry(i).selected ^= 1;
        }
    }

    m_selected_count = 0;
    for (const auto& e : m_entries) {
        if (e.selected) {
            m_selected_count++;
        }
    }

    m_menu->UpdateSubheading();
}
} // namespace sphaira::ui::menu::filebrowser
