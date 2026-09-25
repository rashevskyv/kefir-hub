#include "ui/list.hpp"
#include "ui/list_draw_order.hpp"
#include "ui/layout.hpp"
#include "ui/nvg_util.hpp"
#include <algorithm>
#include <cmath>

namespace sphaira::ui {
namespace {

// extra scissor margin left around the list so the selection highlight
// (border + drop shadow drawn just outside each item rect) isn't clipped.
constexpr float SELECTION_PAD = gfx::SELECTION_OUTLINE_PAD;

// Clips a list to its own bounds, plus the selection margin - but never past
// the header/footer separators. Without the clamp that margin let the top and
// bottom rows of a scrolled list paint 10px into the chrome, which is how a
// half scrolled entry ended up sitting on the separator line.
void ScissorContent(NVGcontext* vg, const Vec4& pos) {
    const auto clip = layout::PaddedContentClipY(pos.y, pos.h, SELECTION_PAD);
    nvgIntersectScissor(vg, pos.x - SELECTION_PAD, clip.y, pos.w + SELECTION_PAD * 2.f, clip.h);
}

} // namespace

void List::Draw(NVGcontext* vg, Theme* theme, s64 count, s64 focus_index, Callback callback) const {
    switch (m_layout) {
        case Layout::HOME:
            DrawHome(vg, theme, count, focus_index, callback);
            break;
        case Layout::GRID:
            DrawGrid(vg, theme, count, focus_index, callback);
            break;
    }
}

void List::DrawHome(NVGcontext* vg, Theme* theme, s64 count, s64 focus_index, Callback callback) const {
    const auto yoff = ClampX(m_yoff + m_y_prog, count);
    auto v = m_v;
    v.x -= yoff;

    nvgSave(vg);
    // the selection highlight (drawRectOutline) draws its border + drop shadow a
    // few px OUTSIDE each item rect. clipping exactly at the list bounds cut the
    // highlight of edge items, making it look sunken; pad the scissor so it can
    // render on top. see drawRectOutlineInternal() in nvg_util.cpp.
    ScissorContent(vg, m_pos);

    draw_order::TraverseHome(
        v,
        v.w + m_pad.x,
        GetX(),
        GetX() + GetW(),
        count,
        focus_index,
        [&](const Vec4& item_v, s64 i) {
            callback(vg, theme, item_v, i);
        }
    );

    nvgRestore(vg);
}

void List::DrawGrid(NVGcontext* vg, Theme* theme, s64 count, s64 focus_index, Callback callback) const {
    const auto yoff = ClampY(m_yoff + m_y_prog, count);
    s64 start = yoff / GetMaxY() * m_row;
    if (m_gap_after >= 0 && count > m_gap_after + 1) {
        const float total_h = ((count + m_row - 1) / m_row) * GetMaxY() + m_gap_size;
        const float max_scroll = std::max(1.f, total_h - m_pos.h);
        const float max_start = std::max<float>(0.f, static_cast<float>(count - m_page));
        start = static_cast<s64>(std::round((yoff / max_scroll) * max_start));
    }
    gfx::drawScrollbar2(vg, theme, m_scrollbar.x, m_scrollbar.y, m_scrollbar.h, start, count, m_row, m_page);

    auto v = m_v;
    v.y -= yoff;

    nvgSave(vg);
    // pad the scissor so the selection highlight of edge items isn't clipped;
    // see the note in DrawHome().
    ScissorContent(vg, m_pos);

    if (m_gap_after >= 0 && count > m_gap_after + 1) {
        const float sep_y = v.y + ((m_gap_after / m_row) + 1) * (m_v.h + m_pad.y) + m_gap_size / 2.f;
        if (sep_y >= m_pos.y && sep_y <= m_pos.y + m_pos.h) {
            gfx::drawRect(vg, m_v.x, sep_y, m_v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }
    }

    draw_order::TraverseGrid(
        v,
        m_row,
        v.w + m_pad.x,
        v.h + m_pad.y,
        GetX(),
        GetX() + GetW(),
        GetY(),
        GetY() + GetH(),
        count,
        focus_index,
        [&](const Vec4& item_v, s64 i) {
            callback(vg, theme, item_v, i);
        },
        m_gap_after,
        m_gap_size
    );

    nvgRestore(vg);
}

} // namespace sphaira::ui
