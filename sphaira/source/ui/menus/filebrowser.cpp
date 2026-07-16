#include "ui/menus/filebrowser.hpp"
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
#include "utils/devoptab_smb2.hpp"

#include "log.hpp"
#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "fs.hpp"
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
#include <algorithm>

namespace sphaira::ui::menu::filebrowser {
using namespace detail;

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





} // namespace

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

FsView::FsView(Menu* menu, const fs::FsPath& path, const FsEntry& entry, ViewSide side) : m_menu{menu}, m_side{side} {
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

            if (IsSd() && m_is_update_folder && m_daybreak_path.has_value()) {
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

            if (entry.type == FsDirEntryType_Dir) {
                Scan(GetNewPathCurrent());
            } else {
                // special case for nro
                if (IsSd() && IsSamePath(entry.GetExtension(), "nro")) {
                    App::Push<OptionBox>("Launch "_i18n + entry.GetName() + '?',
                        "No"_i18n, "Launch"_i18n, 1, [this](auto op_index){
                            if (op_index && *op_index) {
                                nro_launch(GetNewPathCurrent());
                            }
                        });
                } else if (IsExtension(entry.GetExtension(), INSTALL_EXTENSIONS)) {
                    InstallFiles();
                } else if (IsSd() && IsExtension(entry.GetExtension(), IMAGE_EXTENSIONS)) {
                    OpenImageViewer();
                } else if (IsSd()) {
                    const auto assoc_list = m_menu->FindFileAssocFor();
                    if (!assoc_list.empty()) {
                        // for (auto&e : assoc_list) {
                        //     log_write("assoc got: %s\n", e.path.c_str());
                        // }

                        PopupList::Items items;
                        for (const auto&p : assoc_list) {
                            items.emplace_back(p.name);
                        }

                        const auto title = "Launch option for: "_i18n + GetEntry().name;
                        App::Push<PopupList>(
                            title, items, [this, assoc_list](auto op_index){
                                if (op_index) {
                                    log_write("selected: %s\n", assoc_list[*op_index].name.c_str());
                                    nro_launch(assoc_list[*op_index].path, nro_add_arg_file(GetNewPathCurrent()));
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

            std::string_view view{m_path};
            if (view != m_fs->Root()) {
                const auto end = view.find_last_of('/');
                assert(end != view.npos);

                if (end == 0) {
                    Scan(m_fs->Root(), true);
                } else {
                    Scan(view.substr(0, end), true);
                }
            } else {
                if (!m_menu->IsTab()) {
                    m_menu->PromptIfShouldExit();
                }
            }
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

    m_list->Draw(vg, theme, m_entries_current.size(), [this, text_col, &got_dir_count](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = GetEntry(i);

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = m_index == i;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_entries_current.size() - 1) {
                gfx::drawRect(vg, Vec4{x, y + h, w, 1.f}, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        const float x_offset = 15.f;

        if (e.IsDir()) {
            DrawElement(x + x_offset, y + 5, 50, 50, ThemeEntryID_ICON_FOLDER);
        } else {
            auto icon = ThemeEntryID_ICON_FILE;
            const auto ext = e.GetExtension();
            if (IsExtension(ext, AUDIO_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_AUDIO;
            } else if (IsExtension(ext, VIDEO_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_VIDEO;
            } else if (IsExtension(ext, IMAGE_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_IMAGE;
            } else if (IsExtension(ext, INSTALL_EXTENSIONS)) {
                // todo: maybe replace this icon with something else?
                icon = ThemeEntryID_ICON_NRO;
            } else if (IsExtension(ext, ZIP_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_ZIP;
            } else if (IsExtension(ext, "nro")) {
                icon = ThemeEntryID_ICON_NRO;
            }

            DrawElement(x + x_offset, y + 5, 50, 50, icon);
        }

        if (m_selected_count > 0) {
            float box_size = 20.f;
            float box_x = x - 30.f;
            float box_y = y + (h / 2.f) - (box_size / 2.f);

            // Draw checkbox outline and background
            nvgBeginPath(vg);
            nvgRect(vg, box_x, box_y, box_size, box_size);
            nvgFillColor(vg, theme->GetColour(ThemeEntryID_BACKGROUND));
            nvgFill(vg);

            nvgBeginPath(vg);
            nvgRect(vg, box_x, box_y, box_size, box_size);
            nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            nvgStrokeWidth(vg, 2.0f);
            nvgStroke(vg);

            if (e.IsSelected()) {
                // Draw checkmark inside the checkbox on the left
                float check_x = box_x + box_size / 2.f;
                float check_y = y + (h / 2.f) - (18.f / 2.f); // center vertically

                const auto bg_col = theme->GetColour(ThemeEntryID_BACKGROUND);
                float bg_lum = 0.2126f * bg_col.r + 0.7152f * bg_col.g + 0.0722f * bg_col.b;
                NVGcolor outline_col = nvgRGBA(0, 0, 0, 255);

                gfx::drawText(vg, check_x - 1.f, check_y, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline_col);
                gfx::drawText(vg, check_x + 1.f, check_y, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline_col);
                gfx::drawText(vg, check_x, check_y - 1.f, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline_col);
                gfx::drawText(vg, check_x, check_y + 1.f, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline_col);

                gfx::drawText(vg, check_x, check_y, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
            }
        }

        m_scroll_name.Draw(vg, selected, x + x_offset+65, y + (h / 2.f), w-(75+x_offset+65+50), 20, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), e.name);

        // NOTE: make this native only if i disable dir scan from above.
        if (e.IsDir()) {
            // NOTE: this takes longer than 16ms when opening a new folder due to it
            // checking all 9 folders at once.
            if (!got_dir_count && e.file_count == -1 && e.dir_count == -1) {
                got_dir_count = true;
                m_fs->DirGetEntryCount(GetNewPath(e), &e.file_count, &e.dir_count);
            }

            if (e.file_count != -1) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%zd files"_i18n.c_str(), e.file_count);
            }
            if (e.dir_count != -1) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) + 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(text_id), "%zd dirs"_i18n.c_str(), e.dir_count);
            }
        } else if (e.IsFile()) {
            if (!e.time_stamp.is_valid) {
                const auto path = GetNewPath(e);
                if (m_fs->IsNative()) {
                    m_fs->GetFileTimeStampRaw(path, &e.time_stamp);
                } else {
                    m_fs->FileGetSizeAndTimestamp(path, &e.time_stamp, &e.file_size);
                }
            }

            const auto t = (time_t)(e.time_stamp.modified);
            struct tm tm{};
            localtime_r(&t, &tm);
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) + 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(text_id), "%02u/%02u/%u", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
            if ((double)e.file_size / 1024.0 / 1024.0 <= 0.009) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%.2f KiB", (double)e.file_size / 1024.0);
            } else {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%.2f MiB", (double)e.file_size / 1024.0 / 1024.0);
            }
        }
    });
}

void FsView::OnFocusGained() {
    Widget::OnFocusGained();
    if (m_entries.empty()) {
        if (m_path.empty()) {
            Scan(m_fs->Root());
        } else {
            Scan(m_path);
        }
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
    }

    if (IsSd() && !m_entries_current.empty() && !GetEntry().checked_internal_extension && IsSamePath(GetEntry().GetExtension(), "zip")) {
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
    if (m_entries_current.empty()) {
        return;
    }

    if (!m_menu->m_selected.Empty()) {
        m_menu->ResetSelection();
    }

    if (App::GetApp()->m_controller.GotHeld(Button::R2)) {
        s64 visible_selected_count{};
        for (u32 i = 0; i < m_entries_current.size(); i++) {
            if (GetEntry(i).selected) {
                visible_selected_count++;
            }
        }

        const auto set = visible_selected_count != static_cast<s64>(m_entries_current.size());
        for (u32 i = 0; i < m_entries_current.size(); i++) {
            GetEntry(i).selected = set;
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

    m_menu->UpdateSubheading();
}

void FsView::InvertSelection() {
    if (m_entries_current.empty()) {
        return;
    }

    if (!m_menu->m_selected.Empty()) {
        m_menu->ResetSelection();
    }

    for (u32 i = 0; i < m_entries_current.size(); i++) {
        GetEntry(i).selected ^= 1;
    }

    m_selected_count = 0;
    for (const auto& e : m_entries) {
        if (e.selected) {
            m_selected_count++;
        }
    }

    m_menu->UpdateSubheading();
}

void FsView::InstallForwarder() {
    if (IsSamePath(GetEntry().GetExtension(), "nro")) {
        if (R_FAILED(homebrew::Menu::InstallHomebrewFromPath(GetNewPathCurrent()))) {
            log_write("failed to create forwarder\n");
        }
        return;
    }

    const auto assoc_list = m_menu->FindFileAssocFor();
    if (assoc_list.empty()) {
        log_write("failed to find assoc for: %s ext: %s\n", GetEntry().name, GetEntry().GetExtension().c_str());
        return;
    }

    PopupList::Items items;
    for (const auto&p : assoc_list) {
        items.emplace_back(p.name);
    }

    const auto title = std::string{"Select launcher for: "_i18n} + GetEntry().name;
    App::Push<PopupList>(
        title, items, [this, assoc_list](auto op_index){
            if (op_index) {
                const auto assoc = assoc_list[*op_index];
                App::Push<ForwarderForm>(assoc, GetRomDatabaseFromPath(m_path), GetEntry(), GetNewPathCurrent());
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
        if (!entry.IsFile() || !IsExtension(entry.GetExtension(), IMAGE_EXTENSIONS)) {
            continue;
        }

        if (static_cast<s64>(i) == m_index) {
            image_index = static_cast<s64>(paths.size());
        }

        paths.emplace_back(GetNewPath(i));
    }

    App::Push<fileview::Menu>(GetNewPathCurrent(), std::move(paths), image_index);
}





auto FsView::Scan(const fs::FsPath& new_path, bool is_walk_up) -> Result {
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    log_write("new scan path: %s\n", new_path.s);
    if (!is_walk_up && !m_path.empty() && !m_entries_current.empty()) {
        const LastFile f(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
        m_previous_highlighted_file.emplace_back(f);
    }

    m_path = new_path;
    m_entries.clear();
    m_index = 0;
    m_list->SetYoff(0);
    m_menu->SetTitleSubHeading(m_path);
    m_selected_count = 0;

    fs::Dir d;
    R_TRY(m_fs->OpenDirectory(new_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));

    // we won't run out of memory here (tm)
    std::vector<FsDirectoryEntry> dir_entries;
    R_TRY(d.ReadAll(dir_entries));

    const auto count = dir_entries.size();
    m_entries.reserve(count);

    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();

    m_entries_index.reserve(count);
    m_entries_index_hidden.reserve(count);

    u32 i = 0;
    for (const auto& e : dir_entries) {
        m_entries_index_hidden.emplace_back(i);
        if ('.' != e.name[0]) {
            m_entries_index.emplace_back(i);
        }

        m_entries.emplace_back(e);
        i++;
    }

    Sort();

    // quick check to see if this is an update folder
    m_is_update_folder = R_SUCCEEDED(CheckIfUpdateFolder());

    SetIndex(0);

    // find previous entry
    if (is_walk_up && !m_previous_highlighted_file.empty()) {
        ON_SCOPE_EXIT(m_previous_highlighted_file.pop_back());
        SetIndexFromLastFile(m_previous_highlighted_file.back());
    }

    R_SUCCEED();
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
    m_menu->m_selected.Reset();
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
    }

    if (HasFocus()) {
        if (m_path.empty()) {
            Scan(m_fs->Root());
        } else {
            Scan(m_path);
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

    // returns true if all entries match the ext array.
    const auto check_all_ext = [this](auto& exts){
        const auto entries = GetSelectedEntries();
        if (entries.empty()) {
            return false;
        }

        for (auto&e : entries) {
            if (!e.IsFile() || !IsExtension(e.GetExtension(), exts)) {
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

    if (IsSd() && m_entries_current.size() && !m_selected_count) {
        if (GetEntry().IsFile() && (IsSamePath(GetEntry().GetExtension(), "nro") || !m_menu->FindFileAssocFor().empty())) {
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

    if (!m_menu->m_selected.Empty() && (m_menu->m_selected.Type() == SelectedType::Cut || m_menu->m_selected.Type() == SelectedType::Copy)) {
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
        paste_entry->Depends([this](){ return !IsReadOnly(m_path); }, "Destination folder is read-only"_i18n);
    }

    if (m_entries_current.size()) {
        auto cut_entry = options->Add<SidebarEntryCallback>("Cut"_i18n, [this](){
            m_menu->AddSelectedEntries(SelectedType::Cut);
        }, true, "Move the selected files to the clipboard."_i18n);
        cut_entry->Depends([this](){ return !AnySelectedReadOnly(); }, "Cannot cut read-only files"_i18n);

        options->Add<SidebarEntryCallback>("Copy"_i18n, [this](){
            m_menu->AddSelectedEntries(SelectedType::Copy);
        }, true, "Copy the selected files to the clipboard."_i18n);
    }

    if (m_entries_current.size()) {
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
        delete_entry->Depends([this](){ return !AnySelectedReadOnly(); }, "Cannot delete read-only files"_i18n);
    }

    if (m_entries_current.size() && !m_selected_count) {
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

    if (m_entries_current.size()) {
        if (check_all_ext(ZIP_EXTENSIONS)) {
            auto extract_entry = options->Add<SidebarEntryCallback>("Extract zip"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Extract Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("Extract here"_i18n, [this](){
                    UnzipFiles("");
                }, "Extract the archive contents into the current folder."_i18n);

                options->Add<SidebarEntryCallback>("Extract to root"_i18n, [this](){
                    App::Push<OptionBox>("Are you sure you want to extract to root?"_i18n,
                        "No"_i18n, "Yes"_i18n, 0, [this](auto op_index){
                        if (op_index && *op_index) {
                            UnzipFiles(m_fs->Root());
                        }
                    });
                }, "Extract the archive contents to the root of this storage."_i18n);

                options->Add<SidebarEntryCallback>("Extract to..."_i18n, [this](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Enter the path to the folder to extract into", fs::AppendPath(m_path, ""))) && !out.empty()) {
                        UnzipFiles(out);
                    }
                }, "Extract the archive to a custom path you specify."_i18n);
            }, "Extract the contents of the selected ZIP archive."_i18n);
            extract_entry->SetHasSubmenu(true);
        }

        if (!check_all_ext(ZIP_EXTENSIONS) || m_selected_count) {
            auto compress_entry = options->Add<SidebarEntryCallback>("Compress to zip"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Compress Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("Compress"_i18n, [this](){
                    ZipFiles("");
                }, "Compress the selected file(s) into a zip in the current folder."_i18n);

                options->Add<SidebarEntryCallback>("Compress to..."_i18n, [this](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Enter the path to the folder to extract into", m_path)) && !out.empty()) {
                        ZipFiles(out);
                    }
                }, "Compress the selected file(s) to a custom output path."_i18n);
            }, "Compress the selected file(s) into a ZIP archive."_i18n);
            compress_entry->SetHasSubmenu(true);
        }
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

    if (IsSd() && m_entries_current.size() && !m_selected_count && GetEntry().IsFile()) {
        if (IsExtension(GetEntry().GetExtension(), IMAGE_EXTENSIONS)) {
            options->Add<SidebarEntryCallback>("View Image"_i18n, [this](){
                OpenImageViewer();
            }, "Open the selected image in the built-in viewer."_i18n);
            auto theme_entry = options->Add<SidebarEntryCallback>("Create Switch Theme"_i18n, [this](){
                App::Push<theme_creator::Menu>(GetNewPathCurrent());
            }, "Use the selected image to create a custom Switch theme."_i18n);
            theme_entry->SetHasSubmenu(true);
        } else if (GetEntry().file_size < 1024*64) {
            options->Add<SidebarEntryCallback>("View as text (unfinished)"_i18n, [this](){
                App::Push<fileview::Menu>(GetNewPathCurrent());
            }, "Open the selected file as plain text."_i18n);
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

    for (const auto& e: FS_ENTRIES) {
        fs_entries.emplace_back(e);
        mount_items.push_back(i18n::get(e.name));
    }

    const auto network_locations = location::Load();
    for (const auto& e: network_locations) {
        if (e.IsSmb()) {
            FsEntry entry{
                .name = e.name,
                .root = "smb2:/",
                .type = FsType::Network,
                .flags = FsEntryFlag_ReadOnly,
                .url = e.url,
                .user = e.user,
                .pass = e.pass
            };
            fs_entries.emplace_back(entry);
            mount_items.push_back(e.name);
        }
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

            App::Push<ProgressBox>(0, "Connecting to SMB..."_i18n, target_entry.name, [this, target_entry](auto pbox) -> Result {
#ifdef BUILD_SMB2
                if (g_smb2fs) {
                    if (g_smb2fs->GetConnectUrl() == target_entry.url.toString()) {
                        R_SUCCEED();
                    }
                    delete g_smb2fs;
                    g_smb2fs = nullptr;
                }
                std::string server, share;
                ParseSmbUrl(target_entry.url.toString(), server, share);
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
            }, [this, target_entry](Result rc) {
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Failed to connect to SMB server!"_i18n);
                } else {
                    SetFs(target_entry.root, target_entry);
                }
            });
        } else {
            SetFs(target_entry.root, target_entry);
        }
    }, current_index, "Switch the file source to a different storage or mount point."_i18n);

    options->Add<SidebarEntryCallback>("Add network location"_i18n, [this](){
        AddNetworkLocationInteractive([this](){
            ShowSourcePicker();
        });
    }, "Configure a new network location (supported protocols: SMB, WebDAV, FTP, HTTP)."_i18n);
}

Menu::Menu(u32 flags, const ::sphaira::location::Entry* launch_location) : MenuBase{"FileBrowser"_i18n, flags} {
    SetAction(Button::START, Action{"Options"_i18n, [this](){
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

Menu::~Menu() {
}

void Menu::ConnectToLocation(const ::sphaira::location::Entry& e) {
    if (e.IsSmb()) {
        FsEntry target_entry{
            .name = e.name,
            .root = "smb2:/",
            .type = FsType::Network,
            .flags = FsEntryFlag_ReadOnly,
            .url = e.url,
            .user = e.user,
            .pass = e.pass
        };
        App::Push<ProgressBox>(0, "Connecting to SMB..."_i18n, target_entry.name, [this, target_entry](auto pbox) -> Result {
#ifdef BUILD_SMB2
            if (g_smb2fs) {
                if (g_smb2fs->GetConnectUrl() == target_entry.url.toString()) {
                    R_SUCCEED();
                }
                delete g_smb2fs;
                g_smb2fs = nullptr;
            }
            std::string server, share;
            ParseSmbUrl(target_entry.url.toString(), server, share);
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
        }, [this, target_entry](Result rc) {
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to connect to SMB server!"_i18n);
            } else {
                view->SetFs(target_entry.root, target_entry);
            }
        });
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
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
    const auto view_actions = view->GetActions();
    m_actions.insert_range(view_actions);
    ON_SCOPE_EXIT(RemoveActions(view_actions));

    MenuBase::Update(controller, touch);
    view->Update(controller, touch);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    // see Menu::Update().
    const auto view_actions = view->GetActions();
    m_actions.insert_range(view_actions);
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
            } else if (!std::strcmp(Key, "supported_extensions")) {
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

        assoc.name.assign(d->d_name, ext - d->d_name);

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

void Menu::LoadAssocEntries() {
    // load from romfs first
    if (R_SUCCEEDED(romfsInit())) {
        LoadAssocEntriesPath("romfs:/assoc/");
        romfsExit();
    }
    // then load custom entries
    LoadAssocEntriesPath(paths::ASSOC);
}

void Menu::UpdateSubheading() {
    const auto index = view->m_entries_current.empty() ? 0 : view->m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(view->m_entries_current.size()));
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
                    SetTitleSubHeading(view->m_path);
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

    App::Push<ui::OptionBox>(
        "Close FileBrowser?"_i18n,
        "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
            if (op_index && *op_index) {
                SetPop();
            }
        }
    );
}

void AddNetworkLocationInteractive(std::function<void()> on_success) {
    PopupList::Items protocols = {"Samba (SMB)", "WebDAV", "FTP", "HTTP"};
    App::Push<PopupList>("Select Protocol"_i18n, protocols, [on_success](std::optional<s64> op_proto) {
        if (!op_proto) return;
        s64 proto = *op_proto;

        std::string name;
        if (R_FAILED(swkbd::ShowText(name, "Enter location name (e.g. My NAS)"_i18n.c_str(), ""))) return;
        if (name.empty()) return;

        App::Pop();

        location::Entry e;
        e.name = name;
        if (proto == 0) {
            e.protocol = "smb";
            e.url = "smb://";
        } else if (proto == 1) {
            e.protocol = "webdav";
            e.url = "webdav://";
        } else if (proto == 2) {
            e.protocol = "ftp";
            e.url = "ftp://";
            e.port = 21;
        } else if (proto == 3) {
            e.protocol = "http";
            e.url = "http://";
        }
        location::Add(e);
        App::Notify("Location added successfully!"_i18n);
        if (on_success) {
            evman::push(evman::FunctionalEventData{[on_success]() {
                on_success();
            }});
        }
    });
}

} // namespace sphaira::ui::menu::filebrowser
