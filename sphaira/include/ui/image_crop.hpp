#pragma once

#include "image.hpp"

#include <functional>
#include <string>

namespace sphaira::ui::image_crop {

using Callback = std::function<void(std::vector<u8> jpeg, std::string source)>;

void Show(ImageResult img, std::string source, Callback on_apply);

} // namespace sphaira::ui::image_crop
