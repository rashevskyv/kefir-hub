#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "ui/menus/settings/settings_tweaks.hpp"
#include "location.hpp"

namespace sphaira::ui::menu::settings {

// probes the given network location, Result 0 on success.
auto TestLocationConnection(const location::Entry& loc) -> Result;

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
    std::string id{};
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

struct SourceEditMenu final : MenuBase {
    SourceEditMenu(std::string name);
    ~SourceEditMenu();

    auto GetShortTitle() const -> const char* override { return "Edit Source"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    void OnSelect();
    std::vector<SettingsItem> BuildEditItems();

private:
    std::string m_loc_name;
    std::vector<SettingsItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace sphaira::ui::menu::settings
