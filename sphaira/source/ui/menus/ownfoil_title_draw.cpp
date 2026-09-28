#include "ui/menus/ownfoil_title_internal.hpp"

namespace sphaira::ui::menu::ownfoil {

void TitleMenu::Layout() {
    const auto vg = App::GetVg();
    const auto measure = [vg](const std::string& text, float size, float line) -> float {
        if (text.empty()) {
            return 0;
        }

        nvgSave(vg);
        nvgFontSize(vg, size);
        nvgTextLineHeight(vg, line / size);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        float bounds[4]{};
        nvgTextBoxBounds(vg, 0, 0, PAGE_W, text.c_str(), nullptr, bounds);
        nvgRestore(vg);
        return bounds[3] - bounds[1];
    };

    auto y = GetImagesBottom();

    // the tagline heads the description, in place of a heading of its own.
    m_intro_h = measure(m_intro, INTRO_SIZE, INTRO_LINE);
    m_desc_h = measure(m_title.description, TEXT_SIZE, TEXT_LINE);
    if (m_intro_h > 0 || m_desc_h > 0) {
        m_text_y = y + SECTION_GAP;
        m_desc_y = m_text_y + m_intro_h + (m_intro_h > 0 && m_desc_h > 0 ? INTRO_GAP : 0);
        y = m_desc_y + m_desc_h;
    }

    if (!m_rows.empty()) {
        m_rows_y = y + SECTION_GAP;
        y = m_rows_y + HEADING_H + m_rows.size() * DLC_ROW_H;
    }

    m_content_h = y;
    ScrollTo(m_scroll_target);
    m_scroll = std::min(m_scroll, m_scroll_target);
}

auto TitleMenu::GetImagesBottom() const -> float {
    return GetImageCount() > 1 ? IMAGE_H + THUMB_GAP_Y + THUMB_H : IMAGE_H;
}

auto TitleMenu::GetMaxScroll() const -> float {
    return std::max(0.f, m_content_h + PAGE_PAD - VIEW_H);
}

void TitleMenu::ScrollTo(float y) {
    m_scroll_target = std::clamp(y, 0.f, GetMaxScroll());
}

auto TitleMenu::ImagesInView() const -> bool {
    return m_scroll_target < IMAGE_H / 2.f;
}

auto TitleMenu::GetRowTop(s64 index) const -> float {
    return m_rows_y + HEADING_H + index * DLC_ROW_H;
}

auto TitleMenu::RowInView(s64 index) const -> bool {
    const auto top = GetRowTop(index);
    return top >= m_scroll_target && top + DLC_ROW_H <= m_scroll_target + VIEW_H;
}

auto TitleMenu::FirstRowInView() const -> s64 {
    for (s64 i = 0; i < static_cast<s64>(m_rows.size()); i++) {
        if (RowInView(i)) {
            return i;
        }
    }
    return -1;
}

auto TitleMenu::GetRowRect(s64 index) const -> Vec4 {
    return Vec4{PAGE_X, PAGE_Y - m_scroll + GetRowTop(index), PAGE_W, DLC_ROW_H};
}

void TitleMenu::StepDown() {
    if (m_focus >= 0) {
        if (m_focus + 1 < static_cast<s64>(m_rows.size())) {
            App::PlaySoundEffect(SoundEffect_Scroll);
            SetFocus(m_focus + 1);
        }
        return;
    }

    // the rows take the highlight as soon as one is wholly on screen - the first
    // of them, wherever the stick left the page. at the bottom of the page the
    // last row always is.
    if (const auto first = FirstRowInView(); first >= 0) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        SetFocus(first);
        return;
    }

    // a step at a time, until the page can go no further.
    const auto target = std::min(m_scroll_target + SCROLL_STEP, GetMaxScroll());
    if (target > m_scroll_target) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        m_stick_scroll = false;
        ScrollTo(target);
    }
}

