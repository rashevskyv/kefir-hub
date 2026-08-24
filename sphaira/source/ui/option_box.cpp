#include "ui/option_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"

#include <algorithm>

namespace sphaira::ui {
namespace {

constexpr float OPTION_BUTTON_HEIGHT = 75.f;
constexpr float OPTION_NO_IMAGE_TEXT_Y = 58.f;
constexpr float OPTION_IMAGE_TEXT_Y = 110.f;

auto EstimateLineCount(const std::string& text, size_t chars_per_line) -> size_t {
    size_t lines = 1;
    size_t current = 0;

    for (const auto c : text) {
        if (c == '\n') {
            lines += std::max<size_t>(1, (current + chars_per_line - 1) / chars_per_line);
            current = 0;
            continue;
        }

        current++;
    }

    lines += std::max<size_t>(1, (current + chars_per_line - 1) / chars_per_line) - 1;
    return lines;
}

auto GetFontSize(const std::string& message, bool has_image) -> float {
    if (has_image) {
        return 22.f;
    }
    if (message.length() < 60) {
        return 26.f;
    } else if (message.length() < 120) {
        return 22.f;
    }
    return 18.f;
}

auto GetCharsPerLine(float font_size, bool has_image) -> size_t {
    if (has_image) {
        return 36;
    }
    return static_cast<size_t>(710.f / (font_size * 0.55f));
}

auto CalculateButtonYoff(const std::string& message, bool has_image) -> float {
    const auto font_size = GetFontSize(message, has_image);
    const auto text_y = has_image ? OPTION_IMAGE_TEXT_Y : OPTION_NO_IMAGE_TEXT_Y;
    const auto chars_per_line = GetCharsPerLine(font_size, has_image);
    const auto min_yoff = has_image ? 220.f : 190.f;
    const auto lines = static_cast<float>(EstimateLineCount(message, chars_per_line));

    return std::max(min_yoff, text_y + lines * font_size * 1.5f + 28.f);
}

auto AddGlyphIfMissing(const std::string& text, const std::string& glyph) -> std::string {
    if (text.empty()) {
        return text;
    }
    if (text.find(glyph) == 0) {
        return text;
    }
    return glyph + " " + text;
}

} // namespace

OptionBoxEntry::OptionBoxEntry(const std::string& text, Vec4 pos)
: m_text{text} {
    m_pos = pos;
    m_text_pos = Vec2{m_pos.x + (m_pos.w / 2.f), m_pos.y + (m_pos.h / 2.f)};
}

auto OptionBoxEntry::Draw(NVGcontext* vg, Theme* theme) -> void {
    const auto colour = theme->GetColour(m_selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
    if (m_selected) {
        gfx::drawRectOutline(vg, theme, 4.f, m_pos);
    }

    constexpr float pad = 10.f;
    const float inner_w = m_pos.w - pad * 2.f;
    constexpr float sizes[] = {26.f, 22.f, 18.f};
    float size = 26.f;
    bool fits = false;
    for (const float sz : sizes) {
        nvgFontSize(vg, sz);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        float b[4]{};
        nvgTextBounds(vg, 0.f, 0.f, m_text.c_str(), nullptr, b);
        if (b[2] - b[0] <= inner_w) {
            size = sz;
            fits = true;
            break;
        }
        size = sz;
    }

    if (fits) {
        gfx::drawText(vg, m_text_pos, size, colour, m_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }

    gfx::drawTextBox(vg, m_pos.x + pad, m_text_pos.y, size, inner_w, colour, m_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, nullptr, 1.15f);
}

auto OptionBoxEntry::Selected(bool enable) -> void {
    m_selected = enable;
}

void OptionBoxEntry::UpdateLayout(const Vec4& pos) {
    m_pos = pos;
    m_text_pos = Vec2{m_pos.x + (m_pos.w / 2.f), m_pos.y + (m_pos.h / 2.f)};
}

OptionBox::OptionBox(const std::string& message, const Option& a, const Callback& cb, int image, bool own_image)
: m_message{message}
, m_callback{cb}
, m_image{image}
, m_own_image{own_image} {

    m_pos.w = 770.f;
    m_button_yoff = CalculateButtonYoff(message, image != 0);
    m_pos.h = m_button_yoff + OPTION_BUTTON_HEIGHT;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    auto box = m_pos;
    box.y += m_button_yoff;
    box.h -= m_button_yoff;
    m_entries.emplace_back(a, box);

    Setup(0);
}

OptionBox::OptionBox(const std::string& message, const Option& a, const Option& b, const Callback& cb, int image, bool own_image)
: OptionBox{message, a, b, 0, cb, image, own_image} {

}

OptionBox::OptionBox(const std::string& message, const Option& a, const Option& b, s64 index, const Callback& cb, int image, bool own_image)
: m_message{message}
, m_callback{cb}
, m_image{image}
, m_own_image{own_image} {

    m_pos.w = 770.f;
    m_button_yoff = CalculateButtonYoff(message, image != 0);
    m_pos.h = m_button_yoff + OPTION_BUTTON_HEIGHT;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    auto box = m_pos;
    box.w /= 2.f;
    box.y += m_button_yoff;
    box.h -= m_button_yoff;

    std::string text_a = AddGlyphIfMissing(a, "\uE0E1"); // B (No/Cancel)
    std::string text_b = AddGlyphIfMissing(b, "\uE0EF"); // + (Yes/Accept)

    m_entries.emplace_back(text_a, box);
    box.x += box.w;
    m_entries.emplace_back(text_b, box);

    Setup(index);
}

OptionBox::OptionBox(const std::string& message, const Option& a, const Option& b, const Option& c, s64 index, const Callback& cb, int image, bool own_image)
: m_message{message}
, m_callback{cb}
, m_image{image}
, m_own_image{own_image} {

    m_pos.w = 770.f;
    m_button_yoff = CalculateButtonYoff(message, image != 0);
    m_pos.h = m_button_yoff + OPTION_BUTTON_HEIGHT;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    auto box = m_pos;
    box.w /= 3.f;
    box.y += m_button_yoff;
    box.h -= m_button_yoff;

    m_entries.emplace_back(AddGlyphIfMissing(a, "\uE0E1"), box);
    box.x += box.w;
    m_entries.emplace_back(b, box);
    box.x += box.w;
    m_entries.emplace_back(AddGlyphIfMissing(c, "\uE0EF"), box);

    Setup(index);
}

OptionBox::~OptionBox() {
    if (m_image && m_own_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }
}

auto OptionBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (touch->is_clicked) {
        for (s64 i = 0; i < m_entries.size(); i++) {
            auto& e = m_entries[i];
            if (touch->in_range(e.GetPos())) {
                SetIndex(i);
                FireAction(Button::A);
                break;
            }
        }
    }
}

auto OptionBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    if (!m_layout_done) {
        float text_h = 0.f;
        if (m_image) {
            const float padding = 40.f;
            const float text_w = m_pos.w - 40.f - 150.f - padding - padding;
            nvgSave(vg);
            nvgFontSize(vg, 22.f);
            nvgTextLineHeight(vg, 1.4f);
            float bounds[4];
            nvgTextBoxBounds(vg, 0.f, 0.f, text_w, m_message.c_str(), nullptr, bounds);
            text_h = bounds[3] - bounds[1];
            nvgRestore(vg);
            
            m_button_yoff = 40.f + std::max(150.f, text_h) + 40.f;
            m_button_yoff = std::max(230.f, m_button_yoff);
        } else {
            const float padding = 30.f;
            const float font_size = GetFontSize(m_message, false);
            nvgSave(vg);
            nvgFontSize(vg, font_size);
            nvgTextLineHeight(vg, 1.4f);
            float bounds[4];
            nvgTextBoxBounds(vg, 0.f, 0.f, m_pos.w - padding * 2.f, m_message.c_str(), nullptr, bounds);
            text_h = bounds[3] - bounds[1];
            nvgRestore(vg);
            
            m_button_yoff = 40.f + text_h + 40.f;
            m_button_yoff = std::max(190.f, m_button_yoff);
        }

        // Recalculate popup size and position
        m_pos.h = m_button_yoff + OPTION_BUTTON_HEIGHT;
        m_pos.y = (SCREEN_HEIGHT - m_pos.h) / 2.f;

        // Re-align buttons
        m_spacer_line = Vec4{m_pos.x, m_pos.y + m_button_yoff - 2.f, m_pos.w, 2.f};
        
        auto box = m_pos;
        box.y += m_button_yoff;
        box.h -= m_button_yoff;
        
        if (m_entries.size() == 1) {
            m_entries[0].UpdateLayout(box);
        } else if (!m_entries.empty()) {
            box.w /= static_cast<float>(m_entries.size());
            for (auto& e : m_entries) {
                e.UpdateLayout(box);
                box.x += box.w;
            }
        }

        m_layout_done = true;
    }

    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5);

