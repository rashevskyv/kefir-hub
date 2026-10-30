#pragma once

namespace sphaira::bios {

// Game Tools -> Emulators: the page with one "Download BIOS for <emulator>" row per
// emulator found on the SD card. Files come from the retrobios repository one by one
// (romfs:/bios/bios.json lists them); nothing is bundled in the Hub.
void OpenEmulatorsPage();

} // namespace sphaira::bios
