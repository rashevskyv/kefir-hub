#pragma once

#include "ui/menus/game_menu.hpp"

#include <functional>
#include <vector>

namespace sphaira::ui::menu::game {

// Pushes the live Game Details screen (A on a title). DbiDetailsMenu itself
// stays in game_details.cpp so the class body can remain an exact move.
void OpenGameDetails(
    std::vector<Entry>* entries,
    s64 index,
    std::function<void(Entry, u32)> dump_callback,
    std::function<void(Entry, u32)> repack_callback,
    std::function<void(s64)> selection_callback);

} // namespace sphaira::ui::menu::game