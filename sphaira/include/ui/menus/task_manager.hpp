#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "meminfo.hpp"

#include <memory>
#include <vector>

namespace sphaira::ui::menu::tools {

struct TaskManagerMenu final : MenuBase {
    TaskManagerMenu();
    ~TaskManagerMenu() = default;

    auto GetShortTitle() const -> const char* override { return "RAM"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void Refresh();
    void SetIndex(s64 index);

private:
    meminfo::RamBreakdown m_ram{};
    std::vector<meminfo::ProcessRam> m_procs;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    u64 m_listed_bytes{};
};

} // namespace sphaira::ui::menu::tools
