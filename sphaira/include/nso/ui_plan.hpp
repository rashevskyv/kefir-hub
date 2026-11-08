// Pure decisions of the libnx layer (sphaira/source/nso_*.cpp): host-testable, no file access.
#pragma once
#include "n64.hpp"
#include "plan.hpp"

namespace nso {

constexpr u64 kMaxRomSize = 64ull * 1024 * 1024;  // bigger ROMs are refused (the whole file is held in memory)
constexpr size_t kHeadSize = 64 * 1024;           // what PlanTarget needs to look at

// Can an installed NSO app take a file with this extension? ("bin" is the "which one?" case.)
inline bool CanTakeExt(std::string_view ext_in, const Installed& installed) {
    const std::string ext = AsciiLower(std::string(!ext_in.empty() && ext_in[0] == '.' ? ext_in.substr(1) : ext_in));
    for (const auto& s : kSystems) {
        if (installed[size_t(s.id)] && (ExtIn(s.exts, ext) || ext == "bin")) return true;
    }
    return false;
}

inline bool AnyInstalled(const Installed& i) {
    for (bool b : i) if (b) return true;
    return false;
}

// N64 games without a tested emulator config may not start: ask before writing. `head` = first bytes of the file.
inline bool N64NeedsWarning(ByteSpan head, std::span<const N64Config> table = std::span<const N64Config>(kN64Configs)) {
    if (!N64Magic(head) || head.size() < 0x40) return false;
    const N64Header h = ReadN64Header(N64ToZ64(head.first(0x40)));
    return FindN64Config(table, h.crc1, h.crc2) == nullptr;
}

struct Rect { int w = 0, h = 0, x = 0, y = 0; };

// Scale (sw x sh) to fit inside (tw x th), keep aspect, centered. Never returns a zero side.
inline Rect FitInside(int sw, int sh, int tw, int th) {
    Rect r;
    if (sw <= 0 || sh <= 0 || tw <= 0 || th <= 0) return r;
    if (u64(sw) * u64(th) >= u64(sh) * u64(tw)) {  // wider than the target: width decides
        r.w = tw;
        r.h = int(u64(sh) * u64(tw) / u64(sw));
    } else {
        r.h = th;
        r.w = int(u64(sw) * u64(th) / u64(sh));
    }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    r.x = (tw - r.w) / 2;
    r.y = (th - r.h) / 2;
    return r;
}

// What to delete after a remove. A db the Hub made itself is deleted with the last entry; a db that was
// there before (our overlay, a third-party mod) stays.
struct Cleanup { bool marker = false, db = false, flag = false; };
inline Cleanup CleanupAfterRemove(bool marker_empty, bool hub_created_db) {
    return {marker_empty, marker_empty && hub_created_db, marker_empty && hub_created_db};
}

inline std::string CreatedFlagRelPath() { return "titles/.kefir_created_db"; }

// Dir part of "titles/S-3123_e/3123_e.sfrom" -> "titles/S-3123_e".
inline std::string DirPart(std::string_view path) {
    const size_t s = path.rfind('/');
    return std::string(s == std::string_view::npos ? std::string_view{} : path.substr(0, s));
}

inline std::string JoinPath(std::string_view dir, std::string_view name) {
    std::string o(dir);
    if (!o.empty() && o.back() != '/') o += '/';
    return o + std::string(name);
}

// The first ".sfromsig" among romfs paths ("" when none).
inline std::string FindSfromsig(const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        if (p.size() > 9 && p.compare(p.size() - 9, 9, ".sfromsig") == 0) return p;
    }
    return {};
}

// The "title" member of one db entry text (for a list of our games). \" and \\ are unescaped, the rest is kept.
inline std::string EntryTitle(std::string_view entry) {
    constexpr std::string_view key = "\"title\":\"";
    size_t i = entry.find(key);
    if (i == std::string_view::npos) return {};
    std::string o;
    for (i += key.size(); i < entry.size() && entry[i] != '"'; i++) {
        if (entry[i] == '\\' && i + 1 < entry.size() && (entry[i + 1] == '"' || entry[i + 1] == '\\')) i++;
        o += entry[i];
    }
    return o;
}

}  // namespace nso