void TitleMenu::StepUp() {
    if (m_focus > 0) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        SetFocus(m_focus - 1);
        return;
    }

    // up off the first row is back to the text, which is already on screen.
    if (m_focus == 0) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        SetFocus(-1);
        return;
    }

    // the stick left the page past the first row, so up takes the highlight at the
    // first on screen and walks up from there.
    if (const auto first = FirstRowInView(); first > 0) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        SetFocus(first);
        return;
    }

    if (m_scroll_target <= 0) {
        return;
    }

    App::PlaySoundEffect(SoundEffect_Scroll);
    m_stick_scroll = false;
    ScrollTo(m_scroll_target - SCROLL_STEP);
}

void TitleMenu::SetFocus(s64 index) {
    m_focus = index;
    if (index < 0) {
        return;
    }

    // the first row brings its heading on screen with it.
    const auto top = index == 0 ? m_rows_y : GetRowTop(index);
    const auto bottom = GetRowTop(index) + DLC_ROW_H;
    m_stick_scroll = false;
    // a row below comes up with the page's padding under it: flush with the bar,
    // its outline would be cut off.
    if (top < m_scroll_target) {
        ScrollTo(top);
    } else if (bottom + PAGE_PAD > m_scroll_target + VIEW_H) {
        ScrollTo(bottom + PAGE_PAD - VIEW_H);
    }
}

void TitleMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    DrawPage(vg, theme);
    DrawColumn(vg, theme);
}

