#pragma once

// "Add to Nintendo Switch Online": puts a user ROM into an installed NSO emulator app through a LayeredFS
// overlay (/atmosphere/contents/<tid>/romfs/...). Never touches the app itself. The pure rules are in include/nso/.

#include <string>
#include <vector>

namespace sphaira::ui { class Sidebar; }

namespace sphaira::nso_ui {

// Adds "Add to Nintendo Switch Online" and, when the Hub has added games before, "Remove from Nintendo Switch Online".
// dir: the card folder of the files, names: file names inside dir, console: the /roms folder name ("" outside /roms;
// it picks the cover). Adds nothing unless an NSO app is installed that can take one of the files.
void AddOptions(ui::Sidebar* sidebar, const std::string& dir, const std::string& console, const std::vector<std::string>& names);

} // namespace sphaira::nso_ui
