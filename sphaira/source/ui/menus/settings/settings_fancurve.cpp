#include "ui/menus/settings/settings_fancurve.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_translations.hpp"

#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <switch.h>

namespace sphaira::ui::menu::settings {

auto EvaluateFanPercent(const std::vector<FanCurvePoint>& curve, float temp_c) -> float {
    if (curve.empty()) return 0.f;
    if (temp_c <= curve.front().temp_c) return static_cast<float>(curve.front().fan_percent);
    if (temp_c >= curve.back().temp_c) return static_cast<float>(curve.back().fan_percent);

    for (size_t i = 1; i < curve.size(); ++i) {
        const auto& prev = curve[i - 1];
        const auto& cur = curve[i];
        if (temp_c >= prev.temp_c && temp_c <= cur.temp_c) {
            const float temp_span = cur.temp_c - prev.temp_c;
            if (temp_span == 0.f) return static_cast<float>(prev.fan_percent);
            const float fan_span = cur.fan_percent - prev.fan_percent;
            const float temp_offset = temp_c - prev.temp_c;
            return prev.fan_percent + (fan_span * temp_offset) / temp_span;
        }
    }
    return static_cast<float>(curve.back().fan_percent);
}

FanCurveSensorReader::FanCurveSensorReader() {
    m_ts_available = R_SUCCEEDED(tsInitialize());
}

FanCurveSensorReader::~FanCurveSensorReader() {
    if (m_ts_available) {
        tsExit();
    }
}

void FanCurveSensorReader::Update(const std::vector<FanCurvePoint>& active_curve) {
    const auto now = armTicksToNs(armGetSystemTick());
    const float dt = m_last_update_ns ? std::min(static_cast<float>(now - m_last_update_ns) / 1e9f, 0.1f) : 0.05f;
    m_last_update_ns = now;

    FanCurveSensorSample next{};
    SphairaFanState sys_state{};
    bool sys_ok = false;

    FILE* fp = fopen("/switch/sphaira/fan_status.bin", "rb");
    if (fp) {
        if (fread(&sys_state, sizeof(sys_state), 1, fp) == 1) {
            if (sys_state.magic == 0x46414E53 && sys_state.sysmodule_active == 1) {
                if (now >= sys_state.timestamp_ns && (now - sys_state.timestamp_ns) < 3000000000ULL) {
                    sys_ok = true;
                }
            }
        }
        fclose(fp);
    }

    if (sys_ok) {
        next.temp_milli_c = sys_state.temp_milli_c;
        next.valid = true;
    } else if (m_ts_available) {
        s32 temp_milli_c = 0;
        s32 temp_c = 0;
        TsSession session;
        if (R_SUCCEEDED(tsOpenSession(&session, TsDeviceCode_LocationExternal))) {
            float temp_f = 0.0f;
            Result rc = tsSessionGetTemperature(&session, &temp_f);
            tsSessionClose(&session);
            if (R_SUCCEEDED(rc) && temp_f > 0.0f) {
                next.temp_milli_c = static_cast<s32>(temp_f * 1000.0f);
                next.valid = true;
            }
        }
        if (!next.valid && R_SUCCEEDED(tsOpenSession(&session, TsDeviceCode_LocationInternal))) {
            float temp_f = 0.0f;
            Result rc = tsSessionGetTemperature(&session, &temp_f);
            tsSessionClose(&session);
            if (R_SUCCEEDED(rc) && temp_f > 0.0f) {
                next.temp_milli_c = static_cast<s32>(temp_f * 1000.0f);
                next.valid = true;
            }
        }
        if (!next.valid && R_SUCCEEDED(tsGetTemperature(TsLocation_External, &temp_c)) && temp_c > 0) {
            next.temp_milli_c = temp_c * 1000;
            next.valid = true;
        }
        if (!next.valid && R_SUCCEEDED(tsGetTemperature(TsLocation_Internal, &temp_c)) && temp_c > 0) {
            next.temp_milli_c = temp_c * 1000;
            next.valid = true;
        }
        if (!next.valid && R_SUCCEEDED(tsGetTemperatureMilliC(TsLocation_External, &temp_milli_c)) && temp_milli_c > 0) {
            next.temp_milli_c = temp_milli_c;
            next.valid = true;
        }
    }

    if (next.valid) {
        float target_fan = -1.0f;
        if (sys_ok) {
            target_fan = sys_state.fan_level * 100.0f;
        } else {
            const float temp_deg = static_cast<float>(next.temp_milli_c) / 1000.0f;
            target_fan = EvaluateFanPercent(active_curve, temp_deg);
        }

        if (m_displayed_fan_percent < 0.0f) {
            m_displayed_fan_percent = target_fan;
        } else {
            const float diff = target_fan - m_displayed_fan_percent;
            const float abs_diff = std::abs(diff);
            if (abs_diff < 0.2f) {
                m_displayed_fan_percent = target_fan;
            } else {
                const float speed_rate = (diff >= 0.0f) ? 25.0f : 18.0f; // 25% per sec up, 18% per sec down
                const float max_step = std::min(abs_diff, speed_rate * dt);
                m_displayed_fan_percent += (diff >= 0.0f ? max_step : -max_step);
            }
        }

        next.fan_percent = static_cast<s32>(m_displayed_fan_percent + 0.5f);
    }

    m_sample = next;
}

auto FanCurveSensorReader::GetSample() const -> const FanCurveSensorSample* {
    return m_sample.valid ? &m_sample : nullptr;
}

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

FanCurveMenu::FanCurveMenu() : MenuBase{"Fan curve", MenuFlag_None} {
    if (detail::FileExists("/atmosphere/contents/00FF46554E43544C/flags/boot2.flag")) {
        detail::DeletePath("/atmosphere/contents/00FF46554E43544C/flags/boot2.flag");
    }

    if (!detail::IsSphairaFanSysmoduleRunning() && detail::IsSphairaFanSysmoduleInstalled()) {
        detail::RestartSphairaFanSysmodule();
    }

    m_handheld_curve = detail::ReadFanCurve(
        "tskin_rate_table_handheld_on_fwdbg",
        "tskin_rate_table_handheld",
        detail::DefaultHandheldFanCurve()
    );
    m_docked_curve = detail::ReadFanCurve(
        "tskin_rate_table_console_on_fwdbg",
        "tskin_rate_table_console",
        detail::DefaultDockedFanCurve()
    );
    m_applied_handheld_curve = m_handheld_curve;
    m_applied_docked_curve = m_docked_curve;
    
    // Initialize Bezier control points from loaded curves
    m_docked = false;
    InitializeControlPointsFromCurve();
    m_docked = true;
    InitializeControlPointsFromCurve();
    m_docked = false;

    m_sysmodule_enabled = detail::IsSphairaFanSysmoduleRunning();
    m_sensor_reader = std::make_unique<FanCurveSensorReader>();
    RefreshActions();

    m_list = std::make_unique<List>(1, 9, FanCurveListRect(), FanCurveListItemRect());
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

FanCurveMenu::~FanCurveMenu() = default;

auto FanCurveMenu::ActiveCurve() -> std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_curve : m_handheld_curve;
}

auto FanCurveMenu::ActiveCurve() const -> const std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_curve : m_handheld_curve;
}

