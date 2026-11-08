#pragma once

// "Add to RetroArch" / "Add to Tico" for the ROM lists (the pure part is in rom_collections.hpp).

#include <string>
#include <vector>

namespace sphaira::rom {

enum class Frontend { RetroArch, Tico };

// true when `file` (a name inside /roms/<console>/) has an emulator of this frontend that is on the card:
// the entry is shown only then
bool HasFrontend(Frontend frontend, const std::string& console, const std::string& file);

// RetroArch: puts the games into /retroarch/playlists/<system>.lpl, with the cover in /retroarch/thumbnails.
// Tico: puts the cover into /tico/assets/covers/<console>/ (Tico has no game list; it scans folders).
// The cover comes from the preview cache or is fetched. Shows a progress box, then a notification.
void AddToRetroArch(const std::string& console, const std::vector<std::string>& files);
void AddToTico(const std::string& console, const std::vector<std::string>& files);

} // namespace sphaira::rom
