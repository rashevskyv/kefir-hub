#include "ui/widget.hpp"
#include "ui/nvg_util.hpp"
#include "ui/layout.hpp"
#include "app.hpp"
#include "log.hpp"

#include <algorithm>

namespace sphaira::ui {

namespace {

// font sizes of an unscaled hint row, and how far it may be shrunk before the
// text stops being readable and is simply allowed to overflow.
constexpr float UI_HINT_SIZE = 20.f;
constexpr float UI_GLYPH_SIZE = 26.f;

constexpr float UI_HINT_SIZE_2ROW = 17.f;
constexpr float UI_GLYPH_SIZE_2ROW = 22.f;

constexpr float MIN_UI_BUTTON_SCALE = 0.6f;

// Measures intrinsic unspaced content width of buttons in range [start_idx, end_idx)
// and outputs individual hint and glyph lengths.
auto MeasureUiButtonsContent(NVGcontext* vg, const Widget::uiButtons& buttons, size_t start_idx, size_t end_idx,
                             float scale, float base_hint_size, float base_glyph_size,
                             std::vector<float>& out_hint_lens, std::vector<float>& out_glyph_lens) -> float {
    float total_w = 0.f;
    float bounds[4]{};

    const auto hint_size = base_hint_size * scale;
    const auto glyph_size = base_glyph_size * scale;

    out_hint_lens.clear();
    out_glyph_lens.clear();
    out_hint_lens.reserve(end_idx - start_idx);
    out_glyph_lens.reserve(end_idx - start_idx);

    for (size_t i = start_idx; i < end_idx; i++) {
        const auto& e = buttons[i];

        nvgFontSize(vg, hint_size);
        nvgTextBounds(vg, 0.f, 0.f, e.m_action_str.c_str(), nullptr, bounds);
        const float hint_len = bounds[2] - bounds[0];
        out_hint_lens.push_back(hint_len);

        nvgFontSize(vg, glyph_size);
        nvgTextBounds(vg, 0.f, 0.f, e.m_button_str.c_str(), nullptr, bounds);
        const float glyph_len = bounds[2] - bounds[0];
        out_glyph_lens.push_back(glyph_len);

        total_w += hint_len + 8.f * scale + glyph_len;
    }

    return total_w;
}

// Lays out a sub-range [start_idx, end_idx) of buttons right-to-left from pos.
// If apply is false, only measures and returns the total width without modifying button properties.
auto LayoutUiButtonsRow(NVGcontext* vg, Widget::uiButtons& buttons, size_t start_idx, size_t end_idx,
                        const Vec2& pos, float scale, float base_hint_size, float base_glyph_size,
                        bool is_two_rows, int row_index, bool apply) -> float {
    auto [x, y] = pos;
    float bounds[4]{};

    const auto hint_size = base_hint_size * scale;
    const auto glyph_size = base_glyph_size * scale;

    for (size_t i = start_idx; i < end_idx; i++) {
        auto& e = buttons[i];
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

        nvgFontSize(vg, hint_size);
        nvgTextBounds(vg, x, y, e.m_action_str.c_str(), nullptr, bounds);
        const auto hint_len = bounds[2] - bounds[0];

        const float hint_pos_x = x;
        const float hint_pos_y = y;

        x -= hint_len + 8.f * scale;
        nvgFontSize(vg, glyph_size);
        nvgTextBounds(vg, x, y - (is_two_rows ? 4.f : 7.f) * scale, e.m_button_str.c_str(), nullptr, bounds);
        const auto btn_len = bounds[2] - bounds[0];

        const float btn_pos_x = x;
        const float btn_pos_y = y - (is_two_rows ? 4.f : 4.f) * scale;
        x -= btn_len + 16.f * scale;

        if (apply) {
            e.m_hint_pos = {hint_pos_x, hint_pos_y, hint_len, hint_size};
            e.m_hint_size = hint_size;
            e.m_button_pos = {btn_pos_x, btn_pos_y, btn_len, glyph_size};
            e.m_button_size = glyph_size;

            e.SetPos(e.m_button_pos);
            e.SetX(e.GetX() - (is_two_rows ? 30.f : 40.f) * scale);
            e.SetW(e.m_hint_pos.x - e.m_button_pos.x + btn_len + (is_two_rows ? 20.f : 25.f) * scale);

            if (is_two_rows) {
                if (row_index == 1) {
                    // Top row
                    e.SetY(646.f);
                    e.SetH(36.f);
                } else {
                    // Bottom row
                    e.SetY(682.f);
                    e.SetH(38.f);
                }
            } else {
                e.SetY(e.GetY() - 18.f);
                e.SetH(UI_GLYPH_SIZE + 18.f * 2.f);
            }
        }
    }

    return pos.x - x;
}

// Lays out a sub-range [start_idx, end_idx) of buttons with justified spacing
// spanning the entire width between pos.x (right) and left_bound_x (left).
auto LayoutUiButtonsRowJustified(NVGcontext* vg, Widget::uiButtons& buttons, size_t start_idx, size_t end_idx,
                                 const Vec2& pos, float left_bound_x, float scale,
                                 float base_hint_size, float base_glyph_size,
                                 int row_index) -> void {
    const size_t count = end_idx - start_idx;
    if (count == 0) return;

    std::vector<float> hint_lens;
    std::vector<float> glyph_lens;
    const float content_w = MeasureUiButtonsContent(vg, buttons, start_idx, end_idx, scale, base_hint_size, base_glyph_size, hint_lens, glyph_lens);

    const float avail_w = pos.x - left_bound_x;
    const float hint_size = base_hint_size * scale;
    const auto glyph_size = base_glyph_size * scale;

    float inter_gap = 16.f * scale;
    if (count > 1 && avail_w > content_w) {
        inter_gap = (avail_w - content_w) / static_cast<float>(count - 1);
    }

    float current_x = pos.x;

    for (size_t idx = 0; idx < count; idx++) {
        const size_t i = start_idx + idx;
        auto& e = buttons[i];

        const float hint_len = hint_lens[idx];
        const float glyph_len = glyph_lens[idx];

        const float hint_pos_x = current_x;
        const float hint_pos_y = pos.y;

        const float glyph_x = current_x - hint_len - 8.f * scale;
        const float glyph_y = pos.y - 4.f * scale;
        const float btn_left_x = glyph_x - glyph_len;

        e.m_hint_pos = {hint_pos_x, hint_pos_y, hint_len, hint_size};
        e.m_hint_size = hint_size;
        e.m_button_pos = {glyph_x, glyph_y, glyph_len, glyph_size};
        e.m_button_size = glyph_size;

        // Set touch target box to seamlessly cover half the gap on each side
        const float touch_right = (idx == 0) ? pos.x : (hint_pos_x + inter_gap / 2.f);
        const float touch_left = (idx == count - 1) ? left_bound_x : (btn_left_x - inter_gap / 2.f);

        e.SetX(touch_left);
        e.SetW(std::max(1.f, touch_right - touch_left));

        if (row_index == 1) {
            // Top row
            e.SetY(646.f);
            e.SetH(36.f);
        } else {
            // Bottom row
            e.SetY(682.f);
            e.SetH(38.f);
        }

        current_x = btn_left_x - inter_gap;
    }
}

auto GetUiButtonSortPriority(Button button) -> int {
    switch (button) {
        case Button::START: return 0;
        case Button::A: return 10;
        case Button::B: return 20;
        case Button::X: return 30;
        case Button::Y: return 40;
        case Button::L2: return 50;
        case Button::R2: return 60;
        case Button::L: return 70;
        case Button::R: return 80;
        case Button::SELECT: return 90;
        default: return 1000;
    }
}

} // namespace

uiButton::uiButton(Button button, const std::string& button_str, const std::string& action_str)
: m_button{button}
, m_button_str{button_str}
, m_action_str{action_str} {

}

uiButton::uiButton(Button button, const std::string& action_str)
: uiButton{button, gfx::getButton(button), action_str} {

}

auto uiButton::Draw(NVGcontext* vg, Theme* theme) -> void {
    // enable to see button region
    // gfx::drawRect(vg, m_pos, gfx::Colour::RED);

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT));
    nvgFontSize(vg, m_hint_size);
    nvgText(vg, m_hint_pos.x, m_hint_pos.y, m_action_str.c_str(), nullptr);
    nvgFontSize(vg, m_button_size);
    nvgText(vg, m_button_pos.x, m_button_pos.y, m_button_str.c_str(), nullptr);
}

