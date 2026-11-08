#pragma once

// Facts about a ROM (year, publisher, developer, players, genre): the pure part. The source is libretro-database's
// metadat/ folder: one clrmamepro .dat per fact and system, keyed by the No-Intro name. They are merged into one small
// tab-separated file per system on the card. No libnx here: tests/test_rom_meta.cpp runs it on the host.

#include "rom_library.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::rom {

enum MetaField { MetaYear, MetaGenre, MetaDeveloper, MetaPublisher, MetaPlayers, META_FIELDS };

struct MetaDat {
    const char* folder; // under metadat/
    const char* key;    // the line "key value" inside a game block
};

// in MetaField order
inline constexpr MetaDat META_DATS[META_FIELDS] = {
    {"releaseyear", "releaseyear"}, {"genre", "genre"}, {"developer", "developer"}, {"publisher", "publisher"}, {"maxusers", "users"},
};

struct MetaRec {
    std::string name, year, publisher, developer, players, genre;
};

// recs are sorted by name; names holds the same names for PickThumb
struct MetaTable {
    std::vector<MetaRec> recs;
    std::vector<std::string> names;
};

// e.g. https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/releaseyear/Nintendo%20-%20Nintendo%2064.dat; empty for a system without a thumbnail repo
inline std::string MetaDatUrl(std::string_view console, MetaField field) {
    std::string system{ThumbRepo(console)};
    if (system.empty()) return {};
    for (size_t at; (at = system.find("_-_")) != std::string::npos;) system.replace(at, 3, " - ");
    for (auto& c : system) if (c == '_') c = ' ';
    return std::string{"https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/"} + META_DATS[field].folder + "/" + bios::EncodePath(system + ".dat");
}

// the (game name, value) pairs of one dat. A game block holds `comment "Aladdin (USA)"` and `<key> "value"` (a number is not quoted).
inline std::vector<std::pair<std::string, std::string>> ParseDat(std::string_view text, std::string_view key) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string name;
    const auto value_of = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.remove_suffix(1);
        if (!v.empty() && v.front() == '"') {
            v.remove_prefix(1);
            if (!v.empty() && v.back() == '"') v.remove_suffix(1);
        }
        return std::string{v};
    };
    for (size_t at = 0; at < text.size();) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        const auto split = line.find_first_of(" \t");
        if (split == std::string_view::npos) continue;
        const auto word = line.substr(0, split);
        if (word == "game") {
            name.clear(); // some blocks have only a crc and no title: their values belong to nobody
        } else if (word == "comment") {
            name = value_of(line.substr(split));
        } else if (word == key && !name.empty()) {
            if (auto v = value_of(line.substr(split)); !v.empty()) out.emplace_back(name, std::move(v));
        }
    }
    return out;
}

inline std::string& MetaSlot(MetaRec& r, MetaField f) {
    switch (f) {
        case MetaYear: return r.year;
        case MetaGenre: return r.genre;
        case MetaDeveloper: return r.developer;
        case MetaPublisher: return r.publisher;
        default: return r.players;
    }
}

// the dats (texts in MetaField order; an empty text is a fact nobody knows) merged by game name, sorted by name
inline std::vector<MetaRec> MergeDats(const std::vector<std::string_view>& texts) {
    std::map<std::string, MetaRec> by_name;
    for (size_t f = 0; f < texts.size() && f < META_FIELDS; f++) {
        for (auto& [name, value] : ParseDat(texts[f], META_DATS[f].key)) {
            auto& rec = by_name[name];
            rec.name = name;
            MetaSlot(rec, static_cast<MetaField>(f)) = std::move(value);
        }
    }
    std::vector<MetaRec> out;
    out.reserve(by_name.size());
    for (auto& kv : by_name) out.push_back(std::move(kv.second));
    return out;
}

inline std::string MetaClean(std::string_view s) {
    std::string out{s};
    for (auto& c : out) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return out;
}

// name \t year \t publisher \t developer \t players \t genre, one game a line
inline std::string FormatCompact(const std::vector<MetaRec>& recs) {
    std::string out;
    for (const auto& r : recs) {
        for (const auto* s : {&r.name, &r.year, &r.publisher, &r.developer, &r.players}) {
            out += MetaClean(*s);
            out += '\t';
        }
        out += MetaClean(r.genre);
        out += '\n';
    }
    return out;
}

inline MetaTable ParseCompact(std::string_view text) {
    MetaTable t;
    for (size_t at = 0; at < text.size();) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::string cols[6];
        size_t n = 0;
        for (size_t p = 0; n < 6;) {
            const auto tab = line.find('\t', p);
            cols[n++] = std::string{line.substr(p, tab == std::string_view::npos ? std::string_view::npos : tab - p)};
            if (tab == std::string_view::npos) break;
            p = tab + 1;
        }
        if (n < 6 || cols[0].empty()) continue; // not a row of ours
        t.recs.push_back({cols[0], cols[1], cols[2], cols[3], cols[4], cols[5]});
    }
    std::sort(t.recs.begin(), t.recs.end(), [](const MetaRec& a, const MetaRec& b) { return a.name < b.name; });
    t.names.reserve(t.recs.size());
    for (const auto& r : t.recs) t.names.push_back(r.name);
    return t;
}

// the game of a loose file name ("Aladdin (U).smc"), or null. ponytail: PickThumb scans every name (about 3000-10000), per cursor move; precompute keys if it is slow.
inline const MetaRec* FindMeta(const MetaTable& t, std::string_view file) {
    const auto name = PickThumb(t.names, file);
    if (name.empty()) return nullptr;
    const auto it = std::lower_bound(t.recs.begin(), t.recs.end(), name, [](const MetaRec& r, const std::string& n) { return r.name < n; });
    return it != t.recs.end() && it->name == name ? &*it : nullptr;
}

// (label, value) for each fact that is known; the labels are English, the caller translates them. Developer only when it is not the publisher.
inline std::vector<std::pair<const char*, std::string>> MetaLines(const MetaRec& r) {
    std::vector<std::pair<const char*, std::string>> out;
    if (!r.year.empty()) out.emplace_back("Year", r.year);
    if (!r.publisher.empty()) out.emplace_back("Publisher", r.publisher);
    if (!r.developer.empty() && r.developer != r.publisher) out.emplace_back("Developer", r.developer);
    if (!r.players.empty()) out.emplace_back("Players", r.players);
    if (!r.genre.empty()) out.emplace_back("Genre", r.genre);
    return out;
}

} // namespace sphaira::rom
