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
                } else if (IsSd() && path::IsAnyOfIC(entry.GetExtension(), ZIP_EXTENSIONS)) {
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

void FsView::FreeThumbs() {
    auto* vg = App::GetVg();
    for (auto& image : m_thumbs) {
        if (image > 0 && vg) {
            nvgDeleteImage(vg, image);
        }
        image = 0;
    }
    m_thumbs.clear();
    for (auto& mosaic : m_mosaics) {
        file_icon::FreeMosaic(mosaic);
    }
    m_mosaics.clear();
}

auto FsView::TryLoadThumb(u32 entry_index) -> bool {
    if (entry_index >= m_entries.size()) {
        return false;
    }
    auto& e = m_entries[entry_index];
    if (e.IsDir()) {
        if (entry_index >= m_mosaics.size()) {
            return false;
        }
        if (!std::strcmp(e.name, "..")) {
            return false;
        }
        auto& mosaic = m_mosaics[entry_index];
        if (!mosaic.listed) {
            mosaic = file_icon::ListFolderPreview(m_fs.get(), GetNewPath(e));
            return true;
        }
        for (auto& cell : mosaic.cells) {
            if (file_icon::TryLoadCell(cell)) {
                return true;
            }
        }
        return false;
    }
    if (entry_index >= m_thumbs.size() || m_thumbs[entry_index]) {
        return false;
    }
    file_icon::Cell cell;
    cell.path = GetNewPath(e);
    cell.ext = e.GetExtension();
    if (!file_icon::TryLoadCell(cell)) {
        m_thumbs[entry_index] = cell.image ? cell.image : -1;
        return false;
    }
    m_thumbs[entry_index] = cell.image;
    return m_thumbs[entry_index] > 0;
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

void FsView::Draw(NVGcontext* vg, Theme* theme) {
    const auto& text_col = theme->GetColour(ThemeEntryID_TEXT);

    if (m_entries_current.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    constexpr float text_xoffset{15.f};
    bool got_dir_count = false;
    int loaded{};
    const bool icon_grid = m_menu->IsIconLayout();

    nvgSave(vg);
    nvgScissor(vg, m_list_clip.x, m_list_clip.y, m_list_clip.w, m_list_clip.h);
    m_list->Draw(vg, theme, m_entries_current.size(), [this, text_col, &got_dir_count, &loaded, icon_grid](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = GetEntry(i);
        const auto entry_i = m_entries_current.empty() ? 0u : (i < static_cast<s64>(m_entries_current.size()) ? m_entries_current[i] : 0u);
        if (icon_grid && loaded < 2 && TryLoadThumb(entry_i)) {
            loaded++;
        }

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = m_index == i;

        // ticked rows get a tinted band behind them, so which entries are in
        // the selection reads at a glance rather than one checkbox at a time.
        // Drawn under everything else, including the cursor outline.
        if (e.IsSelected()) {
            auto tint = theme->GetColour(ThemeEntryID_FOCUS);
            tint.a *= 0.35f;
            gfx::drawRect(vg, v, tint, 5.f);
        }

        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (!icon_grid) {
            if (i != m_entries_current.size() - 1) {
                gfx::drawRect(vg, Vec4{x, y + h, w, 1.f}, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        } else {
            DrawElement(v, ThemeEntryID_GRID);
        }

        if (icon_grid) {
            const Vec4 preview{x + 4.f, y + 4.f, w - 8.f, h - 32.f};
            const auto draw_name = [&](const char* name) {
                nvgSave(vg);
                nvgIntersectScissor(vg, x + 4.f, y + h - 26.f, w - 8.f, 24.f);
                gfx::drawTextArgs(vg, x + w / 2.f, y + h - 14.f, 14.f,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s", name);
                nvgRestore(vg);
            };
            if (m_menu->IsFolderPicker() && i == 0) {
                file_icon::DrawFolderShape(vg, theme, preview);
                draw_name("Select current folder"_i18n.c_str());
                return;
            }
            if (IsParentEntry(i)) {
                file_icon::DrawFolderShape(vg, theme, preview);
                draw_name("..");
                return;
            }
            if (e.IsDir()) {
                if (entry_i < m_mosaics.size()) {
                    file_icon::DrawMosaic(vg, theme, preview, m_mosaics[entry_i]);
                } else {
                    file_icon::DrawFolderShape(vg, theme, preview);
                }
            } else {
                const int thumb = (entry_i < m_thumbs.size()) ? m_thumbs[entry_i] : 0;
                file_icon::DrawFileThumb(vg, theme, preview, thumb, e.GetExtension());
            }
            if (e.IsFile() && path::EqualsIC(e.GetExtension(), "bin")) {
                const auto title_label = GetTitleLabel(e);
                draw_name(!title_label.empty() ? title_label.c_str() : e.name);
            } else {
                draw_name(e.name);
            }
            return;
        }

        const float x_offset = 15.f;

        // folder-picker mode: row 0 is the synthetic "select current folder"
        // action; draw it distinctly and skip the normal file/dir rendering.
        if (m_menu->IsFolderPicker() && i == 0) {
            DrawElement(x + x_offset, y + 5, 50, 50, ThemeEntryID_ICON_FOLDER);
            gfx::drawText(vg, x + x_offset + 65, y + (h / 2.f), 20.f,
                "Select current folder"_i18n.c_str(), nullptr,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id));
            return;
        }

        // the ".." row: no size, no read-only chip, no metadata -- it is a
        // navigation action wearing a folder icon.
        if (IsParentEntry(i)) {
            DrawElement(x + x_offset, y + 5, 50, 50, ThemeEntryID_ICON_FOLDER);
            gfx::drawText(vg, x + x_offset + 65, y + (h / 2.f), 20.f,
                "..", nullptr,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id));
            return;
        }

        if (e.IsDir()) {
            DrawElement(x + x_offset, y + 5, 50, 50, ThemeEntryID_ICON_FOLDER);
            if (m_fs_entry.type == FsType::Root && e.virtual_target_entry.type == FsType::Network) {
                float badge_x = x + x_offset + 42.f;
                float badge_y = y + 5.f + 42.f;
                float badge_r = 8.f;
                nvgBeginPath(vg);
                nvgCircle(vg, badge_x, badge_y, badge_r);
                if (e.connection_status == ConnectionStatus::Connected) {
                    nvgFillColor(vg, nvgRGBA(46, 204, 113, 255));
                } else if (e.connection_status == ConnectionStatus::Failed) {
                    nvgFillColor(vg, nvgRGBA(231, 76, 60, 255));
                } else {
                    nvgFillColor(vg, nvgRGBA(149, 165, 166, 255));
                }
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgCircle(vg, badge_x, badge_y, badge_r);
                nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_BACKGROUND));
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
            }
        } else {
            auto icon = ThemeEntryID_ICON_FILE;
            const auto ext = e.GetExtension();
            if (path::IsAnyOfIC(ext, AUDIO_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_AUDIO;
            } else if (path::IsAnyOfIC(ext, VIDEO_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_VIDEO;
            } else if (path::IsAnyOfIC(ext, IMAGE_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_IMAGE;
            } else if (path::IsAnyOfIC(ext, INSTALL_EXTENSIONS)) {
                // todo: maybe replace this icon with something else?
                icon = ThemeEntryID_ICON_NRO;
            } else if (path::IsAnyOfIC(ext, ZIP_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_ZIP;
            } else if (path::EqualsIC(ext, "nro")) {
                icon = ThemeEntryID_ICON_NRO;
            }

            DrawElement(x + x_offset, y + 5, 50, 50, icon);
        }

        // read-only marker: a small red "RO" chip on the icon corner for entries
        // that can't be written/deleted/renamed (archive contents, protected
        // system paths). Writable entries are left unmarked.
        if (IsReadOnly(GetNewPath(e))) {
            const float bw = 26.f, bh = 16.f;
            const float bx = x + x_offset + 50.f - bw;
            const float by = y + 5.f;
            gfx::drawRect(vg, bx - 1.f, by - 1.f, bw + 2.f, bh + 2.f, nvgRGBA(0, 0, 0, 255), 4.f);
            gfx::drawRect(vg, bx, by, bw, bh, theme->GetColour(ThemeEntryID_ERROR), 3.f);
            gfx::drawText(vg, bx + bw * 0.5f, by + bh * 0.5f, 13.f, nvgRGBA(255, 255, 255, 255), "RO", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }

        if (m_selected_count > 0) {
            gfx::drawCheckbox(vg, theme, x - 30.f, y + (h - gfx::CHECKBOX_SIZE) / 2.f, gfx::CHECKBOX_SIZE, e.IsSelected());
        }

        const auto name_x = x + x_offset + 65;
        const auto name_w = w - (75 + x_offset + 65 + 50);

        // a title id says nothing on its own, so the game/module name goes under
        // it as a second, smaller, dimmer line -- the id stays the row's name.
        if (const auto title_label = GetTitleLabel(e); !title_label.empty()) {
            m_scroll_name.Draw(vg, selected, name_x, y + (h / 2.f) - 3, name_w, 20, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), e.name);
            m_scroll_title_label.Draw(vg, selected, name_x, y + (h / 2.f) + 5, name_w, 16, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), title_label);
        } else {
            m_scroll_name.Draw(vg, selected, name_x, y + (h / 2.f), name_w, 20, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), e.name);
        }

        // NOTE: make this native only if i disable dir scan from above.
        if (e.IsDir()) {
            // NOTE: this takes longer than 16ms when opening a new folder due to it
            // checking all 9 folders at once.
            // Never perform this synchronous scan for a remote filesystem while
            // drawing. It blocks controller input on every newly visible row.
            if (m_fs->IsNative() && !got_dir_count && e.file_count == -1 && e.dir_count == -1) {
                got_dir_count = true;
                m_fs->DirGetEntryCount(GetNewPath(e), &e.file_count, &e.dir_count);
            }

            if (e.file_count != -1) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%zd files"_i18n.c_str(), e.file_count);
            }
            if (e.dir_count != -1) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) + 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(text_id), "%zd dirs"_i18n.c_str(), e.dir_count);
            } else if (m_fs_entry.type != FsType::Root && !m_fs->IsNative() && e.metadata_failed) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + h / 2.f, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "-" );
            } else if (m_fs_entry.type != FsType::Root && !m_fs->IsNative() && !e.metadata_loaded) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + h / 2.f, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "..." );
            }
        } else if (e.IsFile()) {
            // Remote metadata lookups can take hundreds of milliseconds. The
            // directory listing already supplies the useful size, so do not
            // stall the UI thread to fetch a timestamp while navigating.
            if (m_fs->IsNative() && !e.time_stamp.is_valid) {
                const auto path = GetNewPath(e);
                m_fs->GetFileTimeStampRaw(path, &e.time_stamp);
            }

            if (e.time_stamp.is_valid) {
                const auto t = (time_t)(e.time_stamp.modified);
                struct tm tm{};
                localtime_r(&t, &tm);
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) + 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(text_id), "%02u/%02u/%u", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
            }
            if (!m_fs->IsNative() && e.metadata_failed) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + h / 2.f, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "-" );
            } else if (!m_fs->IsNative() && !e.metadata_loaded) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + h / 2.f, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "..." );
            } else if ((double)e.file_size / 1024.0 / 1024.0 <= 0.009) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%.2f KiB", (double)e.file_size / 1024.0);
            } else {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%.2f MiB", (double)e.file_size / 1024.0 / 1024.0);
            }
        }
    });
    nvgRestore(vg);
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

