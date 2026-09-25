#include "ui/nvg_util.hpp"

namespace sphaira::ui::gfx {

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
            // Undo points left, Redo points right (the previous assignment was reversed).
            const bool redo = icon == ActionIcon::Redo;
            const float nose = redo ? 19.f : 5.f, tail = redo ? 5.f : 19.f, base = redo ? 14.f : 10.f;
            line(nose, 10.f, tail, 10.f); line(tail, 10.f, tail, 18.f);
            nvgBeginPath(vg); nvgMoveTo(vg, x + nose * s, y + 10.f * s); nvgLineTo(vg, x + base * s, y + 5.f * s); nvgLineTo(vg, x + base * s, y + 15.f * s); nvgClosePath(vg); nvgFillColor(vg, colour); nvgFill(vg);
            break;
        }
        case ActionIcon::Save:
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 4.f * s, y + 3.f * s, 16.f * s, 18.f * s, 1.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            line(8.f, 4.f, 8.f, 10.f); line(8.f, 10.f, 16.f, 10.f); line(8.f, 16.f, 16.f, 16.f);
            break;
        case ActionIcon::Search:
            nvgBeginPath(vg); nvgCircle(vg, x + 10.f * s, y + 10.f * s, 5.5f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            line(14.f, 14.f, 19.5f, 19.5f);
            break;
        case ActionIcon::Sort:
            line(8.f, 5.f, 8.f, 19.f); line(5.f, 8.f, 8.f, 5.f); line(11.f, 8.f, 8.f, 5.f);
            line(16.f, 5.f, 16.f, 19.f); line(13.f, 16.f, 16.f, 19.f); line(19.f, 16.f, 16.f, 19.f);
            break;
        case ActionIcon::Layout:
            nvgBeginPath(vg); nvgRect(vg, x + 4.f * s, y + 4.f * s, 7.f * s, 7.f * s); nvgRect(vg, x + 13.f * s, y + 4.f * s, 7.f * s, 7.f * s); nvgRect(vg, x + 4.f * s, y + 13.f * s, 7.f * s, 7.f * s); nvgRect(vg, x + 13.f * s, y + 13.f * s, 7.f * s, 7.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            break;
        case ActionIcon::Launch:
            nvgBeginPath(vg); nvgMoveTo(vg, x + 7.f * s, y + 5.f * s); nvgLineTo(vg, x + 19.f * s, y + 12.f * s); nvgLineTo(vg, x + 7.f * s, y + 19.f * s); nvgClosePath(vg); nvgFillColor(vg, colour); nvgFill(vg);
            break;
        case ActionIcon::Folder:
            nvgBeginPath(vg); nvgMoveTo(vg, x + 4.f * s, y + 8.f * s); nvgLineTo(vg, x + 4.f * s, y + 19.f * s); nvgLineTo(vg, x + 20.f * s, y + 19.f * s); nvgLineTo(vg, x + 20.f * s, y + 10.f * s); nvgLineTo(vg, x + 12.f * s, y + 10.f * s); nvgLineTo(vg, x + 10.f * s, y + 7.f * s); nvgLineTo(vg, x + 4.f * s, y + 7.f * s); nvgClosePath(vg); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            break;
        case ActionIcon::Dump:
            nvgBeginPath(vg); nvgMoveTo(vg, x + 5.f * s, y + 18.f * s); nvgLineTo(vg, x + 5.f * s, y + 20.f * s); nvgLineTo(vg, x + 19.f * s, y + 20.f * s); nvgLineTo(vg, x + 19.f * s, y + 18.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgLineCap(vg, NVG_ROUND); nvgStroke(vg);
            line(12.f, 5.f, 12.f, 16.f); line(12.f, 5.f, 8.f, 9.f); line(12.f, 5.f, 16.f, 9.f);
            break;
        case ActionIcon::Move:
            line(4.f, 9.f, 20.f, 9.f); line(4.f, 9.f, 8.f, 6.f); line(4.f, 9.f, 8.f, 12.f);
            line(4.f, 16.f, 20.f, 16.f); line(20.f, 16.f, 16.f, 13.f); line(20.f, 16.f, 16.f, 19.f);
            break;
        case ActionIcon::Refresh:
            nvgBeginPath(vg); nvgArc(vg, x + 12.f * s, y + 12.f * s, 7.f * s, -0.4f, 4.4f, NVG_CW); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgLineCap(vg, NVG_ROUND); nvgStroke(vg);
            nvgBeginPath(vg); nvgMoveTo(vg, x + 18.f * s, y + 5.f * s); nvgLineTo(vg, x + 18.f * s, y + 10.f * s); nvgLineTo(vg, x + 13.f * s, y + 10.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgLineCap(vg, NVG_ROUND); nvgLineJoin(vg, NVG_ROUND); nvgStroke(vg);
            break;
        case ActionIcon::Random:
            line(7.f, 7.f, 17.f, 17.f); line(17.f, 7.f, 7.f, 17.f);
            nvgBeginPath(vg); nvgCircle(vg, x + 7.f * s, y + 7.f * s, 2.f * s); nvgCircle(vg, x + 17.f * s, y + 17.f * s, 2.f * s); nvgCircle(vg, x + 17.f * s, y + 7.f * s, 2.f * s); nvgCircle(vg, x + 7.f * s, y + 17.f * s, 2.f * s); nvgFillColor(vg, colour); nvgFill(vg);
            break;
        case ActionIcon::Star:
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 12.f * s, y + 3.f * s);
            nvgLineTo(vg, x + 14.5f * s, y + 9.f * s);
            nvgLineTo(vg, x + 21.f * s, y + 9.5f * s);
            nvgLineTo(vg, x + 16.f * s, y + 14.f * s);
            nvgLineTo(vg, x + 17.5f * s, y + 20.f * s);
            nvgLineTo(vg, x + 12.f * s, y + 16.5f * s);
            nvgLineTo(vg, x + 6.5f * s, y + 20.f * s);
            nvgLineTo(vg, x + 8.f * s, y + 14.f * s);
            nvgLineTo(vg, x + 3.f * s, y + 9.5f * s);
            nvgLineTo(vg, x + 9.5f * s, y + 9.f * s);
            nvgClosePath(vg);
            nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgLineJoin(vg, NVG_ROUND); nvgStroke(vg);
            break;
        case ActionIcon::GoTo:
            line(5.f, 7.f, 19.f, 7.f); line(5.f, 12.f, 14.f, 12.f); line(5.f, 17.f, 19.f, 17.f);
            line(14.f, 12.f, 18.f, 9.f); line(14.f, 12.f, 18.f, 15.f);
            break;
        case ActionIcon::Comment:
            line(7.f, 5.f, 11.f, 19.f); line(13.f, 5.f, 17.f, 19.f);
            break;
        case ActionIcon::Range:
            line(7.f, 5.f, 7.f, 19.f); line(7.f, 5.f, 11.f, 5.f); line(7.f, 19.f, 11.f, 19.f);
            line(17.f, 5.f, 17.f, 19.f); line(17.f, 5.f, 13.f, 5.f); line(17.f, 19.f, 13.f, 19.f);
            break;
        case ActionIcon::Toggle:
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 4.f * s, y + 8.f * s, 16.f * s, 8.f * s, 4.f * s); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            nvgBeginPath(vg); nvgCircle(vg, x + 16.f * s, y + 12.f * s, 3.f * s); nvgFillColor(vg, colour); nvgFill(vg);
            break;
        case ActionIcon::Compress:
            nvgBeginPath(vg); nvgMoveTo(vg, x + 7.f * s, y + 6.f * s); nvgLineTo(vg, x + 12.f * s, y + 3.f * s); nvgLineTo(vg, x + 17.f * s, y + 6.f * s); nvgLineTo(vg, x + 17.f * s, y + 20.f * s); nvgLineTo(vg, x + 7.f * s, y + 20.f * s); nvgClosePath(vg); nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 2.f * s); nvgStroke(vg);
            line(10.f, 11.f, 14.f, 11.f); line(10.f, 15.f, 14.f, 15.f);
            break;
        default:
            break;
    }
}

