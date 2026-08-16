#include "ui/nvg_util.hpp"
#include "log.hpp"
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <array>
#include <utility>
#include <algorithm>
#include <cmath>

namespace sphaira::ui::gfx {
namespace {

constexpr auto ALIGN_HOR = NVG_ALIGN_LEFT|NVG_ALIGN_CENTER|NVG_ALIGN_RIGHT;
constexpr auto ALIGN_VER = NVG_ALIGN_TOP|NVG_ALIGN_MIDDLE|NVG_ALIGN_BOTTOM|NVG_ALIGN_BASELINE;

constexpr std::array buttons = {
    std::pair{Button::A, "\uE0E0"},
    std::pair{Button::B, "\uE0E1"},
    std::pair{Button::X, "\uE0E2"},
    std::pair{Button::Y, "\uE0E3"},
    std::pair{Button::L, "\uE0E4"},
    std::pair{Button::R, "\uE0E5"},
    std::pair{Button::L2, "\uE0E6"},
    std::pair{Button::R2, "\uE0E7"},
    std::pair{Button::UP, "\uE0EB"},
    std::pair{Button::DOWN, "\uE0EC"},
    std::pair{Button::LEFT, "\uE0ED"},
    std::pair{Button::RIGHT, "\uE0EE"},
    std::pair{Button::START, "\uE0EF"},
    std::pair{Button::SELECT, "\uE0F0"},
    // std::pair{Button::LS, "\uE101"},
    // std::pair{Button::RS, "\uE102"},
    std::pair{Button::L3, "\uE104"},
    std::pair{Button::R3, "\uE105"},
};

// software based clipping, saves a few cpu cycles.
bool ClipRect(float x, float y) {
    return x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT;
}

bool ClipText(float x, float y, int align) {
    if ((!(align & ALIGN_HOR) || (align & NVG_ALIGN_LEFT)) && x >= SCREEN_WIDTH) {
        return true;
    }

    if ((!(align & ALIGN_VER) || (align & NVG_ALIGN_TOP)) && y >= SCREEN_HEIGHT) {
        return true;
    }

    return false;
}

// NEW ---------------------
void drawRectIntenal(NVGcontext* vg, const Vec4& v, const NVGcolor& c, float rounded) {
    if (ClipRect(v.x, v.y)) {
        return;
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, v.x, v.y, v.w, v.h, rounded);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

void drawRectIntenal(NVGcontext* vg, const Vec4& v, const NVGpaint& p, float rounded) {
    if (ClipRect(v.x, v.y)) {
        return;
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, v.x, v.y, v.w, v.h, rounded);
    nvgFillPaint(vg, p);
    nvgFill(vg);
}

void drawRectOutlineInternal(NVGcontext* vg, const Theme* theme, float size, const Vec4& v, float corner_radius = 4.0F) {
    float gradientX, gradientY, color;
    getHighlightAnimation(&gradientX, &gradientY, &color);

    const auto strokeWidth = size;
    // const auto strokeWidth = 5.F;
    auto v2 = v;
    v2.x -= strokeWidth / 2.F;
    v2.y -= strokeWidth / 2.F;
    v2.w += strokeWidth;
    v2.h += strokeWidth;

    const auto shadow_width = 2.F;
    const auto shadow_offset = 10.F;
    const auto shadow_feather = 10.F;
    const auto shadow_opacity = 128.F;

    // Shadow
    NVGpaint shadowPaint = nvgBoxGradient(vg,
        v2.x, v2.y + shadow_width,
        v2.w, v2.h,
        corner_radius * 2, shadow_feather,
        nvgRGBA(0, 0, 0, shadow_opacity * 1.f), nvgRGBA(0, 0, 0, 0));

    nvgBeginPath(vg);
    nvgRect(vg, v2.x - shadow_offset, v2.y - shadow_offset,
        v2.w + shadow_offset * 2, v2.h + shadow_offset * 3);
    nvgRoundedRect(vg, v2.x, v2.y, v2.w, v2.h, corner_radius);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadowPaint);
    nvgFill(vg);

    const auto color1 = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
    const auto color2 = theme->GetColour(ThemeEntryID_HIGHLIGHT_2);
    const auto borderColor = nvgRGBAf(color2.r, color2.g, color2.b, 0.5);
    const auto transparent = nvgRGBA(0, 0, 0, 0);

    const auto pulsationColor = nvgRGBAf((color * color1.r) + (1 - color) * color2.r,
        (color * color1.g) + (1 - color) * color2.g,
        (color * color1.b) + (1 - color) * color2.b,
        1.f);

    const auto border1Paint = nvgRadialGradient(vg,
        v2.x + gradientX * v2.w, v2.y + gradientY * v2.h,
        strokeWidth * 10, strokeWidth * 40,
        borderColor, transparent);

    const auto border2Paint = nvgRadialGradient(vg,
        v2.x + (1 - gradientX) * v2.w, v2.y + (1 - gradientY) * v2.h,
        strokeWidth * 10, strokeWidth * 40,
        borderColor, transparent);

    nvgBeginPath(vg);
    nvgStrokeColor(vg, pulsationColor);
    nvgStrokeWidth(vg, strokeWidth);
    nvgRoundedRect(vg, v2.x, v2.y, v2.w, v2.h, corner_radius);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgStrokePaint(vg, border1Paint);
    nvgStrokeWidth(vg, strokeWidth);
    nvgRoundedRect(vg, v2.x, v2.y, v2.w, v2.h, corner_radius);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgStrokePaint(vg, border2Paint);
    nvgStrokeWidth(vg, strokeWidth);
    nvgRoundedRect(vg, v2.x, v2.y, v2.w, v2.h, corner_radius);
    nvgStroke(vg);
}

void drawRectOutlineInternal(NVGcontext* vg, const Theme* theme, float size, const Vec4& v, const NVGcolor& c, float corner_radius = 4.0F) {
    if (ClipRect(v.x, v.y)) {
        return;
    }

    drawRectOutlineInternal(vg, theme, size, v, corner_radius);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, v.x, v.y, v.w, v.h, corner_radius);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

void drawTextIntenal(NVGcontext* vg, const Vec2& v, float size, const char* str, const char* end, int align, const NVGcolor& c) {
    if (ClipText(v.x, v.y, align)) {
        return;
    }

    nvgBeginPath(vg);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, align);
    nvgFillColor(vg, c);
    nvgText(vg, v.x, v.y, str, end);
}

void drawTriangleInternal(NVGcontext* vg, float aX, float aY, float bX, float bY, float cX, float cY, const NVGcolor& c) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, aX, aY);
    nvgLineTo(vg, bX, bY);
    nvgLineTo(vg, cX, cY);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

