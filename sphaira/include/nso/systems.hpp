// NSO app table and ROM -> system detection. Spec: docs/dev/NSO-*.md
#pragma once
#include "common.hpp"

#include <string_view>

namespace nso {

enum class System : int { Nes, Snes, N64, N64Mature, Genesis, Gb, Gba, Vb };
constexpr int kSystemCount = 8;
using Installed = std::array<bool, kSystemCount>;  // per System: the NSO app (base title) is installed

enum class CoverKind { Png, XtxZ };

struct SystemInfo {
    System id;
    const char* name;
    u64 base_tid;     // overlay goes to /atmosphere/contents/<base_tid>/romfs
    const char* exts; // extensions (lower case, space separated) that name this system
    CoverKind cover;
    int details_w, details_h;  // details screen size (cover: scale so the longer side is 512)
};

inline constexpr SystemInfo kSystems[kSystemCount] = {
    {System::Nes, "NES", 0x0100D870045B6000ull, "nes", CoverKind::XtxZ, 400, 300},
    {System::Snes, "SNES", 0x01008D300C50C000ull, "sfc smc swc fig", CoverKind::XtxZ, 400, 300},
    {System::N64, "Nintendo 64", 0x0100C9A00ECE6000ull, "z64 n64 v64", CoverKind::Png, 400, 300},
    {System::N64Mature, "Nintendo 64 (Mature 17+)", 0x0100E0601C632000ull, "z64 n64 v64", CoverKind::Png, 400, 300},
    {System::Genesis, "SEGA Genesis", 0x0100B3C014BDA000ull, "gen md smd", CoverKind::Png, 400, 300},
    {System::Gb, "Game Boy / Color", 0x0100C62011050000ull, "gb gbc sgb", CoverKind::Png, 400, 300},
    {System::Gba, "Game Boy Advance", 0x010012F017576000ull, "gba", CoverKind::Png, 400, 300},
    {System::Vb, "Virtual Boy", 0x0100BFC01D976000ull, "vb vboy", CoverKind::Png, 768, 224},
};

inline const SystemInfo& Info(System s) { return kSystems[int(s)]; }
inline u64 UpdateTid(System s) { return Info(s).base_tid + 0x800; }  // holds for all 8 apps

inline std::string TidHex(u64 t) {
    static const char* h = "0123456789ABCDEF";
    std::string o(16, '0');
    for (int i = 15; i >= 0; i--, t >>= 4) o[size_t(i)] = h[t & 15];
    return o;
}
// "/atmosphere/contents/<base tid>/romfs" (no trailing slash). Paths of the files below are relative to it.
inline std::string OverlayRomfs(System s) { return "/atmosphere/contents/" + TidHex(Info(s).base_tid) + "/romfs"; }
inline std::string DbRelPath() { return "titles/lclassics.titlesdb"; }
inline std::string MarkerRelPath() { return "titles/.kefir_added"; }

// Extension without dot, lower case ("" when none).
inline std::string ExtOf(std::string_view name) {
    const size_t d = name.rfind('.');
    const size_t s = name.find_last_of("/\\");
    if (d == std::string_view::npos || (s != std::string_view::npos && d < s)) return {};
    return AsciiLower(std::string(name.substr(d + 1)));
}

inline bool ExtIn(const char* list, std::string_view ext) {
    if (ext.empty()) return false;
    std::string_view l = list;
    while (!l.empty()) {
        const size_t sp = l.find(' ');
        if (l.substr(0, sp) == ext) return true;
        if (sp == std::string_view::npos) break;
        l.remove_prefix(sp + 1);
    }
    return false;
}

constexpr size_t kSmdHeader = 512;
inline bool LooksSmd(ByteSpan h, u64 size) {
    return size > kSmdHeader && size % 16384 == kSmdHeader && h.size() > 9 && h[8] == 0xAA && h[9] == 0xBB;
}

// 16-bit complement + checksum of a SNES internal header at `at` (header start) sum to 0xFFFF.
inline bool SnesHeaderAt(ByteSpan h, size_t at) {
    if (at + 0x20 > h.size()) return false;
    const u32 c = u32(h[at + 0x1C]) | (u32(h[at + 0x1D]) << 8);
    const u32 s = u32(h[at + 0x1E]) | (u32(h[at + 0x1F]) << 8);
    return c + s == 0xFFFF;
}
inline bool LooksSnes(ByteSpan h) {
    return SnesHeaderAt(h, 0x7FC0) || SnesHeaderAt(h, 0xFFC0) || SnesHeaderAt(h, 0x7FC0 + 512);
}

inline bool N64Magic(ByteSpan h) {
    return h.size() >= 4 && ((h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40) ||
                             (h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12) ||
                             (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80));
}

// Header magic first (it wins over the extension), then the extension, then the "which one?" list.
// `head` = first bytes of the file (64 KiB is enough, except a SNES file with a copier header: SNES is
// decided by extension; Virtual Boy has no magic and is decided by extension too).
// The caller intersects the result with the installed apps (see PlanTarget in plan.hpp).
inline std::vector<System> DetectSystem(ByteSpan head, u64 size, std::string_view ext_in) {
    const std::string ext = ext_in.size() && ext_in[0] == '.' ? AsciiLower(std::string(ext_in.substr(1)))
                                                                : AsciiLower(std::string(ext_in));
    if (head.size() >= 16 && Has(head, 0, "NES\x1a")) return {System::Nes};
    if (N64Magic(head)) return {System::N64, System::N64Mature};
    if (Has(head, 0x100, "SEGA")) return {System::Genesis};
    if (LooksSmd(head, size) && head.size() >= kSmdHeader + 16384) {
        // de-interleaved bytes 0x100..0x103 = blk[8192+128], blk[128], blk[8192+129], blk[129]
        const u8* b = head.data() + kSmdHeader;
        if (b[8192 + 128] == 'S' && b[128] == 'E' && b[8192 + 129] == 'G' && b[129] == 'A') return {System::Genesis};
    }
    if (head.size() > 0x108 && head[0x104] == 0xCE && head[0x105] == 0xED && head[0x106] == 0x66 && head[0x107] == 0x66)
        return {System::Gb};
    if (head.size() > 0xB2 && head[4] == 0x24 && head[5] == 0xFF && head[6] == 0xAE && head[7] == 0x51 && head[0xB2] == 0x96)
        return {System::Gba};
    if (head.size() >= 4 && head[0] == 0x04 && head[1] == 0x22 && head[2] == 0x4D && head[3] == 0x18)
        return {System::Gba};  // LZ4 frame: BuildAdd refuses it with a clear error
    for (const auto& s : kSystems) {
        if (s.id == System::N64Mature || !ExtIn(s.exts, ext)) continue;
        if (s.id == System::N64) return {System::N64, System::N64Mature};
        return {s.id};
    }
    if (ExtIn("fds qd", ext)) return {};  // Famicom Disk System: not supported
    if (LooksSnes(head)) return {System::Snes};
    return {System::Genesis, System::Snes, System::Vb};  // .bin and friends: the user must choose
}

}  // namespace nso
