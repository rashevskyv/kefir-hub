#pragma once

// Console Transfer → files and folders between two consoles (Wi-Fi).
//   Send: the chosen files are copied, each chosen folder is packed into a .zip, both into one package folder
//         on the microSD, and the console's web server offers that folder.
//   Receive: the other console's address is entered as for games; the user ticks what to take, picks where it goes
//         (back where it was, or another folder) and whether the folders are unpacked or kept as .zip.
// Pure parts (manifest, target paths) live in files_transfer_plan.hpp.

#include <string>
#include <vector>

namespace sphaira::ui::menu::console_files {

// absolute microSD paths of files and folders.
void Send(std::vector<std::string> paths);

// opens the file browser to pick one folder, then sends it.
void PickFolderAndSend();

void Receive();

} // namespace sphaira::ui::menu::console_files