void drawTriangleInternal(NVGcontext* vg, float aX, float aY, float bX, float bY, float cX, float cY, const NVGpaint& p) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, aX, aY);
    nvgLineTo(vg, bX, bY);
    nvgLineTo(vg, cX, cY);
    nvgFillPaint(vg, p);
    nvgFill(vg);
}

} // namespace

const char* getButton(const Button want) {
    for (auto& [key, val] : buttons) {
        if (key == want) {
            return val;
        }
    }
    std::unreachable();
}

void drawTextArgs(NVGcontext* vg, float x, float y, float size, int align, const NVGcolor& c, const char* str, ...) {
    std::va_list v;
    va_start(v, str);
    char buffer[0x100];
    std::vsnprintf(buffer, sizeof(buffer), str, v);
    va_end(v);
    drawText(vg, x, y, size, buffer, nullptr, align, c);
}

void drawTextBold(NVGcontext* vg, float x, float y, float size, const NVGcolor& c, const char* str, int align) {
    // ~0.6px over-draw reads as a heavier weight at the sizes used in menus.
    drawTextIntenal(vg, {x, y}, size, str, nullptr, align, c);
    drawTextIntenal(vg, {x + 0.6f, y}, size, str, nullptr, align, c);
}

void drawImage(NVGcontext* vg, const Vec4& v, int texture, float rounded, float alpha) {
    const auto paint = nvgImagePattern(vg, v.x, v.y, v.w, v.h, 0, texture, alpha);
    drawRect(vg, v, paint, rounded);
}

void drawImage(NVGcontext* vg, float x, float y, float w, float h, int texture, float rounded, float alpha) {
    drawImage(vg, Vec4(x, y, w, h), texture, rounded, alpha);
}