    nvgSave(vg);
    if (m_image) {
        Vec4 image{m_pos};
        image.x += 40;
        image.y += 40;
        image.w = 150;
        image.h = 150;

        const float padding = 40;
        gfx::drawImage(vg, image, m_image, 5);

        // Measure text height to vertically center it next to the image
        float bounds[4];
        nvgFontSize(vg, 22.f);
        nvgTextLineHeight(vg, 1.4f);
        nvgTextBoxBounds(vg, 0.f, 0.f, m_pos.w - (image.x - m_pos.x) - image.w - padding * 2, m_message.c_str(), nullptr, bounds);
        const float text_h = bounds[3] - bounds[1];
        const float text_y = m_pos.y + 40.f + std::max(0.f, (150.f - text_h) / 2.f);

        gfx::drawTextBox(vg, image.x + image.w + padding, text_y, 22.f, m_pos.w - (image.x - m_pos.x) - image.w - padding * 2, theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.4f);
    } else {
        const float padding = 30;
        const float font_size = GetFontSize(m_message, false);

        nvgFontSize(vg, font_size);
        nvgTextLineHeight(vg, 1.4f);
        float bounds[4];
        nvgTextBoxBounds(vg, m_pos.x + padding, 0.f, m_pos.w - padding * 2, m_message.c_str(), nullptr, bounds);
        const float text_h = bounds[3] - bounds[1];
        const float text_y = m_pos.y + std::max(20.f, (m_button_yoff - text_h) / 2.f);

        gfx::drawTextBox(vg, m_pos.x + padding, text_y, font_size, m_pos.w - padding * 2, theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, 1.4f);
    }
    nvgRestore(vg);

