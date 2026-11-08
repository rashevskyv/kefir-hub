#pragma once

// Internal parts of "Add a ROM to Nintendo Switch Online" (see nso_ui.hpp). The pure rules are in include/nso/.

#include "nso/ui_plan.hpp"
#include "image.hpp"
#include "ui/progress_box.hpp"

#include <span>
#include <string>
#include <vector>

namespace sphaira::nso_ui {

// which NSO apps (nso::Info(s).base_tid) are installed.
auto DetectInstalled() -> nso::Installed;

// Everything the Hub knows about the overlay of one NSO app.
struct SystemState {
    bool loaded{};
    std::string db_text;       // overlay db, else the official one from the app's RomFS
    std::string marker_text;   // titles/.kefir_added
    bool overlay_db{};         // db_text is a file of the overlay (not only read from the RomFS)
    bool hub_created_db{};     // the Hub made the overlay db itself (flag file)
    std::string error;         // set when loading failed (shown to the user)
};

// Reads the overlay, and the official db from the installed app when the overlay has none.
// Never writes. On failure state.error is set and false is returned.
bool LoadState(nso::System s, SystemState& state);

// Only the overlay files (db, marker, flag): for the remove list. False when there is no marker.
bool LoadOverlayOnly(nso::System s, SystemState& state);

// A .sfromsig of the installed SNES app (256 bytes); 256 zero bytes when none can be read.
auto ReadDonorSfromsig() -> std::vector<u8>;

// zlib stream, level 9. Empty on failure.
auto Zlib9(std::span<const u8> in) -> std::vector<u8>;

// The file bytes of a cover / details screen. `source` = the decoded cover; empty data = a plain dark placeholder.
auto RenderCover(const nso::CoverSpec& spec, const ImageResult& source) -> std::vector<u8>;

// Writes the overlay files of one add (ROM, covers, marker, flag, db last). `error` is set on failure.
Result WriteAdd(const nso::AddResult& add, std::span<const u8> rom, const ImageResult& cover, SystemState& state, std::string& error);

// Removes one game that the Hub added. `error` is set on failure.
Result RemoveAdded(nso::System s, const std::string& code, std::string& error);

// The path of a file inside the overlay romfs of a system.
auto OverlayPath(nso::System s, const std::string& rel) -> fs::FsPath;

} // namespace sphaira::nso_ui