void drawTextBox(NVGcontext* vg, float x, float y, float size, float bound, const NVGcolor& c, const char* str, int align, const char* end, float line_height) {
    if (ClipText(x, y, align)) {
        return;
    }

    nvgBeginPath(vg);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, align);
    nvgFillColor(vg, c);
    nvgTextLineHeight(vg, line_height);
    nvgTextBox(vg, x, y, bound, str, end);
    nvgTextLineHeight(vg, 1.0f);
}

void textBounds(NVGcontext* vg, float x, float y, float *bounds, const char* str) {
    nvgTextBounds(vg, x, y, str, nullptr, bounds);
}

void textBoundsArgs(NVGcontext* vg, float x, float y, float *bounds, const char* str, ...) {
    char buf[0x100];
    va_list v;
    va_start(v, str);
    std::vsnprintf(buf, sizeof(buf), str, v);
    va_end(v);
    textBounds(vg, x, y, bounds, buf);
}

// NEW-----------

void dimBackground(NVGcontext* vg) {
    drawRectIntenal(vg, {0.f,0.f,SCREEN_WIDTH,SCREEN_HEIGHT}, nvgRGBA(0, 0, 0, 180), false);
}

void drawRect(NVGcontext* vg, float x, float y, float w, float h, const NVGcolor& c, float rounded) {
    drawRectIntenal(vg, {x,y,w,h}, c, rounded);
}

void drawRect(NVGcontext* vg, const Vec4& v, const NVGcolor& c, float rounded) {
    drawRectIntenal(vg, v, c, rounded);
}

void drawRect(NVGcontext* vg, float x, float y, float w, float h, const NVGpaint& p, float rounded) {
    drawRectIntenal(vg, {x,y,w,h}, p, rounded);
}

void drawRect(NVGcontext* vg, const Vec4& v, const NVGpaint& p, float rounded) {
    drawRectIntenal(vg, v, p, rounded);
}

void drawRectVarying(NVGcontext* vg, const Vec4& v, const NVGcolor& c, float rTL, float rTR, float rBR, float rBL) {
    if (ClipRect(v.x, v.y)) {
        return;
    }

    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, v.x, v.y, v.w, v.h, rTL, rTR, rBR, rBL);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

void drawRectOutline(NVGcontext* vg, const Theme* theme, float size, float x, float y, float w, float h, float rounding) {
    drawRectOutlineInternal(vg, theme, size, {x,y,w,h}, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), rounding);
}

void drawRectOutline(NVGcontext* vg, const Theme* theme, float size, const Vec4& v, float rounding) {
    drawRectOutlineInternal(vg, theme, size, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), rounding);
}

