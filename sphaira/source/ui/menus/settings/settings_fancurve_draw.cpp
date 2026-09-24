#include "ui/menus/settings/settings_fancurve.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace sphaira::ui::menu::settings {

auto FanCurveProfileLabel(bool docked) -> const char* {
    return docked ? "Docked" : "Handheld";
}

auto FanCurveGraphRect() -> Vec4 {
    return {360.f, 106.f, 860.f, 520.f};
}

auto FanCurvePlotRect() -> Vec4 {
    const auto graph = FanCurveGraphRect();
    return {graph.x + 58.f, graph.y + 66.f, graph.w - 90.f, graph.h - 132.f};
}

auto FanCurveListRect() -> Vec4 {
    return {58.f, 134.f, 270.f, 468.f};
}

auto FanCurveListItemRect() -> Vec4 {
    return {66.f, 184.f, 248.f, 46.f};
}

auto FanCurveXForTempValue(const Vec4& plot, float temp_c) -> float {
    const auto span = static_cast<float>(detail::FAN_TEMP_MAX_C - detail::FAN_TEMP_MIN_C);
    const auto value = std::clamp(temp_c, static_cast<float>(detail::FAN_TEMP_MIN_C), static_cast<float>(detail::FAN_TEMP_MAX_C));
    return plot.x + plot.w * ((value - static_cast<float>(detail::FAN_TEMP_MIN_C)) / span);
}

auto FanCurveXForTemp(const Vec4& plot, s32 temp_c) -> float {
    return FanCurveXForTempValue(plot, static_cast<float>(temp_c));
}

auto FanCurveYForFan(const Vec4& plot, s32 fan_percent) -> float {
    return plot.y + plot.h - plot.h * (static_cast<float>(fan_percent) / 100.f);
}

auto FanCurveTempForX(const Vec4& plot, float x) -> s32 {
    const auto ratio = std::clamp((x - plot.x) / plot.w, 0.f, 1.f);
    return detail::FAN_TEMP_MIN_C + static_cast<s32>(ratio * static_cast<float>(detail::FAN_TEMP_MAX_C - detail::FAN_TEMP_MIN_C) + 0.5f);
}

auto FanCurveFanForY(const Vec4& plot, float y) -> s32 {
    const auto ratio = std::clamp((plot.y + plot.h - y) / plot.h, 0.f, 1.f);
    return static_cast<s32>(ratio * 100.f + 0.5f);
}

auto ExpandRect(Vec4 rect, float amount) -> Vec4 {
    rect.x -= amount;
    rect.y -= amount;
    rect.w += amount * 2.f;
    rect.h += amount * 2.f;
    return rect;
}

auto WithAlpha(NVGcolor colour, float alpha) -> NVGcolor {
    colour.a = alpha;
    return colour;
}

auto FormatMilliC(s32 milli_c) -> std::string {
    const auto negative = milli_c < 0;
    const auto abs_milli = negative ? -milli_c : milli_c;
    const auto tenths = (abs_milli + 50) / 100;
    return std::string{negative ? "-" : ""} + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "C";
}

void DrawHorizontalDashes(NVGcontext* vg, float x0, float x1, float y, const NVGcolor& colour) {
    if (x1 < x0) {
        std::swap(x0, x1);
    }

    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, colour);
    for (float x = x0; x < x1; x += 13.f) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y);
        nvgLineTo(vg, std::min(x + 7.f, x1), y);
        nvgStroke(vg);
    }
}

void DrawVerticalDashes(NVGcontext* vg, float x, float y0, float y1, const NVGcolor& colour) {
    if (y1 < y0) {
        std::swap(y0, y1);
    }

    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, colour);
    for (float y = y0; y < y1; y += 13.f) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y);
        nvgLineTo(vg, x, std::min(y + 7.f, y1));
        nvgStroke(vg);
    }
}

