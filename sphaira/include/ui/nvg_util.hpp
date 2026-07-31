#pragma once

#include "nanovg.h"
#include "ui/types.hpp"
#include "ui/scrolling_text.hpp"

namespace sphaira::ui::gfx {

void drawImage(NVGcontext*, float x, float y, float w, float h, int texture, float rounded = 0.F, float alpha = 1.0F);
void drawImage(NVGcontext*, const Vec4& v, int texture, float rounded = 0.F, float alpha = 1.0F);

void dimBackground(NVGcontext*);

void drawRect(NVGcontext*, float x, float y, float w, float h, const NVGcolor& c, float rounding = 0.F);
void drawRect(NVGcontext*, const Vec4& v, const NVGcolor& c, float rounding = 0.F);
void drawRect(NVGcontext*, float x, float y, float w, float h, const NVGpaint& p, float rounding = 0.F);
void drawRect(NVGcontext*, const Vec4& v, const NVGpaint& p, float rounding = 0.F);

// rounded rect with a per-corner radius (top-left, top-right, bottom-right,
// bottom-left). used for tab shapes that round only their top corners.
void drawRectVarying(NVGcontext*, const Vec4& v, const NVGcolor& c, float rTL, float rTR, float rBR, float rBL);

void drawRectOutline(NVGcontext*, const Theme*, float size, float x, float y, float w, float h, float rounding = 4.0F);
void drawRectOutline(NVGcontext*, const Theme*, float size, const Vec4& v, float rounding = 4.0F);

// drawRectOutline draws its border + drop shadow a few px OUTSIDE the given
// rect. any scissor that clips list/selection content should be inflated by
// this much so the highlight of an edge item isn't sunken/clipped.
constexpr float SELECTION_OUTLINE_PAD = 10.f;

// The multi-select checkbox drawn in the left gutter of a list row: an empty
// box while a selection is in progress, a checkmark in it once the row is in
// the selection. Every list that supports X/Y select uses this one, so the
// mark sits in the same place and looks the same everywhere.
constexpr float CHECKBOX_SIZE = 20.f;
void drawCheckbox(NVGcontext*, const Theme*, float x, float y, float size, bool checked);

void drawTriangle(NVGcontext*, float aX, float aY, float bX, float bY, float cX, float cY, const NVGcolor& c);
void drawTriangle(NVGcontext*, float aX, float aY, float bX, float bY, float cX, float cY, const NVGpaint& p);

void drawText(NVGcontext*, float x, float y, float size, const char* str, const char* end, int align, const NVGcolor& c);
void drawText(NVGcontext*, float x, float y, float size, const NVGcolor& c, const char* str, int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP, const char* end = nullptr);
void drawText(NVGcontext*, const Vec2& v, float size, const char* str, const char* end, int align, const NVGcolor& c);
void drawText(NVGcontext*, const Vec2& v, float size, const NVGcolor& c, const char* str, int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP, const char* end = nullptr);
void drawTextArgs(NVGcontext*, float x, float y, float size, int align, const NVGcolor& c, const char* str, ...) __attribute__ ((format (printf, 7, 8)));

// faux-bold text: over-draws the string with a small horizontal offset because
// no bold font face is loaded. keep the offset subtle so it thickens rather
// than blurs the glyphs.
void drawTextBold(NVGcontext*, float x, float y, float size, const NVGcolor& c, const char* str, int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

void drawTextBox(NVGcontext*, float x, float y, float size, float bound, const NVGcolor& c, const char* str, int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP, const char* end = nullptr, float line_height = 1.0f);

void textBounds(NVGcontext*, float x, float y, float *bounds, const char* str);
void textBoundsArgs(NVGcontext*, float x, float y, float *bounds, const char* str, ...) __attribute__ ((format (printf, 5, 6)));

auto getButton(Button button) -> const char*;
void drawScrollbar(NVGcontext*, const Theme*, u32 index_off, u32 count, u32 max_per_page);
void drawScrollbar(NVGcontext*, const Theme*, float x, float y, float h, u32 index_off, u32 count, u32 max_per_page);

void drawScrollbar2(NVGcontext*, const Theme*, float x, float y, float h, s64 index_off, s64 count, s64 row, s64 page);
void drawScrollbar2(NVGcontext*, const Theme*, s64 index_off, s64 count, s64 row, s64 page);

void drawAppLable(NVGcontext* vg, const Theme*, ScrollingText& st, float x, float y, float w, const char* name);

void updateHighlightAnimation();
void getHighlightAnimation(float* gradientX, float* gradientY, float* color);

} // namespace sphaira::ui::gfx
