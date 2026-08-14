#include "image.hpp"

// disable warnings for stb
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Warray-bounds="
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include <stb_image_resize2.h>
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop

#include "app.hpp"
#include "log.hpp"
#ifdef USE_NVJPG
#include <nvjpg.hpp>
#endif
#include <cstring>
#include <limits>
#include <algorithm>

namespace sphaira {
namespace {

constexpr int BPP = 4;
constexpr int ICON_MAX_DIM = 1024;
constexpr uint64_t ICON_MAX_PIXELS = 1024ULL * 1024ULL;
constexpr int ICON_TARGET_DIM = 256;

constexpr auto CheckedMul(size_t a, size_t b, size_t& out) -> bool {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > std::numeric_limits<size_t>::max() / b) {
        return false;
    }
    out = a * b;
    return true;
}

auto GetImageByteSize(int w, int h, size_t& out_bytes) -> bool {
    if (w <= 0 || h <= 0) {
        return false;
    }
    const auto uw = static_cast<size_t>(w);
    const auto uh = static_cast<size_t>(h);
    size_t pixels = 0;
    if (!CheckedMul(uw, uh, pixels)) {
        return false;
    }
    return CheckedMul(pixels, static_cast<size_t>(BPP), out_bytes);
}

constexpr auto IsValidIconDimensions(int w, int h) -> bool {
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (w > ICON_MAX_DIM || h > ICON_MAX_DIM) {
        return false;
    }
    const auto uw = static_cast<uint64_t>(w);
    const auto uh = static_cast<uint64_t>(h);
    if (uw * uh > ICON_MAX_PIXELS) {
        return false;
    }
    return true;
}

auto ImageLoadInternal(stbi_uc* image_data, int x, int y) -> ImageResult {
    if (!image_data) {
        log_write("failed image load\n");
        return {};
    }

    size_t required_bytes = 0;
    if (!GetImageByteSize(x, y, required_bytes)) {
        log_write("failed image load: invalid dimensions or overflow %dx%d\n", x, y);
        stbi_image_free(image_data);
        return {};
    }

    ImageResult result{};
    result.w = x;
    result.h = y;
    result.data.resize(required_bytes);
    std::memcpy(result.data.data(), image_data, required_bytes);
    stbi_image_free(image_data);
    return result;
}

#ifdef USE_NVJPG
auto ImageLoadInternalNvjpg(nj::Image&& image, bool is_icon) -> ImageResult {
    if (!image.is_valid() || image.parse()) {
        log_write("[NVJPG] failed to parse image\n");
        return {};
    }

    if (image.width == 0 || image.height == 0) {
        log_write("[NVJPG] invalid 0 dimensions %zux%zu\n", static_cast<size_t>(image.width), static_cast<size_t>(image.height));
        return {};
    }

    if (static_cast<uint64_t>(image.width) > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        static_cast<uint64_t>(image.height) > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        log_write("[NVJPG] dimensions exceed int range %zux%zu\n", static_cast<size_t>(image.width), static_cast<size_t>(image.height));
        return {};
    }

    const int img_w = static_cast<int>(image.width);
    const int img_h = static_cast<int>(image.height);

    if (is_icon && !IsValidIconDimensions(img_w, img_h)) {
        log_write("[NVJPG] icon exceeds maximum dimensions %dx%d\n", img_w, img_h);
        return {};
    }

    size_t parsed_bytes = 0;
    if (!GetImageByteSize(img_w, img_h, parsed_bytes)) {
        log_write("[NVJPG] parsed byte size overflow %dx%d\n", img_w, img_h);
        return {};
    }

    nj::Surface surf{image.width, image.height};
    if (surf.allocate()) {
        log_write("[NVJPG] failed to allocate surf\n");
        return {};
    }

    if (R_FAILED(App::GetApp()->m_decoder.render(image, surf, 255))) {
        log_write("[NVJPG] failed to render\n");
        return {};
    }

    if (R_FAILED(App::GetApp()->m_decoder.wait(surf))) {
        log_write("[NVJPG] failed to wait\n");
        return {};
    }

    const auto bpp = surf.get_bpp();
    if (bpp != BPP || surf.width != image.width || surf.height != image.height || surf.width == 0 || surf.height == 0) {
        log_write("[NVJPG] unexpected surface properties bpp=%u w=%zu h=%zu\n", bpp, static_cast<size_t>(surf.width), static_cast<size_t>(surf.height));
        return {};
    }

    size_t dst_pitch = 0;
    if (!CheckedMul(surf.width, static_cast<size_t>(bpp), dst_pitch)) {
        log_write("[NVJPG] dst pitch overflow\n");
        return {};
    }

    size_t dst_total_bytes = 0;
    if (!CheckedMul(dst_pitch, surf.height, dst_total_bytes)) {
        log_write("[NVJPG] dst total bytes overflow\n");
        return {};
    }

    if (surf.pitch < dst_pitch) {
        log_write("[NVJPG] invalid surface pitch %zu < %zu\n", static_cast<size_t>(surf.pitch), dst_pitch);
        return {};
    }

    size_t surf_expected_min_bytes = 0;
    if (!CheckedMul(surf.pitch, surf.height, surf_expected_min_bytes) || surf.size() < surf_expected_min_bytes) {
        log_write("[NVJPG] surface size too small %zu\n", surf.size());
        return {};
    }

    ImageResult result{};
    result.w = img_w;
    result.h = img_h;
    result.data.resize(dst_total_bytes);

    if (dst_pitch == surf.pitch) [[likely]] {
        std::memcpy(result.data.data(), surf.data(), dst_total_bytes);
    } else {
        for (size_t i = 0; i < surf.height; i++) {
            size_t dst_row_off = 0;
            size_t src_row_off = 0;
            if (!CheckedMul(i, dst_pitch, dst_row_off) || !CheckedMul(i, surf.pitch, src_row_off)) {
                return {};
            }
            std::memcpy(result.data.data() + dst_row_off, surf.data() + src_row_off, dst_pitch);
        }
    }

    return result;
}
#endif

struct JpgWriteContext {
    std::vector<u8> out;
    bool overflow{false};
};

} // namespace