void drawUsbIcon(NVGcontext* vg, float x, float y, float size, const NVGcolor& colour) {
    const float s = size / 24.f;
    const auto line = [&](float x1, float y1, float x2, float y2) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + x1 * s, y + y1 * s);
        nvgLineTo(vg, x + x2 * s, y + y2 * s);
        nvgStrokeColor(vg, colour);
        nvgStrokeWidth(vg, 1.8f * s);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    };

    // Central trunk
    line(12.f, 6.f, 12.f, 19.f);

    // Bottom base circle
    nvgBeginPath(vg);
    nvgCircle(vg, x + 12.f * s, y + 19.f * s, 2.2f * s);
    nvgFillColor(vg, colour);
    nvgFill(vg);

    // Top arrow (triangle pointing up)
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 12.f * s, y + 2.5f * s);
    nvgLineTo(vg, x + 9.5f * s, y + 6.5f * s);
    nvgLineTo(vg, x + 14.5f * s, y + 6.5f * s);
    nvgClosePath(vg);
    nvgFillColor(vg, colour);
    nvgFill(vg);

    // Right branch: diagonal to square
    line(12.f, 13.f, 17.f, 9.f);
    line(17.f, 9.f, 17.f, 7.5f);
    nvgBeginPath(vg);
    nvgRect(vg, x + 15.2f * s, y + 5.5f * s, 3.6f * s, 3.6f * s);
    nvgFillColor(vg, colour);
    nvgFill(vg);

    // Left branch: diagonal to small circle
    line(12.f, 15.f, 7.f, 11.5f);
    line(7.f, 11.5f, 7.f, 9.5f);
    nvgBeginPath(vg);
    nvgCircle(vg, x + 7.f * s, y + 8.5f * s, 2.0f * s);
    nvgFillColor(vg, colour);
    nvgFill(vg);
}

} // namespace sphaira::ui::gfx