auto FanCurveMenu::ActiveControlPoints() -> std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_control_points : m_handheld_control_points;
}

auto FanCurveMenu::ActiveControlPoints() const -> const std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_control_points : m_handheld_control_points;
}

auto FanCurveMenu::ActiveOriginalTemps() -> std::vector<s32>& {
    return m_docked ? m_docked_original_temps : m_handheld_original_temps;
}

auto FanCurveMenu::ActiveOriginalTemps() const -> const std::vector<s32>& {
    return m_docked ? m_docked_original_temps : m_handheld_original_temps;
}

void FanCurveMenu::InitializeControlPointsFromCurve() {
    auto& curve = ActiveCurve();
    auto& controls = ActiveControlPoints();
    auto& orig_temps = ActiveOriginalTemps();
    controls.clear();
    orig_temps.clear();

    if (curve.empty()) {
        controls.push_back({40, 20});
        controls.push_back({60, 50});
        controls.push_back({80, 100});
        orig_temps = {40, 48, 56, 64, 72, 80};
        return;
    }

    // Save original temperatures
    for (const auto& pt : curve) {
        orig_temps.push_back(pt.temp_c);
    }

    controls.push_back(curve.front());

    s32 t_min = curve.front().temp_c;
    s32 t_max = curve.back().temp_c;
    s32 t_mid = (t_min + t_max) / 2;
    s32 f_mid = static_cast<s32>(EvaluateFanPercent(curve, static_cast<float>(t_mid)) + 0.5f);
    
    controls.push_back({t_mid, f_mid});
    controls.push_back(curve.back());
}