void drawCheckbox(NVGcontext* vg, const Theme* theme, float x, float y, float size, bool checked) {
    nvgBeginPath(vg);
    nvgRect(vg, x, y, size, size);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_BACKGROUND));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, x, y, size, size);
    nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    nvgStrokeWidth(vg, 2.f);
    nvgStroke(vg);

    if (!checked) {
        return;
    }

    // the tick is a glyph, not a shape, so it gets a 1px black halo to stay
    // legible on a light theme where box and tick would otherwise both be pale.
    constexpr float tick = 18.f;
    const float cx = x + size / 2.f;
    const float cy = y + (size - tick) / 2.f;
    const auto outline = nvgRGBA(0, 0, 0, 255);

    drawText(vg, cx - 1.f, cy, tick, "", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
    drawText(vg, cx + 1.f, cy, tick, "", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
    drawText(vg, cx, cy - 1.f, tick, "", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
    drawText(vg, cx, cy + 1.f, tick, "", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
    drawText(vg, cx, cy, tick, "", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
}

void drawActionIcon(NVGcontext* vg, const Theme* theme, float x, float y, float size, ActionIcon icon) {
    const auto colour = theme->elements[ThemeEntryID_ICON_COLOUR].type == ElementType::Colour
        ? theme->GetColour(ThemeEntryID_ICON_COLOUR)
        : theme->GetColour(ThemeEntryID_TEXT);
    const float s = size / 24.f;
    const auto line = [&](float x1, float y1, float x2, float y2) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + x1 * s, y + y1 * s);
        nvgLineTo(vg, x + x2 * s, y + y2 * s);
        nvgStrokeColor(vg, colour);
        nvgStrokeWidth(vg, 2.f * s);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    };

    switch (icon) {
        case ActionIcon::Copy:
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 8.f * s, y + 3.f * s, 11.f * s, 14.f * s, 1.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 4.f * s, y + 7.f * s, 11.f * s, 14.f * s, 1.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            break;
        case ActionIcon::Cut:
            nvgBeginPath(vg); nvgCircle(vg, x + 6.f * s, y + 18.f * s, 2.5f * s); nvgCircle(vg, x + 18.f * s, y + 18.f * s, 2.5f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            line(8.f, 16.f, 18.f, 5.f); line(16.f, 16.f, 6.f, 5.f);
            break;
        case ActionIcon::Paste:
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 5.f * s, y + 5.f * s, 14.f * s, 16.f * s, 1.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 9.f * s, y + 2.f * s, 6.f * s, 5.f * s, 1.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            line(9.f, 12.f, 16.f, 12.f); line(9.f, 16.f, 15.f, 16.f);
            break;
        case ActionIcon::Delete:
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 7.f * s, y + 7.f * s, 10.f * s, 14.f * s, 1.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            line(5.f, 6.f, 19.f, 6.f); line(10.f, 3.f, 14.f, 3.f); line(10.f, 11.f, 10.f, 17.f); line(14.f, 11.f, 14.f, 17.f);
            break;
        case ActionIcon::Edit:
            line(5.f, 19.f, 7.f, 14.f); line(7.f, 14.f, 17.f, 4.f); line(17.f, 4.f, 20.f, 7.f); line(20.f, 7.f, 10.f, 17.f); line(10.f, 17.f, 5.f, 19.f);
            break;
        case ActionIcon::Insert:
            line(12.f, 4.f, 12.f, 20.f); line(4.f, 12.f, 20.f, 12.f);
            break;
        case ActionIcon::Join:
            line(4.f, 8.f, 9.f, 8.f); line(15.f, 8.f, 20.f, 8.f); line(4.f, 16.f, 9.f, 16.f); line(15.f, 16.f, 20.f, 16.f);
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 8.f * s, y + 9.f * s, 8.f * s, 6.f * s, 3.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            break;
        case ActionIcon::Undo:
        case ActionIcon::Redo: {
            const bool redo = icon == ActionIcon::Redo;
            const float left = redo ? 5.f : 19.f, right = redo ? 19.f : 5.f, tip = redo ? 10.f : 14.f;
            line(left, 10.f, right, 10.f); line(right, 10.f, right, 18.f);
            nvgBeginPath(vg); nvgMoveTo(vg, x + left * s, y + 10.f * s); nvgLineTo(vg, x + tip * s, y + 5.f * s); nvgLineTo(vg, x + tip * s, y + 15.f * s); nvgClosePath(vg); nvgFillColor(vg, colour); nvgFill(vg);
            break;
        }
        case ActionIcon::Save:
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 4.f * s, y + 3.f * s, 16.f * s, 18.f * s, 1.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            line(8.f, 4.f, 8.f, 10.f); line(8.f, 10.f, 16.f, 10.f); line(8.f, 16.f, 16.f, 16.f);
            break;
    }
}

void drawText(NVGcontext* vg, float x, float y, float size, const char* str, const char* end, int align, const NVGcolor& c) {
    drawTextIntenal(vg, {x,y}, size, str, end, align, c);
}

void drawText(NVGcontext* vg, float x, float y, float size, const NVGcolor& c, const char* str, int align, const char* end) {
    drawTextIntenal(vg, {x,y}, size, str, end, align, c);
}

void drawText(NVGcontext* vg, const Vec2& v, float size, const char* str, const char* end, int align, const NVGcolor& c) {
    drawTextIntenal(vg, v, size, str, end, align, c);
}

void drawText(NVGcontext* vg, const Vec2& v, float size, const NVGcolor& c, const char* str, int align, const char* end) {
    drawTextIntenal(vg, v, size, str, end, align, c);
}

void drawScrollbar(NVGcontext* vg, const Theme* theme, float x, float y, float h, u32 index_off, u32 count, u32 max_per_page) {
    const s64 SCROLL = index_off;
    const s64 max_entry_display = max_per_page;
    const s64 entry_total = count;
    const float scc2 = 8.0;
    const float scw = 2.0;

    // only draw scrollbar if needed
    if (entry_total > max_entry_display) {
        const float sb_h = 1.f / (float)entry_total * h;
        const float sb_y = SCROLL;
        gfx::drawRect(vg, x, y, scc2, h, theme->GetColour(ThemeEntryID_SCROLLBAR_BACKGROUND), 5);
        gfx::drawRect(vg, x + scw, y + scw + sb_h * sb_y, scc2 - scw * 2, sb_h * float(max_entry_display) - scw * 2, theme->GetColour(ThemeEntryID_SCROLLBAR), 5);
    }
}

void drawScrollbar(NVGcontext* vg, const Theme* theme, u32 index_off, u32 count, u32 max_per_page) {
    drawScrollbar(vg, theme, SCREEN_WIDTH - 50, 100, SCREEN_HEIGHT-200, index_off, count, max_per_page);
}

void drawScrollbar2(NVGcontext* vg, const Theme* theme, float x, float y, float h, s64 index_off, s64 count, s64 row, s64 page) {
    // round up
    if (count % row) {
        count = count + (row - count % row);
    }

    const float scc2 = 6.0;
    const float scw = 2.0;

    // only draw scrollbar if needed
    if (count > page) {
        const float sb_h = 1.f / (float)count * h;
        const float sb_y = index_off;
        gfx::drawRect(vg, x, y, scc2, h, theme->GetColour(ThemeEntryID_SCROLLBAR_BACKGROUND), 5);
        gfx::drawRect(vg, x + scw, y + scw + sb_h * sb_y, scc2 - scw * 2, sb_h * float(page) - scw * 2, theme->GetColour(ThemeEntryID_SCROLLBAR), 5);
    }
}

void drawScrollbar2(NVGcontext* vg, const Theme* theme, s64 index_off, s64 count, s64 row, s64 page) {
    drawScrollbar2(vg, theme, SCREEN_WIDTH - 50, 100, SCREEN_HEIGHT-200, index_off, count, row, page);
}

void drawTriangle(NVGcontext* vg, float aX, float aY, float bX, float bY, float cX, float cY, const NVGcolor& c) {
    drawTriangleInternal(vg, aX, aY, bX, bY, cX, cY, c);
}

void drawTriangle(NVGcontext* vg, float aX, float aY, float bX, float bY, float cX, float cY, const NVGpaint& p) {
    drawTriangleInternal(vg, aX, aY, bX, bY, cX, cY, p);
}

void drawAppLable(NVGcontext* vg, const Theme* theme, ScrollingText& st, float x, float y, float w, const char* name) {
    // todo: no more 5am code
    const float max_box_w = 392.f;
    const float box_h = 48.f;
    // used for adjusting the position of the box.
    const float clip_pad = 25.f;
    const float clip_left = clip_pad;
    const float clip_right = 1220.f - clip_pad;
    const float text_pad = 25.f;
    const float font_size = 22.f;

    nvgTextAlign(vg, NVG_ALIGN_LEFT);
    nvgFontSize(vg, font_size);
    float bounds[4]{};
    nvgTextBounds(vg, 0, 0, name, NULL, bounds);

    const float trinaglex = x + (w / 2.f) - 9.f;
    const float trinagley = y - 14.f;
    const float center_x = x + (w / 2.f);
    const float y_offset = y - 62.f; // top of box
    const float text_width = bounds[2];
    float box_w = text_width + text_pad * 2;
    if (box_w > max_box_w) {
        box_w = max_box_w;
    }

    float box_x = center_x - (box_w / 2.f);
    if (box_x < clip_left) {
        box_x = clip_left;
    }
    if ((box_x + box_w) > clip_right) {
        // box_x -= ((box_x + box_w) - clip_right) / 2;
        box_x = (clip_right - box_w);
    }

    const float text_x = box_x + text_pad;
    const float text_y = y_offset + (box_h / 2.f);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, y_offset, box_w, box_h, 3.f);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND));
    nvgFill(vg);

    drawTriangle(vg, trinaglex, trinagley, trinaglex + 18.f, trinagley, trinaglex + 9.f, trinagley + 12.f, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND));
    st.Draw(vg, true, text_x, text_y, box_w - text_pad * 2, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), name);
}

