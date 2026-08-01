#include "ui/list.hpp"
#include "ui/widget.hpp"
#include "ui/layout.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "log.hpp"
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

// How much of the fling speed survives each frame, and the speed below which it
// is not worth another frame of work. 0.94 at 60fps coasts for roughly a second
// after a hard flick, which is about what a phone does.
constexpr float FLING_FRICTION = 0.94f;
constexpr float FLING_STOP = 0.4f;
// weight of the newest frame in the smoothed speed. A single frame of finger
// jitter should not decide where a flick lands.
constexpr float FLING_SMOOTHING = 0.35f;

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
            StepFling(touch, count, true);
            OnUpdateHome(controller, touch, index, count, callback, widget);
            break;
        case Layout::GRID:
            StepFling(touch, count, false);
            OnUpdateGrid(controller, touch, index, count, callback, widget);
            break;
    }
}

void List::OnUpdateTouchOnly(TouchInfo* touch, s64 count) {
    const bool horizontal = m_layout == Layout::HOME;
    StepFling(touch, count, horizontal);
    OnTouchScroll(touch, count, horizontal);
}

// Coasts the view after the finger leaves. Called every frame, before input, so
// a fresh touch cancels whatever is still moving.
void List::StepFling(TouchInfo* touch, s64 count, bool horizontal) {
    // putting a finger down catches a list that is still coasting, whether the
    // touch turns out to be a drag or a tap.
    if (touch->is_touching && !m_was_touching) {
        m_fling = 0.f;
        m_y_prog_prev = 0.f;
    }
    m_was_touching = touch->is_touching;

    if (touch->is_touching) {
        return;
    }

    if (std::abs(m_fling) < FLING_STOP) {
        m_fling = 0.f;
        return;
    }

    const auto next = m_yoff + m_fling;
    m_yoff = horizontal ? ClampX(next, count) : ClampY(next, count);

    // hit an end: stop dead rather than grinding against the clamp.
    if (m_yoff != next) {
        m_fling = 0.f;
        return;
    }

    m_fling *= FLING_FRICTION;
}

auto List::OnTouchScroll(TouchInfo* touch, s64 count, bool horizontal) -> bool {
    if (touch->is_scroll && touch->in_range(GetPos())) {
        const auto prog = horizontal
            ? (float)touch->initial.x - (float)touch->cur.x
            : (float)touch->initial.y - (float)touch->cur.y;

        // speed over the last frames of the drag, smoothed - one frame of
        // finger jitter should not decide where the flick ends up.
        m_fling += ((prog - m_y_prog_prev) - m_fling) * FLING_SMOOTHING;
        m_y_prog_prev = prog;
        m_y_prog = prog;
        return true;
    }

    if (touch->is_end) {
        // the drag folds into the offset while its trailing speed stays in
        // m_fling, so letting go mid-swipe coasts instead of stopping dead.
        m_yoff = horizontal ? ClampX(m_yoff + m_y_prog, count) : ClampY(m_yoff + m_y_prog, count);
        m_y_prog = 0;
        m_y_prog_prev = 0;
        return true;
    }

    return false;
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

    // only the last row wraps. in a grid whose last row is short, stepping down
    // off a full row still lands on the last entry, the way it always did.
    const auto last_row_start = ((count - 1) / m_row) * m_row;

    if (index + step < count) {
        index += step;
    } else if (m_wrap && index >= last_row_start) {
        index = 0;
    } else {
        index = count - 1;
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
    } else {
        OnTouchScroll(touch, count, true);
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
    } else {
        OnTouchScroll(touch, count, false);
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
