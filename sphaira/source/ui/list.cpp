#include "ui/list.hpp"
#include "ui/widget.hpp"
#include "ui/layout.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "log.hpp"
#include <algorithm>

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

List::List(s64 row, s64 page, const Vec4& pos, const Vec4& v, const Vec2& pad)
: m_row{row}
, m_page{page}
, m_v{v}
, m_pad{pad} {
    m_pos = pos;
    // SetScrollBarPos(SCREEN_WIDTH - 50, 100, SCREEN_HEIGHT-200);
    SetScrollBarPos(pos.x + pos.w, 100, SCREEN_HEIGHT-200);
}

auto List::ClampX(float x, s64 count) const -> float {
    float x_max = 0;

    if (count >= m_page) {
        // round up
        if (count % m_row) {
            count = count + (m_row - count % m_row);
        }
        x_max = (count - m_page) / m_row * GetMaxX();
    }

    return std::clamp(x, 0.F, x_max);
}

auto List::ClampY(float y, s64 count) const -> float {
    float y_max = 0;

    if (count >= m_page) {
        // round up
        if (count % m_row) {
            count = count + (m_row - count % m_row);
        }
        y_max = (count - m_page) / m_row * GetMaxY();
    }

    return std::clamp(y, 0.F, y_max);
}

void List::OnUpdate(Controller* controller, TouchInfo* touch, s64 index, s64 count, TouchCallback callback, const Widget* widget) {
    switch (m_layout) {
        case Layout::HOME:
            OnUpdateHome(controller, touch, index, count, callback, widget);
            break;
        case Layout::GRID:
            OnUpdateGrid(controller, touch, index, count, callback, widget);
            break;
    }
}

void List::Draw(NVGcontext* vg, Theme* theme, s64 count, Callback callback) const {
    switch (m_layout) {
        case Layout::HOME:
            DrawHome(vg, theme, count, callback);
            break;
        case Layout::GRID:
            DrawGrid(vg, theme, count, callback);
            break;
    }
}

auto List::ScrollDown(s64& index, s64 step, s64 count) -> bool {
    const auto old_index = index;
    const auto max = m_layout == Layout::GRID ? GetMaxY() : GetMaxX();

    if (!count) {
        return false;
    }

    if (index + step < count) {
        index += step;
    } else {
        if (m_wrap) {
            index = 0;
        } else {
            index = count - 1;
        }
    }

    if (index != old_index) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        if (m_wrap && index == 0) {
            m_yoff = 0;
            return true;
        }
        s64 delta = index - old_index;
        s64 start = m_yoff / max * m_row;

        while (index < start) {
            start -= m_row;
            m_yoff -= max;
        }

        if (index - start >= m_page) {
            do {
                start += m_row;
                delta -= m_row;
                m_yoff += max;
            } while (delta > 0 && start + m_page < count);
        }

        return true;
    }

    return false;
}

auto List::ScrollUp(s64& index, s64 step, s64 count) -> bool {
    const auto old_index = index;
    const auto max = m_layout == Layout::GRID ? GetMaxY() : GetMaxX();

    if (!count) {
        return false;
    }

    if (index >= step) {
        index -= step;
    } else {
        if (m_wrap) {
            index = count - 1;
        } else {
            index = 0;
        }
    }

    if (index != old_index) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        if (m_wrap && index == count - 1) {
            m_yoff = m_layout == Layout::GRID ? ClampY(1e9f, count) : ClampX(1e9f, count);
            return true;
        }
        s64 start = m_yoff / max * m_row;

        while (index < start) {
            start -= m_row;
            m_yoff -= max;
        }

        while (index - start >= m_page && start + m_page < count) {
            start += m_row;
            m_yoff += max;
        }

        return true;
    }

    return false;
}

auto List::ScrollPageDown(s64& index, s64 count) -> bool {
    if (count <= m_page) {
        return false;
    }

    const auto step = std::max<s64>(m_row, m_page - m_row);
    const auto max = m_layout == Layout::GRID ? GetMaxY() : GetMaxX();
    const auto old_index = index;
    const auto old_yoff = m_yoff;
    auto start = static_cast<s64>(m_yoff / max) * m_row;

    index = std::min<s64>(start + step, count - 1);
    start = (index / m_row) * m_row;
    const auto next_yoff = static_cast<float>(start / m_row) * max;
    m_yoff = m_layout == Layout::GRID ? ClampY(next_yoff, count) : ClampX(next_yoff, count);

    if (index != old_index || m_yoff != old_yoff) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        return true;
    }

    return false;
}

