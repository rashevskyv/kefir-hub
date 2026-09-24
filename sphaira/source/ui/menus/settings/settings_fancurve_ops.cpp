#include "ui/menus/settings/settings_fancurve.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/hold_confirm_box.hpp"
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

} // namespace sphaira::ui::menu::settings
