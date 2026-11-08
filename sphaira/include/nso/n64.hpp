// N64 specifics: byte order, header facts, .dtz payload (old-binary cpio of meta + cfg). Spec: NSO-n64-vb.md
// The cpio is returned UNCOMPRESSED; the libnx side zlib-compresses it (level 9) to make the .dtz.
#pragma once
#include "common.hpp"
#include "n64_table.hpp"

#include <string_view>

namespace nso {

// .z64 (80371240), .v64 (37804012, 16-bit swapped) or .n64 (40123780, 32-bit swapped) -> .z64.
inline Bytes N64ToZ64(ByteSpan b) {
    Bytes o(b.begin(), b.end());
    if (b.size() >= 4 && b[0] == 0x37) {
        for (size_t i = 0; i + 1 < o.size(); i += 2) { o[i] = b[i + 1]; o[i + 1] = b[i]; }
    } else if (b.size() >= 4 && b[0] == 0x40) {
        for (size_t i = 0; i + 3 < o.size(); i += 4) {
            o[i] = b[i + 3]; o[i + 1] = b[i + 2]; o[i + 2] = b[i + 1]; o[i + 3] = b[i];
        }
    }
    return o;
}

struct N64Header {
    u32 crc1 = 0, crc2 = 0;
    char cart[5] = {};  // header 0x3B..0x3E
    bool pal = false;   // region byte 0x3E in "PDFISXYHLWZ"
};
inline N64Header ReadN64Header(ByteSpan z64) {
    N64Header h;
    if (z64.size() < 0x40) return h;
    h.crc1 = Be32(z64, 0x10);
    h.crc2 = Be32(z64, 0x14);
    for (int i = 0; i < 4; i++) h.cart[i] = char(z64[size_t(0x3B + i)]);
    for (const char* p = "PDFISXYHLWZ"; *p; p++) if (char(z64[0x3E]) == *p) h.pal = true;
    return h;
}

// Same bytes as the proven mod files (CRLF, 4 spaces). PAL adds "PAL": 1.
inline std::string N64MetaText(bool pal) {
    std::string s = "{\r\n    \"Hardware\": \"NUS\",\r\n    \"TitleCode\": \"XXXXXX.000\",\r\n"
                    "    \"DataVersion\": \"1.0.0\",\r\n    \"OptionInfo\": {\r\n        \"SndSampleRate\": 32016";
    if (pal) s += ",\r\n        \"PAL\": 1";
    return s + "\r\n    }\r\n}";
}

inline std::string HexAddr(u32 a) {
    static const char* h = "0123456789ABCDEF";
    std::string o = "0x";
    for (int i = 7; i >= 0; i--) o += h[(a >> (4 * i)) & 15];
    return o;
}

// Byte-exact layout of the N-4435_e cfg for the same inputs; for a generic config see kN64Default.
inline std::string N64CfgText(const N64Config& c) {
    std::string s = "{\r\n\t\"RomOption\": {\r\n\t\t\"TickPerInst\": 8,\r\n\t\t\"BackupType\": \"" + std::string(c.backup) +
                    "\",\r\n\t\t\"SIDevice_PakType\": \"" + c.pak + "\"";
    if (c.stick_limit > 0) s += ",\r\n\t\t\"StickLimit\": " + std::to_string(c.stick_limit);
    if (c.ram && *c.ram) s += std::string(",\r\n\t\t\"RAMSize\": \"") + c.ram + "\"";
    s += "\r\n\t},\r\n\t\"RSP\" :{},\r\n\t\"Idle\" :\r\n\t[";
    for (int i = 0; i < c.idle_count; i++) {
        s += i ? ",\r\n\t\t{" : "\r\n\t\t{";
        s += "\r\n\t\t\t\"JmpAddr\": \"" + HexAddr(c.idle[i]) + "\",\r\n\t\t\t\"JmpInst\": \"0x1000FFFF\",\r\n\t\t\t\"IsIdle\": \"0x1\"\r\n\t\t}";
    }
    s += c.idle_count ? "\r\n\t],\t" : "],\t";
    s += "\r\n\t\"SpecialInst\" :\r\n\t[],\r\n\t\"RendererSetting\": {\r\n\t}\t\t\r\n}";
    return s;
}

struct CpioFile {
    std::string name;  // without directory
    std::string data;
    u32 mtime = 0;
};

// Old binary cpio (magic 0x71c7, little endian), layout of the Custom ROM mod: dev 0x8080, names
// "<dirname>\<name>", TRAILER!!! last. Files are written in the given order.
inline Bytes BuildCpio(std::string_view dirname, const std::vector<CpioFile>& files) {
    Bytes o;
    auto put = [&](const std::string& name, ByteSpan data, u32 dev, u32 mtime) {
        const std::string nb = name + '\0';
        PutLe16(o, 0x71c7); PutLe16(o, dev);
        for (int i = 0; i < 6; i++) PutLe16(o, 0);  // ino mode uid gid nlink rdev
        PutLe16(o, mtime >> 16); PutLe16(o, mtime & 0xFFFF);
        PutLe16(o, u32(nb.size()));
        PutLe16(o, u32(data.size() >> 16)); PutLe16(o, u32(data.size() & 0xFFFF));
        o.insert(o.end(), nb.begin(), nb.end());
        if (nb.size() & 1) o.push_back(0);
        o.insert(o.end(), data.begin(), data.end());
        if (data.size() & 1) o.push_back(0);
    };
    for (const auto& f : files) {
        put(std::string(dirname) + char(92) + f.name,
            ByteSpan(reinterpret_cast<const u8*>(f.data.data()), f.data.size()), 0x8080, f.mtime);
    }
    put("TRAILER!!!", {}, 0, 0);
    return o;
}

// Uncompressed .dtz content for a ROM. cfg == nullptr -> generic kN64Default.
inline Bytes BuildDtzCpio(bool pal, const N64Config* cfg, u32 mtime) {
    return BuildCpio("metapack", {{"00_XXXXXX.000.meta", N64MetaText(pal), mtime},
                                  {"02_XXXXXX.000.cfg", N64CfgText(cfg ? *cfg : kN64Default), mtime}});
}

}  // namespace nso
