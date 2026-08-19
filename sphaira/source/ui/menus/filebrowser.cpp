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

namespace sphaira::ui::menu::filebrowser {
using namespace detail;

auto MakeLauncherLabel(const FileAssocEntry& assoc) -> std::string;

namespace {

using RomDatabaseIndexs = std::vector<size_t>;



constinit UEvent g_change_uevent;

constexpr FsEntry FS_ENTRY_DEFAULT{
    "microSD card", "/", FsType::Sd, FsEntryFlag_Assoc,
};

constexpr FsEntry FS_ENTRIES[]{
    FS_ENTRY_DEFAULT,
    { "Image System memory", "/", FsType::ImageNand },
    { "Image microSD card", "/", FsType::ImageSd},
};

std::string MakeNetworkDeviceName(std::string_view url) {
    u32 hash = 2166136261u;
    for (const auto c : url) {
        hash ^= static_cast<u8>(c);
        hash *= 16777619u;
    }

    char name[16]{};
    std::snprintf(name, sizeof(name), "net_%08x", hash);
    return name;
}

std::string MakeNetworkRoot(std::string_view url) {
    return MakeNetworkDeviceName(url) + ":/";
}



#ifdef BUILD_SMB2
static int g_smb_ref_count = 0;

void ParseSmbUrl(const std::string& url, std::string& server, std::string& share) {
    if (url.rfind("smb://", 0) != 0) return;
    size_t host_start = 6;
    size_t slash_pos = url.find('/', host_start);
    if (slash_pos == std::string::npos) {
        server = url.substr(host_start);
        share = "";
    } else {
        server = url.substr(host_start, slash_pos - host_start);
        share = url.substr(slash_pos + 1);
    }
}

static std::string UrlEncode(const std::string& value, bool keep_slash = false) {
    constexpr char HEX[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());

    for (const char c : value) {
        const auto ch = static_cast<unsigned char>(c);
        const bool unreserved =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved || (keep_slash && ch == '/')) {
            encoded += static_cast<char>(ch);
        } else {
            encoded += '%';
            encoded += HEX[ch >> 4];
            encoded += HEX[ch & 0x0F];
        }
    }

    return encoded;
}
#endif

bool IsSameNetworkLocation(const FsEntry& lhs, const FsEntry& rhs) {
    return lhs.type == FsType::Network && rhs.type == FsType::Network &&
        lhs.url.toString() == rhs.url.toString() &&
        lhs.user.toString() == rhs.user.toString() &&
        lhs.pass.toString() == rhs.pass.toString();
}

void metadata_thread_func(void* user) {
    static_cast<FsView*>(user)->MetadataThreadFunction();
}

// is there anything in the root besides the microSD card? the root is a source
// picker, and a picker with one entry is just a keypress in the way -- so with
// nothing mounted the browser treats the card itself as the top level.
bool HasExtraRootSources() {
    return App::GetGodModeEnabled()
        || !location::GetStdio(false).empty()
        || !location::GetMtpHostDevices(false).empty()
        || !location::Load().empty();
}





} // namespace

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

namespace {

// only touched from the main thread (ui callbacks).
std::unordered_map<std::string, ConnectionStatus> g_source_status;

} // namespace

void SetSourceConnectionStatus(const std::string& url, bool connected) {
    if (url.empty()) {
        return;
    }
    g_source_status[url] = connected ? ConnectionStatus::Connected : ConnectionStatus::Failed;
}

