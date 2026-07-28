#pragma once

#include "ui/sidebar.hpp"

namespace sphaira::ui::menu {

// Appends the "Install & Share" group (Web Server, MTP, PC Install over USB)
// to the given sidebar, preceded by a section header. Shared between the
// Tools and Homebrew menus so both can reach the web server / installer from
// their START menu.
void AddInstallShareOptions(Sidebar* sidebar);

// Appends a "Settings" shortcut that opens the Kefir Hub application settings,
// preceded by a section header.
void AddSettingsOption(Sidebar* sidebar);

} // namespace sphaira::ui::menu
