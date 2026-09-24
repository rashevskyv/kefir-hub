#include "ui/menus/file_viewer/file_viewer_internal.hpp"
#include "text_helper.hpp"
#include "path_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "minizip_helper.hpp"
#include "swkbd.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/theme_creator.hpp"
#include "ui/layout.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/remote_input.hpp"
#include "ui/sidebar.hpp"
#include "web.hpp"

#include <minizip/zip.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <utility>

namespace sphaira::ui::menu::fileview {
void Menu::RecreateList() {
    const float item_h = std::round(m_font_size * 1.6f);
    constexpr float pad_y = 2.f;
    m_viewport_rows = std::max<s64>(1, static_cast<s64>(530.f / (item_h + pad_y)));
    m_buffer_rows = m_viewport_rows * 2;

    const Vec4 list_pos{40.f, 100.f, 1200.f, 530.f};
    const Vec4 item_pos{50.f, 105.f, 1180.f, item_h};
    m_text_list = std::make_unique<List>(1, m_viewport_rows, list_pos, item_pos, Vec2{0.f, pad_y});
    if (m_has_range || m_selecting_range) {
        m_text_list->SetWrap(false);
    }
}

void Menu::LoadPage(s64 page_idx) {
    if (page_idx < 0) {
        page_idx = 0;
    }

    auto read_func = [this](int64_t off, char* buf, int64_t sz) -> int64_t {
        u64 bytes_read = 0;
        Result rc = m_file.Read(off, buf, sz, 0, &bytes_read);
        if (R_FAILED(rc)) {
            m_load_result = rc;
            m_load_failed = true;
            return 0;
        }
        if (bytes_read == 0 && sz > 0) {
            m_load_result = FsError_InvalidSize;
            m_load_failed = true;
            return 0;
        }
        return static_cast<int64_t>(bytes_read);
    };

    while (static_cast<s64>(m_page_offsets.size()) <= page_idx) {
        const s64 last_idx = static_cast<s64>(m_page_offsets.size()) - 1;
        const s64 last_offset = m_page_offsets[last_idx];
        const s64 last_line = m_page_start_lines[last_idx];
        const auto p = text_helper::ReadPage(read_func, m_file_size, last_offset, last_line, m_buffer_rows, m_viewport_rows);
        if (p.is_error || p.logical_end_offset <= last_offset) {
            if (!m_load_failed) {
                m_load_result = FsError_InvalidSize;
                m_load_failed = true;
            }
            break;
        }
        if (p.is_eof && p.lines.size() <= static_cast<size_t>(m_viewport_rows)) {
            page_idx = std::min<s64>(page_idx, last_idx);
            break;
        }
        m_page_offsets.push_back(p.logical_end_offset);
        m_page_start_lines.push_back(p.logical_end_line);
        if (p.is_eof) {
            page_idx = std::min<s64>(page_idx, static_cast<s64>(m_page_offsets.size()) - 1);
            break;
        }
    }

    if (m_load_failed) {
        return;
    }

    page_idx = std::clamp<s64>(page_idx, 0, static_cast<s64>(m_page_offsets.size()) - 1);
    m_current_page = page_idx;

    if (!m_page_cache.contains(page_idx)) {
        auto p = text_helper::ReadPage(read_func, m_file_size, m_page_offsets[page_idx], m_page_start_lines[page_idx], m_buffer_rows, m_viewport_rows);
        if (p.is_error) {
            if (!m_load_failed) {
                m_load_result = FsError_InvalidSize;
                m_load_failed = true;
            }
            return;
        }
        p.page_index = page_idx;
        m_page_cache[page_idx] = std::move(p);

        while (m_page_cache.size() > 4) {
            auto furthest = m_page_cache.begin();
            s64 max_dist = -1;
            for (auto it = m_page_cache.begin(); it != m_page_cache.end(); ++it) {
                const s64 dist = std::abs(it->first - m_current_page);
                if (dist > max_dist) {
                    max_dist = dist;
                    furthest = it;
                }
            }
            m_page_cache.erase(furthest);
        }
    }

    const auto& cur_page = m_page_cache[page_idx];
    m_lines = cur_page.lines;
    m_stream_start_line = cur_page.start_line;
}

void Menu::PreloadPages() {
    if (!m_is_streamed || m_load_failed) {
        return;
    }

    auto read_func = [this](int64_t off, char* buf, int64_t sz) -> int64_t {
        u64 bytes_read = 0;
        Result rc = m_file.Read(off, buf, sz, 0, &bytes_read);
        if (R_FAILED(rc)) {
            m_load_result = rc;
            m_load_failed = true;
            return 0;
        }
        if (bytes_read == 0 && sz > 0) {
            m_load_result = FsError_InvalidSize;
            m_load_failed = true;
            return 0;
        }
        return static_cast<int64_t>(bytes_read);
    };

    for (s64 step = 1; step <= 2; step++) {
        if (m_load_failed) {
            break;
        }

        const s64 next_page = m_current_page + step;
        if (m_page_cache.contains(next_page)) {
            continue;
        }

        const s64 prev_page = next_page - 1;
        if (m_page_cache.contains(prev_page) && m_page_cache[prev_page].is_eof) {
            break;
        }

        while (static_cast<s64>(m_page_offsets.size()) <= next_page) {
            const s64 last_idx = static_cast<s64>(m_page_offsets.size()) - 1;
            const s64 last_offset = m_page_offsets[last_idx];
            const s64 last_line = m_page_start_lines[last_idx];
            const auto p = text_helper::ReadPage(read_func, m_file_size, last_offset, last_line, m_buffer_rows, m_viewport_rows);
            if (p.is_error || p.logical_end_offset <= last_offset) {
                if (!m_load_failed) {
                    m_load_result = FsError_InvalidSize;
                    m_load_failed = true;
                }
                break;
            }
            if (p.is_eof && p.lines.size() <= static_cast<size_t>(m_viewport_rows)) {
                break;
            }
            m_page_offsets.push_back(p.logical_end_offset);
            m_page_start_lines.push_back(p.logical_end_line);
            if (p.is_eof) {
                break;
            }
        }

        if (m_load_failed) {
            break;
        }

        if (next_page < static_cast<s64>(m_page_offsets.size())) {
            auto p = text_helper::ReadPage(read_func, m_file_size, m_page_offsets[next_page], m_page_start_lines[next_page], m_buffer_rows, m_viewport_rows);
            if (p.is_error) {
                if (!m_load_failed) {
                    m_load_result = FsError_InvalidSize;
                    m_load_failed = true;
                }
                break;
            }
            p.page_index = next_page;
            m_page_cache[next_page] = std::move(p);
            while (m_page_cache.size() > 4) {
                auto furthest = m_page_cache.begin();
                s64 max_dist = -1;
                for (auto it = m_page_cache.begin(); it != m_page_cache.end(); ++it) {
                    const s64 dist = std::abs(it->first - m_current_page);
                    if (dist > max_dist) {
                        max_dist = dist;
                        furthest = it;
                    }
                }
                m_page_cache.erase(furthest);
            }
        }
    }
}

void Menu::PageUp(s64 count) {
    if (m_is_streamed) {
        if (m_current_page > 0) {
            LoadPage(std::max<s64>(0, m_current_page - count));
            if (m_text_list) {
                m_text_list->SetYoff(0.f);
            }
            m_line_index = 0;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        } else if (m_text_list && m_text_list->GetYoff() > 0.f) {
            m_text_list->SetYoff(0.f);
            m_line_index = 0;
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    } else {
        if (m_text_list) {
            const float step = m_text_list->GetMaxY();
            const float page_shift = static_cast<float>(count * m_viewport_rows) * step;
            const float next_y = std::max(0.f, m_text_list->GetYoff() - page_shift);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::PageDown(s64 count) {
    if (m_is_streamed) {
        if (!m_page_cache[m_current_page].is_eof) {
            LoadPage(m_current_page + count);
            if (m_text_list) {
                m_text_list->SetYoff(0.f);
            }
            m_line_index = 0;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    } else {
        if (m_text_list) {
            const s64 total = static_cast<s64>(m_lines.size());
            const float step = m_text_list->GetMaxY();
            const float y_max = (total > m_viewport_rows) ? static_cast<float>(total - m_viewport_rows) * step : 0.f;
            const float page_shift = static_cast<float>(count * m_viewport_rows) * step;
            const float next_y = std::min(y_max, m_text_list->GetYoff() + page_shift);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::LineUp() {
    const float step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    if (m_is_streamed) {
        const s64 cur_row = (step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / step)) : 0;
        if (cur_row > 0) {
            const float next_y = std::max(0.f, (cur_row - 1) * step);
            if (m_text_list) {
                m_text_list->SetYoff(next_y);
            }
            m_line_index = cur_row - 1;
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        } else if (m_current_page > 0) {
            LoadPage(m_current_page - 1);
            const s64 target_row = std::max<s64>(0, m_viewport_rows - 1);
            if (m_text_list) {
                m_text_list->SetYoff(target_row * step);
            }
            m_line_index = target_row;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    } else {
        if (m_text_list) {
            const s64 count = static_cast<s64>(m_lines.size());
            const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * step : 0.f;
            const float next_y = std::clamp(m_text_list->GetYoff() - step, 0.f, y_max);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::LineDown() {
    const float step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    if (m_is_streamed) {
        const s64 cur_row = (step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / step)) : 0;
        const s64 count = static_cast<s64>(m_lines.size());
        const bool at_eof = m_page_cache[m_current_page].is_eof;

        if (cur_row + 1 < m_viewport_rows) {
            if (cur_row + 1 + m_viewport_rows <= count || (at_eof && cur_row + 1 < count)) {
                const float next_y = (cur_row + 1) * step;
                if (m_text_list) {
                    m_text_list->SetYoff(next_y);
                }
                m_line_index = cur_row + 1;
                App::PlaySoundEffect(SoundEffect_Scroll);
                UpdateTextSubHeading();
            }
        } else if (!at_eof) {
            LoadPage(m_current_page + 1);
            if (m_text_list) {
                m_text_list->SetYoff(0.f);
            }
            m_line_index = 0;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        } else {
            const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * step : 0.f;
            if (m_text_list->GetYoff() + step <= y_max + 1.f) {
                const float next_y = std::min(m_text_list->GetYoff() + step, y_max);
                m_text_list->SetYoff(next_y);
                m_line_index = static_cast<s64>(std::round(next_y / step));
                App::PlaySoundEffect(SoundEffect_Scroll);
                UpdateTextSubHeading();
            }
        }
    } else {
        if (m_text_list) {
            const s64 count = static_cast<s64>(m_lines.size());
            const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * step : 0.f;
            const float next_y = std::clamp(m_text_list->GetYoff() + step, 0.f, y_max);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::ZoomText(float delta) {
    const float new_size = std::clamp(m_font_size + delta, 12.f, 32.f);
    if (std::abs(new_size - m_font_size) < 0.1f) {
        return;
    }

    const float old_step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    const s64 old_visible_row = (old_step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / old_step)) : 0;

    m_font_size = new_size;
    RecreateList();

    if (m_is_streamed) {
        s64 current_offset = 0;
        if (m_current_page < static_cast<s64>(m_page_offsets.size())) {
            current_offset = m_page_offsets[m_current_page];
        }
        s64 current_line = 1;
        if (m_current_page < static_cast<s64>(m_page_start_lines.size())) {
            current_line = m_page_start_lines[m_current_page];
        }
        m_page_offsets = {current_offset};
        m_page_start_lines = {current_line};
        m_page_cache.clear();
        m_current_page = 0;
        LoadPage(0);
        if (m_text_list) {
            m_text_list->SetYoff(0.f);
        }
        m_line_index = 0;
        PreloadPages();
    } else {
        const float new_step = m_text_list ? m_text_list->GetMaxY() : 30.f;
        const s64 count = static_cast<s64>(m_lines.size());
        const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * new_step : 0.f;
        const float next_y = std::clamp(static_cast<float>(old_visible_row) * new_step, 0.f, y_max);
        if (m_text_list) {
            m_text_list->SetYoff(next_y);
        }
        m_line_index = static_cast<s64>(std::round(next_y / new_step));
    }

    UpdateTextSubHeading();
}

auto Menu::BuildText() const -> std::string {
    std::string out;
    for (size_t i = 0; i < m_lines.size(); i++) {
        out += m_lines[i];
        if (i + 1 < m_lines.size()) {
            out += m_line_break;
        }
    }
    return out;
}

void Menu::PushUndo() {
    constexpr size_t MAX_UNDO = 32;

    m_undo.emplace_back(m_lines);
    if (m_undo.size() > MAX_UNDO) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
}

void Menu::Undo() {
    if (m_undo.empty()) {
        App::Notify("Nothing to undo"_i18n);
        return;
    }

    m_redo.emplace_back(m_lines);
    m_lines = m_undo.back();
    m_undo.pop_back();
    m_text_dirty = (BuildText() != m_saved_text);
    m_line_index = std::min<s64>(m_line_index, m_lines.size() - 1);
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    UpdateTextSubHeading();
}
void Menu::Redo() {
    if (m_redo.empty()) {
        App::Notify("Nothing to redo"_i18n);
        return;
    }

    m_undo.emplace_back(m_lines);
    m_lines = m_redo.back();
    m_redo.pop_back();
    m_text_dirty = (BuildText() != m_saved_text);
    m_line_index = std::min<s64>(m_line_index, m_lines.size() - 1);
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    UpdateTextSubHeading();
}

void Menu::UpdateTextSubHeading() {
    const float step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    const s64 cur_row = (step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / step)) : 0;

    if (m_is_streamed) {
        auto heading = "Page "_i18n + std::to_string(m_current_page + 1);
        heading += "  |  " + "Line "_i18n + std::to_string(m_stream_start_line + cur_row);
        if (m_file_size > 0 && m_current_page < static_cast<s64>(m_page_offsets.size())) {
            const s64 offset = m_page_offsets[m_current_page];
            const s64 pct = std::clamp<s64>((offset * 100) / m_file_size, 0, 100);
            heading += "  |  " + std::to_string(pct) + "%";
        }
        if (!m_writable) {
            heading += "  (" + "Read-only"_i18n + ")";
        } else {
            heading += "  (" + "View"_i18n + ")";
        }
        SetSubHeading(heading);
        return;
    }

    const s64 display_line = m_editable ? (m_line_index + 1) : (cur_row + 1);
    auto heading = std::to_string(display_line) + " / " + std::to_string(m_lines.size());
    if (m_mode == TextMode::View) {
        if (!m_writable) {
            heading += "  (" + "Read-only"_i18n + ")";
        } else {
            heading += "  (" + "View"_i18n + ")";
        }
    }
    if (m_text_dirty) {
        heading += "  *";
    }
    SetSubHeading(heading);
}

void Menu::DrawText(NVGcontext* vg, Theme* theme) {
    if (!m_text_list) {
        return;
    }

    const auto gutter = m_is_streamed
        ? std::to_string(m_stream_start_line + static_cast<s64>(m_lines.size()))
        : std::to_string(m_lines.size());
    float bounds[4];
    nvgFontSize(vg, m_font_size);
    gfx::textBounds(vg, 0, 0, bounds, gutter.c_str());
    const float gutter_w = bounds[2] - bounds[0] + 16.f;
    const bool is_ini = text_helper::IsIniFile(m_path);
    const bool has_sel = HasSelection();
    const auto [sel_start, sel_end] = GetTargetRange();
    bool sel_box_drawn = false;

    const auto focus_index = (m_editable && !has_sel) ? m_line_index : List::NO_FOCUS;
    m_text_list->Draw(vg, theme, m_lines.size(), focus_index, [this, gutter_w, is_ini, has_sel, sel_start, sel_end, &sel_box_drawn](auto* vg, auto* theme, const Vec4& pos, s64 index){
        const auto focused = (m_line_index == index);
        const auto in_range = has_sel && (index >= sel_start && index <= sel_end);

        if (in_range && !sel_box_drawn) {
            sel_box_drawn = true;
            Vec4 span = pos;
            span.h = static_cast<float>(sel_end - index) * m_text_list->GetMaxY() + pos.h;
            gfx::drawRectOutline(vg, theme, 4.f, span);
        } else if (focused && m_editable && !has_sel) {
            gfx::drawRectOutline(vg, theme, 4.f, pos);
        }

        const s64 display_line_num = m_is_streamed ? (m_stream_start_line + index) : (index + 1);
        const float gutter_font_size = std::max(10.f, m_font_size - 2.f);
        gfx::drawTextArgs(vg, pos.x + gutter_w - 8.f, pos.y + pos.h / 2.f, gutter_font_size,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%ld", static_cast<long>(display_line_num));

        const auto colour = theme->GetColour((focused && m_editable) ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        const auto text_x = pos.x + gutter_w;
        const auto text_w = pos.w - gutter_w - 10.f;

        if (focused && m_editable && !is_ini) {
            m_line_scroll.Draw(vg, true, text_x, pos.y + pos.h / 2.f, text_w, m_font_size,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, m_lines[index]);
        } else if (is_ini) {
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, pos.y, text_w, pos.h);

            const auto info = text_helper::ParseIniLine(m_lines[index]);
            if (info.type == text_helper::IniLineType::Comment) {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", m_lines[index].c_str());
            } else if (info.type == text_helper::IniLineType::Section) {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "%s", m_lines[index].c_str());
            } else if (info.type == text_helper::IniLineType::KeyValue) {
                const std::string key_str(info.key);
                const std::string eq_str(info.eq);
                const std::string val_str(info.val);
                float key_bounds[4];
                nvgFontSize(vg, m_font_size);
                gfx::textBounds(vg, text_x, pos.y + pos.h / 2.f, key_bounds, key_str.c_str());
                const float key_w = key_bounds[2] - key_bounds[0];

                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_HIGHLIGHT_1), "%s", key_str.c_str());

                float eq_bounds[4];
                gfx::textBounds(vg, text_x + key_w, pos.y + pos.h / 2.f, eq_bounds, eq_str.c_str());
                const float eq_w = eq_bounds[2] - eq_bounds[0];

                gfx::drawTextArgs(vg, text_x + key_w, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", eq_str.c_str());

                gfx::drawTextArgs(vg, text_x + key_w + eq_w, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", val_str.c_str());
            } else {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", m_lines[index].c_str());
            }
            nvgRestore(vg);
        } else {
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, pos.y, text_w, pos.h);
            gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", m_lines[index].c_str());
            nvgRestore(vg);
        }
    });
}

} // namespace sphaira::ui::menu::fileview
