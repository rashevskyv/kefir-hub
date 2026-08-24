#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"
#include <functional>
#include <memory>
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
    ~SystemToolsMenu() = default;

    auto GetShortTitle() const -> const char* override { return "Tools"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<ToolItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace sphaira::ui::menu::tools
