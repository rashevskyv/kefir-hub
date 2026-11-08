#pragma once

// "Add to RetroArch" / "Add to Tico" for the ROM lists (the pure part is in rom_collections.hpp).

#include "ui/menus/filebrowser_entry.hpp"

#include <string>
#include <vector>

namespace sphaira::ui { class Sidebar; struct ProgressBox; }

namespace sphaira::rom {

enum class Frontend { RetroArch, Tico };

// Adds "Add to RetroArch" / "Add to Tico" to a sidebar for the files `files` (names inside /roms/<console>/).
// An entry is added only when the first file has an emulator of that frontend that is on the card.
void AddFrontendOptions(ui::Sidebar* sidebar, const std::string& console, const std::vector<std::string>& files);

// The same for the file manager: `dir` is the current card folder, `names` the selected entries.
// Does nothing unless `dir` is /roms/<known console> and at least one entry is a file.
void AddFrontendOptionsFor(ui::Sidebar* sidebar, const std::string& dir, const std::vector<ui::menu::filebrowser::FileEntry>& entries);

// RetroArch: puts the games into /retroarch/playlists/<system>.lpl, with the cover in /retroarch/thumbnails.
// Tico: puts the cover into /tico/assets/covers/<console>/ (Tico has no game list; it scans folders).
// The cover comes from the preview cache or is fetched. Shows a progress box, then a notification.
// The PNG cover of a game of /roms/<console>: from the preview cache, else fetched and kept there. Empty when there is none.
// Runs in a ProgressBox thread.
auto FetchCover(ui::ProgressBox* pbox, const std::string& console, const std::string& file) -> std::vector<u8>;

void AddToRetroArch(const std::string& console, const std::vector<std::string>& files);
void AddToTico(const std::string& console, const std::vector<std::string>& files);

} // namespace sphaira::rom