auto FanCurveMenu::EvaluateBezierFanPercent(const std::vector<FanCurvePoint>& controls, s32 temp_c) const -> s32 {
    if (controls.size() != 3) {
        return 0;
    }
    const double X0 = controls[0].temp_c;
    const double Y0 = controls[0].fan_percent;
    const double X1 = controls[1].temp_c;
    const double Y1 = controls[1].fan_percent;
    const double X2 = controls[2].temp_c;
    const double Y2 = controls[2].fan_percent;

    if (temp_c <= X0) return static_cast<s32>(Y0);
    if (temp_c >= X2) return static_cast<s32>(Y2);

    const double a = X0 - 2.0 * X1 + X2;
    const double b = 2.0 * (X1 - X0);
    const double c = X0 - temp_c;

    double t = 0.0;
    if (std::abs(a) < 1e-5) {
        if (std::abs(b) > 1e-5) {
            t = -c / b;
        }
    } else {
        const double disc = b * b - 4.0 * a * c;
        if (disc >= 0.0) {
            const double r1 = (-b + std::sqrt(disc)) / (2.0 * a);
            const double r2 = (-b - std::sqrt(disc)) / (2.0 * a);
            if (r1 >= -0.01 && r1 <= 1.01) {
                t = std::clamp(r1, 0.0, 1.0);
            } else {
                t = std::clamp(r2, 0.0, 1.0);
            }
        }
    }
    const double fan = (1.0 - t) * (1.0 - t) * Y0 + 2.0 * (1.0 - t) * t * Y1 + t * t * Y2;
    return std::clamp(static_cast<s32>(fan + 0.5), detail::FAN_PERCENT_MIN, detail::FAN_PERCENT_MAX);
}

void FanCurveMenu::RegenerateCurveFromControls() {
    auto& controls = ActiveControlPoints();
    auto& curve = ActiveCurve();
    const auto& orig_temps = ActiveOriginalTemps();
    if (controls.size() != 3 || curve.empty() || orig_temps.size() != curve.size()) {
        return;
    }

    // Clamp Mid temperature strictly between Min and Max
    controls[1].temp_c = std::clamp(controls[1].temp_c, controls[0].temp_c + 1, controls[2].temp_c - 1);

    const double new_t_min = controls[0].temp_c;
    const double new_t_max = controls[2].temp_c;
    const double old_t_min = orig_temps.front();
    const double old_t_max = orig_temps.back();

    const double old_range = old_t_max - old_t_min;
    const double new_range = new_t_max - new_t_min;

    // 1. Update original points' temperatures proportionally
    for (size_t i = 0; i < curve.size(); i++) {
        if (i == 0) {
            curve[i].temp_c = controls[0].temp_c;
        } else if (i == curve.size() - 1) {
            curve[i].temp_c = controls[2].temp_c;
        } else {
            if (old_range > 0.0) {
                const double pct = (orig_temps[i] - old_t_min) / old_range;
                curve[i].temp_c = static_cast<s32>(new_t_min + pct * new_range + 0.5);
            } else {
                curve[i].temp_c = controls[0].temp_c;
            }
        }
    }

    // Ensure the temperatures of the curve are strictly sorted/monotonic and separated by at least 1 degree
    for (size_t i = 1; i < curve.size(); i++) {
        if (curve[i].temp_c <= curve[i - 1].temp_c) {
            curve[i].temp_c = curve[i - 1].temp_c + 1;
        }
    }
    // Make sure we didn't overshoot max temp
    if (curve.back().temp_c != controls[2].temp_c) {
        curve.back().temp_c = controls[2].temp_c;
        for (size_t i = curve.size() - 2; i > 0; i--) {
            if (curve[i].temp_c >= curve[i + 1].temp_c) {
                curve[i].temp_c = curve[i + 1].temp_c - 1;
            }
        }
    }

    // 2. Update each original point's fan speed based on the green Bezier curve
    for (size_t i = 0; i < curve.size(); i++) {
        curve[i].fan_percent = EvaluateBezierFanPercent(controls, curve[i].temp_c);
    }
}

