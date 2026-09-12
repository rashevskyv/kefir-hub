#pragma once

#include <cstdint>
#include <utility>

namespace sphaira::ui::draw_order {

template <typename RectType, typename Callback>
void TraverseHome(
    RectType initial_v,
    float step_x,
    float min_x,
    float max_x,
    int64_t count,
    int64_t focus_index,
    Callback&& callback
) {
    RectType focused_v{};
    bool has_focused = false;
    auto v = initial_v;

    for (int64_t i = 0; i < count; i++, v.x += step_x) {
        // skip anything not visible
        if (v.x + v.w < min_x) {
            continue;
        }

        if (v.x > max_x) {
            break;
        }

        if (i == focus_index) {
            focused_v = v;
            has_focused = true;
        } else {
            callback(v, i);
        }
    }

    if (has_focused) {
        callback(focused_v, focus_index);
    }
}

template <typename RectType, typename Callback>
void TraverseGrid(
    RectType initial_v,
    int64_t row_count,
    float step_x,
    float step_y,
    float min_x,
    float max_x,
    float min_y,
    float max_y,
    int64_t count,
    int64_t focus_index,
    Callback&& callback
) {
    RectType focused_v{};
    bool has_focused = false;
    auto v = initial_v;

    for (int64_t i = 0; i < count; v.y += step_y) {
        if (v.y > max_y) {
            break;
        }

        const auto x = v.x;

        for (int64_t row = 0; i < count; row++, i++, v.x += step_x) {
            if (row >= row_count) {
                break;
            }

            // only draw if full x is in bounds
            if (v.x + v.w > max_x) {
                break;
            }

            // skip anything not visible
            if (v.y + v.h < min_y) {
                continue;
            }

            if (i == focus_index) {
                focused_v = v;
                has_focused = true;
            } else {
                callback(v, i);
            }
        }

        v.x = x;
    }

    if (has_focused) {
        callback(focused_v, focus_index);
    }
}

} // namespace sphaira::ui::draw_order
