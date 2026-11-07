#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/preview_pane.hpp"
#include "ui/scrolling_text.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::tools {

struct ToolItem {
    std::string label;
    std::string description;
    int icon_texture{};
    std::function<void()> action;
};

struct Menu final : MenuBase {
    Menu();
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Tools"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void LoadIcons();
    void SetIndex(s64 index);
    void OnSelect();
    void DisplayConnectionOptions();

private:
    std::vector<ToolItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    ScrollingText m_scroll_name;
};

struct GameToolsMenu final : MenuBase {
    GameToolsMenu();
    ~GameToolsMenu();

    auto GetShortTitle() const -> const char* override { return "Game Tools"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void LoadIcons();
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<ToolItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct SystemToolsMenu final : MenuBase {
    SystemToolsMenu();
    // the same page for any rows: label on the left, description on the right, captions between.
    SystemToolsMenu(const std::string& title, std::vector<settings::SettingsItem> items);
    ~SystemToolsMenu() = default;

    auto GetShortTitle() const -> const char* override { return "Tools"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

    // rows marked with X / Y (selectable rows only), or the row under the cursor when none is marked and it is selectable
    auto TargetRows() const -> std::vector<size_t>;
    // START: the context menu of the page; gets TargetRows()
    void SetOptions(std::function<void(const std::vector<size_t>&)> cb);
    // A with rows marked runs this on them instead of opening the row under the cursor
    void SetOnMarked(std::function<void(const std::vector<size_t>&)> cb) { m_on_marked = std::move(cb); }
    // the share of the row width the name column takes (the rest is the description)
    void SetNameShare(float share) { m_name_share = share; }
    // a pane on the right shows what `provider` says about the row under the cursor (nullopt: nothing); the list
    // narrows to make room, so call this after SetNameShare
    void SetPreview(std::function<std::optional<PreviewInfo>(size_t row)> provider);

private:
    void SetIndex(s64 index);
    void OnSelect();
    void BuildList(float width);
    void DrawRow(NVGcontext* vg, Theme* theme, const Vec4& v, const settings::SettingsItem& item, bool selected);

private:
    // SettingsItem rows so the list can carry captions (SettingsItemKind::Header).
    std::vector<settings::SettingsItem> m_items;
    s64 m_index{};
    float m_name_share{0.36f};
    std::function<void(const std::vector<size_t>&)> m_on_marked{};
    std::function<std::optional<PreviewInfo>(size_t)> m_preview_provider{};
    std::unique_ptr<PreviewPane> m_preview{};
    s64 m_preview_index{-1};
    std::unique_ptr<List> m_list;
};

} // namespace sphaira::ui::menu::tools
