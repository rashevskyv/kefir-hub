#pragma once

#include "ui/types.hpp"
#include <algorithm>

// Screen chrome layout.
//
// Every screen is split into three horizontal bands:
//
//   [0, HEADER_LINE_Y]              header  - version, title, sub heading and
//                                             the status block (clock, battery,
//                                             network, NAND/SD bars)
//   (HEADER_LINE_Y, FOOTER_LINE_Y)  content - menu bodies, lists, side panels
//   [FOOTER_LINE_Y, screen bottom]  footer  - sub heading and the button hints
//
// The header and footer are *chrome*: they are drawn as a top layer, after the
// menu body, and own their bands outright. Content never draws into them - a
// list clips to the content band, so a partially scrolled row is cut at the
// band edge instead of spilling across the separator.
//
// Exactly one widget owns the chrome at a time: the top of the widget stack
// (see App::OwnsFooter). A menu sitting under another menu or under a side
// panel draws no button hints, and suppresses the parts of its header that the
// panel covers (see Widget::GetChromeOcclusion), so nothing ghosts through.

namespace sphaira::ui::layout {

// horizontal inset shared by the separators and the menu body.
constexpr float SIDE_X = 30.f;

// the two separator lines that fence the content off from the chrome.
constexpr float HEADER_LINE_Y = 86.f;
constexpr float FOOTER_LINE_Y = 646.f;

// the band content is allowed to draw in, starting just below the header line.
constexpr float CONTENT_TOP = HEADER_LINE_Y + 1.f;
constexpr float CONTENT_BOTTOM = FOOTER_LINE_Y;
constexpr float CONTENT_HEIGHT = CONTENT_BOTTOM - CONTENT_TOP;

// anchor of the right-aligned button hint row in the footer.
constexpr Vec2 BUTTON_POS{1220.f, 675.f};

// baseline of the footer sub heading.
constexpr float FOOTER_TEXT_Y = 683.f;

constexpr auto ContentBand() -> Vec4 {
    return Vec4{SIDE_X, CONTENT_TOP, SCREEN_WIDTH - SIDE_X * 2.f, CONTENT_HEIGHT};
}

// vertical extent of a clip rect, as passed to nvgIntersectScissor().
struct ClipY {
    float y;
    float h;
};

// Expands a clip rect by pad - so a selection highlight drawn just outside an
// item isn't cut - without letting that padding reach into the header or the
// footer. Only edges that already sit inside the content band are pulled back,
// which leaves popups and full screen modals (which deliberately span the whole
// screen) exactly as they were.
constexpr auto PaddedContentClipY(float y, float h, float pad) -> ClipY {
    float top = y - pad;
    float bottom = y + h + pad;

    if (y >= HEADER_LINE_Y) {
        top = std::max(top, CONTENT_TOP);
        bottom = std::min(bottom, CONTENT_BOTTOM);
    } else if (y + h <= FOOTER_LINE_Y) {
        bottom = std::min(bottom, CONTENT_BOTTOM);
    }

    return ClipY{top, std::max(0.f, bottom - top)};
}

constexpr auto Intersects(const Vec4& a, const Vec4& b) -> bool {
    if (a.w <= 0.f || a.h <= 0.f || b.w <= 0.f || b.h <= 0.f) {
        return false;
    }

    return a.x < b.x + b.w && b.x < a.x + a.w
        && a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace sphaira::ui::layout
