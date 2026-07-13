#include "ui/hold_confirm_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include <algorithm>

namespace sphaira::ui {

HoldConfirmBox::HoldConfirmBox(std::string message, Callback callback)
: m_message{std::move(message)}
, m_hold_seconds{3.0f}
, m_callback{std::move(callback)}
, m_compact{false} {
    m_pos = Vec4{230.f, 126.f, 820.f, 468.f};

    SetActions(
        std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
            m_callback(false);
            SetPop();
        }})
    );
}

HoldConfirmBox::HoldConfirmBox(std::string message, float hold_seconds, Callback callback)
: m_message{std::move(message)}
, m_hold_seconds{std::max(0.5f, hold_seconds)}
, m_callback{std::move(callback)}
, m_compact{true} {
    m_pos = Vec4{255.f, 168.f, 770.f, 384.f};

    SetActions(
        std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
            m_callback(false);
            SetPop();
        }})
    );
}

void HoldConfirmBox::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);

    if (controller->GotHeld(Button::A)) {
        if (!m_holding) {
            m_holding = true;
            m_hold_start = armTicksToNs(armGetSystemTick());
        }

        const auto now = armTicksToNs(armGetSystemTick());
        m_progress = std::min(1.f, static_cast<float>(now - m_hold_start) / (m_hold_seconds * 1'000'000'000.f));
        if (m_progress >= 1.f) {
            m_callback(true);
            SetPop();
        }
    } else {
        m_holding = false;
        m_progress = 0.f;
    }
}

void HoldConfirmBox::Draw(NVGcontext* vg, Theme* theme) {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

    const float padding = m_compact ? 34.f : 38.f;
    nvgSave(vg);
    float font_size = 22.f;
    if (m_message.length() < 60) {
        font_size = 28.f;
    } else if (m_message.length() < 120) {
        font_size = 25.f;
    } else if (m_message.length() > 200) {
        font_size = 20.f;
    }
    float line_height = 1.55f;

    nvgTextLineHeight(vg, line_height);
    
    float bounds[4]{};
    nvgFontSize(vg, font_size);
    
    float text_area_h = m_compact ? (m_pos.h - 82.f) : (m_pos.h - 86.f - 30.f);
    float text_y_start = m_compact ? m_pos.y : (m_pos.y + 30.f);

    nvgTextBoxBounds(vg, m_pos.x + padding, text_y_start, m_pos.w - padding * 2.f, m_message.c_str(), nullptr, bounds);
    float text_h = bounds[3] - bounds[1];
    float text_y = text_y_start + (text_area_h - text_h) / 2.f;

    gfx::drawTextBox(
        vg, m_pos.x + padding, text_y, font_size, m_pos.w - padding * 2.f,
        theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, line_height
    );
    nvgRestore(vg);

    const float btn_h = m_compact ? 82.f : 86.f;
    const Vec4 button{m_pos.x, m_pos.y + m_pos.h - btn_h, m_pos.w, btn_h};
    gfx::drawRect(vg, button.x, button.y - 2.f, button.w, 2.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    
    if (m_compact) {
        gfx::drawRectOutline(vg, theme, 4.f, Vec4{button.x + 160.f, button.y + 10.f, button.w - 320.f, button.h - 20.f});
        const Vec4 bar{button.x + 180.f, button.y + button.h - 22.f, button.w - 360.f, 6.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * m_progress, bar.h, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 3.f);

        const auto hold_text = "Hold A to continue"_i18n;
        gfx::drawText(
            vg, button.x + button.w / 2.f, button.y + 35.f, 24.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED),
            hold_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE
        );
    } else {
        gfx::drawRectOutline(vg, theme, 4.f, Vec4{button.x + 150.f, button.y + 10.f, button.w - 300.f, button.h - 20.f});
        const Vec4 bar{button.x + 178.f, button.y + button.h - 22.f, button.w - 356.f, 6.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * m_progress, bar.h, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 3.f);

        const auto hold_text = "Hold A for 3 seconds to continue"_i18n;
        gfx::drawText(
            vg, button.x + button.w / 2.f, button.y + 35.f, 22.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED),
            hold_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE
        );
    }
}

} // namespace sphaira::ui