void DrawFanCurveSensorMarker(NVGcontext* vg, Theme* theme, const Vec4& plot, const std::vector<FanCurvePoint>& curve, const FanCurveSensorSample* sensor) {
    if (!sensor) {
        return;
    }

    const auto accent = theme->GetColour(ThemeEntryID_TEXT_SELECTED);
    const auto temp_c = static_cast<float>(sensor->temp_milli_c) / 1000.f;
    const auto x = FanCurveXForTempValue(plot, temp_c);
    const float fan_percent = (sensor->fan_percent >= 0) ? static_cast<float>(sensor->fan_percent) : EvaluateFanPercent(curve, temp_c);
    const auto y = FanCurveYForFan(plot, fan_percent);
    const auto guide_colour = WithAlpha(accent, 0.42f);

    DrawHorizontalDashes(vg, plot.x, x, y, guide_colour);
    DrawVerticalDashes(vg, x, y, plot.y + plot.h, guide_colour);

    nvgBeginPath(vg);
    nvgCircle(vg, x, y, 19.f);
    nvgFillColor(vg, WithAlpha(accent, 0.20f));
    nvgFill(vg);

    nvgStrokeWidth(vg, 2.5f);
    nvgStrokeColor(vg, WithAlpha(accent, 0.95f));
    nvgBeginPath(vg);
    nvgCircle(vg, x, y, 19.f);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, x, y, 4.5f);
    nvgFillColor(vg, WithAlpha(accent, 0.95f));
    nvgFill(vg);

    const auto place_left = x > plot.x + plot.w - 145.f;
    const auto label_x = place_left ? x - 24.f : x + 24.f;
    const auto label_y = std::clamp(y - 22.f, plot.y + 12.f, plot.y + plot.h - 10.f);
    const auto align = (place_left ? NVG_ALIGN_RIGHT : NVG_ALIGN_LEFT) | NVG_ALIGN_MIDDLE;
    const auto label = FormatMilliC(sensor->temp_milli_c);
    gfx::drawTextArgs(
        vg, label_x, label_y, 14.f, align,
        theme->GetColour(ThemeEntryID_TEXT), "%s  %d%%", label.c_str(), static_cast<s32>(fan_percent + 0.5f)
    );
}

