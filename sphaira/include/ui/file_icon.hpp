#pragma once

#include "fs.hpp"
#include "ui/nvg_util.hpp"

#include <string>
#include <utility>
#include <vector>

namespace sphaira::ui::file_icon {

constexpr int kMaxTiles = 24;

struct Cell {
    fs::FsPath path{};
    std::string ext{};
    int image{};
    bool ellipsis{};
};

struct Mosaic {
    std::vector<Cell> cells;
    bool listed{};
};

auto IsImageExt(std::string_view ext) -> bool;
auto TypeIcon(std::string_view ext) -> ThemeEntryID;
auto ChooseGrid(int n) -> std::pair<int, int>;

void DrawContain(NVGcontext* vg, const Vec4& dest, int image, float rounded = 0.f);
void DrawTypeIcon(NVGcontext* vg, const Vec4& dest, std::string_view ext);
void DrawFileThumb(NVGcontext* vg, Theme* theme, const Vec4& dest, int image, std::string_view ext);
void DrawFolderShape(NVGcontext* vg, Theme* theme, const Vec4& dest);
void DrawMosaic(NVGcontext* vg, Theme* theme, const Vec4& dest, Mosaic& mosaic);

void FreeMosaic(Mosaic& mosaic);
auto ListFolderPreview(fs::Fs* fs, const fs::FsPath& dir) -> Mosaic;
auto TryLoadCell(Cell& cell) -> bool;

} // namespace sphaira::ui::file_icon