auto ImageLoadFromMemory(std::span<const u8> data, u32 flags) -> ImageResult {
    if (data.empty() || data.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

#ifdef USE_NVJPG
    if (flags & ImageFlag_JPEG) {
        auto shared_vec = std::make_shared<std::vector<u8>>(data.size());
        std::memcpy(shared_vec->data(), data.data(), shared_vec->size());
        auto result = ImageLoadInternalNvjpg(nj::Image{shared_vec}, false);
        return result.data.empty() ? ImageLoadFromMemory(data, 0) : result;
    }
    else
#endif
    {
        int x = 0, y = 0, channels = 0;
        stbi_uc* img_data = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &x, &y, &channels, BPP);
        return ImageLoadInternal(img_data, x, y);
    }
}

auto ImageLoadFromFile(const fs::FsPath& file, u32 flags) -> ImageResult {
#ifdef USE_NVJPG
    if (flags & ImageFlag_JPEG) {
        auto result = ImageLoadInternalNvjpg(nj::Image{file}, false);
        return result.data.empty() ? ImageLoadFromFile(file, 0) : result;
    }
    else
#endif
    {
        int x = 0, y = 0, channels = 0;
        stbi_uc* img_data = stbi_load(file, &x, &y, &channels, BPP);
        return ImageLoadInternal(img_data, x, y);
    }
}

