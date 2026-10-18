#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "system_info.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::sysinfo {

// Tools → Tools → System information: one list of group rows that open into
// parameter → value rows. A or a tap on a group opens and closes it; Y opens
// or closes every group.
struct Menu final : MenuBase {
    explicit Menu(std::vector<system_info::Group> groups);
    // the same page for another set of groups (Tools → Battery): its own title, every group open,
    // and one extra action on X.
    Menu(std::string title, std::vector<system_info::Group> groups, std::string x_label, std::function<void()> x_action,
         std::string plus_label = {}, std::function<void()> plus_action = {}, std::function<void()> tick = {},
         std::function<std::vector<system_info::Group>()> rebuild = {});
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
    // moves to the previous / next group and opens it.
    void JumpGroup(int step);
    auto GroupOf(s64 index) const -> s64;

private:
    std::vector<system_info::Group> m_groups;
    std::vector<bool> m_open;
    std::vector<Row> m_rows;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    std::function<void()> m_tick; // runs about once a second while this page is the active one
    TimeStamp m_tick_ts{};
    // gives the groups again, so the figures follow the console (charger in or out, the %): every 3 s, when the page
    // gets the focus back and after RequestRebuild()
    std::function<std::vector<system_info::Group>()> m_rebuild;
    TimeStamp m_rebuild_ts{};
    void SetGroups(std::vector<system_info::Group> groups);
    void TakeMetrics(std::vector<system_info::Group>& groups);
    std::vector<system_info::Row> m_metrics; // the strip above the list
};

// collects the data in a ProgressBox, then pushes the menu.
void Open();

// asks the open page that has a rebuild function to rebuild at its next update.
void RequestRebuild();

} // namespace sphaira::ui::menu::sysinfo