#define HIGHLIGHT_SPEED 350.0

static double highlightGradientX = 0;
static double highlightGradientY = 0;
static double highlightColor     = 0;

void updateHighlightAnimation() {
    const auto currentTime = svcGetSystemTick() * 10 / 192 / 1000;

    // Update variables
    highlightGradientX = (std::cos((double)currentTime / HIGHLIGHT_SPEED / 3.0) + 1.0) / 2.0;
    highlightGradientY = (std::sin((double)currentTime / HIGHLIGHT_SPEED / 3.0) + 1.0) / 2.0;
    highlightColor     = (std::sin((double)currentTime / HIGHLIGHT_SPEED * 2.0) + 1.0) / 2.0;
}

void getHighlightAnimation(float* gradientX, float* gradientY, float* color) {
    if (gradientX)
        *gradientX = (float)highlightGradientX;

    if (gradientY)
        *gradientY = (float)highlightGradientY;

    if (color)
        *color = (float)highlightColor;
}

void ImageViewport::Reset() {
    m_zoom = 1.f;
    m_pan_x = 0.f;
    m_pan_y = 0.f;
    m_prev_touch_x = 0;
    m_prev_touch_y = 0;
    m_was_touching = false;
    m_pinch_active = false;
}

void ImageViewport::SetZoom(float zoom, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    m_zoom = std::clamp(zoom, 1.f, 8.f);
    ClampPan(image_w, image_h, viewport, fit);
}