void FanCurveMenu::RefreshActions() {
    RemoveActions();
    SetUiButtonSort(true);
    SetAction(Button::SELECT, Action{App::Exit});

    if (m_editing) {
        SetActions(
            std::make_pair(Button::A, Action{"Done"_i18n, [this](){
                SetEditing(false);
            }}),
            std::make_pair(Button::B, Action{"Done"_i18n, [this](){
                SetEditing(false);
            }})
        );
        return;
    }

    const auto apply_label = "Apply"_i18n;
    const auto apply_mode = FanCurveApplyMode::Live;

    SetActions(
        std::make_pair(Button::A, Action{"Edit"_i18n, [this](){
            SetEditing(true);
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            OnBack();
        }}),
        std::make_pair(Button::X, Action{"Mode"_i18n, [this](){
            SwitchProfile();
        }}),
        std::make_pair(Button::Y, Action{m_helper_curve_mode ? "Manual Mode"_i18n : "Bezier"_i18n, [this](){
            SetEditing(false);
            m_helper_curve_mode = !m_helper_curve_mode;
            if (m_helper_curve_mode) {
                InitializeControlPointsFromCurve();
            }
            SetIndex(0);
            RefreshActions();
        }}),
        std::make_pair(Button::L2, Action{"Load Preset"_i18n, [this](){
            DisplayPresets();
        }}),
        std::make_pair(Button::R2, Action{"Save Preset"_i18n, [this](){
            DisplaySavePreset();
        }}),
        std::make_pair(Button::START, Action{apply_label, [this, apply_mode](){
            ApplyCurves(apply_mode);
        }})
    );

    SetAction(Button::L, Action{"Add Point"_i18n, [this](){
        AddPoint();
    }});
    SetAction(Button::R, Action{"Remove Point"_i18n, [this](){
        RemovePoint();
    }});
}

void FanCurveMenu::RefreshSubHeading() {
    SetSubHeading("");
}

void FanCurveMenu::SetIndex(s64 index) {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        m_index = 0;
        RefreshSubHeading();
        return;
    }

    m_index = std::clamp<s64>(index, 0, static_cast<s64>(points.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    RefreshSubHeading();
}

void FanCurveMenu::SetEditing(bool editing) {
    if (m_editing == editing) {
        return;
    }
    if (editing && ActiveCurve().empty()) {
        return;
    }

    m_editing = editing;
    RefreshActions();
    RefreshSubHeading();
}

void FanCurveMenu::SwitchProfile() {
    SetEditing(false);
    m_docked = !m_docked;
    SetIndex(m_index);
}

void FanCurveMenu::DisplayPresets() {
    App::Push<PopupList>(
        "Fan preset"_i18n,
        detail::FanPresetLabels(m_docked),
        [this](auto index){
            if (index) {
                ApplyPreset(*index);
            }
        },
        0
    );
}

void FanCurveMenu::DisplaySavePreset() {
    App::Push<PopupList>(
        "Save fan preset"_i18n,
        detail::FanCustomPresetLabels(m_docked),
        [this](auto index){
            if (index) {
                SavePreset(*index);
            }
        },
        0
    );
}

void FanCurveMenu::DisplayApplyMenu() {
    const bool live_apply_available = detail::IsSphairaFanSysmoduleInstalled();
    PopupList::Items items;
    if (live_apply_available) {
        items.emplace_back("Apply"_i18n);
    } else {
        items.emplace_back("Save and Reboot"_i18n);
    }

    App::Push<PopupList>(
        "Apply fan curve"_i18n,
        std::move(items),
        [this, live_apply_available](auto index){
            if (!index) {
                return;
            }
            if (live_apply_available && *index == 0) {
                ApplyCurves(FanCurveApplyMode::Live);
            } else {
                ApplyCurves(FanCurveApplyMode::Reboot);
            }
        },
        0
    );
}

void FanCurveMenu::ApplyPreset(s64 index) {
    SetEditing(false);
    std::vector<FanCurvePoint> curve;
    if (index < detail::FAN_BUILTIN_PRESET_COUNT) {
        curve = detail::FanPresetCurve(index, m_docked);
    } else if (!detail::ReadCustomFanPreset(index - detail::FAN_BUILTIN_PRESET_COUNT, m_docked, curve)) {
        App::Notify("Fan preset is empty"_i18n);
        return;
    }

    ActiveCurve() = std::move(curve);
    if (m_helper_curve_mode) {
        InitializeControlPointsFromCurve();
    }
    m_dirty = true;
    SetIndex(m_index);
}

void FanCurveMenu::SavePreset(s64 index) {
    auto name = detail::ReadCustomFanPresetName(index, m_docked);
    if (R_FAILED(swkbd::ShowText(name, "Preset name", name.c_str(), 0, 48))) {
        return;
    }
    name = detail::SanitizeFanPresetName(std::move(name));
    if (name.empty()) {
        name = detail::FanCustomPresetDefaultName(index);
    }

    const auto rc = detail::SaveCustomFanPreset(index, m_docked, ActiveCurve(), name);
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to save fan preset"_i18n);
        return;
    }

    App::Notify("Saved fan preset: "_i18n + name);
}

