#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include <algorithm>

namespace sphaira::ui {

namespace {

// a small filled cloud drawn with vector paths so it renders regardless of the
// loaded font. cx/cy is the centre; s scales the whole shape.
void DrawCloudIcon(NVGcontext* vg, float cx, float cy, float s, NVGcolor colour) {
    nvgBeginPath(vg);
    nvgCircle(vg, cx - 0.9f * s, cy + 0.15f * s, 0.55f * s);
    nvgCircle(vg, cx - 0.1f * s, cy - 0.35f * s, 0.75f * s);
    nvgCircle(vg, cx + 0.85f * s, cy + 0.05f * s, 0.6f * s);
    nvgRect(vg, cx - 0.9f * s, cy + 0.1f * s, 1.75f * s, 0.6f * s);
    nvgFillColor(vg, colour);
    nvgFill(vg);
}

// right-pointing chevron matching SidebarEntryCallback's submenu hint.
void DrawChevron(NVGcontext* vg, float x, float y, NVGcolor colour) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, x - 8.f, y - 8.f);
    nvgLineTo(vg, x, y);
    nvgLineTo(vg, x - 8.f, y + 8.f);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 3.f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

} // namespace

PopupList::PopupList(std::string title, Items items, std::string& index_str_ref, s64& index_ref)
: PopupList{std::move(title), std::move(items), Callback{}, index_ref}  {

    m_callback = [&index_str_ref, &index_ref, this](auto op_idx) {
        if (op_idx) {
            index_ref = *op_idx;
            index_str_ref = m_items[index_ref];
        }
    };
}

PopupList::PopupList(std::string title, Items items, std::string& index_ref)
: PopupList{std::move(title), std::move(items), Callback{}}  {

    const auto it = std::find(m_items.cbegin(), m_items.cend(), index_ref);
    if (it != m_items.cend()) {
        m_index = std::distance(m_items.cbegin(), it);
        if (m_index >= 6) {
            m_list->SetYoff((m_index - 5) * m_list->GetMaxY());
        }
    }

    m_callback = [&index_ref, this](auto op_idx) {
        if (op_idx) {
            index_ref = m_items[*op_idx];
        }
    };
}

PopupList::PopupList(std::string title, Items items, s64& index_ref)
: PopupList{std::move(title), std::move(items), Callback{}, index_ref}  {

    m_callback = [&index_ref, this](auto op_idx) {
        if (op_idx) {
            index_ref = *op_idx;
        }
    };
}

PopupList::PopupList(std::string title, Items items, Callback cb, std::string index)
: PopupList{std::move(title), std::move(items), cb, 0} {

    const auto it = std::find(m_items.cbegin(), m_items.cend(), index);
    if (it != m_items.cend()) {
        SetIndex(std::distance(m_items.cbegin(), it));
        if (m_index >= 6) {
            m_list->SetYoff((m_index - 5) * m_list->GetMaxY());
        }
    }
}

PopupList::PopupList(std::string title, Items items, Callback cb, s64 index)
: m_title{std::move(title)}
, m_items{std::move(items)}
, m_callback{cb}
, m_index{index} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            if (m_callback) {
                m_callback(m_index);
            }
            SetPop();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_starting_index = m_index;

    m_pos.w = SCREEN_WIDTH;
    const float a = std::min(370.f, (60.f * static_cast<float>(m_items.size())));
    m_pos.h = 80.f + 140.f + a;
    m_pos.y = SCREEN_HEIGHT - m_pos.h;
    m_line_top = m_pos.y + 70.f;
    m_line_bottom = SCREEN_HEIGHT - 73.f;

    Vec4 v{m_block};
    v.y = m_line_top + 1.f + 42.f;
    const Vec4 pos{0, m_line_top, SCREEN_WIDTH, m_line_bottom - m_line_top};
    m_list = std::make_unique<List>(1, 6, pos, v);
    m_list->SetScrollBarPos(1250, m_line_top + 20, m_line_bottom - m_line_top - 40);

    if (m_index >= 6) {
        m_list->SetYoff((m_index - 5) * m_list->GetMaxY());
    }
}

auto PopupList::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (!m_items.empty()) {
        if (controller->GotDown(Button::DOWN) && m_index == static_cast<s64>(m_items.size() - 1)) {
            SetIndex(0);
            m_list->SetYoff(0);
            App::PlaySoundEffect(SoundEffect_Scroll);
            return;
        }
        if (controller->GotDown(Button::UP) && m_index == 0) {
            const auto target = static_cast<s64>(m_items.size() - 1);
            SetIndex(target);
            if (m_items.size() >= 6) {
                m_list->SetYoff((target - 5) * m_list->GetMaxY());
            } else {
                m_list->SetYoff(0);
            }
            App::PlaySoundEffect(SoundEffect_Scroll);
            return;
        }
    }

    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        SetIndex(i);
        if (touch) {
            FireAction(Button::A);
        }
    }, this);
}

auto PopupList::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP));
    gfx::drawText(vg, m_pos + m_title_pos, 24.f, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
    gfx::drawRect(vg, 30.f, m_line_top, m_line_width, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, 30.f, m_line_bottom, m_line_width, 1.f, theme->GetColour(ThemeEntryID_LINE));

    const bool has_markers = m_markers.size() == m_items.size();
    const bool has_icons = m_icons.size() == m_items.size();
    const float marker_gutter = has_markers ? 30.f : 0.f;
    const float icon_gutter = has_icons ? 34.f : 0.f;
    const float gutter = marker_gutter + icon_gutter;

    m_list->Draw(vg, theme, m_items.size(), [this, has_markers, has_icons, marker_gutter, gutter](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        auto colour = ThemeEntryID_TEXT;
        const auto selected = m_index == i;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_items.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        const auto mid_y = y + (h / 2.f);

        if (m_menu_style) {
            // action menu: no value tick, a right chevron per row.
            DrawChevron(vg, x + w - 24.f, mid_y, theme->GetColour(ThemeEntryID_TEXT));
        } else if (m_starting_index == i) {
            colour = ThemeEntryID_TEXT_SELECTED;
            gfx::drawText(vg, x + w - m_text_xoffset, mid_y, 20.f, "\uE14B", NULL, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(colour));
        }

        // cloud marker sits in the reserved left gutter so text stays aligned.
        if (has_markers && m_markers[i]) {
            DrawCloudIcon(vg, x + m_text_xoffset + 8.f, mid_y, 7.f, theme->GetColour(colour));
        }

        if (has_icons && m_icons[i]) {
            gfx::drawActionIcon(vg, theme, x + m_text_xoffset + marker_gutter, mid_y - 12.f, 24.f, *m_icons[i]);
        }

        const auto text_x = x + m_text_xoffset + gutter;
        const auto text_clip_w = w - 60.f - m_text_xoffset - gutter;
        m_scroll_text.Draw(vg, selected, text_x, mid_y, text_clip_w, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(colour), m_items[i]);
    });

    Widget::Draw(vg, theme);
}

auto PopupList::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto PopupList::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

void PopupList::SetIndex(s64 index) {
    m_index = index;
}

} // namespace sphaira::ui
