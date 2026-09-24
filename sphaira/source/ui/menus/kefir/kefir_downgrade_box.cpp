#include "ui/menus/kefir/kefir_internal.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sphaira::ui::menu::kefir {

DowngradeWarningBox::DowngradeWarningBox(
    const std::string& current_version,
    const std::string& target_version,
    const std::string& confirm_label,
    Callback cb
)
: m_current_version{current_version}
, m_target_version{target_version}
, m_callback{std::move(cb)}
, m_qr{QrCode::Encode("https://switch.customfw.xyz/downgrade_fw")}
{
    m_pos.w = 900.f;
    m_pos.h = 580.f;
    m_pos.x = (SCREEN_WIDTH - m_pos.w) / 2.f;
    m_pos.y = (SCREEN_HEIGHT - m_pos.h) / 2.f;

    const std::string text_a = "\uE0E1 " + "Cancel"_i18n;
    const std::string text_b = "\uE0EF " + confirm_label;

    auto box = m_pos;
    box.w /= 2.f;
    box.y = m_pos.y + m_pos.h - DOWNGRADE_BUTTON_HEIGHT;
    box.h = DOWNGRADE_BUTTON_HEIGHT;

    m_entries.emplace_back(text_a, box);
    box.x += box.w;
    m_entries.emplace_back(text_b, box);

    m_index = 1;
    m_entries[0].Selected(false);
    m_entries[1].Selected(true);
    LayoutButtons();

    SetActions(
        std::make_pair(Button::LEFT, Action{[this](){
            SetIndex(0);
        }}),
        std::make_pair(Button::RIGHT, Action{[this](){
            SetIndex(1);
        }}),
        std::make_pair(Button::A, Action{[this](){
            m_callback(m_index);
            SetPop();
        }}),
        std::make_pair(Button::B, Action{[this](){
            m_callback(0);
            SetPop();
        }}),
        std::make_pair(Button::START, Action{[this](){
            m_callback(1);
            SetPop();
        }})
    );
}

auto DowngradeWarningBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (touch->is_clicked) {
        for (s64 i = 0; i < static_cast<s64>(m_entries.size()); i++) {
            if (touch->in_range(m_entries[i].GetPos())) {
                SetIndex(i);
                m_callback(i);
                SetPop();
                break;
            }
        }
    }
}