void ImageViewport::SetPan(float pan_x, float pan_y, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    m_pan_x = pan_x;
    m_pan_y = pan_y;
    ClampPan(image_w, image_h, viewport, fit);
}

auto ImageViewport::GetScale(int image_w, int image_h, const Vec4& viewport, ImageFit fit) const -> float {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        return 0.f;
    }

    const float scale_x = viewport.w / static_cast<float>(image_w);
    const float scale_y = viewport.h / static_cast<float>(image_h);
    const float base_scale = (fit == ImageFit::Cover) ? std::max(scale_x, scale_y) : std::min(scale_x, scale_y);
    return base_scale * m_zoom;
}

auto ImageViewport::GetImageRect(int image_w, int image_h, const Vec4& viewport, ImageFit fit) const -> Vec4 {
    const float scale = GetScale(image_w, image_h, viewport, fit);
    if (scale <= 0.f) {
        return {viewport.x, viewport.y, 0.f, 0.f};
    }

    const float w = static_cast<float>(image_w) * scale;
    const float h = static_cast<float>(image_h) * scale;
    const float x = viewport.x + (viewport.w - w) * 0.5f + m_pan_x;
    const float y = viewport.y + (viewport.h - h) * 0.5f + m_pan_y;
    return {x, y, w, h};
}

void ImageViewport::Zoom(float factor, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    const float center_x = viewport.x + viewport.w * 0.5f;
    const float center_y = viewport.y + viewport.h * 0.5f;
    ZoomAt(factor, center_x, center_y, image_w, image_h, viewport, fit);
}

void ImageViewport::ZoomAt(float factor, float anchor_x, float anchor_y, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        return;
    }

    const float old_zoom = m_zoom;
    const float new_zoom = std::clamp(old_zoom * factor, 1.f, 8.f);
    if (old_zoom <= 0.f) {
        m_zoom = new_zoom;
        ClampPan(image_w, image_h, viewport, fit);
        return;
    }

    const float effective_factor = new_zoom / old_zoom;
    const float scale_x = viewport.w / static_cast<float>(image_w);
    const float scale_y = viewport.h / static_cast<float>(image_h);
    const float base_scale = (fit == ImageFit::Cover) ? std::max(scale_x, scale_y) : std::min(scale_x, scale_y);

    const float old_scaled_w = static_cast<float>(image_w) * base_scale * old_zoom;
    const float old_scaled_h = static_cast<float>(image_h) * base_scale * old_zoom;
    const float old_image_x = viewport.x + (viewport.w - old_scaled_w) * 0.5f + m_pan_x;
    const float old_image_y = viewport.y + (viewport.h - old_scaled_h) * 0.5f + m_pan_y;

    const float new_scaled_w = static_cast<float>(image_w) * base_scale * new_zoom;
    const float new_scaled_h = static_cast<float>(image_h) * base_scale * new_zoom;
    const float new_image_x = anchor_x + (old_image_x - anchor_x) * effective_factor;
    const float new_image_y = anchor_y + (old_image_y - anchor_y) * effective_factor;

    m_zoom = new_zoom;
    m_pan_x = new_image_x - (viewport.x + (viewport.w - new_scaled_w) * 0.5f);
    m_pan_y = new_image_y - (viewport.y + (viewport.h - new_scaled_h) * 0.5f);

    ClampPan(image_w, image_h, viewport, fit);
}

