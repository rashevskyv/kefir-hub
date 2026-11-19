#pragma once

// Text helpers of the release analyser (release_plan.hpp): path parts and the words it looks
// for in file and folder names. Pure logic, host-tested through tests/test_release_plan.cpp.

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::release {

namespace detail {

inline auto IsAlpha(char c) -> bool { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline auto IsHex(char c) -> bool { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline auto IsDigit(char c) -> bool { return c >= '0' && c <= '9'; }

// lower case for ASCII and the Cyrillic capitals U+0410..U+042F ("РУС" must read as "рус").
inline auto Lower(std::string_view s) -> std::string {
    std::string out{s};
    for (size_t i = 0; i < out.size(); i++) {
        const auto c = static_cast<unsigned char>(out[i]);
        if (c >= 'A' && c <= 'Z') {
            out[i] = static_cast<char>(c - 'A' + 'a');
        } else if (c == 0xD0 && i + 1 < out.size()) {
            const auto n = static_cast<unsigned char>(out[i + 1]);
            if (n >= 0x90 && n <= 0x9F) {
                out[i + 1] = static_cast<char>(n + 0x20);
            } else if (n >= 0xA0 && n <= 0xAF) {
                out[i] = static_cast<char>(0xD1);
                out[i + 1] = static_cast<char>(n - 0x20);
            }
            i++;
        }
    }
    return out;
}

inline auto EndsWithAny(std::string_view low, std::initializer_list<std::string_view> exts) -> bool {
    return std::any_of(exts.begin(), exts.end(), [&](auto e) { return low.ends_with(e); });
}
inline auto IsPackage(std::string_view low) -> bool { return EndsWithAny(low, {".nsp", ".nsz", ".xci", ".xcz"}); }
inline auto IsArchive(std::string_view low) -> bool { return EndsWithAny(low, {".7z", ".zip", ".rar"}); }

// `word` in lower-case text, with no ASCII letter before it (and none after it when `end`).
inline auto HasWord(std::string_view low, std::string_view word, bool end) -> bool {
    for (auto i = low.find(word); i != low.npos; i = low.find(word, i + 1)) {
        const bool before = i == 0 || !IsAlpha(low[i - 1]);
        const bool after = !end || i + word.size() >= low.size() || !IsAlpha(low[i + word.size()]);
        if (before && after) {
            return true;
        }
    }
    return false;
}

inline auto IsHex16(std::string_view s) -> bool {
    return s.size() == 16 && std::all_of(s.begin(), s.end(), IsHex);
}

inline auto ParseHex(std::string_view s) -> uint64_t {
    uint64_t v = 0;
    for (const char c : s) {
        v = (v << 4) | static_cast<uint64_t>(IsDigit(c) ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

// "firmware", "keys", "switchemu" in a path component: files for emulators, not for the console.
inline auto IsSkipName(std::string_view comp) -> bool {
    const auto low = Lower(comp);
    return low.find("firmware") != low.npos || low.find("switchemu") != low.npos ||
        HasWord(low, "key", true) || HasWord(low, "keys", true);
}
inline auto IsModWord(std::string_view name) -> bool { return HasWord(Lower(name), "mod", true); }
// "Homebrew Update (v0.6.1)", "Updated videos": a later layer over the port, copied last.
inline auto IsUpdateWord(std::string_view name) -> bool { return HasWord(Lower(name), "updat", false); }
// a "Gamesave Data" folder holds files for a user's save.
inline auto IsSaveDir(std::string_view comp) -> bool {
    const auto low = Lower(comp);
    return HasWord(low, "gamesave", true) || HasWord(low, "game save", true) ||
        HasWord(low, "savedata", true) || HasWord(low, "save data", true);
}
// SD_Cache.0003 is cache storage 3.
inline auto CacheIndex(std::string_view comp) -> std::optional<uint32_t> {
    const auto low = Lower(comp);
    constexpr std::string_view prefix = "sd_cache.";
    if (!low.starts_with(prefix) || low.size() == prefix.size() || low.size() > prefix.size() + 5) {
        return std::nullopt;
    }
    uint32_t v = 0;
    for (const char c : low.substr(prefix.size())) {
        if (!IsDigit(c)) {
            return std::nullopt;
        }
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    return v;
}

inline auto Split(std::string_view path) -> std::vector<std::string_view> {
    std::vector<std::string_view> parts;
    while (true) {
        const auto slash = path.find('/');
        parts.push_back(path.substr(0, slash));
        if (slash == path.npos) {
            return parts;
        }
        path.remove_prefix(slash + 1);
    }
}

inline auto Join(const std::vector<std::string_view>& parts, size_t from, size_t to) -> std::string {
    std::string out;
    for (size_t i = from; i < to && i < parts.size(); i++) {
        out += (i > from ? "/" : "");
        out += parts[i];
    }
    return out;
}

inline auto Parent(std::string_view path) -> std::string {
    const auto slash = path.rfind('/');
    return slash == path.npos ? std::string{} : std::string{path.substr(0, slash)};
}
inline auto Basename(std::string_view path) -> std::string {
    const auto slash = path.rfind('/');
    return std::string{slash == path.npos ? path : path.substr(slash + 1)};
}
inline auto IsUnder(std::string_view path, std::string_view folder) -> bool {
    return folder.empty() || path == folder ||
        (path.size() > folder.size() && path.starts_with(folder) && path[folder.size()] == '/');
}
// "./a\\b/" -> "a/b"
inline auto Normalize(std::string_view raw) -> std::string {
    std::string p{raw};
    std::replace(p.begin(), p.end(), '\\', '/');
    while (!p.empty() && p.front() == '/') p.erase(0, 1);
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.starts_with("./")) p.erase(0, 2);
    return p;
}

} // namespace detail

} // namespace sphaira::release