auto GetSourceConnectionStatus(const std::string& url) -> ConnectionStatus {
    const auto it = g_source_status.find(url);
    return it == g_source_status.end() ? ConnectionStatus::Unknown : it->second;
}

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
            // action) or opening any file commits the current folder;
            // directories keep navigating so the user can drill down.
            if (m_menu->IsFolderPicker()) {
                if (m_index == 0 || GetEntry().IsFile()) {
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
                    App::Push<fileview::Menu>(m_fs.get(), GetNewPathCurrent(), fileview::TextMode::View, !IsReadOnly(GetNewPathCurrent()));
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

    nvgSave(vg);
    nvgScissor(vg, m_list_clip.x, m_list_clip.y, m_list_clip.w, m_list_clip.h);
    m_list->Draw(vg, theme, m_entries_current.size(), [this, text_col, &got_dir_count](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = GetEntry(i);

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
        } else {
            if (i != m_entries_current.size() - 1) {
                gfx::drawRect(vg, Vec4{x, y + h, w, 1.f}, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
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

    m_list = std::make_unique<List>(1, 8, m_pos, v);
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

// two launchers can ship the same core (Tico and RetroArch both bundle
// genesis_plus_gx), and the ini name alone is identical for both. tag the row
// with the folder the nro actually lives in.
auto MakeLauncherLabel(const FileAssocEntry& assoc) -> std::string {
    std::string_view path{assoc.path.s};
    if (path.starts_with('/')) {
        path.remove_prefix(1);
    }

    const auto slash = path.find('/');
    if (slash == std::string_view::npos) {
        return assoc.name;
    }

    auto root = std::string{path.substr(0, slash)};
    if (path::EqualsIC(root, "retroarch")) {
        return "RetroArch \u2014 " + assoc.name;
    }
    if (path::EqualsIC(root, "tico")) {
        return "TICO \u2014 " + assoc.name;
    }

    // /switch/<launcher>/... is the common layout, the useful name is deeper.
    if (path::EqualsIC(root, "switch")) {
        const auto rest = path.substr(slash + 1);
        const auto next = rest.find('/');
        if (next == std::string_view::npos) {
            return assoc.name;
        }
        root = std::string{rest.substr(0, next)};
    }

    return assoc.name + "  (" + root + ")";
}

// the launcher list is keyed off the folder the rom sits in, which is not
// obvious from the outside. say so rather than doing nothing.
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

void FsView::OpenArchive() {
    const auto zip_path = GetNewPathCurrent();

    // probe the archive before switching views, so a corrupt zip reports an
    // error instead of dropping the user into an empty mount.
    auto probe = std::make_unique<fs::FsZip>(zip_path);
    if (R_FAILED(probe->GetFsOpenResult())) {
        App::Push<OptionBox>("Failed to open archive!"_i18n, "OK"_i18n);
        return;
    }
    probe.reset();

    // remember where to return when the user backs out of the archive root.
    m_archive_return_entry = m_fs_entry;
    m_archive_return_path = m_path;

    FsEntry archive_entry{};
    archive_entry.name = GetEntry().name;
    archive_entry.root = zip_path;
    archive_entry.type = FsType::Archive;
    archive_entry.flags = FsEntryFlag_ReadOnly;

    SetFs("/", archive_entry);
}

void FsView::MountCurrentOverMtp() {
    // one pinned storage per target, each with a factory that recreates its fs
    // on demand (MTP restarts may recreate it) and a base path to root it at.
    std::vector<haze::PinnedMount> mounts;
    std::vector<fs::FsPath> targets;
    const auto& e = m_fs_entry;

    if (e.type == FsType::Content) {
        const auto app_id = e.content_app_id;
        const auto meta_type = e.content_meta_type;
        const auto storage_id = e.content_storage_id;
        mounts.push_back({[app_id, meta_type, storage_id]{ return std::make_unique<fs::FsNcm>(app_id, meta_type, storage_id); },
            std::string(e.name) + " (content)", {}, {}});
    } else if (e.type == FsType::Archive) {
        const fs::FsPath zip_path = e.root;
        mounts.push_back({[zip_path]{ return std::make_unique<fs::FsZip>(zip_path); },
            std::string(e.name) + " (archive)", {}, {}});
    } else if (IsSd()) {
        targets = GetMountTargets();
        for (const auto& target : targets) {
            const char* leaf = std::strrchr(target.s, '/');
            mounts.push_back({[]{ return std::make_unique<fs::FsNativeSd>(true); },
                (leaf && leaf[1]) ? (leaf + 1) : "microSD", target.toString(), {}});
        }
    } else {
        App::Notify("This source cannot be shared over MTP"_i18n);
        return;
    }

    if (sphaira::haze::MountFs(std::move(mounts))) {
        // real microSD folders become *the* mounts, so they also show up over
        // FTP and HTTP. content / archive mounts are MTP-only (the other two
        // serve the card directly and cannot read a virtual fs).
        if (IsSd()) {
            App::SetMountedFolders(targets);
        }
        App::Notify("Mounted over MTP: "_i18n + haze::GetPinnedName());
    } else {
        App::Push<OptionBox>("Failed to start MTP!"_i18n, "OK"_i18n);
    }
}

void FsView::ShareCurrentFolder() {
    // MTP can also share virtual mounts (content / archive); FTP and HTTP serve
    // real microSD folders only.
    const bool can_mtp = IsSd() || m_fs_entry.type == FsType::Content || m_fs_entry.type == FsType::Archive;
    const bool can_net = IsSd();

    // the popup used to tick its first row unconditionally, which read as
    // "already mounted over MTP" whatever was actually going on. show the state
    // instead: each transport is labelled with the folder it is currently
    // serving, and the tick only lands on a transport that really is serving
    // one (no tick at all when nothing is, or when several are).
    const auto mount_name = [](const fs::FsPath& p) -> std::string {
        if (p.empty()) {
            return {};
        }
        const char* leaf = std::strrchr(p.s, '/');
        return (leaf && leaf[1]) ? (leaf + 1) : p.toString();
    };

    const auto join = [](const auto& parts, auto to_name) -> std::string {
        std::string out;
        for (const auto& p : parts) {
            const auto name = to_name(p);
            if (name.empty()) {
                continue;
            }
            if (!out.empty()) {
                out += ", ";
            }
            out += name;
        }
        return out;
    };

    // MTP names its own pinned storages (it can also hold a content / archive
    // mount, which never becomes a global mount); FTP names its root devices.
    // HTTP has no names of its own -- it just lists the global mounts.
    const auto mounted = App::GetMountedFolders();
    const std::string mtp_on = haze::GetPinnedName();
    const std::string ftp_on = ftpsrv::IsRunning()
        ? join(ftpsrv::GetFtpMountedNames(), [](const std::string& n){ return n; })
        : std::string{};
    const std::string http_on = WebShareIsRunning() ? join(mounted, mount_name) : std::string{};

    PopupList::Items items;
    std::vector<int> actions; // 0 = MTP, 1 = FTP, 2 = HTTP.
    s64 active_index = -1;
    s64 active_count = 0;

    const auto add = [&](const char* label, int action, const std::string& serving) {
        if (!serving.empty()) {
            active_index = (s64)items.size();
            active_count++;
            items.emplace_back(std::string{label} + ": " + serving);
        } else {
            items.emplace_back(label);
        }
        actions.push_back(action);
    };

    if (can_mtp) { add("MTP", 0, mtp_on); }
    if (can_net) { add("FTP", 1, ftp_on); }
    if (can_net) { add("HTTP", 2, http_on); }

    if (items.empty()) {
        App::Notify("This source cannot be shared"_i18n);
        return;
    }

    // name the target in the title: "Mount" acts on the highlighted folder, and
    // the popup is the last chance to notice it is not the one you meant.
    const auto title = "Mount over..."_i18n + " (" + join(GetMountTargets(), mount_name) + ")";

    auto popup = std::make_unique<PopupList>(title, items, [this, actions](std::optional<s64> op_index){
        if (!op_index || *op_index < 0 || *op_index >= (s64)actions.size()) {
            return;
        }
        switch (actions[*op_index]) {
            case 0: MountCurrentOverMtp(); break;
            case 1: ShareCurrentOverFtp(); break;
            case 2: ShareFolder(); break;
        }
    }, active_count == 1 ? active_index : 0);

    if (active_count != 1) {
        popup->SetMenuStyle(true);
    }

    App::Push(std::move(popup));
}

void FsView::ShareCurrentOverFtp() {
    if (!IsSd()) {
        App::Notify("Only microSD folders can be shared over FTP"_i18n);
        return;
    }

    App::SetMountedFolders(GetMountTargets());

    if (!App::GetFtpEnable()) {
        App::SetFtpEnable(true);
    } else if (!ftpsrv::IsRunning()) {
        // the setting says on but the server is not up (it failed to start at
        // boot, say). SetFtpEnable() would see no change and do nothing, so the
        // mount would sit there with nothing serving it.
        ftpsrv::Init();
    }

    if (!ftpsrv::IsRunning()) {
        App::Push<OptionBox>("Failed to start FTP!"_i18n, "OK"_i18n);
        return;
    }

    u32 ip = 0;
    nifmGetCurrentIpAddress(&ip);
    if (ip) {
        char buf[128];
        std::string mname;
        for (const auto& n : ftpsrv::GetFtpMountedNames()) {
            if (!mname.empty()) mname += ", ";
            mname += n;
        }
        if (!mname.empty()) {
            std::snprintf(buf, sizeof(buf), "ftp://%u.%u.%u.%u:%u (%s)", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, (unsigned)App::GetFtpPort(), mname.c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "ftp://%u.%u.%u.%u:%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, (unsigned)App::GetFtpPort());
        }
        App::Notify(std::string("FTP: ") + buf);
    } else {
        App::Notify("FTP enabled (no network connection)"_i18n);
    }
}





auto FsView::Scan(const fs::FsPath& new_path, bool is_walk_up) -> Result {
    SCOPED_MUTEX(&m_metadata_io_mutex);
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    log_write("new scan path: %s\n", new_path.s);
    if (!is_walk_up && !m_path.empty() && !m_entries_current.empty()) {
        const LastFile f(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
        m_previous_highlighted_file.emplace_back(f);
    }

    m_path = new_path;
    mutexLock(&m_metadata_mutex);
    m_metadata_generation++;
    m_metadata_jobs.clear();
    m_metadata_updates.clear();
    mutexUnlock(&m_metadata_mutex);
    m_entries.clear();
    m_index = 0;
    m_list->SetYoff(0);
    m_menu->SetTitleSubHeading(m_path, true);
    m_selected_count = 0;

    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();

    if (m_fs_entry.type == FsType::Root) {
        // catches every other way into the root (startup, the sources picker):
        // with only the card to pick from there is nothing to pick, so drop
        // straight into it.
        if (!HasExtraRootSources()) {
            SetFs("/", FS_ENTRY_DEFAULT);
            R_SUCCEED();
        }

        std::vector<FsDirectoryEntry> dir_entries;

        FsDirectoryEntry sd{};
        std::strcpy(sd.name, "microSD card");
        sd.type = FsDirEntryType_Dir;
        dir_entries.push_back(sd);

        // connected usb mass storage sits directly under the sd card, so a
        // plugged in drive turns up where the user is already looking rather
        // than only in the sources sidebar. Empty when the hdd option is off.
        const auto stdio_locations = location::GetStdio(false);
        for (const auto& e : stdio_locations) {
            FsDirectoryEntry hdd{};
            std::snprintf(hdd.name, sizeof(hdd.name), "%s", e.name.c_str());
            hdd.type = FsDirEntryType_Dir;
            dir_entries.push_back(hdd);
        }

        const auto mtp_locations = location::GetMtpHostDevices(false);
        for (const auto& e : mtp_locations) {
            FsDirectoryEntry mtp_dev{};
            std::snprintf(mtp_dev.name, sizeof(mtp_dev.name), "%s", e.name.c_str());
            mtp_dev.type = FsDirEntryType_Dir;
            dir_entries.push_back(mtp_dev);
        }

        const u32 phys_devices = usbHsFsGetPhysicalDeviceCount();
        if (!mtp_locations.empty()) {
            App::Notify("MTP Host: Connected " + std::to_string(mtp_locations.size()) + " storage(s)");
        } else if (!stdio_locations.empty()) {
            App::Notify("USB Host: Mounted " + std::to_string(stdio_locations.size()) + " drive(s)");
        } else if (phys_devices > 0) {
            App::Notify("USB Host: Device detected! (No FAT32/exFAT volume mounted)");
        } else {
            App::Notify("USB Host: No physical device detected on USB port");
        }

        if (App::GetGodModeEnabled()) {
            FsDirectoryEntry nand{};
            std::strcpy(nand.name, "Image System memory");
            nand.type = FsDirEntryType_Dir;
            dir_entries.push_back(nand);

            FsDirectoryEntry sdimag{};
            std::strcpy(sdimag.name, "Image microSD card");
            sdimag.type = FsDirEntryType_Dir;
            dir_entries.push_back(sdimag);
        }

        const auto network_locations = location::Load();
        for (const auto& e : network_locations) {
            if (e.IsNfs() && !sphaira::nfs::ValidateUrl(e.url)) {
                continue;
            }

            FsDirectoryEntry net{};
            std::strcpy(net.name, e.name.c_str());
            net.type = FsDirEntryType_Dir;
            dir_entries.push_back(net);
        }

        const auto count = dir_entries.size();
        m_entries.reserve(count);
        m_entries_index.reserve(count);
        m_entries_index_hidden.reserve(count);

        u32 i = 0;
        for (const auto& e : dir_entries) {
            m_entries_index_hidden.emplace_back(i);
            m_entries_index.emplace_back(i);

            FileEntry fe{};
            std::strcpy(fe.name, e.name);
            fe.type = e.type;

            if (std::strcmp(e.name, "microSD card") == 0) {
                fe.virtual_target_entry = FS_ENTRY_DEFAULT;
            } else if (std::strcmp(e.name, "Image System memory") == 0) {
                fe.virtual_target_entry.type = FsType::ImageNand;
                std::strcpy(fe.virtual_target_entry.name, "Image System memory");
                std::strcpy(fe.virtual_target_entry.root, "/");
            } else if (std::strcmp(e.name, "Image microSD card") == 0) {
                fe.virtual_target_entry.type = FsType::ImageSd;
                std::strcpy(fe.virtual_target_entry.name, "Image microSD card");
                std::strcpy(fe.virtual_target_entry.root, "/");
            } else if (const auto hdd = std::ranges::find_if(stdio_locations,
                [&e](const auto& loc) { return loc.name == e.name; }); hdd != stdio_locations.end()) {
                fe.virtual_target_entry.type = FsType::Stdio;
                std::strcpy(fe.virtual_target_entry.name, hdd->name.c_str());
                std::strcpy(fe.virtual_target_entry.root, hdd->mount.c_str());
                fe.virtual_target_entry.flags = hdd->flags;
            } else if (const auto mtp = std::ranges::find_if(mtp_locations,
                [&e](const auto& loc) { return loc.name == e.name; }); mtp != mtp_locations.end()) {
                fe.virtual_target_entry.type = FsType::Stdio;
                std::strcpy(fe.virtual_target_entry.name, mtp->name.c_str());
                std::strcpy(fe.virtual_target_entry.root, mtp->mount.c_str());
                fe.virtual_target_entry.flags = mtp->flags;
            } else {
                for (const auto& loc : network_locations) {
                    if (loc.name == e.name) {
                        const auto root_p = loc.IsSmb() ? std::string{"smb2:/"} : MakeNetworkRoot(loc.url);
                        fe.virtual_target_entry.type = FsType::Network;
                        std::strncpy(fe.virtual_target_entry.name, loc.name.c_str(), sizeof(fe.virtual_target_entry.name) - 1);
                        fe.virtual_target_entry.name[sizeof(fe.virtual_target_entry.name) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.root, root_p.c_str(), sizeof(fe.virtual_target_entry.root) - 1);
                        fe.virtual_target_entry.root[sizeof(fe.virtual_target_entry.root) - 1] = '\0';
                        fe.virtual_target_entry.flags = loc.IsNfs() ? FsEntryFlag_ReadOnly : FsEntryFlag_None;
                        std::strncpy(fe.virtual_target_entry.url, loc.url.c_str(), sizeof(fe.virtual_target_entry.url) - 1);
                        fe.virtual_target_entry.url[sizeof(fe.virtual_target_entry.url) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.protocol, loc.protocol.c_str(), sizeof(fe.virtual_target_entry.protocol) - 1);
                        fe.virtual_target_entry.protocol[sizeof(fe.virtual_target_entry.protocol) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.user, loc.user.c_str(), sizeof(fe.virtual_target_entry.user) - 1);
                        fe.virtual_target_entry.user[sizeof(fe.virtual_target_entry.user) - 1] = '\0';
                        std::strncpy(fe.virtual_target_entry.pass, loc.pass.c_str(), sizeof(fe.virtual_target_entry.pass) - 1);
                        fe.virtual_target_entry.pass[sizeof(fe.virtual_target_entry.pass) - 1] = '\0';
                        fe.virtual_target_entry.port = loc.port;

                        // A registered/mounted devoptab does not prove that the
                        // remote server is currently reachable. Show the result
                        // of the last real probe this session, if any.
                        fe.connection_status = GetSourceConnectionStatus(loc.url);
                        break;
                    }
                }
            }

            m_entries.emplace_back(fe);
            i++;
        }
    } else {
        fs::Dir d;
        R_TRY(m_fs->OpenDirectory(new_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));

        std::vector<FsDirectoryEntry> dir_entries;
        R_TRY(d.ReadAll(dir_entries));

        const auto count = dir_entries.size();
        m_entries.reserve(count);
        m_entries_index.reserve(count);
        m_entries_index_hidden.reserve(count);

        u32 i = 0;
        for (const auto& e : dir_entries) {
            m_entries_index_hidden.emplace_back(i);
            if ('.' != e.name[0]) {
                m_entries_index.emplace_back(i);
            }

            FileEntry fe{};
            std::strcpy(fe.name, e.name);
            fe.type = e.type;
            fe.file_size = e.file_size;
            // a mount that opts out of stat never gets a second pass, so the
            // listing itself is all the metadata there will ever be.
            const auto no_stat = e.type == FsDirEntryType_Dir ? m_fs_entry.NoStatDir() : m_fs_entry.NoStatFile();
            fe.metadata_loaded = m_fs->IsNative() || no_stat || e.file_size > 0;

            m_entries.emplace_back(fe);
            i++;
        }
    }

    // a ".." row, so "up" is something the user can see and point at rather
    // than a button they have to know about. it doubles as the way to say "the
    // folder I am standing in" to Mount. not at the top of the filesystem,
    // where there is nothing above to go to.
    m_has_parent_entry = false;
    if (!m_menu->IsFolderPicker() && m_fs_entry.type != FsType::Root && m_path != m_fs->Root()) {
        FileEntry up{};
        std::strcpy(up.name, "..");
        up.type = FsDirEntryType_Dir;
        up.metadata_loaded = true;
        up.file_count = 0;
        up.dir_count = 0;
        m_parent_entry_index = static_cast<u32>(m_entries.size());
        m_entries.emplace_back(up);
        m_has_parent_entry = true;
    }

    // folder-picker mode: add a synthetic entry that Sort() pins to the top of
    // the listing so every folder offers "select current folder" as row 0.
    if (m_menu->IsFolderPicker()) {
        FileEntry synth{};
        synth.type = FsDirEntryType_Dir;
        synth.metadata_loaded = true;
        synth.file_count = 0;
        synth.dir_count = 0;
        m_picker_entry_index = static_cast<u32>(m_entries.size());
        m_entries.emplace_back(synth);
    }

    Sort();
    LoadTitleLabels();

    // quick check to see if this is an update folder (never in picker mode).
    m_is_update_folder = !m_menu->IsFolderPicker() && R_SUCCEEDED(CheckIfUpdateFolder());

    // start on the first real row, not on ".." -- landing on "go back up" every
    // time you open a folder makes A into a no-op you have to steer around.
    SetIndex(m_has_parent_entry && m_entries_current.size() > 1 ? 1 : 0);
    QueueRemoteMetadata();

    // find previous entry
    if (is_walk_up && !m_previous_highlighted_file.empty()) {
        ON_SCOPE_EXIT(m_previous_highlighted_file.pop_back());
        SetIndexFromLastFile(m_previous_highlighted_file.back());
    }

    R_SUCCEED();
}

void FsView::LoadTitleLabels() {
    std::string_view path{m_path.s};
    if (path.ends_with('/')) {
        path.remove_suffix(1);
    }

    if (!IsSd() || !path::EqualsIC(path, "/atmosphere/contents")) {
        return;
    }

    for (auto& e : m_entries) {
        const auto id = e.IsDir() ? path::ParseTitleIdName(e.name) : 0;
        if (!id) {
            continue;
        }

        // sysmodules name themselves; games need the control nacp, which is slow
        // enough to read that it happens on the title:: thread instead.
        e.title_label = hats::GetModuleName(id);
        if (e.title_label.empty()) {
            if (!m_title_service) {
                m_title_service = R_SUCCEEDED(title::Init());
            }
            if (m_title_service) {
                title::PushAsync(id);
                e.title_id = id;
            }
        }
    }
}

auto FsView::GetTitleLabel(FileEntry& e) -> std::string {
    if (e.title_id) {
        if (auto data = title::GetAsync(e.title_id); data && data->status != title::NacpLoadStatus::Progress) {
            if (data->status == title::NacpLoadStatus::Loaded && data->lang.name[0] && !title::IsPlaceholderName(data->lang.name)) {
                e.title_label = data->lang.name;
            }
            e.title_id = 0; // resolved, or never will be: stop polling
        }
    }

    return e.title_label;
}

void FsView::QueueRemoteMetadata() {
    if (m_metadata_paused || m_fs->IsNative() || m_fs_entry.type == FsType::Root || !m_metadata_thread_created) {
        return;
    }

    // position of each entry in the sorted view, entries hidden from the
    // current view are fetched last.
    std::vector<s64> view_position(m_entries.size(), -1);
    for (size_t i = 0; i < m_entries_current.size(); i++) {
        view_position[m_entries_current[i]] = static_cast<s64>(i);
    }

    mutexLock(&m_metadata_mutex);
    for (size_t i = 0; i < m_entries.size(); i++) {
        // never queue metadata for a synthetic row -- there is no such path.
        if (m_menu->IsFolderPicker() && i == m_picker_entry_index) {
            continue;
        }
        if (m_has_parent_entry && i == m_parent_entry_index) {
            continue;
        }
        auto& entry = m_entries[i];
        // mounts that charge a round trip per stat (MTP) opt out entirely, so
        // browsing a folder costs one listing rather than one per row.
        if (entry.IsDir() ? m_fs_entry.NoStatDir() : m_fs_entry.NoStatFile()) {
            entry.metadata_loaded = true;
            continue;
        }
        const auto wanted = entry.IsDir() || (entry.IsFile() && !entry.metadata_loaded);
        if (!wanted) {
            continue;
        }
        const auto pos = view_position[i];
        m_metadata_jobs.push_back(MetadataJob{
            .generation = m_metadata_generation,
            .entry_index = i,
            .view_index = pos >= 0 ? pos : static_cast<s64>(100000 + i),
            .path = GetNewPath(entry),
            .is_dir = entry.IsDir(),
        });
    }
    m_metadata_focus = m_index;
    condvarWakeOne(&m_metadata_cond);
    mutexUnlock(&m_metadata_mutex);
}

void FsView::PauseRemoteMetadata() {
    if (m_fs->IsNative() || !m_metadata_thread_created) {
        return;
    }

    m_metadata_paused = true;
    mutexLock(&m_metadata_mutex);
    m_metadata_generation++;
    m_metadata_jobs.clear();
    m_metadata_updates.clear();
    mutexUnlock(&m_metadata_mutex);

    // Wait for at most the one request which was already in flight. Once this
    // lock is acquired, the worker has no queued work left and the installer
    // can use the remote filesystem without competing metadata requests.
    mutexLock(&m_metadata_io_mutex);
    mutexUnlock(&m_metadata_io_mutex);
}

void FsView::MetadataThreadFunction() {
    for (;;) {
        mutexLock(&m_metadata_mutex);
        while (m_metadata_jobs.empty() && !m_metadata_thread_exit) {
            condvarWait(&m_metadata_cond, &m_metadata_mutex);
        }
        if (m_metadata_thread_exit) {
            mutexUnlock(&m_metadata_mutex);
            return;
        }
        // fetch whatever is nearest the cursor first: the visible screen,
        // then one screen above/below, then the rest. Directory counts are
        // slower, so within the same area file sizes win.
        size_t best = 0;
        s64 best_score = std::numeric_limits<s64>::max();
        for (size_t i = 0; i < m_metadata_jobs.size(); i++) {
            const auto& j = m_metadata_jobs[i];
            const auto score = std::abs(j.view_index - m_metadata_focus) + (j.is_dir ? 24 : 0);
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
        auto job = std::move(m_metadata_jobs[best]);
        m_metadata_jobs[best] = std::move(m_metadata_jobs.back());
        m_metadata_jobs.pop_back();
        mutexUnlock(&m_metadata_mutex);

        MetadataUpdate update{
            .generation = job.generation,
            .entry_index = job.entry_index,
        };
        Result rc{};
        mutexLock(&m_metadata_io_mutex);
        if (job.is_dir) {
            rc = m_fs->DirGetEntryCount(job.path, &update.file_count, &update.dir_count);
        } else {
            rc = m_fs->FileGetSizeAndTimestamp(job.path, &update.timestamp, &update.file_size);
        }
        mutexUnlock(&m_metadata_io_mutex);
        update.success = R_SUCCEEDED(rc);

        mutexLock(&m_metadata_mutex);
        if (job.generation == m_metadata_generation) {
            m_metadata_updates.emplace_back(std::move(update));
        }
        mutexUnlock(&m_metadata_mutex);
    }
}

void FsView::ApplyRemoteMetadata() {
    std::vector<MetadataUpdate> updates;
    mutexLock(&m_metadata_mutex);
    std::swap(updates, m_metadata_updates);
    mutexUnlock(&m_metadata_mutex);

    bool selected_size_changed{};
    for (const auto& update : updates) {
        if (update.generation != m_metadata_generation || update.entry_index >= m_entries.size()) {
            continue;
        }
        auto& entry = m_entries[update.entry_index];
        selected_size_changed |= entry.selected && entry.IsFile();
        entry.metadata_loaded = true;
        entry.metadata_failed = !update.success;
        if (!update.success) {
            continue;
        }
        if (entry.IsDir()) {
            entry.file_count = update.file_count;
            entry.dir_count = update.dir_count;
        } else {
            entry.file_size = update.file_size;
            entry.time_stamp = update.timestamp;
        }
    }
    if (selected_size_changed) {
        m_menu->UpdateSubheading();
    }
}

// one level up, from B or from the ".." row. leaving the top of a filesystem
// unmounts an archive, drops to the source list when there is more than the
// card to pick from, and otherwise closes the browser.
void FsView::WalkUp() {
    std::string_view view{m_path};
    if (m_fs_entry.type != FsType::Root && view != m_fs->Root()) {
        const auto end = view.find_last_of('/');
        assert(end != view.npos);

        if (end == 0) {
            Scan(m_fs->Root(), true);
        } else {
            Scan(view.substr(0, end), true);
        }
    } else if (m_fs_entry.type == FsType::Archive) {
        // at the archive root: unmount and return to the opening view.
        SetFs(m_archive_return_path, m_archive_return_entry);
    } else if (m_fs_entry.type != FsType::Root && HasExtraRootSources()) {
        FsEntry root_entry{
            .name = "System Root",
            .root = "root:/",
            .type = FsType::Root
        };
        SetFs("root:/", root_entry);
    } else if (!m_menu->IsTab()) {
        m_menu->PromptIfShouldExit();
    }
}

void FsView::Sort() {
    // returns true if lhs should be before rhs
    const auto sort = m_menu->m_sort.Get();
    const auto order = m_menu->m_order.Get();
    const auto folders_first = m_menu->m_folders_first.Get();
    const auto hidden_last = m_menu->m_hidden_last.Get();

    const auto sorter = [this, sort, order, folders_first, hidden_last](u32 _lhs, u32 _rhs) -> bool {
        const auto& lhs = m_entries[_lhs];
        const auto& rhs = m_entries[_rhs];

        if (hidden_last) {
            if (lhs.IsHidden() && !rhs.IsHidden()) {
                return false;
            } else if (!lhs.IsHidden() && rhs.IsHidden()) {
                return true;
            }
        }

        if (folders_first) {
            if (lhs.type == FsDirEntryType_Dir && !(rhs.type == FsDirEntryType_Dir)) { // left is folder
                return true;
            } else if (!(lhs.type == FsDirEntryType_Dir) && rhs.type == FsDirEntryType_Dir) { // right is folder
                return false;
            }
        }

        switch (sort) {
            case SortType_Size: {
                if (lhs.file_size == rhs.file_size) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else if (order == OrderType_Descending) {
                    return lhs.file_size > rhs.file_size;
                } else {
                    return lhs.file_size < rhs.file_size;
                }
            } break;
            case SortType_Alphabetical: {
                if (order == OrderType_Descending) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) > 0;
                }
            } break;
        }

        std::unreachable();
    };

    if (m_menu->m_show_hidden.Get()) {
        m_entries_current = m_entries_index_hidden;
    } else {
        m_entries_current = m_entries_index;
    }

    std::sort(m_entries_current.begin(), m_entries_current.end(), sorter);

    // prepend the pinned synthetic row so it is always first, whatever the
    // sort order: "select current folder" in picker mode, ".." otherwise.
    std::optional<u32> pinned;
    if (m_menu->IsFolderPicker() && m_picker_entry_index < m_entries.size()) {
        pinned = m_picker_entry_index;
    } else if (m_has_parent_entry && m_parent_entry_index < m_entries.size()) {
        pinned = m_parent_entry_index;
    }

    if (pinned) {
        m_pinned_view.assign(1, *pinned);
        m_pinned_view.insert(m_pinned_view.end(), m_entries_current.begin(), m_entries_current.end());
        m_entries_current = m_pinned_view;
    }
}

void FsView::SortAndFindLastFile(bool scan) {
    std::optional<LastFile> last_file;
    if (!m_path.empty() && !m_entries_current.empty()) {
        last_file = LastFile(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
    }

    if (scan) {
        Scan(m_path);
    } else {
        Sort();
    }

    if (last_file.has_value()) {
        SetIndexFromLastFile(*last_file);
    }
}

void FsView::SetIndexFromLastFile(const LastFile& last_file) {
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        if (last_file.name == GetEntry(i).name) {
            index = i;
            break;
        }
    }
    if (index >= 0) {
        if (index == last_file.index && m_entries_current.size() == last_file.entries_count) {
            m_list->SetYoff(last_file.offset);
            log_write("index is the same as last time\n");
        } else {
            // file position changed!
            log_write("file position changed\n");
            // guesstimate where the position is
            if (index >= 8) {
                m_list->SetYoff(((index - 8) + 1) * m_list->GetMaxY());
            } else {
                m_list->SetYoff(0);
            }
        }
        SetIndex(index);
    }
}

void FsView::SetFs(const fs::FsPath& new_path, const FsEntry& new_entry) {
    if (m_fs && m_fs_entry.root == new_entry.root && m_fs_entry.type == new_entry.type) {
        if (new_entry.type != FsType::Network || IsSameNetworkLocation(m_fs_entry, new_entry)) {
            log_write("same fs, ignoring\n");
            return;
        }
    }

    mutexLock(&m_metadata_io_mutex);

#ifdef BUILD_SMB2
    if (m_fs_entry.type == FsType::Network) {
        g_smb_ref_count--;
        if (g_smb_ref_count <= 0 && g_smb2fs) {
            delete g_smb2fs;
            g_smb2fs = nullptr;
            g_smb_ref_count = 0;
        }
    }
    if (new_entry.type == FsType::Network) {
        g_smb_ref_count++;
    }
#endif

    // m_fs.reset();
    m_path = new_path;
    m_entries.clear();
    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();
    m_entries_current = {};
    m_previous_highlighted_file.clear();
    // keep a copy that owns its own source fs (an archive copy) so it can be
    // pasted after leaving the archive; otherwise clear the pending selection.
    if (!m_menu->m_selected.HasOwnedFs()) {
        m_menu->m_selected.Reset();
    }
    m_selected_count = 0;
    m_fs_entry = new_entry;

    switch (new_entry.type) {
         case FsType::Sd:
            m_fs = std::make_unique<fs::FsNativeSd>(m_menu->m_ignore_read_only.Get());
            break;
        case FsType::ImageNand:
            m_fs = std::make_unique<fs::FsNativeImage>(FsImageDirectoryId_Nand);
            break;
        case FsType::ImageSd:
            m_fs = std::make_unique<fs::FsNativeImage>(FsImageDirectoryId_Sd);
            break;
        case FsType::Stdio:
            m_fs = std::make_unique<fs::FsStdio>(true, new_entry.root);
            break;
        case FsType::Network:
            m_fs = std::make_unique<fs::FsStdio>(true, new_entry.root);
            break;
        case FsType::Root:
            m_fs = std::make_unique<fs::FsStdio>(true, "root:/");
            break;
        case FsType::Archive:
            // new_entry.root holds the absolute path of the .zip to mount.
            m_fs = std::make_unique<fs::FsZip>(new_entry.root);
            break;
        case FsType::Content:
            m_fs = std::make_unique<fs::FsNcm>(new_entry.content_app_id, new_entry.content_meta_type, new_entry.content_storage_id);
            break;
    }

    m_path = new_path.empty() ? m_fs->Root() : new_path;
    mutexUnlock(&m_metadata_io_mutex);

    if (HasFocus()) {
        const auto rc = Scan(m_path);
        // a mounted network share or usb/mtp device can still fail to list
        // (server rejected the request, phone dropped the link). Without this,
        // the user is left staring at a green "Empty..." screen.
        if (R_FAILED(rc) && (m_fs_entry.type == FsType::Network || m_fs_entry.type == FsType::Stdio)) {
            log_write("[FILEBROWSER] listing failed: 0x%X\n", rc);
            if (m_fs_entry.type == FsType::Network) {
                App::Push<OptionBox>("Failed to list network storage!"_i18n + "\n" +
                    "The server is reachable but the listing failed. Check the credentials and the shared folder path."_i18n, "OK"_i18n);
            } else {
                App::PushErrorBox(rc, "Failed to list storage!"_i18n);
            }
            const FsEntry root_entry{
                .name = "System Root",
                .root = "root:/",
                .type = FsType::Root
            };
            SetFs("root:/", root_entry);
        }
    }
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

void FsView::DisplayOptions() {
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
                        App::Push<OptionBox>("Connection test failed!"_i18n, "OK"_i18n);
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
    }

    if (m_entries_current.size() && !m_selected_count && GetEntry().IsFile()) {
        const auto new_path = GetNewPathCurrent();
        const auto name = std::string_view{GetEntry().name};
        if (name.ends_with(".disa") || name.ends_with(".bin") || name.size() == 16 || save::IsDisaSaveFile(m_fs.get(), new_path)) {
            options->Add<SidebarEntryCallback>("Restore save data"_i18n, [this](){
                RestoreSaveFile(GetEntry());
            }, "Restore this save data file to the console."_i18n);
        }
    }

    if (IsSd() && m_entries_current.size() && !m_selected_count) {
        if (GetEntry().IsFile() && (path::EqualsIC(GetEntry().GetExtension(), "nro") || !m_menu->FindFileAssocFor().empty())) {
            auto entry = options->Add<SidebarEntryCallback>("Install Forwarder"_i18n, [this](){;
                InstallForwarder();
            }, "Install a forwarder shortcut for this file."_i18n);
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);
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
            const std::string buf = "Paste file(s)?"_i18n;
            App::Push<OptionBox>(
                buf, "No"_i18n, "Yes"_i18n, 0, [this](auto op_index){
                if (op_index && *op_index) {
                    App::PopToMenu();
                    OnPasteCallback();
                }
            });
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

        SidebarEntryArray::Items sort_items;
        sort_items.push_back("Size"_i18n);
        sort_items.push_back("Alphabetical"_i18n);

        SidebarEntryArray::Items order_items;
        order_items.push_back("Descending"_i18n);
        order_items.push_back("Ascending"_i18n);

        options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
            m_menu->m_sort.Set(index_out);
            SortAndFindLastFile();
        }, m_menu->m_sort.Get(), "Select which field to sort files and folders by."_i18n);

        options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
            m_menu->m_order.Set(index_out);
            SortAndFindLastFile();
        }, m_menu->m_order.Get(), "Sort entries from largest to smallest or A to Z."_i18n);

        options->Add<SidebarEntryBool>("Show Hidden"_i18n, m_menu->m_show_hidden.Get(), [this](bool& v_out){
            m_menu->m_show_hidden.Set(v_out);
            SortAndFindLastFile();
        }, "Show files and folders that start with a dot (hidden)."_i18n);

        options->Add<SidebarEntryBool>("Folders First"_i18n, m_menu->m_folders_first.Get(), [this](bool& v_out){
            m_menu->m_folders_first.Set(v_out);
            SortAndFindLastFile();
        }, "Place folders before files in the listing."_i18n);

        options->Add<SidebarEntryBool>("Hidden Last"_i18n, m_menu->m_hidden_last.Get(), [this](bool& v_out){
            m_menu->m_hidden_last.Set(v_out);
            SortAndFindLastFile();
        }, "Push hidden entries to the bottom of the listing."_i18n);
    }, "Change display order and visibility settings for files."_i18n);
    view_entry->SetHasSubmenu(true);

    if (m_fs_entry.type == FsType::Archive && m_entries_current.size()) {
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
        if (check_all_ext(ZIP_EXTENSIONS)) {
            auto extract_entry = options->Add<SidebarEntryCallback>("Extract zip"_i18n, [this](){
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
            }, "Extract the contents of the selected ZIP archive."_i18n);
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
        if (IsSd() && path::IsAnyOfIC(GetEntry().GetExtension(), IMAGE_EXTENSIONS)) {
            options->Add<SidebarEntryCallback>("View Image"_i18n, [this](){
                OpenImageViewer();
            }, "Open the selected image in the built-in viewer."_i18n);
            auto theme_entry = options->Add<SidebarEntryCallback>("Create Switch Theme"_i18n, [this](){
                App::Push<theme_creator::Menu>(GetNewPathCurrent());
            }, "Use the selected image to create a custom Switch theme."_i18n);
            theme_entry->SetHasSubmenu(true);
        }

        options->Add<SidebarEntryCallback>("View as text"_i18n, [this](){
            App::Push<fileview::Menu>(m_fs.get(), GetNewPathCurrent(), fileview::TextMode::View, !IsReadOnly(GetNewPathCurrent()));
        }, "Open the selected file in read-only text view mode."_i18n);

        if (text_helper::IsTextFile(GetEntry().GetName())) {
            auto edit_entry = options->Add<SidebarEntryCallback>("Edit"_i18n, [this](){
                App::Push<fileview::Menu>(m_fs.get(), GetNewPathCurrent(), fileview::TextMode::Edit, true);
            }, "Open the selected file in text editor mode."_i18n);
            edit_entry->Depends([this](){
                return !IsReadOnly(GetNewPathCurrent()) && GetEntry().file_size <= 4 * 1024 * 1024;
            }, IsReadOnly(GetNewPathCurrent()) ? "File is read-only"_i18n : "File is too large to edit"_i18n);
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

void FsView::ConnectToLocation(const FsEntry& target_entry) {
    const auto target_url_str = target_entry.url.toString();
    const auto target_proto_str = target_entry.protocol.toString();
    std::string proto = (target_url_str.starts_with("smb://") || target_proto_str == "smb") ? "SMB" :
                        ((target_url_str.starts_with("nfs://") || target_proto_str == "nfs") ? "NFS" : "Network Storage");
    std::string msg = "Connecting to " + proto + "...";

    App::Push<ProgressBox>(0, msg, target_entry.name, [this, target_entry](auto pbox) -> Result {
        const auto url_str = target_entry.url.toString();
        const auto proto_str = target_entry.protocol.toString();
        if (url_str.starts_with("smb://") || proto_str == "smb") {
#ifdef BUILD_SMB2
            if (g_smb2fs) {
                if (g_smb2fs->GetConnectUrl() == url_str) {
                    R_SUCCEED();
                }
                delete g_smb2fs;
                g_smb2fs = nullptr;
            }
            std::string server, share;
            ParseSmbUrl(url_str, server, share);
            g_smb2fs = new CSMB2FS(server, target_entry.user.toString(), target_entry.pass.toString(), share, "smb2", "smb2");
            if (g_smb2fs->RegisterFilesystem_v2()) {
                R_SUCCEED();
            } else {
                delete g_smb2fs;
                g_smb2fs = nullptr;
                R_THROW(Result_SmbConnectionFailed);
            }
#else
            R_THROW(Result_SmbNotSupported);
#endif
        } else if (url_str.starts_with("nfs://") || proto_str == "nfs") {
            if (!sphaira::nfs::ValidateUrl(url_str)) {
                log_write("[FILEBROWSER] invalid NFS URL format\n");
                R_THROW(0xCCCC);
            }

            if (devoptab::common::IsNetworkDeviceMounted(url_str)) {
                R_SUCCEED();
            }

            sphaira::devoptab::common::MountConfig config{
                .name = target_entry.name.toString(),
                .url = url_str,
                .user = "",
                .pass = "",
                .port = target_entry.port,
                .read_only = true
            };

            const auto dev_name = MakeNetworkDeviceName(url_str);
            const auto mount_name = MakeNetworkRoot(url_str);

            if (sphaira::devoptab::nfs::Mount(config, dev_name.c_str(), mount_name.c_str())) {
                R_SUCCEED();
            } else {
                R_THROW(0xCCCC);
            }
        } else {
            curl::Api api{
                curl::Url{url_str},
                curl::UserPass{target_entry.user.toString(), target_entry.pass.toString()},
                curl::Port{target_entry.port},
            };
            auto probe_type = curl::ProbeType::Http;
            if (proto_str == "webdav" ||
                url_str.starts_with("webdav://") || url_str.starts_with("webdavs://")) {
                probe_type = curl::ProbeType::Webdav;
            } else if (url_str.starts_with("ftp://") || url_str.starts_with("ftps://")) {
                probe_type = curl::ProbeType::Ftp;
            }

            const auto probe = curl::Probe(api, probe_type);
            log_write("[FILEBROWSER] source probe: success=%d code=%ld url=%s\n", probe.success, probe.code, target_entry.url.s);
            if (!probe.success) {
                R_THROW(0xCCCC);
            }

            if (devoptab::common::IsNetworkDeviceMounted(url_str)) {
                R_SUCCEED();
            }

            sphaira::devoptab::common::MountConfig config{
                .name = target_entry.name.toString(),
                .url = url_str,
                .user = target_entry.user.toString(),
                .pass = target_entry.pass.toString(),
                .port = target_entry.port,
                .read_only = target_entry.IsReadOnly()
            };
            auto device = std::make_unique<sphaira::devoptab::common::MountCurlDevice>(config);
            const auto dev_name = MakeNetworkDeviceName(url_str);
            const auto mount_name = MakeNetworkRoot(url_str);

            if (sphaira::devoptab::common::MountNetworkDevice2(std::move(device), config, sizeof(sphaira::devoptab::common::CurlFileState), sizeof(sphaira::devoptab::common::CurlDirState), dev_name.c_str(), mount_name.c_str())) {
                R_SUCCEED();
            } else {
                R_THROW(0xCCCC);
            }
        }
        R_SUCCEED();
    }, [this, target_entry](Result rc) {
        SetSourceConnectionStatus(target_entry.url.toString(), R_SUCCEEDED(rc));

        // reflect the outcome on the root view badges, if we are still there.
        if (m_fs_entry.type == FsType::Root) {
            for (auto& e : m_entries) {
                if (e.virtual_target_entry.type == FsType::Network && IsSameNetworkLocation(e.virtual_target_entry, target_entry)) {
                    e.connection_status = R_FAILED(rc) ? ConnectionStatus::Failed : ConnectionStatus::Connected;
                }
            }
        }

        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Failed to connect to network storage!"_i18n + "\n" +
                "Check that the server is powered on and reachable on your network."_i18n, "OK"_i18n);
        } else {
            SetFs(target_entry.root, target_entry);
        }
    });
}

void FsView::ShowSourcePicker() {
    auto options = std::make_unique<Sidebar>("Sources"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items mount_items;
    std::vector<FsEntry> fs_entries;

    const auto stdio_locations = location::GetStdio(false);
    for (const auto& e: stdio_locations) {
        u32 flags{};
        if (e.flags & FsEntryFlag_ReadOnly) {
            flags |= FsEntryFlag_ReadOnly;
        }

        fs_entries.emplace_back(e.name, e.mount, FsType::Stdio, flags);
        mount_items.push_back(e.name);
    }

    const auto mtp_locations = location::GetMtpHostDevices(false);
    for (const auto& e: mtp_locations) {
        fs_entries.emplace_back(e.name, e.mount, FsType::Stdio, e.flags);
        mount_items.push_back(e.name);
    }

    for (const auto& e: FS_ENTRIES) {
        fs_entries.emplace_back(e);
        mount_items.push_back(i18n::get(e.name));
    }

    const auto network_locations = location::Load();
    for (const auto& e: network_locations) {
        if (e.IsNfs() && !sphaira::nfs::ValidateUrl(e.url)) {
            continue;
        }

        FsEntry entry{
            .name = e.name,
            .root = e.IsSmb() ? "smb2:/" : MakeNetworkRoot(e.url),
            .type = FsType::Network,
            .flags = e.IsNfs() ? FsEntryFlag_ReadOnly : FsEntryFlag_None,
            .url = e.url,
            .protocol = e.protocol,
            .user = e.user,
            .pass = e.pass,
            .port = e.port
        };
        fs_entries.emplace_back(entry);

        std::string proto = e.protocol;
        if (proto.empty()) {
            if (e.IsSmb()) proto = "smb";
            else if (e.IsNfs()) proto = "nfs";
            else if (e.url.starts_with("ftp://") || e.url.starts_with("ftps://")) proto = "ftp";
            else if (e.url.starts_with("http://") || e.url.starts_with("https://")) proto = "http";
            else proto = "webdav";
        }
        std::string proto_upper = proto;
        std::transform(proto_upper.begin(), proto_upper.end(), proto_upper.begin(), ::toupper);

        mount_items.push_back(e.name + " (" + proto_upper + ")");
    }

    s64 current_index = 0;
    for (size_t i = 0; i < fs_entries.size(); ++i) {
        bool is_current = false;
        if (m_fs_entry.type == fs_entries[i].type && m_fs_entry.root == fs_entries[i].root) {
            if (m_fs_entry.type != FsType::Network || IsSameNetworkLocation(m_fs_entry, fs_entries[i])) {
                is_current = true;
            }
        }
        if (is_current) {
            mount_items[i] = "-> " + mount_items[i];
            current_index = i;
        }
    }

    options->Add<SidebarEntryArray>("Mount"_i18n, mount_items, [this, fs_entries](s64& index_out){
        App::PopToMenu();
        const auto& target_entry = fs_entries[index_out];
        if (target_entry.type == FsType::Network) {
            FsView* other_view = (this == m_menu->view_left.get()) ? m_menu->view_right.get() : m_menu->view_left.get();
            if (other_view && other_view->m_fs_entry.type == FsType::Network && !IsSameNetworkLocation(other_view->m_fs_entry, target_entry)) {
                other_view->SetFs("/", FS_ENTRY_DEFAULT);
            }
            if (m_fs_entry.type == FsType::Network && !IsSameNetworkLocation(m_fs_entry, target_entry)) {
                SetFs("/", FS_ENTRY_DEFAULT);
            }

            ConnectToLocation(target_entry);
        } else {
            SetFs(target_entry.root, target_entry);
        }
    }, current_index, "Switch the file source to a different storage or mount point."_i18n);

    options->Add<SidebarEntryCallback>("Mount USB drive"_i18n, [this](){
        MountUsbStorage();
    }, "Bring up a connected USB drive and open it."_i18n);

    options->Add<SidebarEntryCallback>("Add network location"_i18n, [this](){
        AddNetworkLocationInteractive([this](){
            ShowSourcePicker();
        });
    }, "Configure a new network location (supported protocols: SMB, NFS, WebDAV, FTP, HTTP)."_i18n);
}

void FsView::MountUsbStorage() {
    if (!App::GetHddEnable()) {
        App::Push<OptionBox>("USB storage is turned off in Settings, under Sources."_i18n, "OK"_i18n);
        return;
    }

    // mtp and usb host storage cannot both own the usb port, so at boot
    // usbhsfs is skipped entirely when mtp is on. Say so instead of reporting
    // "no drive found", which would send the user looking at the cable.
    if (haze::IsRunning()) {
        haze::Exit();
    }

    usbHsFsSetFileSystemMountFlags(App::GetWriteProtect() ? UsbHsFsMountFlags_ReadOnly : 0);
    // a no-op when the stack is already up; this is the path that recovers the
    // case where it was never started at boot.
    usbHsFsInitialize(1);

    const auto devices = location::GetStdio(false);
    if (devices.empty()) {
        App::Push<OptionBox>("No USB drive found.\nCheck that it has power and is formatted as FAT32, exFAT or NTFS."_i18n, "OK"_i18n);
        return;
    }

    // open the drive straight away: mounting it was the point.
    const auto& e = devices.front();
    FsEntry entry{};
    std::strcpy(entry.name, e.name.c_str());
    std::strcpy(entry.root, e.mount.c_str());
    entry.type = FsType::Stdio;
    entry.flags = e.flags;

    App::PopToMenu();
    App::Notify("Mounted"_i18n + ": " + e.name);
    SetFs(entry.root, entry);
}

Menu::Menu(u32 flags, const ::sphaira::location::Entry* launch_location) : MenuBase{"FileBrowser"_i18n, flags} {
    SetAction(Button::START, Action{"Options"_i18n, [this](){
        if (IsFolderPicker()) {
            ConfirmFolderPick(view->m_path);
            return;
        }
        if (App::GetApp()->m_controller.GotHeld(Button::R2)) {
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
        ConnectToLocation(*launch_location);
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

void Menu::ConnectToLocation(const ::sphaira::location::Entry& e) {
    if (e.IsNfs() && !sphaira::nfs::ValidateUrl(e.url)) {
        log_write("[FILEBROWSER] ConnectToLocation rejected invalid NFS URL\n");
        App::Push<OptionBox>("Failed to connect to network storage!"_i18n + "\n" +
            "Check that the server is powered on and reachable on your network."_i18n, "OK"_i18n);
        return;
    }

    const auto root_p = e.IsSmb() ? std::string{"smb2:/"} : MakeNetworkRoot(e.url);
    FsEntry target_entry{};
    std::strncpy(target_entry.name, e.name.c_str(), sizeof(target_entry.name) - 1);
    target_entry.name[sizeof(target_entry.name) - 1] = '\0';
    std::strncpy(target_entry.root, root_p.c_str(), sizeof(target_entry.root) - 1);
    target_entry.root[sizeof(target_entry.root) - 1] = '\0';
    target_entry.type = FsType::Network;
    target_entry.flags = e.IsNfs() ? FsEntryFlag_ReadOnly : FsEntryFlag_None;
    std::strncpy(target_entry.url, e.url.c_str(), sizeof(target_entry.url) - 1);
    target_entry.url[sizeof(target_entry.url) - 1] = '\0';
    std::strncpy(target_entry.protocol, e.protocol.c_str(), sizeof(target_entry.protocol) - 1);
    target_entry.protocol[sizeof(target_entry.protocol) - 1] = '\0';
    std::strncpy(target_entry.user, e.user.c_str(), sizeof(target_entry.user) - 1);
    target_entry.user[sizeof(target_entry.user) - 1] = '\0';
    std::strncpy(target_entry.pass, e.pass.c_str(), sizeof(target_entry.pass) - 1);
    target_entry.pass[sizeof(target_entry.pass) - 1] = '\0';
    target_entry.port = e.port;
    view->ConnectToLocation(target_entry);
}

void Menu::SetFolderPicker(FolderPickCallback cb) {
    m_on_folder_picked = std::move(cb);
    SetTitle("Select firmware folder"_i18n);
    // relabel START so the "select this folder" affordance is visible.
    SetAction(Button::START, Action{"Select folder"_i18n, [this](){
        ConfirmFolderPick(view->m_path);
    }});
}

void Menu::ConfirmFolderPick(const fs::FsPath& folder) {
    if (!m_on_folder_picked) {
        return;
    }

    const std::string path_str = folder.s[0] ? folder.s : "/";
    App::Push<OptionBox>(
        "Install firmware from this folder?"_i18n + "\n\n" + path_str,
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

bool IsUrlLike(const std::string& str) {
    if (str.find("://") != std::string::npos) return true;
    if (str.starts_with("192.168.") || str.starts_with("10.") || str.starts_with("172.")) return true;
    if (str.find('.') != std::string::npos && (str.find('/') != std::string::npos || str.find(':') != std::string::npos)) return true;
    return false;
}

void AddNetworkLocationInteractive(std::function<void()> on_success) {
    PopupList::Items protocols = {"Samba (SMB)", "NFS", "WebDAV", "FTP", "HTTP"};
    App::Push<PopupList>("Select Protocol"_i18n, protocols, [on_success](std::optional<s64> op_proto) {
        if (!op_proto) return;
        s64 proto = *op_proto;

        std::string name;
        if (R_FAILED(swkbd::ShowText(name, "Enter location name (e.g. My NAS)"_i18n.c_str(), ""))) return;
        if (name.empty()) return;

        App::Pop();

        std::string target_proto;
        if (proto == 0) target_proto = "smb";
        else if (proto == 1) target_proto = "nfs";
        else if (proto == 2) target_proto = "webdav";
        else if (proto == 3) target_proto = "ftp";
        else if (proto == 4) target_proto = "http";

        auto network_locations = location::Load();
        bool has_exact_name = false;
        bool has_same_type = false;

        for (const auto& loc : network_locations) {
            if (loc.name == name) {
                has_exact_name = true;
                std::string existing_proto = loc.protocol;
                if (existing_proto.empty()) {
                    if (loc.url.starts_with("smb://")) existing_proto = "smb";
                    else if (loc.url.starts_with("nfs://")) existing_proto = "nfs";
                    else if (loc.url.starts_with("ftp://") || loc.url.starts_with("ftps://")) existing_proto = "ftp";
                    else if (loc.url.starts_with("http://") || loc.url.starts_with("https://")) existing_proto = "http";
                    else existing_proto = "webdav";
                }
                if (existing_proto == target_proto) {
                    has_same_type = true;
                }
            }
        }

        bool is_url = IsUrlLike(name);

        auto add_location_func = [proto, name, is_url, on_success](std::string final_name) {
            location::Entry e;
            e.name = final_name;
            std::string target_url;

            if (proto == 0) {
                e.protocol = "smb";
                if (is_url) {
                    target_url = name;
                    if (!target_url.starts_with("smb://")) {
                        if (size_t pos = target_url.find("://"); pos != std::string::npos) {
                            target_url = "smb://" + target_url.substr(pos + 3);
                        } else {
                            target_url = "smb://" + target_url;
                        }
                    }
                } else {
                    target_url = "smb://";
                }
                e.url = target_url;
            } else if (proto == 1) {
                e.protocol = "nfs";
                if (is_url) {
                    target_url = name;
                    if (!target_url.starts_with("nfs://")) {
                        if (size_t pos = target_url.find("://"); pos != std::string::npos) {
                            target_url = "nfs://" + target_url.substr(pos + 3);
                        } else {
                            target_url = "nfs://" + target_url;
                        }
                    }
                } else {
                    target_url = "nfs://";
                }
                e.url = target_url;
            } else if (proto == 2) {
                e.protocol = "webdav";
                if (is_url) {
                    target_url = name;
                    if (!target_url.starts_with("webdav://") && !target_url.starts_with("webdavs://") &&
                        !target_url.starts_with("http://") && !target_url.starts_with("https://")) {
                        if (size_t pos = target_url.find("://"); pos != std::string::npos) {
                            target_url = "webdav://" + target_url.substr(pos + 3);
                        } else {
                            target_url = "webdav://" + target_url;
                        }
                    }
                } else {
                    target_url = "webdav://";
                }
                e.url = target_url;
            } else if (proto == 3) {
                e.protocol = "ftp";
                e.port = 21;
                if (is_url) {
                    target_url = name;
                    if (!target_url.starts_with("ftp://") && !target_url.starts_with("ftps://")) {
                        if (size_t pos = target_url.find("://"); pos != std::string::npos) {
                            target_url = "ftp://" + target_url.substr(pos + 3);
                        } else {
                            target_url = "ftp://" + target_url;
                        }
                    }
                    if (size_t colon_pos = target_url.find_last_of(':'); colon_pos != std::string::npos && colon_pos > 6) {
                        size_t slash_pos = target_url.find('/', colon_pos);
                        std::string port_str = (slash_pos == std::string::npos) ? target_url.substr(colon_pos + 1) : target_url.substr(colon_pos + 1, slash_pos - colon_pos - 1);
                        if (!port_str.empty()) {
                            bool is_num = true;
                            for (char c : port_str) {
                                if (!std::isdigit(static_cast<unsigned char>(c))) {
                                    is_num = false;
                                    break;
                                }
                            }
                            if (is_num) {
                                const auto parsed = std::strtoul(port_str.c_str(), nullptr, 10);
                                if (parsed >= 1 && parsed <= 65535) {
                                    e.port = static_cast<u16>(parsed);
                                }
                            }
                        }
                    }
                } else {
                    target_url = "ftp://";
                }
                e.url = target_url;
            } else if (proto == 4) {
                e.protocol = "http";
                if (is_url) {
                    target_url = name;
                    if (!target_url.starts_with("http://") && !target_url.starts_with("https://")) {
                        if (size_t pos = target_url.find("://"); pos != std::string::npos) {
                            target_url = "http://" + target_url.substr(pos + 3);
                        } else {
                            target_url = "http://" + target_url;
                        }
                    }
                } else {
                    target_url = "http://";
                }
                e.url = target_url;
            }

            location::Add(e);
            App::Notify("Location added successfully!"_i18n);
            if (on_success) {
                evman::push(evman::FunctionalEventData{[on_success]() {
                    on_success();
                }});
            }
        };

        auto generate_numbered_name = [name, network_locations]() -> std::string {
            int counter = 2;
            std::string temp_name;
            bool name_exists = true;
            while (name_exists) {
                temp_name = name + " (" + std::to_string(counter) + ")";
                name_exists = false;
                for (const auto& loc : network_locations) {
                    if (loc.name == temp_name) {
                        name_exists = true;
                        break;
                    }
                }
                counter++;
            }
            return temp_name;
        };

        if (has_exact_name) {
            if (has_same_type) {
                App::Push<OptionBox>(
                    "A location with the same name and protocol already exists. Overwrite?"_i18n,
                    "Overwrite"_i18n, "Keep Both"_i18n, 1,
                    [add_location_func, generate_numbered_name, name](auto op_idx) {
                        if (op_idx && *op_idx == 0) {
                            add_location_func(name);
                        } else {
                            add_location_func(generate_numbered_name());
                        }
                    }
                );
            } else {
                add_location_func(generate_numbered_name());
            }
        } else {
            add_location_func(name);
        }
    });
}

} // namespace sphaira::ui::menu::filebrowser