void ImageViewport::Pan(float dx, float dy, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    m_pan_x += dx;
    m_pan_y += dy;
    ClampPan(image_w, image_h, viewport, fit);
}

void ImageViewport::ClampPan(int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        m_pan_x = 0.f;
        m_pan_y = 0.f;
        return;
    }

    const float scale = GetScale(image_w, image_h, viewport, fit);
    const float w = static_cast<float>(image_w) * scale;
    const float h = static_cast<float>(image_h) * scale;
    const float max_pan_x = std::max(0.f, (w - viewport.w) * 0.5f);
    const float max_pan_y = std::max(0.f, (h - viewport.h) * 0.5f);

    m_pan_x = std::clamp(m_pan_x, -max_pan_x, max_pan_x);
    m_pan_y = std::clamp(m_pan_y, -max_pan_y, max_pan_y);
}

void ImageViewport::Update(Controller* controller, TouchInfo* touch, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        return;
    }

    if (controller) {
        const auto zoom_modifier = controller->GotDown(Button::L2) || controller->GotHeld(Button::L2);
        if (zoom_modifier) {
            const auto zoom_in = controller->GotDown(Button::DPAD_UP | Button::LS_UP | Button::RS_UP) ||
                controller->GotHeld(Button::DPAD_UP | Button::LS_UP | Button::RS_UP);
            const auto zoom_out = controller->GotDown(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN) ||
                controller->GotHeld(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN);

            if (zoom_in) {
                Zoom(1.05f, image_w, image_h, viewport, fit);
            } else if (zoom_out) {
                Zoom(1.f / 1.05f, image_w, image_h, viewport, fit);
            }
        } else {
            constexpr float PAN_SPEED = 12.f;
            if (controller->GotDown(Button::DPAD_UP | Button::LS_UP | Button::RS_UP) ||
                controller->GotHeld(Button::DPAD_UP | Button::LS_UP | Button::RS_UP)) {
                Pan(0.f, PAN_SPEED, image_w, image_h, viewport, fit);
            }
            if (controller->GotDown(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN) ||
                controller->GotHeld(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN)) {
                Pan(0.f, -PAN_SPEED, image_w, image_h, viewport, fit);
            }
            if (controller->GotDown(Button::DPAD_LEFT | Button::LS_LEFT | Button::RS_LEFT) ||
                controller->GotHeld(Button::DPAD_LEFT | Button::LS_LEFT | Button::RS_LEFT)) {
                Pan(PAN_SPEED, 0.f, image_w, image_h, viewport, fit);
            }
            if (controller->GotDown(Button::DPAD_RIGHT | Button::LS_RIGHT | Button::RS_RIGHT) ||
                controller->GotHeld(Button::DPAD_RIGHT | Button::LS_RIGHT | Button::RS_RIGHT)) {
                Pan(-PAN_SPEED, 0.f, image_w, image_h, viewport, fit);
            }
        }
    }

    if (touch) {
        if (touch->touch_count >= 2) {
            m_pinch_active = true;
            m_was_touching = false;
            if (touch->is_pinch) {
                const float clamped_scale = std::clamp(touch->pinch_scale, 0.9f, 1.1f);
                ZoomAt(clamped_scale, touch->pinch_x, touch->pinch_y, image_w, image_h, viewport, fit);
            }
        } else if (!touch->is_touching || touch->touch_count == 0) {
            m_was_touching = false;
            m_pinch_active = false;
        } else if (!m_pinch_active) {
            if (m_was_touching) {
                const float dx = static_cast<float>(static_cast<s32>(touch->cur.x) - m_prev_touch_x);
                const float dy = static_cast<float>(static_cast<s32>(touch->cur.y) - m_prev_touch_y);
                Pan(dx, dy, image_w, image_h, viewport, fit);
            }
            m_prev_touch_x = touch->cur.x;
            m_prev_touch_y = touch->cur.y;
            m_was_touching = true;
        }
    }
}

} // namespace sphaira::ui::gfx