void DrawFanCurveGraph(NVGcontext* vg, Theme* theme, const std::vector<FanCurvePoint>& curve, const std::vector<FanCurvePoint>& control_points, bool easy_curve_mode, s64 selected, bool docked, bool dirty, bool editing, const FanCurveSensorSample* sensor) {
    const auto graph = FanCurveGraphRect();
    const auto plot = FanCurvePlotRect();

    gfx::drawRect(vg, graph, theme->GetColour(ThemeEntryID_SIDEBAR), 5.f);

    gfx::drawTextArgs(
        vg, graph.x + 24.f, graph.y + 20.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
        theme->GetColour(ThemeEntryID_TEXT), "%s curve", FanCurveProfileLabel(docked)
    );
    if (easy_curve_mode) {
        if (!control_points.empty()) {
            const auto safe_index = std::clamp<s64>(selected, 0, static_cast<s64>(control_points.size() - 1));
            const auto& point = control_points[safe_index];
            const char* name = (safe_index == 0) ? "Min" : ((safe_index == 1) ? "Mid" : "Max");
            gfx::drawTextArgs(
                vg, graph.x + graph.w - 24.f, graph.y + 20.f, 18.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "%s %s   %dC   %d%%", editing ? "Editing point" : "Point", name, point.temp_c, point.fan_percent
            );
        }
    } else {
        if (!curve.empty()) {
            const auto safe_index = std::clamp<s64>(selected, 0, static_cast<s64>(curve.size() - 1));
            const auto& point = curve[safe_index];
            gfx::drawTextArgs(
                vg, graph.x + graph.w - 24.f, graph.y + 20.f, 18.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "%s %d   %dC   %d%%", editing ? "Editing point" : "Point", static_cast<int>(safe_index + 1), point.temp_c, point.fan_percent
            );
        }
    }
    if (dirty) {
        gfx::drawText(
            vg, graph.x + 24.f, graph.y + 44.f, 15.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED), "Unsaved changes", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE
        );
    }
    if (easy_curve_mode) {
        gfx::drawText(
            vg, graph.x + 24.f, graph.y + (dirty ? 64.f : 44.f), 15.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "Bezier", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE
        );
    }

    const auto line_colour = theme->GetColour(ThemeEntryID_LINE_SEPARATOR);
    const auto info_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto text_colour = theme->GetColour(ThemeEntryID_TEXT);
    const auto accent_colour = theme->GetColour(ThemeEntryID_TEXT_SELECTED);

    nvgSave(vg);
    nvgStrokeWidth(vg, 1.f);
    nvgStrokeColor(vg, line_colour);

    for (s32 fan = 0; fan <= 100; fan += 25) {
        const auto y = FanCurveYForFan(plot, fan);
        nvgBeginPath(vg);
        nvgMoveTo(vg, plot.x, y);
        nvgLineTo(vg, plot.x + plot.w, y);
        nvgStroke(vg);
        gfx::drawTextArgs(vg, plot.x - 10.f, y, 14.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info_colour, "%d%%", fan);
    }

    for (s32 temp = 0; temp <= detail::FAN_TEMP_MAX_C; temp += 15) {
        const auto x = FanCurveXForTemp(plot, temp);
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, plot.y);
        nvgLineTo(vg, x, plot.y + plot.h);
        nvgStroke(vg);
        gfx::drawTextArgs(vg, x, plot.y + plot.h + 14.f, 14.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, info_colour, "%dC", temp);
    }

    nvgStrokeWidth(vg, 2.f);
    nvgStrokeColor(vg, text_colour);
    nvgBeginPath(vg);
    nvgMoveTo(vg, plot.x, plot.y);
    nvgLineTo(vg, plot.x, plot.y + plot.h);
    nvgLineTo(vg, plot.x + plot.w, plot.y + plot.h);
    nvgStroke(vg);

    if (!curve.empty()) {
        if (easy_curve_mode) {
            // 1. Draw original curve (thin line + small dots)
            nvgStrokeWidth(vg, 2.5f);
            nvgStrokeColor(vg, WithAlpha(text_colour, 0.4f));
            nvgBeginPath(vg);
            for (size_t i = 0; i < curve.size(); i++) {
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                if (i) {
                    nvgLineTo(vg, x, y);
                } else {
                    nvgMoveTo(vg, x, y);
                }
            }
            nvgStroke(vg);

            for (size_t i = 0; i < curve.size(); i++) {
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                nvgBeginPath(vg);
                nvgCircle(vg, x, y, 4.f);
                nvgFillColor(vg, WithAlpha(text_colour, 0.6f));
                nvgFill(vg);
            }

            // 2. Draw smooth Bezier green helper curve
            const auto green_colour = nvgRGBA(46, 204, 113, 255); // emerald green
            nvgStrokeWidth(vg, 4.5f);
            nvgStrokeColor(vg, green_colour);
            nvgBeginPath(vg);

            const double bx0 = control_points[0].temp_c;
            const double bx2 = control_points[2].temp_c;
            const s32 steps = 30;
            for (s32 s = 0; s <= steps; s++) {
                const s32 temp = static_cast<s32>(bx0 + (bx2 - bx0) * s / steps + 0.5);
                const auto eval_bezier = [&](s32 temp_val) -> float {
                    const double X0 = control_points[0].temp_c;
                    const double Y0 = control_points[0].fan_percent;
                    const double X1 = control_points[1].temp_c;
                    const double Y1 = control_points[1].fan_percent;
                    const double X2 = control_points[2].temp_c;
                    const double Y2 = control_points[2].fan_percent;

                    if (temp_val <= X0) return Y0;
                    if (temp_val >= X2) return Y2;

                    const double a = X0 - 2.0 * X1 + X2;
                    const double b = 2.0 * (X1 - X0);
                    const double c = X0 - temp_val;

                    double t = 0.0;
                    if (std::abs(a) < 1e-5) {
                        if (std::abs(b) > 1e-5) t = -c / b;
                    } else {
                        const double disc = b * b - 4.0 * a * c;
                        if (disc >= 0.0) {
                            const double r1 = (-b + std::sqrt(disc)) / (2.0 * a);
                            const double r2 = (-b - std::sqrt(disc)) / (2.0 * a);
                            t = (r1 >= -0.01 && r1 <= 1.01) ? std::clamp(r1, 0.0, 1.0) : std::clamp(r2, 0.0, 1.0);
                        }
                    }
                    const double fan = (1.0 - t) * (1.0 - t) * Y0 + 2.0 * (1.0 - t) * t * Y1 + t * t * Y2;
                    return std::clamp(fan, static_cast<double>(detail::FAN_PERCENT_MIN), static_cast<double>(detail::FAN_PERCENT_MAX));
                };

                const auto x = FanCurveXForTemp(plot, temp);
                const auto y = FanCurveYForFan(plot, eval_bezier(temp));
                if (s) {
                    nvgLineTo(vg, x, y);
                } else {
                    nvgMoveTo(vg, x, y);
                }
            }
            nvgStroke(vg);

            // 3. Draw the 3 green control points
            for (size_t i = 0; i < control_points.size(); i++) {
                const auto point_selected = static_cast<s64>(i) == selected;
                const auto x = FanCurveXForTemp(plot, control_points[i].temp_c);
                const auto y = FanCurveYForFan(plot, control_points[i].fan_percent);
                nvgBeginPath(vg);
                nvgCircle(vg, x, y, point_selected ? 10.f : 7.f);
                nvgFillColor(vg, point_selected ? text_colour : green_colour);
                nvgFill(vg);
                if (point_selected && editing) {
                    nvgStrokeWidth(vg, 2.5f);
                    nvgStrokeColor(vg, WithAlpha(green_colour, 0.95f));
                    nvgBeginPath(vg);
                    nvgCircle(vg, x, y, 15.f);
                    nvgStroke(vg);
                }
            }

        } else {
            // Draw normal curve
            nvgStrokeWidth(vg, 4.f);
            nvgStrokeColor(vg, accent_colour);
            nvgBeginPath(vg);
            for (size_t i = 0; i < curve.size(); i++) {
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                if (i) {
                    nvgLineTo(vg, x, y);
                } else {
                    nvgMoveTo(vg, x, y);
                }
            }
            nvgStroke(vg);

            for (size_t i = 0; i < curve.size(); i++) {
                const auto point_selected = static_cast<s64>(i) == selected;
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                nvgBeginPath(vg);
                nvgCircle(vg, x, y, point_selected ? 10.f : 7.f);
                nvgFillColor(vg, point_selected ? text_colour : accent_colour);
                nvgFill(vg);
                if (point_selected && editing) {
                    nvgStrokeWidth(vg, 2.5f);
                    nvgStrokeColor(vg, WithAlpha(accent_colour, 0.95f));
                    nvgBeginPath(vg);
                    nvgCircle(vg, x, y, 15.f);
                    nvgStroke(vg);
                }
            }
        }

        DrawFanCurveSensorMarker(vg, theme, plot, curve, sensor);
    }
    nvgRestore(vg);
}

