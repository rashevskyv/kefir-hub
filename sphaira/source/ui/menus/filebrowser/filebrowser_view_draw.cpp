#include "ui/menus/filebrowser.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "ui/menus/filebrowser/filebrowser_internal.hpp"
#include "path_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"

#include <cstring>
#include <ctime>
#include <algorithm>

namespace sphaira::ui::menu::filebrowser {

using namespace detail;

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
    m_list->Draw(vg, theme, m_entries_current.size(), m_index, [this, text_col, &got_dir_count, &loaded, icon_grid](auto* vg, auto* theme, auto v, auto i) {
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

} // namespace sphaira::ui::menu::filebrowser
