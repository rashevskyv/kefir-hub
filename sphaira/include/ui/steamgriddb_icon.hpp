#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui::steamgriddb {

using IconCallback = std::function<void(std::vector<u8>)>;

// Converts supported image data into a 256x256 JPEG suitable for a Control
// NCA icon. Returns an empty vector if the image cannot be decoded.
auto NormalizeIcon(std::span<const u8> icon) -> std::vector<u8>;

// Searches SteamGridDB using the supplied title, then displays a grid of
// suitable HOME Menu icons. The API key is requested on first use and saved
// in the config file under [steamgriddb] api_key.
void ShowIconPicker(const std::string& title, const IconCallback& callback);

auto GetApiKey() -> std::string;
// called from the web server when the phone posts the key, see web.cpp.
void SetApiKey(const std::string& key);

// Brings up the qr handoff so the key can be pasted from a phone. Callable on
void RequestApiKey(std::function<void(std::string)> on_key = {});

auto IsApiKeyWebRequestActive() -> bool;
void SetApiKeyWebRequestActive(bool active);

} // namespace sphaira::ui::steamgriddb
