#pragma once

#include <string>

// What the L button does on the main screen (Settings › General). The targets are in l_button_targets.hpp.
namespace sphaira::ui::menu::l_button {

// the name of the chosen menu or action; empty when none is chosen or it is not on this console
auto Label() -> std::string;
// opens or starts it; does nothing when Label() is empty
void Run();
// the page where the user picks it: the Tools tiles (the menus among them open a page of their rows) and Install & Share
void OpenPicker();

} // namespace sphaira::ui::menu::l_button
