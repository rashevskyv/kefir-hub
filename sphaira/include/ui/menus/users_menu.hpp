#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "account_link.hpp"

#include <memory>
#include <string>
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
    void ConfirmImport(bool all);
    void ConfirmOffline(bool all);
    void ConfirmUnlink(bool all);
    void ConfirmExport();
    void RunUnlink(bool all);
    void RunOffline(bool all);
    void RunImport(bool all, const std::string& dump_dir);
    void RunExport();
    auto SelectedUids(bool all) const -> std::vector<AccountUid>;

private:
    std::vector<account_link::User> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace sphaira::ui::menu::users
