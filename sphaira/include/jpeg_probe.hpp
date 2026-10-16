#pragma once

// Whether the hardware JPEG decoder (nvjpg) can take a JPEG: baseline/extended Huffman, 8 bit, grayscale
// or YCbCr with the luma sampled 1x1 (4:4:4), 2x1 (4:2:2) or 2x2 (4:2:0) and chroma 1x1. Anything else
// (progressive, 4:4:0 like the NFS Most Wanted port icon, 4:1:1, CMYK) goes to stb_image, which reads it.
// Pure; host test tests/test_jpeg_probe.cpp.

#include <cstddef>
#include <cstdint>
#include <span>

namespace sphaira::image {

inline auto NvjpgSupports(std::span<const std::uint8_t> jpg) -> bool {
    if (jpg.size() < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) {
        return false;
    }
    std::size_t i = 2;
    while (i + 4 <= jpg.size()) {
        if (jpg[i] != 0xFF) {
            return false;
        }
        const auto m = jpg[i + 1];
        if (m == 0xFF) { // fill byte
            i++;
            continue;
        }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            i += 2;
            continue;
        }
        const std::size_t len = (jpg[i + 2] << 8) | jpg[i + 3];
        if (len < 2 || i + 2 + len > jpg.size()) {
            return false;
        }
        if (m == 0xC0 || m == 0xC1) {
            const auto* seg = jpg.data() + i + 4;
            if (len < 8 || seg[0] != 8) {
                return false;
            }
            const int n = seg[5];
            if (n == 1) {
                return true;
            }
            if (n != 3 || len < 8 + 3 * 3) {
                return false;
            }
            const int yh = seg[7] >> 4, yv = seg[7] & 15;
            const bool luma_ok = (yh == 1 && yv == 1) || (yh == 2 && yv == 1) || (yh == 2 && yv == 2);
            return luma_ok && seg[10] == 0x11 && seg[13] == 0x11;
        }
        if ((m >= 0xC2 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) || m == 0xDA) {
            return false; // progressive, lossless, arithmetic, or scan data before any frame header
        }
        i += 2 + len;
    }
    return false;
}

} // namespace sphaira::image
