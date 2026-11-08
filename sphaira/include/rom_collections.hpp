#pragma once

// "Add to RetroArch" / "Add to Tico": the pure part. No libnx here: tests/test_rom_collections.cpp runs it on the host.
//
// RetroArch (libnx) keeps a game list per system as /retroarch/playlists/<db name>.lpl (JSON, "version": "1.5") and reads
// the cover from /retroarch/thumbnails/<db name>/Named_Boxarts/<label>.png. Both folders are the defaults of
// frontend/drivers/platform_switch.c (base /retroarch/).
// Tico has no list file: it scans /tico/roms/<console>/ (and the user's "Game folders") and reads the cover from
// /tico/assets/covers/<console>/<ROM file name without extension>.jpg (ticoverse.com wiki, "How to setup assets manually").

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::rom::coll {

inline constexpr const char* RA_PLAYLISTS = "/retroarch/playlists/";
inline constexpr const char* RA_THUMBNAILS = "/retroarch/thumbnails/";
inline constexpr const char* RA_INFO = "/retroarch/info/";
inline constexpr const char* TICO_COVERS = "/tico/assets/covers/";

struct PlaylistGame {
    std::string path;      // /roms/snes/Game.sfc
    std::string label;     // the name RetroArch shows, and names the thumbnail by
    std::string core_path; // /retroarch/cores/snes9x_libretro_libnx.nro
    std::string core_name; // "Snes9x"
    std::string db_name;   // "Nintendo - Super Nintendo Entertainment System.lpl"
    std::string crc32 = "DETECT";
};

// the first folder of a card path, lower case: "/retroarch/cores/a.nro" -> "retroarch"
inline std::string RootOf(std::string_view path) {
    if (!path.empty() && path.front() == '/') path.remove_prefix(1);
    path = path.substr(0, path.find('/'));
    std::string out{path};
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// the console folder of a card folder that is exactly "/roms/<console>" (a trailing slash is allowed); "" for any other folder
inline std::string RomConsoleOf(std::string_view dir) {
    constexpr std::string_view prefix = "/roms/";
    if (!dir.starts_with(prefix)) return {};
    dir.remove_prefix(prefix.size());
    if (!dir.empty() && dir.back() == '/') dir.remove_suffix(1);
    if (dir.empty() || dir.find('/') != std::string_view::npos) return {};
    return std::string{dir};
}

// the file name without its folder and its extension (an extension is up to 4 letters after the last dot)
inline std::string Stem(std::string_view file) {
    const auto slash = file.rfind('/');
    if (slash != std::string_view::npos) file = file.substr(slash + 1);
    const auto dot = file.rfind('.');
    if (dot != std::string_view::npos && dot > 0 && file.size() - dot <= 5) file = file.substr(0, dot);
    return std::string{file};
}

// a label as a file name: & * / : ` < > ? \ | " are written as _ (the rule RetroArch uses for thumbnails)
inline std::string ThumbFile(std::string_view label) {
    std::string out{label};
    for (auto& c : out) {
        if (std::string_view{"&*/:`<>?\\|\""}.find(c) != std::string_view::npos) c = '_';
    }
    return out;
}

// "/retroarch/cores/snes9x_libretro_libnx.nro" -> "snes9x"
inline std::string CoreStem(std::string_view core_path) {
    const auto slash = core_path.rfind('/');
    if (slash != std::string_view::npos) core_path = core_path.substr(slash + 1);
    for (const std::string_view tail : {"_libretro_libnx.nro", "_libretro.nro", ".nro"}) {
        if (core_path.size() > tail.size() && core_path.substr(core_path.size() - tail.size()) == tail) {
            return std::string{core_path.substr(0, core_path.size() - tail.size())};
        }
    }
    return std::string{core_path};
}

// display_name from the text of a RetroArch .info file (display_name = "Snes9x"); empty when absent
inline std::string InfoDisplayName(std::string_view info) {
    size_t at = 0;
    while (at < info.size()) {
        auto end = info.find('\n', at);
        if (end == std::string_view::npos) end = info.size();
        auto line = info.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
        if (!line.starts_with("display_name")) continue;
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        auto value = line.substr(eq + 1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        return std::string{value};
    }
    return {};
}

// "Sega_-_Mega_Drive_-_Genesis" (the thumbnail repository name) -> "Sega - Mega Drive - Genesis" (the RetroArch database name)
inline std::string DbNameFromRepo(std::string_view repo) {
    std::string out{repo};
    for (size_t at; (at = out.find("_-_")) != std::string::npos;) out.replace(at, 3, " - ");
    for (auto& c : out) if (c == '_') c = ' ';
    return out;
}

// ---- the playlist text ----

inline std::string JsonString(std::string_view s) {
    std::string out{"\""};
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            static constexpr char HEX[] = "0123456789abcdef";
            out += "\\u00";
            out += HEX[c >> 4];
            out += HEX[c & 15];
        } else out += ch;
    }
    return out + "\"";
}

namespace detail {

inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

inline size_t SkipSpace(std::string_view s, size_t i) {
    while (i < s.size() && IsSpace(s[i])) i++;
    return i;
}

// s[i] is the opening quote; returns the index after the closing quote, or npos
inline size_t StringEnd(std::string_view s, size_t i) {
    for (i++; i < s.size(); i++) {
        if (s[i] == '\\') i++;
        else if (s[i] == '"') return i + 1;
    }
    return std::string_view::npos;
}

inline void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

// the value of a JSON string token (with its quotes)
inline std::string Unquote(std::string_view tok) {
    std::string out;
    for (size_t i = 1; i + 1 < tok.size(); i++) {
        if (tok[i] != '\\' || i + 2 >= tok.size()) { out += tok[i]; continue; }
        const char e = tok[++i];
        switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                size_t k = 0;
                for (; k < 4 && i + 1 + k + 1 < tok.size(); k++) {
                    const char h = tok[i + 1 + k];
                    cp = cp * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (std::tolower(static_cast<unsigned char>(h)) - 'a' + 10));
                }
                AppendUtf8(out, cp);
                i += k;
                break;
            }
            default: out += e; break; // \" \\ \/
        }
    }
    return out;
}

