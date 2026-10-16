// Host test for sphaira/include/jpeg_probe.hpp

#include "jpeg_probe.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using sphaira::image::NvjpgSupports;

namespace {

// SOI, APP0 (JFIF), SOFn with 3 components, SOS
std::vector<std::uint8_t> Jpeg(std::uint8_t sof, std::uint8_t ysamp, std::uint8_t csamp = 0x11, int comps = 3) {
    std::vector<std::uint8_t> v{0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    const std::uint8_t len = 8 + 3 * comps;
    std::vector<std::uint8_t> sofseg{0xFF, sof, 0x00, len, 8, 0x01, 0x00, 0x01, 0x00, static_cast<std::uint8_t>(comps)};
    for (int k = 0; k < comps; k++) {
        sofseg.push_back(static_cast<std::uint8_t>(k + 1));
        sofseg.push_back(k == 0 ? ysamp : csamp);
        sofseg.push_back(k == 0 ? 0 : 1);
    }
    v.insert(v.end(), sofseg.begin(), sofseg.end());
    const std::vector<std::uint8_t> sos{0xFF, 0xDA, 0x00, 0x08, 1, 1, 0, 0, 63, 0};
    v.insert(v.end(), sos.begin(), sos.end());
    return v;
}

} // namespace

int main() {
    assert(NvjpgSupports(Jpeg(0xC0, 0x22)));   // 4:2:0
    assert(NvjpgSupports(Jpeg(0xC0, 0x21)));   // 4:2:2
    assert(NvjpgSupports(Jpeg(0xC0, 0x11)));   // 4:4:4
    assert(NvjpgSupports(Jpeg(0xC1, 0x22)));   // extended sequential
    assert(NvjpgSupports(Jpeg(0xC0, 0x11, 0x11, 1))); // grayscale
    assert(!NvjpgSupports(Jpeg(0xC0, 0x12)));  // 4:4:0: the NFS Most Wanted port icon (luma 1x2)
    assert(!NvjpgSupports(Jpeg(0xC0, 0x41)));  // 4:1:1
    assert(!NvjpgSupports(Jpeg(0xC2, 0x22)));  // progressive
    assert(!NvjpgSupports(Jpeg(0xC0, 0x22, 0x21))); // chroma not 1x1
    assert(!NvjpgSupports({}));
    const std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G'};
    assert(!NvjpgSupports(png));
    std::puts("test_jpeg_probe: ok");
}
