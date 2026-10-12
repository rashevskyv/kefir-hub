#pragma once

#include "ui/widget.hpp"

#include <string>
#include <vector>

namespace sphaira::ui {

// todo: remove fixed values from appstore
struct ScrollableText final : Widget {
    // markdown: render the text as Markdown (bold, headings, lists, links) instead of plain text.
    ScrollableText(const std::string& text, float x, float y, float y_clip, float w, float font_size, bool as_markdown = false);
    void Draw(NVGcontext* vg, Theme* theme) override;

    // markdown mode: one word (or word part) at a laid out position, relative to the top of the text.
    enum class MdStyle { Body, Bold, Italic, Code, Heading };
    struct MdRun {
        float x;
        float y;
        float size;
        MdStyle style;
        std::string text;
    };
    // markdown mode: bullet dot, quote bar or rule line.
    enum class MdMarkKind { Dot, Bar, Rule };
    struct MdMark {
        float x;
        float y;
        float w;
        float h;
        MdMarkKind kind;
    };

    std::string m_text;
    bool m_markdown{};
    std::vector<MdRun> m_runs{};
    std::vector<MdMark> m_marks{};
    // static constexpr float m_y_off_base = 374;
    // float m_y_off = m_y_off_base;
    // static constexpr float m_clip_y = 250.0F;

    static constexpr inline float m_step = 30;
    const float m_font_size;
    const float m_y_off_base;
    const float m_clip_y;
    const float m_end_w;

    float m_y_off{};
    int m_index{};
    float m_bounds[4]{};
};

} // namespace sphaira::ui
