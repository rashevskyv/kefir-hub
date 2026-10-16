#include "ui/menus/settings/settings_fancurve.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <switch.h>

namespace sphaira::ui::menu::settings {

void OpenFanCurveMenu() {
    App::Push<FanCurveMenu>();
}


FanCurveMenu::FanCurveMenu() : MenuBase{"Fan curve", MenuFlag_None} {
    // the menu drives the fan itself while it is open (FanCurveSensorReader); a module
    // that runs a curve applied earlier this boot is started again when the menu closes.
    m_live_curve = detail::StopSphairaFanSysmodule();

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
    
    // Initialize Bezier control points from loaded curves
    m_docked = false;
    InitializeControlPointsFromCurve();
    m_docked = true;
    InitializeControlPointsFromCurve();
    m_docked = false;

    m_sensor_reader = std::make_unique<FanCurveSensorReader>();
    RefreshActions();

    m_list = std::make_unique<List>(1, 9, FanCurveListRect(), FanCurveListItemRect());
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

FanCurveMenu::~FanCurveMenu() {
    // gives the fan back to tc first.
    m_sensor_reader.reset();

    // tc reads the curve at boot only: until the next reboot the module keeps the new one.
    if (m_live_curve) {
        const auto rc = detail::StartSphairaFanSysmodule();
        if (R_FAILED(rc)) {
            // no UI here: this also runs while the app exits.
            log_write("[FAN] failed to start the fan module: 0x%X\n", rc);
        }
    }
}


void FanCurveMenu::RefreshActions() {
    RemoveActions();
    SetUiButtonSort(true);
    SetAction(Button::SELECT, Action{App::HandleMinus});

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
        std::make_pair(Button::START, Action{"Apply"_i18n, [this](){
            ApplyCurves();
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


void FanCurveMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_sensor_reader) {
        // the fan follows the curve on screen, so the user hears each edit.
        m_sensor_reader->Update(ActiveCurve());
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
        m_list->Draw(vg, theme, controls.size(), m_index, [this, &controls](auto* vg, auto* theme, Vec4 v, auto i) {
            DrawFanCurveListItem(vg, theme, v, controls[i], i, m_index == i);
        });
    } else {
        m_list->Draw(vg, theme, curve.size(), m_index, [this, &curve](auto* vg, auto* theme, Vec4 v, auto i) {
            DrawFanCurveListItem(vg, theme, v, curve[i], i, m_index == i);
        });
    }

    DrawFanCurveGraph(vg, theme, curve, controls, m_helper_curve_mode, m_index, m_docked, m_dirty, m_editing, m_sensor_reader ? m_sensor_reader->GetSample() : nullptr);
}

} // namespace sphaira::ui::menu::settings