void FanCurveMenu::AddPoint() {
    auto& curve = ActiveCurve();
    if (curve.empty()) {
        curve.push_back({40, 20});
        m_dirty = true;
        SetIndex(0);
        return;
    }

    if (curve.size() >= static_cast<size_t>(detail::FAN_TEMP_MAX_C - detail::FAN_TEMP_MIN_C + 1)) {
        App::Notify("No room for another fan point"_i18n);
        return;
    }

    detail::NormalizeFanCurve(curve);

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(curve.size() - 1));
    s64 insert_index = index + 1;
    FanCurvePoint point{};
    bool found{};

    if (index + 1 < static_cast<s64>(curve.size()) && curve[index + 1].temp_c - curve[index].temp_c > 1) {
        const auto& left = curve[index];
        const auto& right = curve[index + 1];
        point.temp_c = (left.temp_c + right.temp_c) / 2;
        point.fan_percent = (left.fan_percent + right.fan_percent) / 2;
        found = true;
    } else if (index > 0 && curve[index].temp_c - curve[index - 1].temp_c > 1) {
        const auto& left = curve[index - 1];
        const auto& right = curve[index];
        insert_index = index;
        point.temp_c = (left.temp_c + right.temp_c) / 2;
        point.fan_percent = (left.fan_percent + right.fan_percent) / 2;
        found = true;
    } else if (curve[index].temp_c < detail::FAN_TEMP_MAX_C) {
        const auto& base = curve[index];
        point.temp_c = std::max(base.temp_c + 1, (base.temp_c + detail::FAN_TEMP_MAX_C) / 2);
        point.fan_percent = base.fan_percent;
        found = true;
    } else if (curve[index].temp_c > detail::FAN_TEMP_MIN_C) {
        const auto& base = curve[index];
        insert_index = index;
        point.temp_c = std::min(base.temp_c - 1, (detail::FAN_TEMP_MIN_C + base.temp_c) / 2);
        point.fan_percent = base.fan_percent;
        found = true;
    }

    if (!found) {
        App::Notify("No room for another fan point"_i18n);
        return;
    }

    curve.insert(curve.begin() + insert_index, point);
    detail::NormalizeFanCurve(curve);
    m_dirty = true;
    if (m_helper_curve_mode) {
        InitializeControlPointsFromCurve();
    }
    SetIndex(insert_index);
}

void FanCurveMenu::RemovePoint() {
    auto& curve = ActiveCurve();
    if (curve.size() <= 2) {
        App::Notify("Fan curve needs at least two points"_i18n);
        return;
    }

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(curve.size() - 1));
    curve.erase(curve.begin() + index);
    detail::NormalizeFanCurve(curve);
    m_dirty = true;
    if (m_helper_curve_mode) {
        InitializeControlPointsFromCurve();
    }
    SetEditing(false);
    SetIndex(std::min<s64>(index, static_cast<s64>(curve.size() - 1)));
}

void FanCurveMenu::AdjustSelectedFan(s32 delta) {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        return;
    }

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(points.size() - 1));
    const auto& point = points[index];
    SetSelectedPoint(index, point.temp_c, point.fan_percent + delta);
}

void FanCurveMenu::AdjustSelectedTemp(s32 delta) {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        return;
    }

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(points.size() - 1));
    const auto& point = points[index];
    SetSelectedPoint(index, point.temp_c + delta, point.fan_percent);
}

