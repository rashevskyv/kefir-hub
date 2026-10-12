#include "ui/scrollable_text.hpp"
#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "log.hpp"
#include "markdown_lite.hpp"
#include <cstring>
#include <string_view>

namespace sphaira::ui {
namespace {

using Run = ScrollableText::MdRun;
using Mark = ScrollableText::MdMark;
using Style = ScrollableText::MdStyle;
using MdMarkKind = ScrollableText::MdMarkKind;

constexpr float TEXT_X = 110.0F; // left edge of the text, as in plain mode
constexpr float LINE_SPACING = 1.7F; // as in plain mode
constexpr float LIST_INDENT = 32.0F; // per nesting level

auto TextWidth(NVGcontext* vg, float size, std::string_view s) -> float {
    float bounds[4]{};
    nvgFontSize(vg, size);
    return nvgTextBounds(vg, 0, 0, s.data(), s.data() + s.size(), bounds);
}

// One line being filled. Positions are relative to the top of the text.
struct Flow {
    NVGcontext* vg{};
    std::vector<Run>* runs{};
    float size{};
    float line_h{};
    float left{};
    float right{};
    float cx{};
    float cy{};
    float space{};  // width of the space before the next word
    bool line_empty{true};
    Style style{};
};

void NewLine(Flow& f) {
    f.cy += f.line_h;
    f.cx = f.left;
    f.space = 0;
    f.line_empty = true;
}

// Places one word. A word wider than the line is cut at character boundaries.
void PutWord(Flow& f, std::string_view word) {
    // a space run is kept: indentation in code, the gaps in table rows
    if (!f.line_empty && f.cx + f.space + TextWidth(f.vg, f.size, word) > f.right) {
        NewLine(f);
    } else {
        f.cx += f.space;
    }
    f.space = 0;

    while (!word.empty()) {
        std::size_t n = word.size();
        if (f.cx + TextWidth(f.vg, f.size, word) > f.right) {
            // take at least one character, then as many as fit
            const auto next_char = [&](std::size_t k) {
                k++;
                while (k < word.size() && (static_cast<unsigned char>(word[k]) & 0xC0) == 0x80) {
                    k++;
                }
                return k;
            };
            n = next_char(0);
            while (n < word.size() && f.cx + TextWidth(f.vg, f.size, word.substr(0, next_char(n))) <= f.right) {
                n = next_char(n);
            }
        }

        const auto part = word.substr(0, n);
        f.runs->push_back(Run{f.cx, f.cy, f.size, f.style, std::string(part)});
        f.cx += TextWidth(f.vg, f.size, part);
        f.line_empty = false;
        word.remove_prefix(n);
        if (!word.empty()) {
            NewLine(f);
        }
    }
}

void PlaceSpan(Flow& f, const markdown::Span& span, Style style) {
    const auto text = std::string_view(span.text);
    std::size_t i{};
    while (i < text.size()) {
        if (text[i] == ' ' || text[i] == '\t') {
            f.space += TextWidth(f.vg, f.size, " ") * (text[i] == '\t' ? 4 : 1);
            i++;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t') {
            j++;
        }
        f.style = style;
        PutWord(f, text.substr(i, j - i));
        i = j;
    }
}

// Styles a span as the block asks, with the span's own bold/italic/code on top.
auto SpanStyle(const markdown::Span& span, Style block_style) -> Style {
    if (span.code) {
        return Style::Code;
    }
    if (block_style == Style::Heading) {
        return Style::Heading;
    }
    if (span.bold) {
        return Style::Bold;
    }
    if (span.italic || block_style == Style::Italic) {
        return Style::Italic;
    }
    return Style::Body;
}

// Lays out all blocks from y = 0. Returns the total height.
auto LayoutMarkdown(NVGcontext* vg, const std::vector<markdown::Block>& blocks, float body, float width, std::vector<Run>& runs, std::vector<Mark>& marks) -> float {
    const float line = body * LINE_SPACING;
    float y{};

    for (const auto& b : blocks) {
        if (b.kind == markdown::Kind::Paragraph && b.spans.empty()) {
            y += line * 0.6F; // blank gap
            continue;
        }
        if (b.kind == markdown::Kind::Rule) {
            marks.push_back(Mark{TEXT_X, y + line * 0.5F, width, 2, MdMarkKind::Rule});
            y += line;
            continue;
        }

        float size = body;
        float text_x = TEXT_X;
        Style block_style = Style::Body;
        if (b.kind == markdown::Kind::Heading) {
            size = body + (b.level == 1 ? 6.0F : (b.level == 2 ? 3.0F : 1.0F));
            block_style = Style::Heading;
            if (y > 0) {
                y += line * 0.3F;
            }
        } else if (b.kind == markdown::Kind::Bullet || b.kind == markdown::Kind::Numbered) {
            // room for a number like "100." after the level indent
            text_x = TEXT_X + static_cast<float>(b.level) * LIST_INDENT + (b.kind == markdown::Kind::Numbered ? 44.0F : 24.0F);
        } else if (b.kind == markdown::Kind::Quote) {
            text_x = TEXT_X + 24.0F;
            block_style = Style::Italic;
        } else if (b.kind == markdown::Kind::Code) {
            text_x = TEXT_X + 16.0F;
            block_style = Style::Code;
        }

        const float lh = size * LINE_SPACING;
        const float top = y;
        Flow f{vg, &runs, size, lh, text_x, TEXT_X + width, text_x, top, 0, true, Style::Body};

        if (b.kind == markdown::Kind::Bullet) {
            const float r = size * 0.16F;
            const float cx = TEXT_X + static_cast<float>(b.level) * LIST_INDENT + 8.0F;
            marks.push_back(Mark{cx - r, top + size * 0.55F - r, 2 * r, 2 * r, MdMarkKind::Dot});
        } else if (b.kind == markdown::Kind::Numbered) {
            const auto label = std::to_string(b.number) + ".";
            runs.push_back(Run{TEXT_X + static_cast<float>(b.level) * LIST_INDENT, top, size, Style::Body, label});
        }

        for (const auto& span : b.spans) {
            PlaceSpan(f, span, SpanStyle(span, block_style));
        }

        const float height = (f.cy - top) + lh;
        if (b.kind == markdown::Kind::Quote) {
            marks.push_back(Mark{TEXT_X, top, 4, height, MdMarkKind::Bar});
        }
        y = top + height;
        if (b.kind == markdown::Kind::Heading) {
            y += line * 0.2F;
        }
    }

    return y;
}

} // namespace

ScrollableText::ScrollableText(const std::string& text, float x, float y, float y_clip, float w, float font_size, bool as_markdown)
: m_font_size{font_size}
, m_y_off_base{y}
, m_clip_y{y_clip}
, m_end_w{w}
, m_y_off{y}
{
    SetActions(
        std::make_pair(Button::LS_DOWN, Action{[this](){
            const auto bound = m_bounds[3];
            if (bound < m_clip_y) {
                return;
            }
            const auto a = m_y_off_base + m_clip_y;
            const auto norm = m_bounds[3] - m_bounds[1];
            const auto b = m_y_off + norm;
            if (b <= a) {
                return;
            }
            m_y_off -= m_step;
            m_index++;
            App::PlaySoundEffect(SoundEffect_Scroll);
        }}),
        std::make_pair(Button::LS_UP, Action{[this](){
            if (m_y_off == m_y_off_base) {
                return;
            }
            m_y_off += m_step;
            m_index--;
            App::PlaySoundEffect(SoundEffect_Scroll);
        }})
    );

    #if 1
    // converts '\''n' to '\n' without including <regex> because it bloats
    // the binary by over 400 KiB lol
    const auto mini_regex = [](std::string_view str, std::string_view regex, std::string_view replace) -> std::string {
        std::string out;
        out.reserve(str.size());
        u32 i{};

        while (i < str.size()) {
            if ((i + regex.size()) <= str.size() && !std::memcmp(str.data() + i, regex.data(), regex.size())) {
                out.append(replace.data(), replace.size());
                i += regex.size();
            } else {
                out.push_back(str[i]);
                i++;
            }
        }

        return out;
    };

    m_text = mini_regex(text, "\r", "");
    m_text = mini_regex(m_text, "\\n", "\n");
    #else
    m_text = std::regex_replace(text, std::regex("\\\\n"), "\n");
    #endif
    if (m_text.size() > 4096) {
        m_text.resize(4096);
        m_text += "...";
    }

    if (as_markdown) {
        m_markdown = true;
        const auto height = LayoutMarkdown(App::GetVg(), markdown::Parse(m_text), m_font_size, m_end_w, m_runs, m_marks);
        m_bounds[0] = TEXT_X;
        m_bounds[1] = m_y_off_base;
        m_bounds[2] = TEXT_X + m_end_w;
        m_bounds[3] = m_y_off_base + height;
        return;
    }

    nvgFontSize(App::GetVg(), m_font_size);
    nvgTextLineHeight(App::GetVg(), 1.7);
    nvgTextBoxBounds(App::GetVg(), 110.0F, m_y_off_base, m_end_w, m_text.c_str(), nullptr, m_bounds);
    // log_write("bounds x: %.2f y: %.2f w: %.2f h: %.2f\n", m_bounds[0], m_bounds[1], m_bounds[2], m_bounds[3]);
}

// void ScrollableText::Update(Controller* controller, TouchInfo* touch) {

// }

void ScrollableText::Draw(NVGcontext* vg, Theme* theme) {
    Widget::Draw(vg, theme);

    const Vec4 line_vec(30, 86, 1220, 646);
    // const Vec4 banner_vec(70, line_vec.y + 20, 848.f, 208.f);
    const Vec4 banner_vec(70, line_vec.y + 20, m_end_w + (110.0F), 208.f);

    const auto max_index = (m_bounds[3] - m_bounds[1]) / m_step;
    gfx::drawScrollbar2(vg, theme, banner_vec.w + 25, m_y_off_base, m_clip_y, m_index, max_index, 1, m_clip_y / m_step - 1);

    nvgSave(vg);
    nvgIntersectScissor(vg, 0, m_y_off_base - m_font_size, 1280, m_clip_y + m_font_size); // clip

    if (m_markdown) {
        // skip what is far outside the clip, scissor does the rest
        const auto top = m_y_off_base - m_font_size;
        const auto bottom = m_y_off_base + m_clip_y;

        for (const auto& m : m_marks) {
            const auto y = m_y_off + m.y;
            if (y + m.h < top || y > bottom) {
                continue;
            }
            const auto c = theme->GetColour(m.kind == MdMarkKind::Bar ? ThemeEntryID_FOCUS : ThemeEntryID_TEXT_INFO);
            nvgBeginPath(vg);
            if (m.kind == MdMarkKind::Dot) {
                nvgCircle(vg, m.x + m.w / 2, y + m.h / 2, m.w / 2);
            } else {
                nvgRect(vg, m.x, y, m.w, m.h);
            }
            nvgFillColor(vg, c);
            nvgFill(vg);
        }

        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        for (const auto& r : m_runs) {
            const auto y = m_y_off + r.y;
            if (y + r.size < top || y > bottom) {
                continue;
            }
            // no bold or italic font: bold is drawn twice, italic is dimmer
            const bool faux_bold = r.style == MdStyle::Bold || r.style == MdStyle::Heading;
            const auto c = theme->GetColour(r.style == MdStyle::Italic ? ThemeEntryID_TEXT_INFO
                : (r.style == MdStyle::Code || r.style == MdStyle::Heading) ? ThemeEntryID_FOCUS
                : ThemeEntryID_TEXT);
            nvgFontSize(vg, r.size);
            nvgFillColor(vg, c);
            nvgText(vg, r.x, y, r.text.c_str(), nullptr);
            if (faux_bold) {
                nvgText(vg, r.x + 0.6F, y, r.text.c_str(), nullptr);
            }
        }
        nvgRestore(vg);
        return;
    }

    nvgTextLineHeight(App::GetVg(), 1.7);
    gfx::drawTextBox(vg, banner_vec.x + 40, m_y_off, m_font_size, m_bounds[2] - m_bounds[0], theme->GetColour(ThemeEntryID_TEXT), m_text.c_str());
    nvgRestore(vg);
}

} // namespace sphaira::ui
