#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "account_link.hpp"

#include <memory>
#include <vector>

namespace sphaira::ui::menu::users {

struct Menu final : MenuBase {
    Menu();
    ~Menu() = default;

    auto GetShortTitle() const -> const char* override { return "Users"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void Refresh();
    void ShowContextMenu();
    void ConfirmLink(bool all);
    void ConfirmUnlink(bool all);
    void Run(bool link, bool all);

private:
    std::vector<account_link::User> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace sphaira::ui::menu::users