// s[i] starts a value; returns the index after it, or npos. Strings and nested {} [] are skipped whole.
inline size_t ValueEnd(std::string_view s, size_t i) {
    if (i >= s.size()) return std::string_view::npos;
    if (s[i] == '"') return StringEnd(s, i);
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        for (; i < s.size(); i++) {
            if (s[i] == '"') {
                i = StringEnd(s, i);
                if (i == std::string_view::npos) return i;
                i--;
            } else if (s[i] == '{' || s[i] == '[') depth++;
            else if (s[i] == '}' || s[i] == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::string_view::npos;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !IsSpace(s[i])) i++;
    return i;
}

struct Member {
    std::string key;
    std::string raw; // the value, byte for byte
};

// the members of the object s[begin, end) ("{ ... }"); false when it is not well formed
inline bool Members(std::string_view s, std::vector<Member>& out) {
    size_t i = SkipSpace(s, 0);
    if (i >= s.size() || s[i] != '{') return false;
    i = SkipSpace(s, i + 1);
    while (i < s.size() && s[i] != '}') {
        if (s[i] != '"') return false;
        const auto key_end = StringEnd(s, i);
        if (key_end == std::string_view::npos) return false;
        const auto key = Unquote(s.substr(i, key_end - i));
        i = SkipSpace(s, key_end);
        if (i >= s.size() || s[i] != ':') return false;
        i = SkipSpace(s, i + 1);
        const auto end = ValueEnd(s, i);
        if (end == std::string_view::npos || end == i) return false;
        out.push_back({key, std::string{s.substr(i, end - i)}});
        i = SkipSpace(s, end);
        if (i < s.size() && s[i] == ',') i = SkipSpace(s, i + 1);
    }
    return i < s.size();
}

struct Span { size_t begin, end; };

// the [ ] of the top-level "items" and the { } of every entry in it
inline bool FindItems(std::string_view s, Span& array, std::vector<Span>& entries) {
    size_t i = SkipSpace(s, 0);
    if (i >= s.size() || s[i] != '{') return false;
    i = SkipSpace(s, i + 1);
    while (i < s.size() && s[i] != '}') {
        if (s[i] != '"') return false;
        const auto key_end = StringEnd(s, i);
        if (key_end == std::string_view::npos) return false;
        const auto key = Unquote(s.substr(i, key_end - i));
        i = SkipSpace(s, key_end);
        if (i >= s.size() || s[i] != ':') return false;
        i = SkipSpace(s, i + 1);
        const auto end = ValueEnd(s, i);
        if (end == std::string_view::npos || end == i) return false;
        if (key == "items") {
            if (s[i] != '[') return false;
            array = {i, end - 1};
            size_t j = SkipSpace(s, i + 1);
            while (j < array.end) {
                if (s[j] != '{') return false;
                const auto e = ValueEnd(s, j);
                if (e == std::string_view::npos || e > array.end) return false;
                entries.push_back({j, e});
                j = SkipSpace(s, e);
                if (j < array.end && s[j] == ',') j = SkipSpace(s, j + 1);
            }
            return true;
        }
        i = SkipSpace(s, end);
        if (i < s.size() && s[i] == ',') i = SkipSpace(s, i + 1);
    }
    return false; // no "items"
}

inline std::string EntryText(const PlaylistGame& g, const std::vector<Member>& old) {
    const auto value_of = [&](std::string_view key) -> const std::string* {
        for (const auto& m : old) if (m.key == key) return &m.raw;
        return nullptr;
    };
    std::string out = "    {\n";
    const auto add = [&](std::string_view key, const std::string& raw, bool last) {
        out += "      " + JsonString(key) + ": " + raw + (last ? "\n" : ",\n");
    };
    add("path", JsonString(g.path), false);
    add("label", JsonString(g.label), false);
    add("core_path", JsonString(g.core_path), false);
    add("core_name", JsonString(g.core_name), false);
    // a CRC32 RetroArch has found for this game stays
    const auto* crc = value_of("crc32");
    add("crc32", crc && *crc != "\"DETECT\"" ? *crc : JsonString(g.crc32), false);
    std::vector<const Member*> rest;
    for (const auto& m : old) {
        if (m.key != "path" && m.key != "label" && m.key != "core_path" && m.key != "core_name" && m.key != "crc32" && m.key != "db_name") {
            rest.push_back(&m);
        }
    }
    add("db_name", JsonString(g.db_name), rest.empty());
    for (size_t i = 0; i < rest.size(); i++) add(rest[i]->key, rest[i]->raw, i + 1 == rest.size());
    return out + "    }";
}

} // namespace detail

