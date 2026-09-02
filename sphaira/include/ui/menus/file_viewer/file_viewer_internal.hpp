#pragma once

#include "ui/menus/file_viewer.hpp"
#include "fs.hpp"
#include "ui/nvg_util.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::fileview {

auto GetCurrentTimeMs() -> u64;
auto SplitLines(std::string_view text) -> std::vector<std::string>;
auto PathFileName(const fs::FsPath& path) -> std::string;
auto PathDirectory(const fs::FsPath& path) -> fs::FsPath;
auto IsJpegExtension(std::string_view ext) -> bool;
auto IsImageExtension(std::string_view ext) -> bool;
auto ImageBounds(bool fullscreen) -> Vec4;

extern std::vector<std::string> s_line_clipboard;

} // namespace sphaira::ui::menu::fileview