void FsView::SetSide(ViewSide side) {
    m_side = side;

    const auto pos = m_menu->GetPos();
    this->SetPos(pos);
    Vec4 v{75, GetY() + 1.f + 42.f, 1220.f - 45.f * 2, 60};

    if (m_menu->IsSplitScreen()) {
        if (m_side == ViewSide::Left) {
            this->SetW(pos.w / 2 - pos.x / 2);
            this->SetX(pos.x / 2 + 20.f);
        } else if (m_side == ViewSide::Right) {
            this->SetW(pos.w / 2 - pos.x / 2);
            this->SetX(pos.x / 2 + SCREEN_WIDTH / 2);
        }

        v.w /= 2;
        v.w -= v.x / 2;

        if (m_side == ViewSide::Left) {
            v.x = v.x / 2 + 20.f;
        } else if (m_side == ViewSide::Right) {
            v.x = v.x / 2 + SCREEN_WIDTH / 2;
        }
    }

    if (m_menu->IsIconLayout()) {
        const float tile = 174.f;
        const float pad = 10.f;
        const int cols = std::max(1, static_cast<int>((GetW() - 24.f) / (tile + pad)));
        const int rows = std::max(2, static_cast<int>((GetH() - 90.f) / (tile + pad)));
        const Vec4 icon_v{GetX() + 16.f, GetY() + 52.f, tile, tile};
        m_list = std::make_unique<List>(cols, cols * rows, m_pos, icon_v, Vec2{pad, pad});
    } else {
        m_list = std::make_unique<List>(1, 8, m_pos, v);
    }
    m_list_clip = Vec4{GetX(), v.y - gfx::SELECTION_OUTLINE_PAD, GetW(),
        GetY() + GetH() - (v.y - gfx::SELECTION_OUTLINE_PAD)};
    if (m_menu->IsSplitScreen()) {
        m_list->SetPageJump(false);
    }

    // reset scroll position.
    m_scroll_name.Reset();
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
