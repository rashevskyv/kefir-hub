#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"
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
    // a caption that splits a long category into blocks. Drawn small and
    // dimmed, and the cursor steps over it -- it is a label, not a row.
    Header,
};

struct SettingsItem {
    std::string label;
    std::string description;
    std::function<std::string()> value;
    std::function<void()> action;
    SettingsItemKind kind{SettingsItemKind::Normal};
    std::string id{};
    // set on a Folder row that opens *inside* the settings menu: the right pane
    // is replaced by these items while the category column stays put, with the
    // folder listed indented under its category. Built on open so the rows read
    // current state. When unset, a Folder row just runs its action.
    std::function<std::vector<SettingsItem>()> folder_items{};
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

    // the left column lists every category plus, while a folder is open, the
    // folder itself on the row below its category. "row" is an index into that
    // list, "category index" an index into m_categories.
    auto CategoryRowCount() const -> s64;
    auto CategoryRow() const -> s64;
    void SetCategoryRow(s64 row);

    // the right pane shows the open folder's items when there is one, and the
    // selected category's items otherwise.
    auto CurrentItems() const -> const std::vector<SettingsItem>&;
    auto CurrentItemIndex() const -> s64;
    void SetCurrentItemIndex(s64 index);

    void OpenFolder(const SettingsItem& item);
    void CloseFolder();
    void SetFolderIndex(s64 index);

    void DrawItemRow(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected, bool focused);

private:
    std::vector<SettingsCategory> m_categories;
    s64 m_category_index{};
    s64 m_item_index{};
    FocusPane m_focus_pane{FocusPane::Categories};
    std::unique_ptr<List> m_category_list;
    std::unique_ptr<List> m_item_list;

    // open folder page, drawn in place of the category's items.
    bool m_folder_open{};
    std::string m_folder_label{};
    std::vector<SettingsItem> m_folder_items{};
    std::function<std::vector<SettingsItem>()> m_folder_builder{};
    s64 m_folder_index{};
    // the category pane's cursor and scroll, restored when the folder closes.
    s64 m_saved_item_index{};
    float m_saved_item_yoff{};

    // label and description are each one line: the label must never wrap onto
    // the second line, because that is where the description lives. Only the
    // selected row scrolls, the rest are simply clipped.
    ScrollingText m_scroll_label{};
    ScrollingText m_scroll_description{};
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
