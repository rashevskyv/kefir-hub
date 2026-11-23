#pragma once

#include "ui/menus/game_menu.hpp"
#include "ui/sidebar.hpp"

#include <functional>
#include <vector>

namespace sphaira::ui::menu::game {

// Edit name and icon: one screen for the name, publisher, version and icon of an installed game
// (written into its control data, needs sigpatches), with rows for language and restrictions.
// The entry with this id in `entries` is reloaded after a change.
void ShowEditGame(std::vector<Entry>* entries, u64 app_id);
// the two entries above for a Game Actions / Game Options sidebar (restore only when there is one).
void AddEditGameEntries(Sidebar* options, std::vector<Entry>* entries, u64 app_id);
// puts back the name, publisher, version and icon from before the first change (asks first).
void ShowRestoreOriginal(std::vector<Entry>* entries, u64 app_id);

// shared with Game details. `langs` = NACP indexes of the languages the game has.
void ShowGameLanguage(u64 app_id, std::vector<int> langs, std::function<void()> on_done = {});
void ShowGameRestrictions(u64 app_id);

} // namespace sphaira::ui::menu::game
