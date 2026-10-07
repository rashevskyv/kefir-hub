#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "system_info.hpp"

#include <memory>
#include <vector>

namespace sphaira::ui::menu::sysinfo {

// Tools → Tools → System information: one list of group rows that open into
// parameter → value rows. A or a tap on a group opens and closes it; Y opens
// or closes every group.
struct Menu final : MenuBase {
    explicit Menu(std::vector<system_info::Group> groups);
    ~Menu() = default;

    auto GetShortTitle() const -> const char* override { return "System information"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    // one list row: a group caption (row < 0) or row `row` of group `group`.
    struct Row {
        s64 group;
        s64 row;
    };

    void Rebuild();
    void SetIndex(s64 index);
    void ToggleGroup(s64 group);
    void ToggleAll();
    auto GroupOf(s64 index) const -> s64;

private:
    std::vector<system_info::Group> m_groups;
    std::vector<bool> m_open;
    std::vector<Row> m_rows;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

// collects the data in a ProgressBox, then pushes the menu.
void Open();

} // namespace sphaira::ui::menu::sysinfo