void Widget::Update(Controller* controller, TouchInfo* touch) {
    // 1. If a button goes down, mark it as pending
    for (const auto& [button, action] : m_actions) {
        if ((action.m_type & ActionType::DOWN) && controller->GotDown(button)) {
            m_pending_button = button;
            break;
        }
    }

    // 2. Cancel pending action if any other button goes down simultaneously or subsequently
    if (m_pending_button != Button::NONE) {
        const u64 other_buttons_mask = ~static_cast<u64>(m_pending_button);
        if (controller->m_kdown & other_buttons_mask) {
            m_pending_button = Button::NONE;
        }
    }

    // 3. Process actions
    for (const auto& [button, action] : m_actions) {
        if (action.m_type & ActionType::DOWN) {
            // Trigger action on release only if it was the pending button and wasn't cancelled
            if (button == m_pending_button && controller->GotUp(button)) {
                if (static_cast<u64>(button) & static_cast<u64>(Button::ANY_BUTTON)) {
                    App::PlaySoundEffect(SoundEffect_Focus);
                }
                action.Invoke(true);
                m_pending_button = Button::NONE;
                break;
            }
        }
        else if ((action.m_type & ActionType::UP) && controller->GotUp(button)) {
            action.Invoke(false);
            break;
        }
        else if ((action.m_type & ActionType::HELD) && controller->GotHeld(button)) {
            action.Invoke(true);
            break;
        }
    }

    // Clear pending button on release
    if (m_pending_button != Button::NONE && controller->GotUp(m_pending_button)) {
        m_pending_button = Button::NONE;
    }

    if (!touch->is_clicked) {
        return;
    }

    for (auto& e : GetUiButtons()) {
        if (touch->in_range(e.GetPos())) {
            log_write("got click: %s\n", e.m_action_str.c_str());
            FireAction(e.m_button);
            break;
        }
    }
}

