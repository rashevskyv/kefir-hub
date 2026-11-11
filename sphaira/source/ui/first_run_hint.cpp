#include "ui/first_run_hint.hpp"

#include "app.hpp"
#include "option.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>

namespace sphaira::ui {
namespace {

constexpr float BOX_W = 560.f;
constexpr float BOX_PAD = 30.f;
constexpr float BOX_TEXT_SIZE = 22.f;
constexpr float BOX_LINE_HEIGHT = 1.4f;
constexpr float BOX_EDGE = 40.f;

// ponytail: one stored flag per hint, read and written lazily through option::OptionBool.
option::OptionBool g_flags[] = {
    {"hints", "main_shown", false},
    {"hints", "tools_shown", false},
    {"hints", "kefir_settings_shown", false},
};

} // namespace

namespace hint {

auto TakeFirst(FirstRunHint which) -> bool {
    auto& flag = g_flags[static_cast<size_t>(which)];
    if (flag.Get()) {
        return false;
    }
    flag.Set(true);
    return true;
}

void ResetAll() {
    for (auto& flag : g_flags) {
        flag.Set(false);
    }
}

} // namespace hint

HintBox::HintBox(std::string text, uiButtons row, Button target) : m_text{std::move(text)}, m_row{std::move(row)} {
    for (const auto& b : m_row) {
        if (b.m_button == target) {
            // The frame goes around what is drawn: the glyph and its label are right- and top-aligned at their positions.
            // (The button's own box is its touch area: wider, and it ran past the screen's edge, owner 2026-11-11.)
            const float left = b.m_button_pos.x - b.m_button_pos.w;
            const float top = std::min(b.m_button_pos.y, b.m_hint_pos.y);
            const float bottom = std::max(b.m_button_pos.y + b.m_button_size, b.m_hint_pos.y + b.m_hint_size);
            m_frame = Vec4{left - 12.f, top - 8.f, b.m_hint_pos.x - left + 24.f, bottom - top + 20.f};
        }
    }
}

void HintBox::Update(Controller* controller, TouchInfo* touch) {
    if (controller && (controller->GotDown(Button::A) || controller->GotDown(Button::B))) {
        SetPop();
    }
}

void HintBox::Draw(NVGcontext* vg, Theme* theme) {
    gfx::dimBackground(vg);

    // The frame first: it fills its inside, and drawn after the button it covered the button it was to point at.
    if (m_frame.w > 0.f) {
        gfx::drawRectOutline(vg, theme, 4.f, m_frame);
    }
    for (auto& b : m_row) { // the whole legend, so the screen below does not lose its footer
        b.Draw(vg, theme);
    }

    // measure the text, then place the box above the target or in the centre.
    nvgSave(vg);
    nvgFontSize(vg, BOX_TEXT_SIZE);
    nvgTextLineHeight(vg, BOX_LINE_HEIGHT);
    float bounds[4];
    nvgTextBoxBounds(vg, 0.f, 0.f, BOX_W - BOX_PAD * 2.f, m_text.c_str(), nullptr, bounds);
    nvgRestore(vg);
    const float text_h = bounds[3] - bounds[1];
    const float box_h = BOX_PAD + text_h + BOX_PAD * 2.f;

    Vec4 box{(SCREEN_WIDTH - BOX_W) / 2.f, (SCREEN_HEIGHT - box_h) / 2.f, BOX_W, box_h};
    if (m_frame.w > 0.f) {
        const float right = m_frame.x + m_frame.w;
        box.x = std::clamp(right - BOX_W, BOX_EDGE, SCREEN_WIDTH - BOX_EDGE - BOX_W);
        box.y = std::max(BOX_EDGE, m_frame.y - 24.f - box_h);
    }

    gfx::drawRect(vg, box, theme->GetColour(ThemeEntryID_POPUP), 5);
    gfx::drawTextBox(
        vg, box.x + BOX_PAD, box.y + BOX_PAD, BOX_TEXT_SIZE, BOX_W - BOX_PAD * 2.f,
        theme->GetColour(ThemeEntryID_TEXT), m_text.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, BOX_LINE_HEIGHT
    );
    gfx::drawTextBox(
        vg, box.x + BOX_PAD, box.y + box_h - BOX_PAD - 20.f, 18.f, BOX_W - BOX_PAD * 2.f,
        theme->GetColour(ThemeEntryID_TEXT_INFO), "Press A or B to close."_i18n.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP
    );
}

} // namespace sphaira::ui