auto ImageLoadIcon(std::span<const u8> data) -> ImageResult {
    if (data.empty() || data.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

#ifdef USE_NVJPG
    const bool is_jpeg = data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
    if (is_jpeg) {
        auto shared_vec = std::make_shared<std::vector<u8>>(data.size());
        std::memcpy(shared_vec->data(), data.data(), shared_vec->size());
        auto result = ImageLoadInternalNvjpg(nj::Image{shared_vec}, true);
        if (!result.data.empty()) {
            if (result.w != ICON_TARGET_DIM || result.h != ICON_TARGET_DIM) {
                result = ImageResize(result.data, result.w, result.h, ICON_TARGET_DIM, ICON_TARGET_DIM);
            }
            if (!result.data.empty() && result.w == ICON_TARGET_DIM && result.h == ICON_TARGET_DIM) {
                return result;
            }
        }
    }
#endif

    int info_w = 0, info_h = 0, info_comp = 0;
    if (!stbi_info_from_memory(data.data(), static_cast<int>(data.size()), &info_w, &info_h, &info_comp)) {
        log_write("ImageLoadIcon: failed to probe image metadata\n");
        return {};
    }

    if (!IsValidIconDimensions(info_w, info_h)) {
        log_write("ImageLoadIcon: metadata dimensions out of bounds (%dx%d)\n", info_w, info_h);
        return {};
    }

    int x = 0, y = 0, channels = 0;
    stbi_uc* img_data = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &x, &y, &channels, BPP);
    if (!img_data) {
        log_write("ImageLoadIcon: failed to decode image\n");
        return {};
    }

    if (!IsValidIconDimensions(x, y)) {
        log_write("ImageLoadIcon: decoded dimensions out of bounds (%dx%d)\n", x, y);
        stbi_image_free(img_data);
        return {};
    }

    auto result = ImageLoadInternal(img_data, x, y);
    if (result.data.empty()) {
        return {};
    }

    if (result.w != ICON_TARGET_DIM || result.h != ICON_TARGET_DIM) {
        result = ImageResize(result.data, result.w, result.h, ICON_TARGET_DIM, ICON_TARGET_DIM);
    }

    if (result.data.empty() || result.w != ICON_TARGET_DIM || result.h != ICON_TARGET_DIM) {
        return {};
    }

    return result;
}

auto ImageNormalizeIcon(std::span<const u8> data) -> std::vector<u8> {
    if (data.empty()) {
        return {};
    }

    const auto decoded = ImageLoadIcon(data);
    if (decoded.data.empty() || decoded.w != ICON_TARGET_DIM || decoded.h != ICON_TARGET_DIM) {
        return {};
    }

    const auto jpg = ImageConvertToJpg(decoded.data, decoded.w, decoded.h);
    return jpg.data;
}

auto ImageGetDefaultIcon() -> std::vector<u8> {
    const auto raw = App::GetDefaultImageData();
    if (raw.empty()) {
        return {};
    }

    auto normalized = ImageNormalizeIcon(raw);
    if (!normalized.empty()) {
        return normalized;
    }

    return std::vector<u8>(raw.begin(), raw.end());
}

auto ImageResize(std::span<const u8> data, int inx, int iny, int outx, int outy) -> ImageResult {
    log_write("doing resize inx: %d iny: %d outx: %d outy: %d\n", inx, iny, outx, outy);

    if (inx <= 0 || iny <= 0 || outx <= 0 || outy <= 0) {
        log_write("ImageResize: invalid non-positive dimensions %dx%d -> %dx%d\n", inx, iny, outx, outy);
        return {};
    }

    if (inx > std::numeric_limits<int>::max() / BPP || outx > std::numeric_limits<int>::max() / BPP) {
        log_write("ImageResize: width exceeds pitch limit\n");
        return {};
    }

    size_t in_bytes = 0;
    size_t out_bytes = 0;
    if (!GetImageByteSize(inx, iny, in_bytes) || !GetImageByteSize(outx, outy, out_bytes)) {
        log_write("ImageResize: byte size overflow\n");
        return {};
    }

    if (data.size() < in_bytes) {
        log_write("ImageResize: input data size %zu < expected %zu\n", data.size(), in_bytes);
        return {};
    }

    const int in_pitch = inx * BPP;
    const int out_pitch = outx * BPP;

    std::vector<u8> resized_data(out_bytes);
    if (stbir_resize_uint8_linear(data.data(), inx, iny, in_pitch, resized_data.data(), outx, outy, out_pitch, (stbir_pixel_layout)BPP)) {
        log_write("did resize\n");
        return { std::move(resized_data), outx, outy };
    }

    log_write("failed resize\n");
    return {};
}

