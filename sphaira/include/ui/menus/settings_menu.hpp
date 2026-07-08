#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::settings {

enum class SettingsItemKind {
    Normal,
    Folder,
    Download,
    Favorite,
};

struct SettingsItem {
    std::string label;
    std::string description;
    std::function<std::string()> value;
    std::function<void()> action;
    SettingsItemKind kind{SettingsItemKind::Normal};
};

struct SettingsCategory {
    std::string label;
    std::string description;
    std::vector<SettingsItem> items;
};

struct Menu final : MenuBase {
    Menu();
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Settings"; }
    auto IsSettings() const -> bool override { return true; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    enum class FocusPane {
        Categories,
        Items,
    };

    void BuildCategories();
    void SetFocusPane(FocusPane pane);
    void SetCategoryIndex(s64 index);
    void SetItemIndex(s64 index);
    void OnSelect();
    void OnBack();

private:
    std::vector<SettingsCategory> m_categories;
    s64 m_category_index{};
    s64 m_item_index{};
    FocusPane m_focus_pane{FocusPane::Categories};
    std::unique_ptr<List> m_category_list;
    std::unique_ptr<List> m_item_list;
};

struct SoftwareMenu final : MenuBase {
    SoftwareMenu();
    ~SoftwareMenu();

    auto GetShortTitle() const -> const char* override { return "Software"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<SettingsItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct DbiMenu final : MenuBase {
    DbiMenu();
    ~DbiMenu();

    auto GetShortTitle() const -> const char* override { return "DBI"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<SettingsItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct KefirSettingsMenu final : MenuBase {
    KefirSettingsMenu();
    ~KefirSettingsMenu();

    auto GetShortTitle() const -> const char* override { return "Kefir Settings"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<SettingsItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct FanCurvePoint {
    s32 temp_c{};
    s32 fan_percent{};
};

struct FanCurveSensorReader;

enum class FanCurveApplyMode {
    Live,
    Reboot,
};

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
    void DisplayApplyMenu();
    void ApplyPreset(s64 index);
    void SavePreset(s64 index);
    void AddPoint();
    void RemovePoint();
    void AdjustSelectedFan(s32 delta);
    void AdjustSelectedTemp(s32 delta);
    void SetSelectedPoint(s64 index, s32 temp_c, s32 fan_percent);
    auto HandleGraphTouch(TouchInfo* touch) -> bool;
    void ApplyCurves(FanCurveApplyMode mode);
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
    std::vector<FanCurvePoint> m_applied_handheld_curve;
    std::vector<FanCurvePoint> m_applied_docked_curve;
    std::vector<FanCurvePoint> m_handheld_control_points;
    std::vector<FanCurvePoint> m_docked_control_points;
    std::vector<s32> m_handheld_original_temps;
    std::vector<s32> m_docked_original_temps;
    s64 m_index{};
    bool m_docked{};
    bool m_dirty{};
    bool m_editing{};
    bool m_touch_dragging{};
    bool m_sysmodule_enabled{};
    bool m_helper_curve_mode{false}; // disabled by default (auxiliary mode)
    std::unique_ptr<FanCurveSensorReader> m_sensor_reader;
    std::unique_ptr<List> m_list;
};

struct ThemesMenu final : MenuBase {
    ThemesMenu();
    ~ThemesMenu();

    auto GetShortTitle() const -> const char* override { return "Themes"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<SettingsItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct TranslateMenu final : MenuBase {
    TranslateMenu();
    ~TranslateMenu();

    auto GetShortTitle() const -> const char* override { return "Translate Interface"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<SettingsItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace sphaira::ui::menu::settings