void FanCurveMenu::SetSelectedPoint(s64 index, s32 temp_c, s32 fan_percent) {
    if (m_helper_curve_mode) {
        auto& controls = ActiveControlPoints();
        if (controls.size() != 3) {
            return;
        }
        index = std::clamp<s64>(index, 0, 2);
        
        s32 min_temp = detail::FAN_TEMP_MIN_C;
        s32 max_temp = detail::FAN_TEMP_MAX_C;
        if (index == 0) {
            min_temp = detail::FAN_TEMP_MIN_C;
            max_temp = controls[1].temp_c - 1;
        } else if (index == 1) {
            min_temp = controls[0].temp_c + 1;
            max_temp = controls[2].temp_c - 1;
        } else if (index == 2) {
            min_temp = controls[1].temp_c + 1;
            max_temp = detail::FAN_TEMP_MAX_C;
        }

        s32 min_fan = detail::FAN_PERCENT_MIN;
        s32 max_fan = detail::FAN_PERCENT_MAX;

        auto& point = controls[index];
        const auto next_temp = std::clamp<s32>(temp_c, min_temp, max_temp);
        const auto next_fan = std::clamp<s32>(fan_percent, min_fan, max_fan);

        m_index = index;
        if (point.temp_c != next_temp || point.fan_percent != next_fan) {
            point.temp_c = next_temp;
            point.fan_percent = next_fan;
            RegenerateCurveFromControls();
            m_dirty = true;
        }
    } else {
        auto& curve = ActiveCurve();
        if (curve.empty()) {
            return;
        }

        index = std::clamp<s64>(index, 0, static_cast<s64>(curve.size() - 1));
        const auto min_value = index ? curve[index - 1].temp_c + 1 : detail::FAN_TEMP_MIN_C;
        const auto max_value = index + 1 < static_cast<s64>(curve.size()) ? curve[index + 1].temp_c - 1 : detail::FAN_TEMP_MAX_C;
        const auto min_fan = index ? curve[index - 1].fan_percent : detail::FAN_PERCENT_MIN;
        const auto max_fan = index + 1 < static_cast<s64>(curve.size()) ? curve[index + 1].fan_percent : detail::FAN_PERCENT_MAX;
        auto& point = curve[index];
        const auto next_temp = std::clamp<s32>(temp_c, min_value, max_value);
        const auto next_fan = std::clamp<s32>(fan_percent, min_fan, max_fan);

        m_index = index;
        if (point.temp_c != next_temp || point.fan_percent != next_fan) {
            point.temp_c = next_temp;
            point.fan_percent = next_fan;
            m_dirty = true;
        }
    }
    RefreshSubHeading();
}

auto FanCurveMenu::HandleGraphTouch(TouchInfo* touch) -> bool {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        m_touch_dragging = false;
        return false;
    }

    const auto graph_touch = ExpandRect(FanCurveGraphRect(), 18.f);
    const auto plot = FanCurvePlotRect();
    const auto pick_point = [&](float x, float y, s64& out_index) {
        constexpr float PICK_RADIUS = 34.f;
        constexpr float PICK_DISTANCE = PICK_RADIUS * PICK_RADIUS;
        auto best_distance = std::numeric_limits<float>::max();
        s64 best_index{};

        for (size_t i = 0; i < points.size(); i++) {
            const auto point_x = FanCurveXForTemp(plot, points[i].temp_c);
            const auto point_y = FanCurveYForFan(plot, points[i].fan_percent);
            const auto dx = point_x - x;
            const auto dy = point_y - y;
            const auto distance = dx * dx + dy * dy;
            if (distance < best_distance) {
                best_distance = distance;
                best_index = static_cast<s64>(i);
            }
        }

        if (best_distance > PICK_DISTANCE) {
            return false;
        }

        out_index = best_index;
        return true;
    };

    if (touch->is_clicked && touch->in_range(graph_touch)) {
        m_touch_dragging = false;
        s64 best_index{};
        if (pick_point(static_cast<float>(touch->cur.x), static_cast<float>(touch->cur.y), best_index)) {
            SetIndex(best_index);
            return true;
        }
        return false;
    }

    if (touch->is_touching && (m_touch_dragging || touch->in_range(graph_touch))) {
        if (!m_touch_dragging) {
            s64 best_index{};
            if (touch->is_scroll || !pick_point(static_cast<float>(touch->cur.x), static_cast<float>(touch->cur.y), best_index)) {
                return false;
            }
            m_index = best_index;
            m_touch_dragging = true;
        }

        SetSelectedPoint(m_index, FanCurveTempForX(plot, touch->cur.x), FanCurveFanForY(plot, touch->cur.y));
        return true;
    }

    if (touch->is_end) {
        m_touch_dragging = false;
    }

    return false;
}