void Widget::Draw(NVGcontext* vg, Theme* theme) {
    // the footer belongs to whichever widget is on top of the stack. anything
    // it covers keeps its actions live (they still fire) but stops drawing
    // hints, otherwise both rows land on the same right-aligned anchor and
    // overlap - a menu's hints reading through the panel opened above it.
    if (!App::OwnsFooter(this)) {
        return;
    }

    for (auto& e : GetUiButtons()) {
        e.Draw(vg, theme);
    }
}

auto Widget::HasAction(Button button) const -> bool {
    return m_actions.contains(button);
}

void Widget::SetAction(Button button, Action action) {
    // only the label and the glyph decide the layout - swapping the callback
    // under an unchanged hint must not cost a re-measure. Menus that rebuild
    // their whole action set every frame (the file browser inherits its view's)
    // would otherwise never hit the cache.
    const auto it = m_actions.find(button);
    if (it == m_actions.end() ||
        it->second.m_hint != action.m_hint ||
        it->second.m_button_str != action.m_button_str) {
        m_ui_buttons_dirty = true;
    }

    m_actions.insert_or_assign(button, action);
}

void Widget::RemoveAction(Button button) {
    if (auto it = m_actions.find(button); it != m_actions.end()) {
        m_actions.erase(it);
        m_ui_buttons_dirty = true;
    }
}

auto Widget::FireAction(Button b, u8 type) -> bool {
    for (const auto& [button, action] : m_actions) {
        if (button == b && (action.m_type & type)) {
            App::PlaySoundEffect(SoundEffect_Focus);
            action.Invoke(true);
            return true;
        }
    }
    return false;
}

