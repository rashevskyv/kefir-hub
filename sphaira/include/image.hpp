#pragma once

#include <vector>
#include <span>
#include <switch.h>
#include "fs.hpp"

namespace sphaira {

struct ImageResult {
    std::vector<u8> data;
    int w, h;
};

enum ImageFlag {
    ImageFlag_None = 0,
    // set this if the image is a jpeg, will use oss-nvjpg to load.
    ImageFlag_JPEG = 1 << 0,
};

auto ImageLoadFromMemory(std::span<const u8> data, u32 flags = ImageFlag_None) -> ImageResult;
auto ImageLoadFromFile(const fs::FsPath& file, u32 flags = ImageFlag_None) -> ImageResult;
auto ImageResize(std::span<const u8> data, int inx, int iny, int outx, int outy) -> ImageResult;
auto ImageCrop(std::span<const u8> data, int inx, int iny, int crop_x, int crop_y, int crop_w, int crop_h) -> ImageResult;
auto ImageConvertToJpg(std::span<const u8> data, int x, int y) -> ImageResult;

// Loads and decodes an icon with strict bounds: positive dimensions, max 1024
// per axis, max 1024*1024 pixels. Valid non-256x256 icons are resized to 256x256.
// Returns 256x256 RGBA ImageResult, or empty ImageResult on failure.
auto ImageLoadIcon(std::span<const u8> data) -> ImageResult;

// Normalizes an icon into 256x256 JPEG format.
// Returns empty vector on failure.
auto ImageNormalizeIcon(std::span<const u8> data) -> std::vector<u8>;

// Returns a normalized 256x256 JPEG default icon, falling back to raw default bytes.
auto ImageGetDefaultIcon() -> std::vector<u8>;

} // namespace sphaira
