#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::settings {

struct SettingsItem {
    std::string label;
    std::string description;
    std::function<std::string()> value;
    std::function<void()> action;
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

} // namespace sphaira::ui::menu::settings
