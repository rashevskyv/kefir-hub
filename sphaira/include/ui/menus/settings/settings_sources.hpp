#pragma once

#include "ui/menus/settings_menu.hpp"

#include <vector>

namespace sphaira::ui::menu::settings {

auto BuildSourcesCategoryItems(Menu* menu) -> std::vector<SettingsItem>;

} // namespace sphaira::ui::menu::settings
