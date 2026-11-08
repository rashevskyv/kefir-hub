// NSO add-a-ROM core: shared tiny helpers. Pure C++20, no libnx. Spec: docs/dev/NSO-*.md
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nso {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using Bytes = std::vector<u8>;
using ByteSpan = std::span<const u8>;

inline u32 Crc32(ByteSpan d, u32 crc = 0) {
    static const auto table = [] {
        std::array<u32, 256> t{};
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (u8 b : d) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline u32 Be32(ByteSpan d, size_t o) {
    return (u32(d[o]) << 24) | (u32(d[o + 1]) << 16) | (u32(d[o + 2]) << 8) | u32(d[o + 3]);
}
inline u32 Le32(ByteSpan d, size_t o) {
    return (u32(d[o + 3]) << 24) | (u32(d[o + 2]) << 16) | (u32(d[o + 1]) << 8) | u32(d[o]);
}
inline void PutLe32(Bytes& o, u32 v) { for (int i = 0; i < 4; i++) o.push_back(u8(v >> (8 * i))); }
inline void PutLe64(Bytes& o, u64 v) { for (int i = 0; i < 8; i++) o.push_back(u8(v >> (8 * i))); }
inline void PutLe16(Bytes& o, u32 v) { o.push_back(u8(v)); o.push_back(u8(v >> 8)); }

inline bool Has(ByteSpan d, size_t off, const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    if (off + n > d.size()) return false;
    for (size_t i = 0; i < n; i++) if (d[off + i] != u8(s[i])) return false;
    return true;
}

inline std::string AsciiLower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

}  // namespace nso