auto List::ScrollPageUp(s64& index, s64 count) -> bool {
    if (count <= m_page) {
        return false;
    }

    const auto step = std::max<s64>(m_row, m_page - m_row);
    const auto max = m_layout == Layout::GRID ? GetMaxY() : GetMaxX();
    const auto old_index = index;
    const auto old_yoff = m_yoff;
    auto start = static_cast<s64>(m_yoff / max) * m_row;

    start = std::max<s64>(0, start - step);
    index = std::min<s64>(start, count - 1);
    const auto next_yoff = static_cast<float>(start / m_row) * max;
    m_yoff = m_layout == Layout::GRID ? ClampY(next_yoff, count) : ClampX(next_yoff, count);

    if (index != old_index || m_yoff != old_yoff) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        return true;
    }

    return false;
}

auto List::ScrollToEnd(s64& index, s64 count) -> bool {
    if (count <= 0) {
        return false;
    }

    const auto old_index = index;
    const auto old_yoff = m_yoff;
    const auto max = m_layout == Layout::GRID ? GetMaxY() : GetMaxX();

    index = count - 1;
    if (count > m_page) {
        const auto next_yoff = static_cast<float>(count) * max;
        m_yoff = m_layout == Layout::GRID ? ClampY(next_yoff, count) : ClampX(next_yoff, count);
    } else {
        m_yoff = 0;
    }

    if (index != old_index || m_yoff != old_yoff) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        return true;
    }

    return false;
}

auto List::ScrollToStart(s64& index, s64 count) -> bool {
    if (count <= 0) {
        return false;
    }

    const auto old_index = index;
    const auto old_yoff = m_yoff;

    index = 0;
    m_yoff = 0;

    if (index != old_index || m_yoff != old_yoff) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        return true;
    }

    return false;
}

void List::EnsureVisible(s64 index, s64 count) {
    if (count <= 0) {
        return;
    }

    const auto max = m_layout == Layout::GRID ? GetMaxY() : GetMaxX();
    if (max <= 0.f) {
        return;
    }

    index = std::clamp<s64>(index, 0, count - 1);

    // index of the first entry currently on screen, and the one just past the
    // last. both are rounded down to the start of a row.
    s64 start = static_cast<s64>((m_yoff + max / 2.f) / max) * m_row;

    if (index < start) {
        start = (index / m_row) * m_row;
    } else if (index >= start + m_page) {
        start = std::max<s64>(0, (index / m_row) * m_row - (m_page - m_row));
    } else {
        return;
    }

    const auto next_yoff = static_cast<float>(start / m_row) * max;
    m_yoff = m_layout == Layout::GRID ? ClampY(next_yoff, count) : ClampX(next_yoff, count);
}