void DrawFanCurveListItem(NVGcontext* vg, Theme* theme, Vec4 v, const FanCurvePoint& point, s64 index, bool selected) {
    const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
    const auto value_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO;

    if (selected) {
        gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        gfx::drawRectOutline(vg, theme, 4.f, v);
    } else {
        gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    }

    gfx::drawTextArgs(
        vg, v.x + 14.f, v.y + v.h / 2.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
        theme->GetColour(label_id), "%d", static_cast<int>(index + 1)
    );
    gfx::drawTextArgs(
        vg, v.x + 142.f, v.y + v.h / 2.f, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
        theme->GetColour(value_id), "%dC", point.temp_c
    );
    gfx::drawTextArgs(
        vg, v.x + v.w - 14.f, v.y + v.h / 2.f, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
        theme->GetColour(value_id), "%d%%", point.fan_percent
    );
}

void DrawFanCurveListHeader(NVGcontext* vg, Theme* theme) {
    const auto v = FanCurveListRect();
    const auto title_colour = theme->GetColour(ThemeEntryID_TEXT);
    const auto column_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    gfx::drawText(vg, v.x + 22.f, v.y, 23.f, title_colour, "Points:", NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    gfx::drawText(vg, v.x + 22.f, v.y + 34.f, 14.f, column_colour, "#", NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    gfx::drawText(vg, v.x + 150.f, v.y + 34.f, 14.f, column_colour, "Temp", NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    gfx::drawText(vg, v.x + v.w - 22.f, v.y + 34.f, 14.f, column_colour, "Fan", NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
}

} // namespace sphaira::ui::menu::settings