auto ImageConvertToJpg(std::span<const u8> data, int x, int y) -> ImageResult {
    if (x <= 0 || y <= 0) {
        log_write("ImageConvertToJpg: invalid dimensions %dx%d\n", x, y);
        return {};
    }

    size_t expected_bytes = 0;
    if (!GetImageByteSize(x, y, expected_bytes)) {
        log_write("ImageConvertToJpg: dimension overflow %dx%d\n", x, y);
        return {};
    }

    if (data.size() < expected_bytes) {
        log_write("ImageConvertToJpg: input size %zu < expected %zu\n", data.size(), expected_bytes);
        return {};
    }

    log_write("doing jpeg convert\n");

    JpgWriteContext ctx;
    ctx.out.reserve(std::min<size_t>(expected_bytes, 1024 * 1024));

    const auto cb = [](void *context, void *chunk_data, int size) -> void {
        if (!context || !chunk_data || size <= 0) {
            return;
        }
        auto* write_ctx = static_cast<JpgWriteContext*>(context);
        if (write_ctx->overflow) {
            return;
        }
        const auto chunk_size = static_cast<size_t>(size);
        const auto current_size = write_ctx->out.size();
        if (chunk_size > std::numeric_limits<size_t>::max() - current_size) {
            write_ctx->overflow = true;
            return;
        }
        write_ctx->out.resize(current_size + chunk_size);
        std::memcpy(write_ctx->out.data() + current_size, chunk_data, chunk_size);
    };

    if (stbi_write_jpg_to_func(cb, &ctx, x, y, BPP, data.data(), 93) && !ctx.overflow && !ctx.out.empty()) {
        log_write("did jpg convert\n");
        return { std::move(ctx.out), x, y };
    }

    log_write("failed jpg convert\n");
    return {};
}

auto ImageCrop(std::span<const u8> data, int inx, int iny, int crop_x, int crop_y, int crop_w, int crop_h) -> ImageResult {
    log_write("doing crop inx: %d iny: %d to %d %d %d %d\n", inx, iny, crop_x, crop_y, crop_w, crop_h);
    if (inx <= 0 || iny <= 0 || crop_w <= 0 || crop_h <= 0) {
        log_write("invalid crop dimensions\n");
        return {};
    }
    if (crop_x < 0 || crop_y < 0 || crop_x >= inx || crop_y >= iny) {
        log_write("invalid crop coordinates\n");
        return {};
    }
    if (crop_w > inx - crop_x || crop_h > iny - crop_y) {
        log_write("crop rectangle exceeds image bounds\n");
        return {};
    }

    if (inx > std::numeric_limits<int>::max() / BPP || crop_w > std::numeric_limits<int>::max() / BPP) {
        log_write("crop pitch overflow\n");
        return {};
    }

    size_t in_bytes = 0;
    size_t crop_bytes = 0;
    if (!GetImageByteSize(inx, iny, in_bytes) || !GetImageByteSize(crop_w, crop_h, crop_bytes)) {
        log_write("crop dimension overflow\n");
        return {};
    }

    if (data.size() < in_bytes) {
        log_write("input size too small for crop\n");
        return {};
    }

    std::vector<u8> cropped_data(crop_bytes);
    const size_t row_bytes = static_cast<size_t>(crop_w) * BPP;
    for (int row = 0; row < crop_h; ++row) {
        const size_t src_offset = (static_cast<size_t>(crop_y + row) * static_cast<size_t>(inx) + static_cast<size_t>(crop_x)) * BPP;
        const size_t dst_offset = static_cast<size_t>(row) * row_bytes;
        std::memcpy(cropped_data.data() + dst_offset, data.data() + src_offset, row_bytes);
    }
    return { std::move(cropped_data), crop_w, crop_h };
}

} // namespace sphaira
