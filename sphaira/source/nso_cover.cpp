// Cover and details images for the Nintendo Switch Online app, and the zlib step of the file writer.

#include "nso_internal.hpp"
#include "log.hpp"
#include "nso/xtx.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace sphaira::nso_ui {
namespace {

constexpr u8 DARK[4] = {0x14, 0x14, 0x1C, 0xFF};

void Fill(std::vector<u8>& px) {
    for (size_t i = 0; i + 4 <= px.size(); i += 4) std::memcpy(&px[i], DARK, 4);
}

} // namespace

auto Zlib9(std::span<const u8> in) -> std::vector<u8> {
    uLongf n = compressBound(static_cast<uLong>(in.size()));
    std::vector<u8> out(n);
    if (compress2(out.data(), &n, in.data(), static_cast<uLong>(in.size()), 9) != Z_OK) return {};
    out.resize(n);
    return out;
}

auto RenderCover(const nso::CoverSpec& spec, const ImageResult& source) -> std::vector<u8> {
    int w = spec.w, h = spec.h;
    std::vector<u8> px(size_t(w) * size_t(h) * 4);
    Fill(px);

    if (!source.data.empty() && source.w > 0 && source.h > 0) {
        const auto fit = nso::FitInside(source.w, source.h, spec.w, spec.h);
        const auto scaled = ImageResize(source.data, source.w, source.h, fit.w, fit.h);
        if (!scaled.data.empty()) {
            if (!spec.is_details) {
                // the cover keeps the aspect of the picture: no bars
                w = fit.w;
                h = fit.h;
                px = scaled.data;
            } else {
                for (int y = 0; y < fit.h; y++) {
                    u8* dst = &px[(size_t(fit.y + y) * size_t(w) + size_t(fit.x)) * 4];
                    std::memcpy(dst, &scaled.data[size_t(y) * size_t(fit.w) * 4], size_t(fit.w) * 4);
                    for (int x = 0; x < fit.w; x++) dst[x * 4 + 3] = 0xFF;
                }
            }
        }
    }

    if (spec.kind == nso::CoverKind::Png) {
        return ImageConvertToPng(px, w, h).data;
    }
    const auto xtx = nso::EncodeXtx(px, w, h);
    return xtx.empty() ? std::vector<u8>{} : Zlib9(xtx);
}

} // namespace sphaira::nso_ui