auto DowngradeWarningBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

    auto draw_box = [&](float x, float& cur_y, float size, float bound, const NVGcolor& colour, const char* str, float line_height = 1.25f, float margin_after = 0.f, bool bold = false) {
        nvgSave(vg);
        nvgFontSize(vg, size);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgTextLineHeight(vg, line_height);
        float b[4]{};
        nvgTextBoxBounds(vg, x, cur_y, bound, str, nullptr, b);
        nvgRestore(vg);
        gfx::drawTextBox(vg, x, cur_y, size, bound, colour, str, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, line_height);
        if (bold) {
            gfx::drawTextBox(vg, x + 0.6f, cur_y, size, bound, colour, str, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, line_height);
        }
        const float text_h = b[3] - b[1];
        cur_y += text_h + margin_after;
    };

    // 1. Title
    const std::string title = "Firmware downgrade warning"_i18n;
    gfx::drawTextBold(vg, m_pos.x + m_pos.w / 2.f, m_pos.y + 20.f, 25.5f,
        theme->GetColour(ThemeEntryID_TEXT_SELECTED), title.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    constexpr float pad_x = 36.f;
    const float col1_x = m_pos.x + pad_x;
    const float content_w = m_pos.w - pad_x * 2.f;

    float cur_y = m_pos.y + 20.f + 25.5f + 14.f;

    // 2. Compact version rows: Current and Target side-by-side
    const std::string current_label = "Current:"_i18n;
    const std::string target_label = "Target:"_i18n;
    const float y_ver = cur_y;

    nvgSave(vg);
    nvgFontSize(vg, 17.f);
    float b1[4]{}, b2[4]{};
    nvgTextBounds(vg, 0.f, 0.f, current_label.c_str(), nullptr, b1);
    nvgTextBounds(vg, 0.f, 0.f, target_label.c_str(), nullptr, b2);
    const float cur_lbl_w = b1[2] - b1[0];
    const float tgt_lbl_w = b2[2] - b2[0];
    nvgRestore(vg);

    gfx::drawTextBold(vg, col1_x, y_ver, 17.f, theme->GetColour(ThemeEntryID_TEXT), current_label.c_str());
    gfx::drawText(vg, col1_x + cur_lbl_w + 8.f, y_ver, 17.f, theme->GetColour(ThemeEntryID_TEXT), m_current_version.c_str());

    const float target_col_x = col1_x + 280.f;
    gfx::drawTextBold(vg, target_col_x, y_ver, 17.f, theme->GetColour(ThemeEntryID_TEXT), target_label.c_str());
    gfx::drawText(vg, target_col_x + tgt_lbl_w + 8.f, y_ver, 17.f, theme->GetColour(ThemeEntryID_TEXT), m_target_version.c_str());

    cur_y += 17.f + 18.f;

    // 3. Automated fix info
    const std::string fix_label = "Downgrade fix:"_i18n;
    const std::string fix_text = "System save 8000000000000073, custom themes, and interface translations will be removed as part of the downgrade process."_i18n;

    nvgSave(vg);
    nvgFontSize(vg, 16.5f);
    float fix_b[4]{};
    nvgTextBounds(vg, 0.f, 0.f, fix_label.c_str(), nullptr, fix_b);
    const float fix_lbl_w = fix_b[2] - fix_b[0];
    nvgRestore(vg);

    gfx::drawTextBold(vg, col1_x, cur_y, 16.5f, theme->GetColour(ThemeEntryID_TEXT), fix_label.c_str());

    const float fix_text_x = col1_x + fix_lbl_w + 8.f;
    const float fix_text_w = content_w - fix_lbl_w - 8.f;
    float fix_y = cur_y;
    draw_box(fix_text_x, fix_y, 15.f, fix_text_w, theme->GetColour(ThemeEntryID_TEXT_INFO), fix_text.c_str(), 1.25f, 0.f);
    cur_y = std::max(cur_y + 16.5f, fix_y) + 22.f;

    // 4. Maintenance Mode instructions
    const std::string maint_title = "If the console fails to boot or shows an error (Maintenance Mode):"_i18n;
    draw_box(col1_x, cur_y, 16.5f, content_w, theme->GetColour(ThemeEntryID_TEXT_SELECTED), maint_title.c_str(), 1.25f, 10.f, /*bold=*/true);

    const std::string step1 = "1. Launch firmware; wait for Nintendo and Kefir boot logos to pass."_i18n;
    draw_box(col1_x + 8.f, cur_y, 15.f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT), step1.c_str(), 1.25f, 8.f);

    const std::string step2 = "2. Press and hold both Volume buttons (+ and -) until Maintenance Mode opens."_i18n;
    draw_box(col1_x + 8.f, cur_y, 15.f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT), step2.c_str(), 1.25f, 8.f);

    const std::string step3 = "3. Select 'Initialize Console Without Deleting Save Data'."_i18n;
    draw_box(col1_x + 8.f, cur_y, 15.f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT), step3.c_str(), 1.25f, 10.f);

    const std::string warn_note = "Warning: All installed games and system settings will be wiped; saves are preserved."_i18n;
    draw_box(col1_x + 8.f, cur_y, 14.5f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT_INFO), warn_note.c_str(), 1.25f, 8.f);

    const std::string sd_note = "Note: The 'Nintendo' folder on the SD card will become invalid and the console will prompt to delete it. Agree to delete it; this will NOT affect your saves."_i18n;
    draw_box(col1_x + 8.f, cur_y, 14.5f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT_INFO), sd_note.c_str(), 1.25f, 26.f);

    // 5. Guide & QR code section + Responsibility note
    const float guide_y = cur_y;

    constexpr int qr_border = 3;
    constexpr float qr_scale = 3.f;
    constexpr float qr_total_w = (QrCode::SIZE + qr_border * 2) * qr_scale;
    const float qr_x = m_pos.x + m_pos.w - pad_x - qr_total_w;
    const float qr_y = guide_y;

    // Draw scan-ready QR code with white quiet zone border
    gfx::drawRect(vg, qr_x, qr_y, qr_total_w, qr_total_w, nvgRGBA(255, 255, 255, 255), 4.f);
    nvgBeginPath(vg);
    for (int qy = 0; qy < QrCode::SIZE; qy++) {
        for (int qx = 0; qx < QrCode::SIZE; qx++) {
            if (m_qr.Get(qx, qy)) {
                nvgRect(vg, qr_x + (qx + qr_border) * qr_scale, qr_y + (qy + qr_border) * qr_scale, qr_scale, qr_scale);
            }
        }
    }
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
    nvgFill(vg);

    // Left text next to QR
    const float left_w = qr_x - col1_x - 18.f;
    const std::string guide_label = "Manual downgrade guide:"_i18n;
    draw_box(col1_x, cur_y, 16.5f, left_w, theme->GetColour(ThemeEntryID_TEXT), guide_label.c_str(), 1.25f, 6.f, /*bold=*/true);

    const char* guide_url = "https://switch.customfw.xyz/downgrade_fw";
    draw_box(col1_x, cur_y, 15.f, left_w, theme->GetColour(ThemeEntryID_TEXT_INFO), guide_url, 1.25f, 10.f);

    const std::string scan_hint = "If you prefer manual downgrade or issues persist, scan the QR code to open the guide."_i18n;
    draw_box(col1_x, cur_y, 14.5f, left_w, theme->GetColour(ThemeEntryID_TEXT), scan_hint.c_str(), 1.25f, 10.f);

    const std::string resp_text = "By continuing, you accept full responsibility."_i18n;
    draw_box(col1_x, cur_y, 14.5f, left_w, theme->GetColour(ThemeEntryID_TEXT_INFO), resp_text.c_str(), 1.25f, 0.f);

    // 6. Separator line and buttons
    gfx::drawRect(vg, m_spacer_line, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    for (auto& entry : m_entries) {
        entry.Draw(vg, theme);
    }
}

auto DowngradeWarningBox::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto DowngradeWarningBox::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

void DowngradeWarningBox::SetIndex(s64 index) {
    if (m_index != index && index >= 0 && index < static_cast<s64>(m_entries.size())) {
        m_entries[m_index].Selected(false);
        m_index = index;
        m_entries[m_index].Selected(true);
    }
}

void DowngradeWarningBox::LayoutButtons() {
    m_spacer_line = Vec4{m_pos.x, m_pos.y + m_pos.h - DOWNGRADE_BUTTON_HEIGHT - 2.f, m_pos.w, 2.f};

    auto box = m_pos;
    box.w = m_pos.w / 2.f;
    box.y = m_pos.y + m_pos.h - DOWNGRADE_BUTTON_HEIGHT;
    box.h = DOWNGRADE_BUTTON_HEIGHT;

    m_entries[0].UpdateLayout(box);
    box.x += box.w;
    m_entries[1].UpdateLayout(box);
}

} // namespace sphaira::ui::menu::kefir
