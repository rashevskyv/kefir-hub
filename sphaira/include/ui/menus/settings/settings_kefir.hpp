#pragma once

#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"

namespace sphaira::ui::menu::settings {

auto MakePackageAction(PackageAction action) -> SettingsItem;

} // namespace sphaira::ui::menu::settings