void List::OnUpdateHome(Controller* controller, TouchInfo* touch, s64 index, s64 count, TouchCallback callback, const Widget* widget) {
    const bool has_r2 = widget && widget->HasAction(Button::R2);
    const bool has_l2 = widget && widget->HasAction(Button::L2);
    const bool has_r  = widget && widget->HasAction(Button::R);
    const bool has_l  = widget && widget->HasAction(Button::L);

    if (m_fast_scroll && !has_r2 && controller->GotDown(Button::R2)) {
        if (ScrollToEnd(index, count)) {
            callback(false, index);
        }
    } else if (m_fast_scroll && !has_l2 && controller->GotDown(Button::L2)) {
        if (ScrollToStart(index, count)) {
            callback(false, index);
        }
    } else if (GetPageJump() && (!has_r && controller->GotDown(Button::R))) {
        if (ScrollPageDown(index, count)) {
            callback(false, index);
        }
    } else if (GetPageJump() && (!has_l && controller->GotDown(Button::L))) {
        if (ScrollPageUp(index, count)) {
            callback(false, index);
        }
    } else if (controller->GotDown(Button::RIGHT)) {
        if (ScrollDown(index, m_row, count)) {
            callback(false, index);
        }
    } else if (controller->GotDown(Button::LEFT)) {
        if (ScrollUp(index, m_row, count)) {
            callback(false, index);
        }
    } else if (touch->is_clicked && touch->in_range(GetPos())) {
        auto v = m_v;
        v.x -= ClampX(m_yoff + m_y_prog, count);

        for (s64 i = 0; i < count; i++, v.x += v.w + m_pad.x) {
            if (v.x > GetX() + GetW()) {
                break;
            }

            Vec4 vv = v;
            // if not drawing, only return clipped v as its used for touch
            vv.w = std::min(v.x + v.w, m_pos.x + m_pos.w) - v.x;
            vv.h = std::min(v.y + v.h, m_pos.y + m_pos.h) - v.y;

            if (touch->in_range(vv)) {
                callback(true, i);
                return;
            }
        }
    } else if (touch->is_scroll && touch->in_range(GetPos())) {
        m_y_prog = (float)touch->initial.x - (float)touch->cur.x;
    } else if (touch->is_end) {
        m_yoff = ClampX(m_yoff + m_y_prog, count);
        m_y_prog = 0;
    }
}

void List::OnUpdateGrid(Controller* controller, TouchInfo* touch, s64 index, s64 count, TouchCallback callback, const Widget* widget) {
    const bool has_r2 = widget && widget->HasAction(Button::R2);
    const bool has_l2 = widget && widget->HasAction(Button::L2);
    const bool has_r  = widget && widget->HasAction(Button::R);
    const bool has_l  = widget && widget->HasAction(Button::L);

    if (m_fast_scroll && !has_r2 && controller->GotDown(Button::R2)) {
        if (ScrollToEnd(index, count)) {
            callback(false, index);
        }
    } else if (m_fast_scroll && !has_l2 && controller->GotDown(Button::L2)) {
        if (ScrollToStart(index, count)) {
            callback(false, index);
        }
    } else if (GetPageJump() && ((!has_r && controller->GotDown(Button::R)) || (m_row == 1 && controller->GotDown(Button::RIGHT)))) {
        if (m_row == 1) {
            if (ScrollStepList(index, count, true)) {
                callback(false, index);
            }
        } else {
            if (ScrollPageDown(index, count)) {
                callback(false, index);
            }
        }
    } else if (GetPageJump() && ((!has_l && controller->GotDown(Button::L)) || (m_row == 1 && controller->GotDown(Button::LEFT)))) {
        if (m_row == 1) {
            if (ScrollStepList(index, count, false)) {
                callback(false, index);
            }
        } else {
            if (ScrollPageUp(index, count)) {
                callback(false, index);
            }
        }
    } else if (controller->GotDown(Button::DOWN)) {
        if (ScrollDown(index, m_row, count)) {
            callback(false, index);
        }
    } else if (controller->GotDown(Button::UP)) {
        if (ScrollUp(index, m_row, count)) {
            callback(false, index);
        }
    } else if (m_row > 1 && controller->GotDown(Button::RIGHT)) {
        if (ScrollDown(index, 1, count)) {
            callback(false, index);
        }
    } else if (m_row > 1 && controller->GotDown(Button::LEFT)) {
        if (ScrollUp(index, 1, count)) {
            callback(false, index);
        }
    } else if (touch->is_clicked && touch->in_range(GetPos())) {
        auto v = m_v;
        v.y -= ClampY(m_yoff + m_y_prog, count);

        for (s64 i = 0; i < count; v.y += v.h + m_pad.y) {
            if (v.y > GetY() + GetH()) {
                break;
            }

            const auto x = v.x;

            for (; i < count; i++, v.x += v.w + m_pad.x) {
                // only draw if full x is in bounds
                if (v.x + v.w > GetX() + GetW()) {
                    break;
                }

                // skip anything not visible
                if (v.y + v.h < GetY()) {
                    continue;
                }

                Vec4 vv = v;
                // if not drawing, only return clipped v as its used for touch
                vv.w = std::min(v.x + v.w, m_pos.x + m_pos.w) - v.x;
                vv.h = std::min(v.y + v.h, m_pos.y + m_pos.h) - v.y;

                if (touch->in_range(vv)) {
                    callback(true, i);
                    return;
                }
            }

            v.x = x;
        }
    } else if (touch->is_scroll && touch->in_range(GetPos())) {
        m_y_prog = (float)touch->initial.y - (float)touch->cur.y;
    } else if (touch->is_end) {
        m_yoff = ClampY(m_yoff + m_y_prog, count);
        m_y_prog = 0;
    }
}

