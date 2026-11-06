#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/menus/update_choice.hpp"
#include "ui/list.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::dbi {

// After the queue: one row per game whose question the queue did not wait for.
// A changes the row, X installs the choices, B skips them all.
struct ResolveMenu final : MenuBase {
    explicit ResolveMenu(std::shared_ptr<ResolveData> data);
    ~ResolveMenu();

    auto GetShortTitle() const -> const char* override { return "Resolve"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void Finish(bool skip_all);

    std::shared_ptr<ResolveData> m_data;
    std::unique_ptr<List> m_list;
    s64 m_index{};
};

// the choices of a row as the page shows them: [0] keeps the console, [k] installs file k - 1
std::vector<std::string> ResolveChoiceLabels(const ResolveRow& row);

} // namespace sphaira::ui::menu::dbi
