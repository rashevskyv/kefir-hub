// ROM bytes -> bytes the NSO app reads. Pure functions. Spec: docs/dev/NSO-nes-snes.md, -genesis-gb-gba.md, -n64-vb.md
#pragma once
#include <algorithm>

#include "common.hpp"
#include "n64.hpp"
#include "systems.hpp"

namespace nso {

// SNES cfg blob: 425 of 552 mod games use it (PROVEN usage, meaning UNKNOWN). Chip games need their own: UNPROVEN.
inline const Bytes kSnesCfgDefault = {0x47, 0x02, 0x00, 0x00, 0x00, 0x10, 0x74, 0x06, 0x63, 0x01, 0x6a, 0x38, 0x70, 0x02};

inline ByteSpan SnesStripCopier(ByteSpan r) { return r.size() % 1024 == 512 ? r.subspan(512) : r; }

// .sfrom = headerless ROM + cfg + u32le(len(cfg)) + "Can1"   (PROVEN, 30 mod files rebuilt)
inline Bytes SnesWrap(ByteSpan rom, ByteSpan cfg) {
    ByteSpan r = SnesStripCopier(rom);
    Bytes o(r.begin(), r.end());
    o.insert(o.end(), cfg.begin(), cfg.end());
    PutLe32(o, u32(cfg.size()));
    for (char c : {'C', 'a', 'n', '1'}) o.push_back(u8(c));
    return o;
}

// iNES kept as is; header bytes 8..15 zeroed (PROVEN: 97 files). Flag 6 is not touched.
inline Bytes NesWrap(ByteSpan rom) {
    Bytes o(rom.begin(), rom.end());
    for (size_t i = 8; i < 16; i++) o[i] = 0;
    return o;
}

// Super Magic Drive dump -> linear ROM (LIKELY: implemented from the format, no real SMD dump tested).
inline Bytes SmdToBin(ByteSpan b) {
    const size_t body = b.size() - kSmdHeader;
    Bytes o(body - body % 16384);
    for (size_t off = 0; off < o.size(); off += 16384) {
        const u8* blk = b.data() + kSmdHeader + off;
        for (size_t i = 0; i < 8192; i++) { o[off + 2 * i + 1] = blk[i]; o[off + 2 * i] = blk[8192 + i]; }
    }
    return o;
}

inline bool GbIsCgb(ByteSpan r) { return r.size() > 0x143 && (r[0x143] & 0x80); }
inline u32 GbHeaderWord(ByteSpan r) { return (u32(r[0x14E]) << 8) | r[0x14F]; }  // checksum word STORED in the header
inline std::string GbaGameCode(ByteSpan r) {  // 4 chars at 0xAC, forced to [0-9A-Z]
    std::string c;
    for (size_t i = 0; i < 4; i++) {
        const char ch = char(r[0xAC + i]);
        c += ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')) ? ch : '0';
    }
    return c;
}

struct Normalized {
    bool ok = false;
    std::string error;
    bool passthrough = false;  // true: the file is the source ROM unchanged (bytes is empty)
    Bytes bytes;               // otherwise: the content to write
    std::string ext;           // extension of the ROM file we write ("sfrom" "nes" "gen" "gb" "gbc" "gba" "z64" "vb")
    bool n64_pal = false;
    u32 n64_crc1 = 0, n64_crc2 = 0;
};

inline Normalized Fail(std::string e) { Normalized n; n.error = std::move(e); return n; }

inline ByteSpan View(const Normalized& n, ByteSpan src) { return n.passthrough ? src : ByteSpan(n.bytes); }

inline Normalized Normalize(System s, ByteSpan rom) {
    Normalized n;
    n.ok = true;
    switch (s) {
        case System::Nes:
            if (rom.size() < 16 || !Has(rom, 0, "NES\x1a")) return Fail("not an iNES file");
            n.bytes = NesWrap(rom); n.ext = "nes";
            break;
        case System::Snes: {
            const ByteSpan r = SnesStripCopier(rom);
            if (r.size() < 0x8000) return Fail("SNES ROM is too small");
            n.bytes = SnesWrap(rom, kSnesCfgDefault); n.ext = "sfrom";
            break;
        }
        case System::N64:
        case System::N64Mature:
            if (!N64Magic(rom) || rom.size() < 0x1000 || rom.size() % 4) return Fail("not an N64 ROM");
            n.ext = "z64";
            if (rom[0] == 0x80) n.passthrough = true; else n.bytes = N64ToZ64(rom);
            {
                const N64Header h = ReadN64Header(N64ToZ64(rom.first(0x40)));
                n.n64_pal = h.pal; n.n64_crc1 = h.crc1; n.n64_crc2 = h.crc2;
            }
            break;
        case System::Genesis:
            if (rom.size() < 0x200) return Fail("Genesis ROM is too small");
            n.ext = "gen";
            if (LooksSmd(rom.first(std::min<size_t>(rom.size(), 16)), rom.size())) n.bytes = SmdToBin(rom);
            else n.passthrough = true;
            break;
        case System::Gb:
            if (rom.size() < 0x150) return Fail("Game Boy ROM is too small");
            n.passthrough = true; n.ext = GbIsCgb(rom) ? "gbc" : "gb";
            break;
        case System::Gba:
            if (rom.size() >= 4 && rom[0] == 0x04 && rom[1] == 0x22 && rom[2] == 0x4D && rom[3] == 0x18)
                return Fail("LZ4-compressed GBA ROM is not supported");
            if (rom.size() < 0xC0) return Fail("GBA ROM is too small");
            n.passthrough = true; n.ext = "gba";
            break;
        case System::Vb:
            if (rom.size() < 0x220) return Fail("Virtual Boy ROM is too small");
            n.passthrough = true; n.ext = "vb";
            break;
    }
    return n;
}

}  // namespace nso