void List::DrawHome(NVGcontext* vg, Theme* theme, s64 count, Callback callback) const {
    const auto yoff = ClampX(m_yoff + m_y_prog, count);
    auto v = m_v;
    v.x -= yoff;

    nvgSave(vg);
    // the selection highlight (drawRectOutline) draws its border + drop shadow a
    // few px OUTSIDE each item rect. clipping exactly at the list bounds cut the
    // highlight of edge items, making it look sunken; pad the scissor so it can
    // render on top. see drawRectOutlineInternal() in nvg_util.cpp.
    ScissorContent(vg, m_pos);

    for (s64 i = 0; i < count; i++, v.x += v.w + m_pad.x) {
        // skip anything not visible
        if (v.x + v.w < GetX()) {
            continue;
        }

        if (v.x > GetX() + GetW()) {
            break;
        }

        callback(vg, theme, v, i);
    }

    nvgRestore(vg);
}

void List::DrawGrid(NVGcontext* vg, Theme* theme, s64 count, Callback callback) const {
    const auto yoff = ClampY(m_yoff + m_y_prog, count);
    const s64 start = yoff / GetMaxY() * m_row;
    gfx::drawScrollbar2(vg, theme, m_scrollbar.x, m_scrollbar.y, m_scrollbar.h, start, count, m_row, m_page);

    auto v = m_v;
    v.y -= yoff;

    nvgSave(vg);
    // pad the scissor so the selection highlight of edge items isn't clipped;
    // see the note in DrawHome().
    ScissorContent(vg, m_pos);

    for (s64 i = 0; i < count; v.y += v.h + m_pad.y) {
        if (v.y > GetY() + GetH()) {
            break;
        }

        const auto x = v.x;

        for (s64 row = 0; i < count; row++, i++, v.x += v.w + m_pad.x) {
            if (row >= m_row) {
                break;
            }

            // only draw if full x is in bounds
            if (v.x + v.w > GetX() + GetW()) {
                break;
            }

            // skip anything not visible
            if (v.y + v.h < GetY()) {
                continue;
            }

            callback(vg, theme, v, i);
        }

        v.x = x;
    }

    nvgRestore(vg);
}

auto List::ScrollStepList(s64& index, s64 count, bool forward) -> bool {
    if (count <= 0) return false;
    const auto max = m_layout == Layout::GRID ? GetMaxY() : GetMaxX();
    const auto old_index = index;
    const auto old_yoff = m_yoff;

    s64 start_index = static_cast<s64>(m_yoff / max);
    s64 middle_index = start_index + m_page / 2;
    s64 new_start = start_index;

    if (forward) {
        if (index < middle_index) {
            index = std::min<s64>(middle_index, count - 1);
            new_start = start_index;
        } else {
            index = std::min<s64>(start_index + m_page, count - 1);
            new_start = start_index + m_page;
        }
    } else {
        if (index > middle_index) {
            index = std::max<s64>(middle_index, 0);
            new_start = start_index;
        } else {
            index = std::max<s64>(start_index - m_page, 0);
            new_start = start_index - m_page;
        }
    }

    s64 max_start = std::max<s64>(0, count - m_page);
    new_start = std::clamp<s64>(new_start, 0, max_start);

    const auto next_yoff = static_cast<float>(new_start) * max;
    m_yoff = m_layout == Layout::GRID ? ClampY(next_yoff, count) : ClampX(next_yoff, count);

    if (index != old_index || m_yoff != old_yoff) {
        App::PlaySoundEffect(SoundEffect_Scroll);
        return true;
    }
    return false;
}

} // namespace sphaira::ui