void FanCurveMenu::ApplyCurves(FanCurveApplyMode mode) {
    const auto handheld = m_handheld_curve;
    const auto docked = m_docked_curve;
    const bool live_apply = mode == FanCurveApplyMode::Live;

    App::Push<ProgressBox>(
        0,
        "Applying"_i18n,
        "Fan curve",
        [handheld, docked, mode, live_apply](auto pbox) -> Result {
            pbox->NewTransfer("Writing Atmosphere fan curve and restarting fan module...");
            return detail::ApplyFanCurves(handheld, docked, mode);
        },
        [this, live_apply, handheld, docked](Result rc){
            if (R_FAILED(rc)) {
                App::Push<HoldConfirmBox>(
                    "Failed to activate fan module.\n\nChanges will apply on next reboot.\n\nHold A to reboot now and apply changes."_i18n,
                    3.f,
                    [](bool confirmed){
                        if (confirmed) {
                            detail::RebootAfterSetting();
                        }
                    }
                );
                m_dirty = false;
                return;
            }
            m_applied_handheld_curve = handheld;
            m_applied_docked_curve = docked;
            m_dirty = false;
            App::Notify("Fan curve applied"_i18n);
        }
    );
}

void FanCurveMenu::OnBack() {
    if (m_editing) {
        SetEditing(false);
        return;
    }

    if (!m_dirty) {
        SetPop();
        return;
    }

    App::Push<OptionBox>(
        "Discard unsaved fan curve changes?"_i18n,
        "Cancel"_i18n,
        "Discard"_i18n,
        0,
        [this](auto op_index){
            if (op_index && *op_index) {
                SetPop();
            }
        }
    );
}

void FanCurveMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_sensor_reader) {
        const auto& applied_curve = m_docked ? m_applied_docked_curve : m_applied_handheld_curve;
        m_sensor_reader->Update(applied_curve);
    }

    if (HandleGraphTouch(touch)) {
        return;
    }

    if (m_editing) {
        s32 temp_delta{};
        s32 fan_delta{};
        if (controller->GotDown(Button::LEFT)) {
            temp_delta--;
        }
        if (controller->GotDown(Button::RIGHT)) {
            temp_delta++;
        }
        if (controller->GotDown(Button::UP)) {
            fan_delta++;
        }
        if (controller->GotDown(Button::DOWN)) {
            fan_delta--;
        }

        if (temp_delta || fan_delta) {
            const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
            if (!points.empty()) {
                const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(points.size() - 1));
                const auto point = points[index];
                SetSelectedPoint(index, point.temp_c + temp_delta, point.fan_percent + fan_delta);
            }
        }
        return;
    }

    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    m_list->OnUpdate(controller, touch, m_index, points.size(), [this](bool touch, auto i) {
        if (!touch || m_index != i) {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void FanCurveMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto& curve = ActiveCurve();
    const auto& controls = ActiveControlPoints();
    DrawFanCurveListHeader(vg, theme);

    if (m_helper_curve_mode) {
        m_list->Draw(vg, theme, controls.size(), [this, &controls](auto* vg, auto* theme, Vec4 v, auto i) {
            DrawFanCurveListItem(vg, theme, v, controls[i], i, m_index == i);
        });
    } else {
        m_list->Draw(vg, theme, curve.size(), [this, &curve](auto* vg, auto* theme, Vec4 v, auto i) {
            DrawFanCurveListItem(vg, theme, v, curve[i], i, m_index == i);
        });
    }

    DrawFanCurveGraph(vg, theme, curve, controls, m_helper_curve_mode, m_index, m_docked, m_dirty, m_editing, m_sensor_reader ? m_sensor_reader->GetSample() : nullptr);
}

} // namespace sphaira::ui::menu::settings
