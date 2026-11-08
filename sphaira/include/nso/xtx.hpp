// XTX (NX texture "DFvN") encoder for RGBA8 2D covers. Spec: docs/dev/NSO-cover-xtx.md (container PROVEN
// on 80/80 official files; mips 1..5 use our own box filter, the original tools used another resampler).
// Output is the RAW xtx; the libnx side zlib-compresses it (level 9) into <name>.xtx.z.
#pragma once
#include "common.hpp"

#include <algorithm>

namespace nso {
namespace xtx_detail {

inline int EffBlockHeight(int h, int bh) {
    const int n = (h + 7) / 8;
    while (bh > 1 && bh / 2 >= n) bh /= 2;
    return bh;
}
inline size_t LevelSize(int w, int h, int bh) {
    const int wg = (w * 4 + 63) / 64;
    bh = EffBlockHeight(h, bh);
    const int rows = (h + 8 * bh - 1) / (8 * bh);
    return size_t(wg) * size_t(rows) * size_t(bh) * 512;
}
// Appends the block-linear (Tegra X1) swizzle of an RGBA8 image.
inline void Swizzle(const Bytes& px, int w, int h, int bh_in, Bytes& out) {
    const int bh = EffBlockHeight(h, bh_in);
    const int wg = (w * 4 + 63) / 64;
    const size_t base = out.size();
    out.resize(base + LevelSize(w, h, bh_in), 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int xb = x * 4;
            const size_t gob = size_t((xb % 64) / 32 * 256 + ((y % 8) / 2) * 64 + ((xb % 32) / 16) * 32 + (y % 2) * 16 + (xb % 16));
            const size_t off = (size_t(y / (8 * bh)) * size_t(wg) + size_t(xb / 64)) * size_t(512 * bh) +
                               size_t((y / 8) % bh) * 512 + gob;
            const u8* s = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
            for (int c = 0; c < 4; c++) out[base + off + size_t(c)] = s[c];
        }
    }
}
// 2x2 box, rounded: (sum + 2) / 4, edge clamped.
inline Bytes MipDown(const Bytes& a, int w, int h, int& nw, int& nh) {
    nw = std::max(1, w / 2); nh = std::max(1, h / 2);
    Bytes o(size_t(nw) * size_t(nh) * 4);
    for (int y = 0; y < nh; y++) {
        const int y0 = y * 2, y1 = std::min(y0 + 1, h - 1);
        for (int x = 0; x < nw; x++) {
            const int x0 = x * 2, x1 = std::min(x0 + 1, w - 1);
            for (int c = 0; c < 4; c++) {
                const u32 s = u32(a[(size_t(y0) * size_t(w) + size_t(x0)) * 4 + size_t(c)]) +
                              a[(size_t(y0) * size_t(w) + size_t(x1)) * 4 + size_t(c)] +
                              a[(size_t(y1) * size_t(w) + size_t(x0)) * 4 + size_t(c)] +
                              a[(size_t(y1) * size_t(w) + size_t(x1)) * 4 + size_t(c)];
                o[(size_t(y) * size_t(nw) + size_t(x)) * 4 + size_t(c)] = u8((s + 2) / 4);
            }
        }
    }
    return o;
}
inline void Block(Bytes& o, u64 data_size, u64 data_off, u32 type, u32 gidx) {
    for (char c : {'H', 'B', 'v', 'N'}) o.push_back(u8(c));
    PutLe32(o, 0x24); PutLe64(o, data_size); PutLe64(o, data_off); PutLe32(o, type); PutLe32(o, gidx); PutLe32(o, 0);
}
}  // namespace xtx_detail

// rgba: w*h*4 bytes (R,G,B,A). Returns the raw xtx (official variant: imageSize = total mip data size).
inline Bytes EncodeXtx(ByteSpan rgba, int w, int h) {
    using namespace xtx_detail;
    constexpr int kMips = 6, kBlockHeightLog2 = 4;
    if (w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4) return {};
    Bytes buf, cur(rgba.begin(), rgba.end());
    u32 offs[17] = {};
    int cw = w, ch = h;
    for (int i = 0; i < kMips; i++) {
        offs[i] = u32(buf.size());
        Swizzle(cur, cw, ch, 1 << kBlockHeightLog2, buf);
        if (i + 1 < kMips) { int nw, nh; cur = MipDown(cur, cw, ch, nw, nh); cw = nw; ch = nh; }
    }
    buf.resize(buf.size() + (0x400 - buf.size() % 0x400) % 0x400, 0);

    Bytes o = {'D', 'F', 'v', 'N'};
    PutLe32(o, 16); PutLe32(o, 1); PutLe32(o, 1);
    Block(o, 0x78, 0x24, 2, 0);
    PutLe64(o, buf.size());                          // imageSize
    PutLe32(o, 512); PutLe32(o, u32(w)); PutLe32(o, u32(h));
    PutLe32(o, 1); PutLe32(o, 1); PutLe32(o, 0x25);  // depth, target 2D, format RGBA8
    PutLe32(o, kMips); PutLe32(o, u32(buf.size()));  // mipCount, sliceSize
    for (u32 v : offs) PutLe32(o, v);
    PutLe32(o, kBlockHeightLog2); PutLe32(o, 0x10007); PutLe32(o, 0);
    const size_t data_at = (o.size() + 0x24 + 0x1FF) / 0x200 * 0x200;
    Block(o, buf.size(), data_at - o.size(), 3, 1);
    o.resize(data_at, 0);
    o.insert(o.end(), buf.begin(), buf.end());
    Block(o, 0x18, 0x24, 5, 2);
    for (u32 v : {0u, 0u, 1u, 1u, 0u, 0u}) PutLe32(o, v);
    return o;
}

}  // namespace nso
