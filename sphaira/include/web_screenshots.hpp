#pragma once

#include <string>

namespace sphaira {

auto GetAlbumRoot() -> const std::string&;
auto BuildScreenshotGalleryPage(const std::string& query) -> std::string;

} // namespace sphaira
