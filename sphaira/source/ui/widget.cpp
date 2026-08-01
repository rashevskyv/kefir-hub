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
constexpr float MIN_UI_BUTTON_SCALE = 0.6f;

// Lays the hint row out right to left from pos at the given scale and returns
// how wide it came out, so the caller can measure before committing.
auto LayoutUiButtons(NVGcontext* vg, Widget::uiButtons& buttons, const Vec2& pos, float scale) -> float {
    auto [x, y] = pos;
    float bounds[4]{};

    const auto hint_size = UI_HINT_SIZE * scale;
    const auto glyph_size = UI_GLYPH_SIZE * scale;

    for (auto& e : buttons) {
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

        nvgFontSize(vg, hint_size);
        nvgTextBounds(vg, x, y, e.m_action_str.c_str(), nullptr, bounds);
        auto len = bounds[2] - bounds[0];
        e.m_hint_pos = {x, y, len, hint_size};
        e.m_hint_size = hint_size;

        x -= len + 8.f * scale;
        nvgFontSize(vg, glyph_size);
        nvgTextBounds(vg, x, y - 7.f * scale, e.m_button_str.c_str(), nullptr, bounds);
        len = bounds[2] - bounds[0];
        e.m_button_pos = {x, y - 4.f * scale, len, glyph_size};
        e.m_button_size = glyph_size;
        x -= len + 16.f * scale;

        // the touch box keeps its full height at any scale: shrinking the text
        // is a layout fix, shrinking the target the finger has to hit is not.
        e.SetPos(e.m_button_pos);
        e.SetX(e.GetX() - 40.f * scale);
        e.SetW(e.m_hint_pos.x - e.m_button_pos.x + len + 25.f * scale);
        e.SetY(e.GetY() - 18.f);
        e.SetH(UI_GLYPH_SIZE + 18.f * 2.f);
    }

    return pos.x - x;
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
    auto vg = App::GetVg();

    // measure at full size first. if the row would run off the left of the
    // footer, shrink the whole row until it fits - a hint that has to be merged
    // with its neighbour to make room loses its own touch target.
    const auto avail = button_pos.x - layout::SIDE_X;
    const auto want = LayoutUiButtons(vg, buttons, button_pos, 1.f);

    if (want > avail && avail > 0.f) {
        LayoutUiButtons(vg, buttons, button_pos, std::max(MIN_UI_BUTTON_SCALE, avail / want));
    }
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