void TitleMenu::DrawPage(NVGcontext* vg, Theme* theme) {
    // drawn at its place on the page less the scroll, clipped to the space between
    // the heading's line and the bottom bar, and skipped when wholly outside it.
    const auto top = PAGE_Y - m_scroll;
    const auto on_screen = [top](float y, float h) {
        return top + y + h > CLIP_Y && top + y < CLIP_BOTTOM;
    };

    nvgSave(vg);
    nvgIntersectScissor(vg, PAGE_X - CLIP_BLEED, CLIP_Y, PAGE_W + CLIP_BLEED * 2.f, CLIP_BOTTOM - CLIP_Y);

    const auto count = GetImageCount();
    const Vec4 image_pos{PAGE_X, top, PAGE_W, IMAGE_H};

    if (on_screen(0, GetImagesBottom())) {
        if (count > 0) {
            // an image still on its way holds the space it will fill.
            if (const auto image = GetImage(m_image_index)) {
                gfx::drawImage(vg, image_pos, image, IMAGE_ROUNDING);
            } else {
                gfx::drawRect(vg, image_pos, theme->GetColour(ThemeEntryID_GRID), IMAGE_ROUNDING);
            }

            if (count > 1) {
                // the strip scrolls to keep the current image in view once there
                // are more than fit.
                const auto first = std::clamp<s64>(m_image_index - THUMB_COUNT / 2, 0, std::max<s64>(0, count - THUMB_COUNT));
                const auto last = std::min(count, first + THUMB_COUNT);

                for (s64 i = first; i < last; i++) {
                    const Vec4 v{PAGE_X + (i - first) * (THUMB_W + THUMB_GAP), top + IMAGE_H + THUMB_GAP_Y, THUMB_W, THUMB_H};
                    const auto current = i == m_image_index;

                    // first, as the catalog draws it: the outline lays a shadow
                    // over the rect it surrounds, which would blank the thumbnail.
                    if (current) {
                        gfx::drawRectOutline(vg, theme, 3.f, v);
                    }

                    if (const auto thumb = GetImage(i)) {
                        gfx::drawImage(vg, v, thumb, 4.f, current ? 1.f : 0.5f);
                    } else {
                        gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_GRID), 4.f);
                    }
                }
            }
        } else {
            // nothing to show but an icon: the game's own, where the banner would be.
            gfx::drawRect(vg, image_pos, theme->GetColour(ThemeEntryID_GRID), IMAGE_ROUNDING);
            const Vec4 v{image_pos.x + (image_pos.w - ICON_SIZE) / 2.f, image_pos.y + (image_pos.h - ICON_SIZE) / 2.f, ICON_SIZE, ICON_SIZE};
            gfx::drawImage(vg, v, m_icon ? m_icon : App::GetDefaultImage(), 14.f);
        }
    }

    if (!m_intro.empty() && on_screen(m_text_y, m_intro_h)) {
        nvgSave(vg);
        nvgTextLineHeight(vg, INTRO_LINE / INTRO_SIZE);
        gfx::drawTextBox(vg, PAGE_X, top + m_text_y, INTRO_SIZE, PAGE_W, theme->GetColour(ThemeEntryID_TEXT), m_intro.c_str());
        nvgRestore(vg);
    }

    if (!m_title.description.empty() && on_screen(m_desc_y, m_desc_h)) {
        nvgSave(vg);
        nvgTextLineHeight(vg, TEXT_LINE / TEXT_SIZE);
        gfx::drawTextBox(vg, PAGE_X, top + m_desc_y, TEXT_SIZE, PAGE_W, theme->GetColour(ThemeEntryID_TEXT), m_title.description.c_str());
        nvgRestore(vg);
    }

    const auto rows = static_cast<s64>(m_rows.size());
    if (rows > 0 && on_screen(m_rows_y, HEADING_H + rows * DLC_ROW_H)) {
        const auto heading = "Add-on content"_i18n;
        const auto heading_mid = top + m_rows_y + (HEADING_H - INTRO_GAP) / 2.f;

        nvgFontSize(vg, HEADING_SIZE);
        const auto heading_w = nvgTextBounds(vg, 0, 0, heading.c_str(), nullptr, nullptr);
        gfx::drawTextArgs(vg, PAGE_X, heading_mid, HEADING_SIZE, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%s", heading.c_str());
        gfx::drawTextArgs(vg, PAGE_X + heading_w + 12.f, heading_mid, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%ld", rows);

        for (s64 i = 0; i < rows; i++) {
            const auto v = GetRowRect(i);
            if (v.y + v.h <= CLIP_Y || v.y >= CLIP_BOTTOM) {
                continue;
            }

            const auto& row = m_rows[i];
            const auto selected = i == m_focus;

            // the highlight goes down before what is drawn on it.
            if (selected) {
                gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND));
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                gfx::drawRect(vg, v.x, v.y + v.h - 1.f, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }

            const Vec4 art{v.x + 6.f, v.y + (v.h - DLC_IMAGE_H) / 2.f, DLC_IMAGE_W, DLC_IMAGE_H};
            if (row.image) {
                gfx::drawImage(vg, art, row.image, 4.f);
            } else {
                gfx::drawRect(vg, art, theme->GetColour(ThemeEntryID_GRID), 4.f);
            }

            const auto text_x = art.x + art.w + 18.f;
            auto text_right = v.x + v.w - 16.f;
            const auto mid = v.y + v.h / 2.f;

            // tagged the way the server list tags a server nobody has saved yet.
            if (row.installed) {
                const auto tag = "Installed"_i18n;
                nvgFontSize(vg, 16.f);
                const auto tag_w = nvgTextBounds(vg, 0, 0, tag.c_str(), nullptr, nullptr) + 20.f;
                const Vec4 t{text_right - tag_w, mid - 14.f, tag_w, 28.f};
                gfx::drawRect(vg, t, theme->GetColour(ThemeEntryID_HIGHLIGHT_1), 5.f);
                gfx::drawTextArgs(vg, t.x + t.w / 2.f, t.y + t.h / 2.f, 16.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_BACKGROUND), "%s", tag.c_str());
                text_right = t.x - 16.f;
            }

            const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
            const auto info_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO;

            // a long name or tagline is cut short of the tag.
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, v.y, std::max(0.f, text_right - text_x), v.h);
            if (row.tagline.empty()) {
                gfx::drawTextArgs(vg, text_x, mid, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s", row.name.c_str());
            } else {
                gfx::drawTextArgs(vg, text_x, mid - 12.f, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s", row.name.c_str());
                gfx::drawTextArgs(vg, text_x, mid + 14.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(info_id), "%s", row.tagline.c_str());
            }
            nvgRestore(vg);
        }
    }

    nvgRestore(vg);

    // a bar beside a page that runs longer than the screen, showing how long it
    // is and where in it the view is.
    if (const auto max = GetMaxScroll(); max > 0) {
        const auto bar_h = std::max(24.f, SCROLLBAR_H * VIEW_H / (m_content_h + PAGE_PAD));
        const auto bar_y = PAGE_Y + (SCROLLBAR_H - bar_h) * std::clamp(m_scroll / max, 0.f, 1.f);
        gfx::drawRect(vg, SCROLLBAR_X, PAGE_Y, 4.f, SCROLLBAR_H, theme->GetColour(ThemeEntryID_SCROLLBAR_BACKGROUND), 2.f);
        gfx::drawRect(vg, SCROLLBAR_X, bar_y, 4.f, bar_h, theme->GetColour(ThemeEntryID_SCROLLBAR), 2.f);
    }
}

