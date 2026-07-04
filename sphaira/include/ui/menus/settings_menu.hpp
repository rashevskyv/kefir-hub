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
    void ApplyCurves(bool reboot);
    void OnBack();
    auto ActiveCurve() -> std::vector<FanCurvePoint>&;
    auto ActiveCurve() const -> const std::vector<FanCurvePoint>&;
    void RefreshSubHeading();

private:
    std::vector<FanCurvePoint> m_handheld_curve;
    std::vector<FanCurvePoint> m_docked_curve;
    s64 m_index{};
    bool m_docked{};
    bool m_dirty{};
    bool m_editing{};
    bool m_touch_dragging{};
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

} // namespace sphaira::ui::menu::settings
