#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>
#include <string>

namespace sphaira::ui::menu::settings {

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    gfx::drawRect(vg, 392.f, 118.f, 1.f, 504.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    const s64 category_focus = m_focus_pane == FocusPane::Categories ? (m_folder_open ? m_category_index + 1 : m_category_index) : List::NO_FOCUS;
    m_category_list->Draw(vg, theme, CategoryRowCount(), category_focus, [this](auto* vg, auto* theme, Vec4 v, auto row) {
        // while a folder is open it sits on its own row under its category,
        // indented, so the column shows where the right pane came from.
        const bool folder_row = m_folder_open && row == m_category_index + 1;
        const s64 category = (m_folder_open && row > m_category_index) ? row - 1 : row;
        const bool parent_row = m_folder_open && row == m_category_index;
        const bool selected = m_folder_open ? folder_row : (m_category_index == row);
        const auto focused = selected && m_focus_pane == FocusPane::Categories;
        const auto text_id = (selected || parent_row) ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const auto& label = folder_row ? m_folder_label : m_categories[category].label;

        if (selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        }
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        {
            const float indent = folder_row ? 30.f : 0.f;
            const float text_x = v.x + 18.f + indent;
            const float text_w = v.w - 36.f - indent;
            const float font_size = folder_row ? 18.f : 20.f;
            nvgFontSize(vg, font_size);
            nvgTextLineHeight(vg, 1.0f);
            float label_bounds[4];
            nvgTextBoxBounds(vg, text_x, 0, text_w, label.c_str(), nullptr, label_bounds);
            const float label_h = label_bounds[3] - label_bounds[1];
            const float label_y = v.y + (v.h - label_h) / 2.f;

            // a short elbow tying the indented row back to its category above.
            if (folder_row) {
                const float elbow_x = v.x + 26.f;
                const float mid_y = v.y + v.h / 2.f;
                nvgBeginPath(vg);
                nvgMoveTo(vg, elbow_x, v.y + 6.f);
                nvgLineTo(vg, elbow_x, mid_y);
                nvgLineTo(vg, elbow_x + 10.f, mid_y);
                nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_TEXT_INFO));
                nvgStrokeWidth(vg, 2.f);
                nvgLineCap(vg, NVG_ROUND);
                nvgLineJoin(vg, NVG_ROUND);
                nvgStroke(vg);
            }

            gfx::drawTextBox(
                vg, text_x, label_y, font_size, text_w,
                theme->GetColour(text_id), label.c_str()
            );
        }
    });

    if (m_categories.empty()) {
        return;
    }

    const auto& items = CurrentItems();
    const s64 item_focus = m_focus_pane == FocusPane::Items ? CurrentItemIndex() : List::NO_FOCUS;
    m_item_list->Draw(vg, theme, items.size(), item_focus, [this, &items](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto selected = CurrentItemIndex() == i;
        const auto focused = selected && m_focus_pane == FocusPane::Items;
        DrawItemRow(vg, theme, v, items[i], selected, focused);
    });
}

void Menu::DrawItemRow(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected, bool focused) {
    {
        // a section caption: a dimmed label with a rule running off to the
        // right, never highlighted because the cursor cannot land on it.
        if (item.kind == SettingsItemKind::Header) {
            const float text_x = v.x + 18.f;
            if (!item.label.empty()) {
                const auto colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
                const float text_y = v.y + v.h - 22.f;
                gfx::drawTextArgs(vg, text_x, text_y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", item.label.c_str());
                float bounds[4];
                nvgFontSize(vg, 15.f);
                gfx::textBounds(vg, 0, 0, bounds, item.label.c_str());
                const float rule_x = text_x + (bounds[2] - bounds[0]) + 12.f;
                gfx::drawRect(vg, rule_x, text_y + 9.f, std::max(0.f, v.x + v.w - 20.f - rule_x), 1.f,
                    theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
            return;
        }

        const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;

        if (selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        } else {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        DrawSettingsItemKindIcon(vg, theme, item, v, selected);
        const auto text_x = SettingsItemTextX(item, v.x);
        const auto text_offset = text_x - v.x;

        // one line each, never wrapped: a wrapped label would land on top of
        // the description below it. Long text scrolls while the row is
        // selected and is clipped otherwise.
        m_scroll_label.Draw(
            vg, selected, text_x, v.y + 10.f, v.w - 242.f - text_offset, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(label_id), item.label
        );
        if (!item.description.empty()) {
            m_scroll_description.Draw(
                vg, selected, text_x, v.y + 37.f, v.w - 212.f - text_offset, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), item.description
            );
        }

        if (item.value) {
            const auto value = item.value();
            gfx::drawText(
                vg, v.x + v.w - 20.f, v.y + 21.f, 18.f,
                SettingsValueColour(theme, value, selected),
                value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP
            );
        }

        if (item.kind == SettingsItemKind::Folder) {
            const float x1 = v.x + v.w - 24.f;
            const float y1 = v.y + v.h / 2.f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, x1 - 8.f, y1 - 8.f);
            nvgLineTo(vg, x1, y1);
            nvgLineTo(vg, x1 - 8.f, y1 + 8.f);
            nvgStrokeColor(vg, theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));
            nvgStrokeWidth(vg, 3.f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
        }
    }
}

} // namespace sphaira::ui::menu::settings