// The playlist text with `game` added. An entry with the same path is replaced (its other fields, like play time, stay);
// every other byte of the text stays. Empty/blank `existing` gives a new playlist. nullopt: the text is not a playlist
// this code can read, and must not be overwritten.
inline std::optional<std::string> AddToPlaylist(std::string_view existing, const PlaylistGame& game) {
    if (detail::SkipSpace(existing, 0) >= existing.size()) {
        return "{\n  \"version\": \"1.5\",\n  \"default_core_path\": \"\",\n  \"default_core_name\": \"\",\n"
               "  \"label_display_mode\": 0,\n  \"right_thumbnail_mode\": 0,\n  \"left_thumbnail_mode\": 0,\n"
               "  \"sort_mode\": 0,\n  \"items\": [\n" + detail::EntryText(game, {}) + "\n  ]\n}\n";
    }

    detail::Span array{};
    std::vector<detail::Span> entries;
    if (!detail::FindItems(existing, array, entries)) return std::nullopt;

    std::string out{existing};
    for (const auto& e : entries) {
        std::vector<detail::Member> members;
        const auto text = existing.substr(e.begin, e.end - e.begin);
        if (!detail::Members(text, members)) return std::nullopt;
        for (const auto& m : members) {
            if (m.key == "path" && m.raw.size() >= 2 && m.raw.front() == '"' && detail::Unquote(m.raw) == game.path) {
                // keep the indentation the entry had
                auto start = e.begin;
                while (start > 0 && (existing[start - 1] == ' ' || existing[start - 1] == '\t')) start--;
                const auto entry = detail::EntryText(game, members);
                out.replace(start, e.end - start, entry);
                return out;
            }
        }
    }

    if (entries.empty()) {
        out.replace(array.begin + 1, array.end - (array.begin + 1), "\n" + detail::EntryText(game, {}) + "\n  ");
    } else {
        out.insert(entries.back().end, ",\n" + detail::EntryText(game, {}));
    }
    return out;
}

} // namespace sphaira::rom::coll