    gfx::drawRect(vg, m_spacer_line, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    for (auto& p : m_entries) {
        p.Draw(vg, theme);
    }
}

auto OptionBox::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto OptionBox::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

auto OptionBox::Setup(s64 index) -> void {
    m_index = std::min<s64>(m_entries.size() - 1, index);
    m_entries[m_index].Selected(true);
    m_spacer_line = Vec4{m_pos.x, m_pos.y + m_button_yoff - 2.f, m_pos.w, 2.f};

    SetActions(
        std::make_pair(Button::LEFT, Action{[this](){
            if (m_index) {
                SetIndex(m_index - 1);
            }
        }}),
        std::make_pair(Button::RIGHT, Action{[this](){
            if (m_index < (m_entries.size() - 1)) {
                SetIndex(m_index + 1);
            }
        }}),
        std::make_pair(Button::A, Action{[this](){
            m_callback(m_index);
            SetPop();
        }}),
        std::make_pair(Button::B, Action{[this](){
            if (m_entries.size() >= 2) {
                m_callback(0);
            } else {
                m_callback({});
            }
            SetPop();
        }}),
        std::make_pair(Button::START, Action{[this](){
            if (m_entries.size() >= 2) {
                m_callback(static_cast<s64>(m_entries.size() - 1));
                SetPop();
            }
        }})
    );
}

void OptionBox::SetIndex(s64 index) {
    if (m_index != index) {
        m_entries[m_index].Selected(false);
        m_index = index;
        m_entries[m_index].Selected(true);
    }
}

} // namespace sphaira::ui
