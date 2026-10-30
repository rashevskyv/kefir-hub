#pragma once

#include "ui/menus/settings_menu.hpp"
#include <functional>
#include <vector>

namespace sphaira::rom {

// the ROMS part of Game Tools -> Emulators: the user's archive.org collections, one row each,
// and "Add a collection". `reopen` rebuilds the page after a collection is added or removed.
void AddRows(std::vector<ui::menu::settings::SettingsItem>& rows, std::function<void()> reopen);

} // namespace sphaira::rom
