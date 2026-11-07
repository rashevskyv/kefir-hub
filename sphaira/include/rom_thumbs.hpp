#pragma once

#include "ui/menus/preview_pane.hpp"
#include <cstdint>
#include <stop_token>
#include <string>

namespace sphaira::rom {

// The url of the box art (else the screenshot) of the game in `file` on thumbnails.libretro.com, or empty when the
// system has no pictures or no name on the server matches. Reads the server's folder page once per system and keeps
// the names on the card. Blocking: call it from a worker thread.
auto ResolveThumbUrl(const std::string& folder, const std::string& file, std::stop_token token) -> std::string;

// what the pane of a ROM row shows: the cover (found by the loose name, see PickThumb) and the facts
auto GamePreview(const std::string& console, const std::string& file, std::uint64_t size) -> ui::menu::tools::PreviewInfo;

} // namespace sphaira::rom
