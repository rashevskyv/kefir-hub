#include "ui/menus/file_picker.hpp"
#include "path_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/file_icon.hpp"
#include "ui/menus/file_viewer.hpp"

#include "app.hpp"
#include "image.hpp"
#include "ui/nvg_util.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "location.hpp"

#include <cstring>
#include <ctime>
#include <vector>
#include <utility>

namespace sphaira::ui::menu::filepicker {
namespace {

constexpr FsEntry FS_ENTRY_DEFAULT{
    "microSD card", "/", FsType::Sd, FsEntryFlag_Assoc,
};

constexpr FsEntry FS_ENTRIES[]{
    FS_ENTRY_DEFAULT,
};

constexpr std::string_view AUDIO_EXTENSIONS[] = {
    "mp3", "ogg", "flac", "wav", "aac" "ac3", "aif", "asf", "bfwav",
    "bfsar", "bfstm",
};
constexpr std::string_view VIDEO_EXTENSIONS[] = {
    "mp4", "mkv", "m3u", "m3u8", "hls", "vob", "avi", "dv", "flv", "m2ts",
    "m2v", "m4a", "mov", "mpeg", "mpg", "mts", "swf", "ts", "vob", "wma", "wmv",
};
constexpr std::string_view IMAGE_EXTENSIONS[] = {
    "png", "jpg", "jpeg", "bmp", "gif",
};
constexpr std::string_view INSTALL_EXTENSIONS[] = {
    "nsp", "xci", "nsz", "xcz",
};
constexpr std::string_view ZIP_EXTENSIONS[] = {
    "zip",
};

} // namespace

void Menu::ApplyLayout() {
    const Vec4 content_pos{40, 97, 1200, 539};
    if (IsIconLayout()) {
        const Vec2 pad{10, 10};
        const Vec4 v{93, 186, 174, 174};
        m_list = std::make_unique<List>(6, 6 * 2, content_pos, v, pad);
    } else {
        const Vec4 v{75, GetY() + 1.f + 42.f, 1220.f - 45.f * 2, 60};
        m_list = std::make_unique<List>(1, 8, m_pos, v);
    }
}

void Menu::FreeThumbs() {
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

auto Menu::IsImagePicker() const -> bool {
    for (const auto& filter : m_filter) {
        const char* ext = (!filter.empty() && filter[0] == '.') ? filter.c_str() + 1 : filter.c_str();
        if (path::EqualsIC(ext, "jpg") || path::EqualsIC(ext, "jpeg") ||
            path::EqualsIC(ext, "png") || path::EqualsIC(ext, "bmp") ||
            path::EqualsIC(ext, "gif")) {
            return true;
        }
    }
    return false;
}

auto Menu::TryLoadThumb(u32 entry_index) -> bool {
    if (entry_index >= m_entries.size()) {
        return false;
    }
    auto& e = m_entries[entry_index];
    if (e.IsDir()) {
        if (entry_index >= m_mosaics.size()) {
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

auto Menu::CollectFolderImages() const -> std::pair<std::vector<fs::FsPath>, s64> {
    std::vector<fs::FsPath> paths;
    s64 current = 0;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        const auto& e = GetEntry(i);
        if (!e.IsFile() || !path::IsAnyOfIC(e.GetExtension(), IMAGE_EXTENSIONS)) {
            continue;
        }
        if (static_cast<s64>(i) == m_index) {
            current = static_cast<s64>(paths.size());
        }
        paths.push_back(GetNewPath(e));
    }
    return {std::move(paths), current};
}

void Menu::OpenPreview() {
    if (m_entries_current.empty() || !GetEntry().IsFile()) {
        return;
    }
    auto [paths, index] = CollectFolderImages();
    if (paths.empty()) {
        paths.push_back(GetNewPathCurrent());
        index = 0;
    }
    auto viewer = std::make_unique<fileview::Menu>(GetNewPathCurrent(), std::move(paths), index);
    viewer->SetImagePickCallback([this](const fs::FsPath& path) {
        if (m_callback(path, m_fs_entry)) {
            SetPop();
            return true;
        }
        return false;
    });
    App::Push(std::move(viewer));
}

void Menu::UseCurrentFile() {
    if (m_entries_current.empty() || !GetEntry().IsFile()) {
        return;
    }
    if (m_callback(GetNewPathCurrent(), m_fs_entry)) {
        SetPop();
    }
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("File Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (IsImagePicker()) {
        options->Add<SidebarEntryCallback>("Preview"_i18n, [this](){
            OpenPreview();
        }, true, "Open the image so you can see it. L/R flips through the folder."_i18n);
        if (!m_pick_directory) {
            options->Add<SidebarEntryCallback>("Use this image"_i18n, [this](){
                UseCurrentFile();
            }, true, "Select the highlighted file."_i18n);
        }
    }
    SidebarEntryArray::Items layout_items;
    layout_items.push_back("List"_i18n);
    layout_items.push_back("Icon"_i18n);
    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        m_image_layout.Set(index_out);
        ApplyLayout();
    }, m_image_layout.Get(), "Icon shows file thumbnails and folder previews."_i18n);

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

    options->Add<SidebarEntryArray>("Mount"_i18n, mount_items, [this, fs_entries](s64& index_out){
        App::PopToMenu();
        SetFs(fs_entries[index_out].root, fs_entries[index_out]);
    }, i18n::get(m_fs_entry.name), "Switch the file source to a different storage or mount point."_i18n);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto& text_col = theme->GetColour(ThemeEntryID_TEXT);

    if (m_entries_current.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    constexpr float text_xoffset{15.f};
    bool got_dir_count = false;
    int loaded{};
    const bool icon_grid = IsIconLayout();

    m_list->Draw(vg, theme, m_entries_current.size(), m_index, [this, text_col, &got_dir_count, &loaded, icon_grid](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = GetEntry(i);
        const auto entry_i = m_entries_current[i];
        if (loaded < 2 && TryLoadThumb(entry_i)) {
            loaded++;
        }
        const int thumb = (entry_i < m_thumbs.size()) ? m_thumbs[entry_i] : 0;

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = m_index == i;
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
            if (e.IsDir()) {
                if (entry_i < m_mosaics.size()) {
                    file_icon::DrawMosaic(vg, theme, preview, m_mosaics[entry_i]);
                } else {
                    file_icon::DrawFolderShape(vg, theme, preview);
                }
            } else {
                file_icon::DrawFileThumb(vg, theme, preview, thumb, e.GetExtension());
            }
            nvgSave(vg);
            nvgIntersectScissor(vg, x + 4.f, y + h - 26.f, w - 8.f, 24.f);
            gfx::drawTextArgs(vg, x + w / 2.f, y + h - 14.f, 15.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s", e.name);
            nvgRestore(vg);
            return;
        }

        if (e.IsDir()) {
            DrawElement(x + text_xoffset, y + 5, 50, 50, ThemeEntryID_ICON_FOLDER);
        } else if (thumb > 0) {
            file_icon::DrawContain(vg, Vec4{x + text_xoffset, y + 5, 50, 50}, thumb, 4);
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
                icon = ThemeEntryID_ICON_NRO;
            } else if (path::IsAnyOfIC(ext, ZIP_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_ZIP;
            } else if (path::EqualsIC(ext, "nro")) {
                icon = ThemeEntryID_ICON_NRO;
            }

            DrawElement(x + text_xoffset, y + 5, 50, 50, icon);
        }

        m_scroll_name.Draw(vg, selected, x + text_xoffset+65, y + (h / 2.f), w-(75+text_xoffset+65+50), 20, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), e.name);

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

} // namespace sphaira::ui::menu::filepicker
