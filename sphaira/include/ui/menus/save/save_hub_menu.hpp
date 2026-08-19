#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {

struct SaveHubItem {
    std::string label;
    std::string description;
    int icon_texture{};
    std::function<void()> action;
};

struct SaveHubMenu final : MenuBase {
    SaveHubMenu(u32 flags = MenuFlag_None);
    ~SaveHubMenu();

    auto GetShortTitle() const -> const char* override { return "Saves"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void LoadIcons();
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<SaveHubItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    ScrollingText m_scroll_name;
};

} // namespace sphaira::ui::menu::save
