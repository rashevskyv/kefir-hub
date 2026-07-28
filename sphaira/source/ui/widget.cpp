#include "ui/widget.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "log.hpp"

#include <algorithm>
#include "i18n.hpp"

namespace sphaira::ui {

namespace {

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
    nvgFontSize(vg, 20);
    nvgText(vg, m_hint_pos.x, m_hint_pos.y, m_action_str.c_str(), nullptr);
    nvgFontSize(vg, 26);
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

    auto draw_actions = GetUiButtons();
    for (auto& e : draw_actions) {
        if (touch->is_clicked && touch->in_range(e.GetPos())) {
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

    auto draw_actions = GetUiButtons();

    for (auto& e : draw_actions) {
        e.Draw(vg, theme);
    }
}

auto Widget::HasAction(Button button) const -> bool {
    return m_actions.contains(button);
}

void Widget::SetAction(Button button, Action action) {
    m_actions.insert_or_assign(button, action);
}

void Widget::RemoveAction(Button button) {
    if (auto it = m_actions.find(button); it != m_actions.end()) {
        m_actions.erase(it);
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
    auto [x, y] = button_pos;

    float bounds[4]{};
    for (auto& e : buttons) {
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

        nvgFontSize(vg, 20.f);
        nvgTextBounds(vg, x, y, e.m_action_str.c_str(), nullptr, bounds);
        auto len = bounds[2] - bounds[0];
        e.m_hint_pos = {x, y, len, 20};

        x -= len + 8.f;
        nvgFontSize(vg, 26.f);
        nvgTextBounds(vg, x, y - 7.f, e.m_button_str.c_str(), nullptr, bounds);
        len = bounds[2] - bounds[0];
        e.m_button_pos = {x, y - 4.f, len, 26};
        x -= len + 16.f;

        e.SetPos(e.m_button_pos);
        e.SetX(e.GetX() - 40);
        e.SetW(e.m_hint_pos.x - e.m_button_pos.x + len + 25);
        e.SetY(e.GetY() - 18);
        e.SetH(26 + 18 * 2);
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

    // --- COMBINING LOGIC ---
    std::string l_hint, r_hint, l2_hint, r2_hint, left_hint, right_hint;
    for (const auto& btn : draw_actions) {
        if (btn.m_button == Button::L) l_hint = btn.m_action_str;
        if (btn.m_button == Button::R) r_hint = btn.m_action_str;
        if (btn.m_button == Button::L2) l2_hint = btn.m_action_str;
        if (btn.m_button == Button::R2) r2_hint = btn.m_action_str;
        if (btn.m_button == Button::LEFT) left_hint = btn.m_action_str;
        if (btn.m_button == Button::RIGHT) right_hint = btn.m_action_str;
    }

    uiButtons merged_actions;
    merged_actions.reserve(draw_actions.size());

    bool L_R_merged = false;
    bool L_R_page_merged = false;
    bool L_R_game_merged = false;
    bool L_R_tab_merged = false;
    bool L2_R2_merged = false;
    bool L2_R2_jump_merged = false;
    bool L2_R2_tab_merged = false;
    bool L2_R2_game_merged = false;
    bool LEFT_RIGHT_merged = false;

    const bool should_merge_L_R = (!l_hint.empty() && l_hint == "Add Point"_i18n) && (!r_hint.empty() && r_hint == "Remove Point"_i18n);
    const bool should_merge_L_R_page = (!l_hint.empty() && l_hint == "Previous Page"_i18n) && (!r_hint.empty() && r_hint == "Next Page"_i18n);
    const bool should_merge_L_R_game = (!l_hint.empty() && l_hint == "Previous game"_i18n) && (!r_hint.empty() && r_hint == "Next game"_i18n);
    const bool should_merge_L_R_tab = (!l_hint.empty() && l_hint == "Previous tab"_i18n) && (!r_hint.empty() && r_hint == "Next tab"_i18n);
    const bool should_merge_L2_R2 = (!l2_hint.empty() && l2_hint == "Load Preset"_i18n) && (!r2_hint.empty() && r2_hint == "Save Preset"_i18n);
    const bool should_merge_L2_R2_jump = (!l2_hint.empty() && l2_hint == "Jump Backward"_i18n) && (!r2_hint.empty() && r2_hint == "Jump Forward"_i18n);
    const bool should_merge_L2_R2_tab = (!l2_hint.empty() && l2_hint == "Previous tab"_i18n) && (!r2_hint.empty() && r2_hint == "Next tab"_i18n);
    const bool should_merge_L2_R2_game = (!l2_hint.empty() && l2_hint == "Previous game"_i18n) && (!r2_hint.empty() && r2_hint == "Next game"_i18n);
    const bool should_merge_LEFT_RIGHT = (!left_hint.empty() && (left_hint == "Previous Image"_i18n || left_hint == "Prev Image"_i18n)) &&
                                          (!right_hint.empty() && right_hint == "Next Image"_i18n);

    for (const auto& btn : draw_actions) {
        if (btn.m_button == Button::L || btn.m_button == Button::R) {
            if (should_merge_L_R) {
                if (!L_R_merged) {
                    uiButton combined{Button::L, "\uE0E4/\uE0E5", "Add/Remove Point"_i18n};
                    merged_actions.push_back(combined);
                    L_R_merged = true;
                }
            } else if (should_merge_L_R_page) {
                if (!L_R_page_merged) {
                    uiButton combined{Button::L, "\uE0E4/\uE0E5", "Prev/Next Page"_i18n};
                    merged_actions.push_back(combined);
                    L_R_page_merged = true;
                }
            } else if (should_merge_L_R_game) {
                if (!L_R_game_merged) {
                    uiButton combined{Button::L, "\uE0E4/\uE0E5", "Previous/Next game"_i18n};
                    merged_actions.push_back(combined);
                    L_R_game_merged = true;
                }
            } else if (should_merge_L_R_tab) {
                if (!L_R_tab_merged) {
                    uiButton combined{Button::L, "\uE0E4/\uE0E5", "Previous/Next tab"_i18n};
                    merged_actions.push_back(combined);
                    L_R_tab_merged = true;
                }
            } else {
                merged_actions.push_back(btn);
            }
        }
        else if (btn.m_button == Button::L2 || btn.m_button == Button::R2) {
            if (should_merge_L2_R2) {
                if (!L2_R2_merged) {
                    uiButton combined{Button::L2, "\uE0E6/\uE0E7", "Load/Save Preset"_i18n};
                    merged_actions.push_back(combined);
                    L2_R2_merged = true;
                }
            } else if (should_merge_L2_R2_jump) {
                if (!L2_R2_jump_merged) {
                    uiButton combined{Button::L2, "\uE0E6/\uE0E7", "Jump Back/Forward"_i18n};
                    merged_actions.push_back(combined);
                    L2_R2_jump_merged = true;
                }
            } else if (should_merge_L2_R2_tab) {
                if (!L2_R2_tab_merged) {
                    uiButton combined{Button::L2, "\uE0E6/\uE0E7", "Previous/Next tab"_i18n};
                    merged_actions.push_back(combined);
                    L2_R2_tab_merged = true;
                }
            } else if (should_merge_L2_R2_game) {
                if (!L2_R2_game_merged) {
                    uiButton combined{Button::L2, "\uE0E6/\uE0E7", "Previous/Next game"_i18n};
                    merged_actions.push_back(combined);
                    L2_R2_game_merged = true;
                }
            } else {
                merged_actions.push_back(btn);
            }
        }
        else if (btn.m_button == Button::LEFT || btn.m_button == Button::RIGHT) {
            if (should_merge_LEFT_RIGHT) {
                if (!LEFT_RIGHT_merged) {
                    uiButton combined{Button::LEFT, "\uE0ED/\uE0EE", "Prev / Next Image"_i18n};
                    merged_actions.push_back(combined);
                    LEFT_RIGHT_merged = true;
                }
            } else {
                merged_actions.push_back(btn);
            }
        }
        else {
            merged_actions.push_back(btn);
        }
    }
    draw_actions = std::move(merged_actions);
    // ----------------------------

    // setup positions.
    SetupUiButtons(draw_actions, button_pos);

    return draw_actions;
}

auto Widget::GetUiButtons() const -> uiButtons {
    return GetUiButtons(m_actions, m_button_pos, m_sort_ui_buttons);
}

} // namespace sphaira::ui