void Widget::SetupUiButtons(uiButtons& buttons, const Vec2& button_pos) {
    if (buttons.empty()) {
        return;
    }

    auto vg = App::GetVg();
    const float left_bound_x = layout::SIDE_X;
    const auto avail = button_pos.x - left_bound_x;

    // 1. Measure 1-row layout at full size first
    const auto want_1row = LayoutUiButtonsRow(vg, buttons, 0, buttons.size(), button_pos, 1.f, UI_HINT_SIZE, UI_GLYPH_SIZE, false, 0, false);
    const float scale_1row = (want_1row > 0.f && avail > 0.f) ? (avail / want_1row) : 1.f;

    // If it fits nicely on 1 row with scale >= 0.85 (or if there is only 1 button)
    if ((want_1row <= avail || scale_1row >= 0.85f || buttons.size() < 2) && avail > 0.f) {
        LayoutUiButtonsRow(vg, buttons, 0, buttons.size(), button_pos, std::max(MIN_UI_BUTTON_SCALE, std::min(1.f, scale_1row)), UI_HINT_SIZE, UI_GLYPH_SIZE, false, 0, true);
        return;
    }

    // 2. 2-row layout: find split index k in [1, buttons.size() - 1]
    // Balance rows by occupied pixel content: minimize |W_bottom - W_top|
    std::vector<float> unscaled_widths(buttons.size(), 0.f);
    float total_content_pixels = 0.f;
    for (size_t i = 0; i < buttons.size(); i++) {
        std::vector<float> hl, gl;
        unscaled_widths[i] = MeasureUiButtonsContent(vg, buttons, i, i + 1, 1.f, UI_HINT_SIZE_2ROW, UI_GLYPH_SIZE_2ROW, hl, gl);
        total_content_pixels += unscaled_widths[i];
    }

    size_t best_k = buttons.size() / 2;
    float best_diff = 1e9f;
    float best_max_w = 1e9f;

    float current_bottom_w = 0.f;
    for (size_t k = 1; k < buttons.size(); k++) {
        current_bottom_w += unscaled_widths[k - 1];
        const float current_top_w = total_content_pixels - current_bottom_w;

        const float diff = std::abs(current_bottom_w - current_top_w);
        const float max_w = std::max(current_bottom_w, current_top_w);

        if (diff < best_diff || (std::abs(diff - best_diff) < 1.f && max_w < best_max_w)) {
            best_diff = diff;
            best_max_w = max_w;
            best_k = k;
        }
    }

    const float scale_2row = (best_max_w > 0.f && avail > 0.f) ? (avail / best_max_w) : 1.f;
    const float final_scale = std::clamp(scale_2row, MIN_UI_BUTTON_SCALE, 1.f);

    const float y_top = (button_pos.y >= 640.f) ? 655.f : (button_pos.y - 14.f);
    const float y_bottom = (button_pos.y >= 640.f) ? 687.f : (button_pos.y + 14.f);

    // Apply justified layout across full width for Row 1 (top: buttons [best_k, N)) and Row 2 (bottom: buttons [0, best_k))
    LayoutUiButtonsRowJustified(vg, buttons, best_k, buttons.size(), Vec2{button_pos.x, y_top}, left_bound_x, final_scale, UI_HINT_SIZE_2ROW, UI_GLYPH_SIZE_2ROW, 1);
    LayoutUiButtonsRowJustified(vg, buttons, 0, best_k, Vec2{button_pos.x, y_bottom}, left_bound_x, final_scale, UI_HINT_SIZE_2ROW, UI_GLYPH_SIZE_2ROW, 2);
}

auto Widget::GetUiButtons(const Actions& actions, const Vec2& button_pos, bool sort) -> uiButtons {
    uiButtons draw_actions;
    draw_actions.reserve(actions.size());

    const std::pair<Button, Button> swap_buttons[] = {
        {Button::L, Button::R},
        {Button::L2, Button::R2},
        {Button::LEFT, Button::RIGHT},
    };

    // build array
    for (const auto& [button, action] : actions) {
        if (action.IsHidden() || action.m_hint.empty()) {
            continue;
        }

        const auto btn_str = action.m_button_str.empty() ? gfx::getButton(button) : action.m_button_str;
        uiButton ui_button{button, btn_str, action.m_hint};

        bool should_swap = false;
        if (!sort) {
            for (auto [left, right] : swap_buttons) {
                if (button == right && draw_actions.size() && draw_actions.back().m_button == left) {
                    const auto s = draw_actions.back();
                    draw_actions.back().m_button = button;
                    draw_actions.back().m_button_str = btn_str;
                    draw_actions.back().m_action_str = action.m_hint;
                    draw_actions.emplace_back(s);
                    should_swap = true;
                    break;
                }
            }
        }

        if (!should_swap) {
            draw_actions.emplace_back(ui_button);
        }
    }

    if (sort) {
        std::stable_sort(draw_actions.begin(), draw_actions.end(), [](const auto& lhs, const auto& rhs) {
            const auto lhs_priority = GetUiButtonSortPriority(lhs.m_button);
            const auto rhs_priority = GetUiButtonSortPriority(rhs.m_button);
            if (lhs_priority != rhs_priority) {
                return lhs_priority < rhs_priority;
            }
            return static_cast<u64>(lhs.m_button) < static_cast<u64>(rhs.m_button);
        });
    }


    // setup positions.
    SetupUiButtons(draw_actions, button_pos);

    return draw_actions;
}

auto Widget::GetUiButtons() -> uiButtons& {
    if (m_ui_buttons_dirty) {
        m_ui_buttons = GetUiButtons(m_actions, m_button_pos, m_sort_ui_buttons);
        m_ui_buttons_dirty = false;
    }

    return m_ui_buttons;
}

} // namespace sphaira::ui
