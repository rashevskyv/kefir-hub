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

// one pager for both views: hex rows are fixed width, text rows are split on newlines.
template <typename ReadFunc>
auto ReadViewPage(bool hex, ReadFunc&& read_func, s64 file_size, s64 offset, s64 line, s64 rows, s64 logical_rows) -> text_helper::Page {
    return hex ? text_helper::ReadHexPage(read_func, file_size, offset, line, rows, logical_rows)
               : text_helper::ReadPage(read_func, file_size, offset, line, rows, logical_rows);
}

// draws every character in a cell of the same width, so hex columns line up with a proportional font.
void DrawMonoText(NVGcontext* vg, float x, float y, float font_size, const NVGcolor& colour, std::string_view text);

} // namespace sphaira::ui::menu::fileview