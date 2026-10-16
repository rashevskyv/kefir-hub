#pragma once

#include <vector>
#include <string>
#include <memory>
#include <switch.h>
#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/types.hpp"

namespace sphaira::ui::menu::settings {

struct FanCurveSensorSample {
    bool valid{};
    s32 temp_milli_c{};
    s32 fan_percent{-1};
};

auto EvaluateFanPercent(const std::vector<FanCurvePoint>& curve, float temp_c) -> float;

void OpenFanCurveMenu();

// While the fan curve menu is open, Kefir Hub takes the fan from tc and drives it
// with the curve on screen, so every edit is heard at once. The destructor gives
// the fan back to tc, which uses the saved curve after the next reboot.
struct FanCurveSensorReader final {
    FanCurveSensorReader();
    ~FanCurveSensorReader();

    void Update(const std::vector<FanCurvePoint>& active_curve);
    auto GetSample() const -> const FanCurveSensorSample*;

private:
    auto ReadTemperature(s32& out_milli_c) -> bool;

    FanCurveSensorSample m_sample{};
    u64 m_last_update_ns{};
    u64 m_last_poll_ns{};
    float m_displayed_fan_percent{-1.0f};
    s32 m_temp_milli_c{};
    bool m_temp_valid{};
    s32 m_set_fan_percent{-1};
    bool m_ts_available{};
    bool m_ts_session_open{};
    TsSession m_ts_session{};
    bool m_tc_available{};
    bool m_fan_available{};
    bool m_fan_controller_open{};
    FanController m_fan_controller{};
};

auto FanCurveProfileLabel(bool docked) -> const char*;
auto FanCurveGraphRect() -> Vec4;
auto FanCurvePlotRect() -> Vec4;
auto FanCurveListRect() -> Vec4;
auto FanCurveListItemRect() -> Vec4;
auto FanCurveXForTempValue(const Vec4& plot, float temp_c) -> float;
auto FanCurveXForTemp(const Vec4& plot, s32 temp_c) -> float;
auto FanCurveYForFan(const Vec4& plot, s32 fan_percent) -> float;
auto FanCurveTempForX(const Vec4& plot, float x) -> s32;
auto FanCurveFanForY(const Vec4& plot, float y) -> s32;
auto ExpandRect(Vec4 rect, float amount) -> Vec4;
auto WithAlpha(NVGcolor colour, float alpha) -> NVGcolor;
auto FormatMilliC(s32 milli_c) -> std::string;
void DrawHorizontalDashes(NVGcontext* vg, float x0, float x1, float y, const NVGcolor& colour);
void DrawVerticalDashes(NVGcontext* vg, float x, float y0, float y1, const NVGcolor& colour);
void DrawFanCurveSensorMarker(NVGcontext* vg, Theme* theme, const Vec4& plot, const std::vector<FanCurvePoint>& curve, const FanCurveSensorSample* sensor);
void DrawFanCurveGraph(NVGcontext* vg, Theme* theme, const std::vector<FanCurvePoint>& curve, const std::vector<FanCurvePoint>& control_points, bool easy_curve_mode, s64 selected, bool docked, bool dirty, bool editing, const FanCurveSensorSample* sensor);
void DrawFanCurveListItem(NVGcontext* vg, Theme* theme, Vec4 v, const FanCurvePoint& point, s64 index, bool selected);
void DrawFanCurveListHeader(NVGcontext* vg, Theme* theme);

struct FanCurveMenu final : MenuBase {
    FanCurveMenu();
    ~FanCurveMenu();

    auto GetShortTitle() const -> const char* override { return "Fan curve"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void RefreshActions();
    void SetIndex(s64 index);
    void SetEditing(bool editing);
    void SwitchProfile();
    void DisplayPresets();
    void DisplaySavePreset();
    void ApplyPreset(s64 index);
    void SavePreset(s64 index);
    void AddPoint();
    void RemovePoint();
    void SetSelectedPoint(s64 index, s32 temp_c, s32 fan_percent);
    auto HandleGraphTouch(TouchInfo* touch) -> bool;
    void ApplyCurves();
    void OnBack();
    auto ActiveCurve() -> std::vector<FanCurvePoint>&;
    auto ActiveCurve() const -> const std::vector<FanCurvePoint>&;
    auto ActiveControlPoints() -> std::vector<FanCurvePoint>&;
    auto ActiveControlPoints() const -> const std::vector<FanCurvePoint>&;
    auto ActiveOriginalTemps() -> std::vector<s32>&;
    auto ActiveOriginalTemps() const -> const std::vector<s32>&;
    void RegenerateCurveFromControls();
    void InitializeControlPointsFromCurve();
    auto EvaluateBezierFanPercent(const std::vector<FanCurvePoint>& controls, s32 temp_c) const -> s32;
    void RefreshSubHeading();

private:
    std::vector<FanCurvePoint> m_handheld_curve;
    std::vector<FanCurvePoint> m_docked_curve;
    std::vector<FanCurvePoint> m_handheld_control_points;
    std::vector<FanCurvePoint> m_docked_control_points;
    std::vector<s32> m_handheld_original_temps;
    std::vector<s32> m_docked_original_temps;
    s64 m_index{};
    bool m_docked{};
    bool m_dirty{};
    bool m_editing{};
    bool m_touch_dragging{};
    bool m_helper_curve_mode{false}; // disabled by default (auxiliary mode)
    std::unique_ptr<FanCurveSensorReader> m_sensor_reader;
    std::unique_ptr<List> m_list;
};

} // namespace sphaira::ui::menu::settings