void TitleMenu::DrawColumn(NVGcontext* vg, Theme* theme) {
    auto y = COLUMN_Y;

    // the name wraps onto a second line rather than scrolling, and is cut there.
    {
        constexpr float size = 28.f;
        constexpr float line = 34.f;
        constexpr float max_h = line * 2.f;
        const auto& name = GetName();

        nvgSave(vg);
        nvgFontSize(vg, size);
        nvgTextLineHeight(vg, line / size);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

        float bounds[4]{};
        nvgTextBoxBounds(vg, COLUMN_X, y, COLUMN_W, name.c_str(), nullptr, bounds);

        nvgIntersectScissor(vg, COLUMN_X, y, COLUMN_W, max_h);
        gfx::drawTextBox(vg, COLUMN_X, y, size, COLUMN_W, theme->GetColour(ThemeEntryID_TEXT), name.c_str());
        nvgRestore(vg);

        y += std::min(bounds[3] - bounds[1], max_h) + 4.f;
    }

    if (const auto& publisher = GetPublisher(); !publisher.empty()) {
        gfx::drawTextArgs(vg, COLUMN_X, y, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", publisher.c_str());
        y += 26.f;
    }

    y += 20.f;

    if (m_page.dlc && !m_page.game_name.empty()) {
        constexpr float icon_size = 44.f;
        gfx::drawImage(vg, COLUMN_X, y, icon_size, icon_size, m_icon ? m_icon : App::GetDefaultImage(), 6.f);
        gfx::drawTextArgs(vg, COLUMN_X + icon_size + 12.f, y + icon_size / 2.f, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", ("Requires "_i18n + m_page.game_name).c_str());
        y += icon_size + 16.f;
    }

    for (const auto& fact : m_facts) {
        if (fact.group) {
            y += GROUP_GAP;
        }

        // a value too long for its row - a genre list, say - wraps onto a second
        // line, and the row grows to hold it. past that it is cut.
        const auto value_x = COLUMN_X + LABEL_W;
        const auto value_w = COLUMN_W - LABEL_W;
        nvgFontSize(vg, VALUE_SIZE);
        const auto wraps = nvgTextBounds(vg, 0, 0, fact.value.c_str(), nullptr, nullptr) > value_w;
        const auto row_h = wraps ? ROW_H + VALUE_LINE : ROW_H;

        const auto mid = y + row_h / 2.f;
        gfx::drawTextArgs(vg, COLUMN_X, mid, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", fact.label.c_str());

        nvgSave(vg);
        if (wraps) {
            nvgIntersectScissor(vg, value_x, mid - VALUE_LINE, value_w, VALUE_LINE * 2.f);
            nvgTextLineHeight(vg, VALUE_LINE / VALUE_SIZE);
            gfx::drawTextBox(vg, value_x, mid - VALUE_LINE, VALUE_SIZE, value_w, theme->GetColour(ThemeEntryID_TEXT), fact.value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, nullptr);
        } else {
            gfx::drawTextArgs(vg, COLUMN_X + COLUMN_W, mid, VALUE_SIZE, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%s", fact.value.c_str());
        }
        nvgRestore(vg);

        y += row_h;
        gfx::drawRect(vg, COLUMN_X, y - 1.f, COLUMN_W, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    }
}

} // namespace sphaira::ui::menu::ownfoil
