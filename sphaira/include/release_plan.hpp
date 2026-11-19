#pragma once

// Release analyser: what a folder with a Switch release holds and what to install from it.
// A port of DBI Backend Qt's src/release_analyzer.py (same rules, same order), so "Install all
// from folder" in the file browser and a folder dropped on the PC give the same result.
//
// Input: the file paths inside the folder (relative, '/' or '\\') and a callback that lists an
// archive's files. Model:
// - Game: one title, joined by the title ID read from the file name.
// - Option: files for the microSD, from a folder or an archive: a port (an NRO and its folder),
//   data (files a port needs, often one per language), a mod (LayeredFS). A mod may carry its own
//   update package: when the mod is chosen that update is installed, not the release's newer one.
//   Console saves (cache storages, a user's save) have destinations that start with '@'.
// Choice and install plan: release_choice.hpp. Pure logic, host-tested in tests/test_release_plan.cpp.

#include "release_text.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sphaira::release {

inline constexpr std::string_view CACHE_DST = "@cache/";
inline constexpr std::string_view SAVE_DST = "@save/";

enum class Kind { Port, Data, Mod, Cache, Save }; // also the copy order of a game's options
enum class Skip { NotForConsole, Partial, ArchiveUnreadable, PackageInArchive, UnknownLayout };

using FilePair = std::pair<std::string, std::string>; // (source, destination)
// the archive's file entries, or nullopt when it cannot be read.
using ListArchive = std::function<std::optional<std::vector<std::string>>(const std::string& path)>;

struct Package {
    std::string path;
    std::optional<uint64_t> tid;
    uint32_t version{};

    enum class Type { Unknown, Base, Update, Dlc };
    auto type() const -> Type {
        if (!tid) return Type::Unknown;
        const auto low = *tid & 0xFFF;
        if (low == 0x800) return Type::Update;
        // DLC ends in 001..7FF; any other ending is a homebrew stub with a random title ID
        return low > 0 && low < 0x800 ? Type::Dlc : Type::Base;
    }
    auto game_tid() const -> uint64_t {
        if (!tid) return 0;
        return type() == Type::Dlc ? (*tid & ~0xFFFULL) - 0x1000 : *tid & ~0xFFFULL;
    }
    auto name() const -> std::string { return detail::Basename(path); }
};

struct Option {
    std::string key{};               // stable id: the folder or archive path inside the release
    std::string name{};
    Kind kind{};
    std::string home{};              // folder the option lives in
    std::vector<FilePair> files{};   // source is inside `archive` when that is set
    std::string archive{};           // empty: loose files
    std::set<uint64_t> tids{};
    std::vector<Package> updates{};
    std::string lang{};              // "ru", "uk", "en" or empty
    bool tid_mismatch{};             // mod for a title that is not in this release, joined by folder
    bool incomplete{};               // a file in its folder is still downloading
    std::string parent{};            // add-on: key of the mod it needs
};

struct Game {
    uint64_t tid{};
    std::string root{};
    std::optional<Package> base{};
    std::vector<Package> updates{};
    std::vector<Package> dlcs{};
    std::vector<Option> options{};

    auto name() const -> std::string {
        const Package* pkg = base ? &*base : !updates.empty() ? &updates[0] : !dlcs.empty() ? &dlcs[0] : nullptr;
        if (!pkg) { // mod-only: the game is already on the console
            static constexpr char hex[] = "0123456789ABCDEF";
            std::string out(16, '0');
            for (int i = 0; i < 16; i++) out[15 - i] = hex[(tid >> (i * 4)) & 0xF];
            return out;
        }
        auto full = pkg->name();
        auto cut = full.substr(0, full.find_first_of("[("));
        while (!cut.empty() && (cut.back() == ' ' || cut.back() == '\t')) cut.pop_back();
        while (!cut.empty() && (cut.front() == ' ' || cut.front() == '\t')) cut.erase(0, 1);
        return cut.empty() ? full : cut;
    }
};

struct Release {
    std::vector<Game> games;
    std::vector<Option> orphans;                       // options no game claims
    std::vector<std::string> unknown;                  // packages without a title ID in the name
    std::vector<std::pair<std::string, Skip>> skipped;
};

inline auto ParsePackage(std::string_view path) -> Package {
    using namespace detail;
    const auto name = Basename(path);
    // the first run of exactly 16 hex digits
    for (size_t i = 0; i < name.size();) {
        if (!IsHex(name[i])) {
            i++;
            continue;
        }
        size_t end = i;
        while (end < name.size() && IsHex(name[end])) end++;
        if (end - i != 16) {
            i = end;
            continue;
        }
        Package pkg{std::string{path}, ParseHex(std::string_view{name}.substr(i, 16)), 0};
        // "[v65536]", "]v196608]", "[v0x10000]" after the title ID
        for (size_t v = end; v + 2 < name.size(); v++) {
            if ((name[v] != '[' && name[v] != ']') || name[v + 1] != 'v') continue;
            const auto rest = std::string_view{name}.substr(v + 2);
            const bool hex = rest.size() > 2 && rest[0] == '0' && rest[1] == 'x' && IsHex(rest[2]);
            size_t n = hex ? 2 : 0;
            uint64_t value = 0;
            while (n < rest.size() && (hex ? IsHex(rest[n]) : IsDigit(rest[n]))) {
                value = hex ? (value << 4) | ParseHex(rest.substr(n, 1)) : value * 10 + (rest[n] - '0');
                n++;
            }
            if (n > (hex ? 2u : 0u) && n < rest.size() && rest[n] == ']') {
                pkg.version = static_cast<uint32_t>(value);
                break;
            }
        }
        return pkg;
    }
    return Package{std::string{path}, std::nullopt, 0};
}

inline auto DetectLang(std::string_view name) -> std::string {
    const auto low = detail::Lower(name);
    if (detail::HasWord(low, "rus", false) || low.find("рус") != low.npos) return "ru";
    if (detail::HasWord(low, "ukr", false) || low.find("укр") != low.npos) return "uk";
    if (detail::HasWord(low, "eng", false) || low.find("англ") != low.npos) return "en";
    return {};
}

namespace detail {

struct SdSplit {
    size_t root; // how many leading parts are the option's own folder
    std::string dst;
};

// the title a file under atmosphere/contents/<TID>/ (or the old titles/) belongs to.
inline auto ModTid(std::string_view dst) -> std::optional<uint64_t> {
    const auto low = Lower(dst);
    for (const std::string_view prefix : {"atmosphere/contents/", "atmosphere/titles/"}) {
        if (low.starts_with(prefix) && low.size() > prefix.size() + 16 && low[prefix.size() + 16] == '/' &&
            IsHex16(std::string_view{low}.substr(prefix.size(), 16))) {
            // a DLC's romfs.bin (Xenoblade 2 Torna) belongs to its game
            return Package{{}, ParseHex(std::string_view{low}.substr(prefix.size(), 16)), 0}.game_tid();
        }
    }
    return std::nullopt;
}

// where the microSD tree starts in a path.
inline auto SdPath(const std::vector<std::string_view>& parts) -> std::optional<SdSplit> {
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        const auto low = Lower(parts[i]);
        if (low == "switch" || low == "atmosphere") {
            return SdSplit{i, low + "/" + Join(parts, i + 1, parts.size())};
        }
        // bare LayeredFS folder: <TID>/romfs or <TID>/exefs, without atmosphere/contents above it
        if (IsHex16(parts[i]) && i + 2 < parts.size()) {
            const auto next = Lower(parts[i + 1]);
            if (next == "romfs" || next == "exefs") {
                return SdSplit{i, "atmosphere/contents/" + Join(parts, i, parts.size())};
            }
        }
    }
    return std::nullopt;
}

// console save files. All SD_Cache.* folders of one parent are one unit; a save folder is its own.
inline auto SavePath(const std::vector<std::string_view>& parts) -> std::optional<SdSplit> {
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        if (const auto index = CacheIndex(parts[i])) {
            return SdSplit{i, std::string{CACHE_DST} + std::to_string(*index) + "/" + Join(parts, i + 1, parts.size())};
        }
        if (IsSaveDir(parts[i])) {
            return SdSplit{i + 1, std::string{SAVE_DST} + Join(parts, i + 1, parts.size())};
        }
    }
    return std::nullopt;
}

inline auto Classify(const std::vector<FilePair>& files) -> std::pair<Kind, std::set<uint64_t>> {
    for (const auto& [kind, prefix] : {std::pair{Kind::Cache, CACHE_DST}, std::pair{Kind::Save, SAVE_DST}}) {
        if (!files.empty() && std::all_of(files.begin(), files.end(), [&](auto& f) { return f.second.starts_with(prefix); })) {
            return {kind, {}};
        }
    }
    std::set<uint64_t> tids;
    bool has_nro = false;
    for (const auto& [src, dst] : files) {
        // a game's romfs holds .nro modules too (Yakuza Kiwami); a port's NRO lives under switch/
        const auto low = Lower(dst);
        has_nro |= low.ends_with(".nro") && low.starts_with("switch/");
        if (const auto tid = ModTid(dst)) {
            tids.insert(*tid);
        }
    }
    return {has_nro ? Kind::Port : !tids.empty() ? Kind::Mod : Kind::Data, tids};
}

// a mod for several games (one folder for Scarlet and Violet): keep this game's part.
inline auto ForGame(Option o, uint64_t tid, bool shared) -> Option {
    std::erase_if(o.updates, [&](const Package& u) { return u.game_tid() != tid; });
    if (!shared) {
        return o;
    }
    std::erase_if(o.files, [&](const FilePair& f) {
        const auto t = ModTid(f.second);
        return t && *t != tid && o.tids.contains(*t);
    });
    o.tids = {tid};
    o.tid_mismatch = o.incomplete = false;
    return o;
}

// files for several games in one folder (Donkey Kong Country 1-3): one option per switch/<port>/.
inline auto SplitPorts(const Option& o) -> std::vector<Option> {
    std::vector<std::pair<std::string, std::vector<FilePair>>> groups; // sorted by key
    for (const auto& f : o.files) {
        const auto parts = Split(f.second);
        const auto top = parts.size() > 2 && Lower(parts[0]) == "switch" ? Join(parts, 0, 2) : std::string{};
        auto it = std::lower_bound(groups.begin(), groups.end(), top, [](auto& g, auto& k) { return g.first < k; });
        if (it == groups.end() || it->first != top) {
            it = groups.insert(it, {top, {}});
        }
        it->second.push_back(f);
    }
    if (groups.size() < 2 || groups.front().first.empty()) {
        return {o};
    }
    std::vector<Option> out;
    for (auto& [top, files] : groups) {
        const bool nro = std::any_of(files.begin(), files.end(), [](auto& f) { return Lower(f.second).ends_with(".nro"); });
        Option part{o.key + "::" + top, top.substr(top.find('/') + 1), nro ? o.kind : Kind::Data, o.home, std::move(files), o.archive};
        part.lang = o.lang;
        part.incomplete = o.incomplete;
        out.push_back(std::move(part));
    }
    return out;
}

// index of the game whose folder holds `home` most closely; nullopt when none or two games tie.
inline auto Nearest(const std::vector<Game>& games, std::string_view home) -> std::optional<size_t> {
    std::optional<size_t> best;
    int depth = -1;
    bool tie = false;
    for (size_t i = 0; i < games.size(); i++) {
        const auto& root = games[i].root;
        if (!IsUnder(home, root)) continue;
        const int d = root.empty() ? 0 : static_cast<int>(std::count(root.begin(), root.end(), '/')) + 1;
        if (d > depth) {
            best = i, depth = d, tie = false;
        } else if (d == depth) {
            tie = true;
        }
    }
    return tie ? std::nullopt : best;
}

} // namespace detail

// `root_name`: the analysed folder's own name. A port whose NRO lies at the top of that folder
// (the user picked the port's folder itself) is copied to switch/<root_name>/.
inline auto Analyze(const std::vector<std::string>& paths, const ListArchive& list_archive = {}, std::string_view root_name = {}) -> Release {
    using namespace detail;
    Release rel;
    std::vector<Package> packages;
    std::vector<std::pair<std::string, std::vector<FilePair>>> units; // unit root -> files, in first-seen order
    std::unordered_map<std::string, size_t> unit_index;
    std::vector<std::string> archives, loose;
    const auto unit = [&](const std::string& root) -> std::vector<FilePair>& {
        const auto [it, added] = unit_index.try_emplace(root, units.size());
        if (added) units.push_back({root, {}});
        return units[it->second].second;
    };

    for (const auto& raw : paths) {
        const auto p = Normalize(raw);
        if (p.empty()) continue;
        const auto parts = Split(p);
        const auto low = Lower(p);
        if (std::any_of(parts.begin(), parts.end(), IsSkipName) ||
            EndsWithAny(low, {".exe", ".msi", ".dll", ".bat", ".cmd", ".sh", ".lnk", ".url"})) {
            rel.skipped.push_back({p, Skip::NotForConsole});
            continue;
        }
        if (EndsWithAny(low, {".!qb", ".part", ".crdownload", ".!ut", ".aria2"})) {
            rel.skipped.push_back({p, Skip::Partial});
            continue;
        }
        auto sd = SdPath(parts);
        if (!sd && !IsPackage(low) && !IsArchive(low)) sd = SavePath(parts);
        if (sd) {
            unit(Join(parts, 0, sd->root)).push_back({p, sd->dst});
        } else if (IsPackage(low)) {
            packages.push_back(ParsePackage(p));
        } else if (IsArchive(low)) {
            archives.push_back(p);
        } else {
            loose.push_back(p);
        }
    }

    // a port folder without switch/ above it (defjamx/defjamx.nro): copy it to switch/<folder>/
    std::set<std::string> package_dirs, port_dirs;
    for (const auto& pkg : packages) package_dirs.insert(Parent(pkg.path));
    for (const auto& p : loose) {
        if (Lower(p).ends_with(".nro") && !package_dirs.contains(Parent(p))) port_dirs.insert(Parent(p));
    }
    for (const auto& d : port_dirs) {
        const auto name = d.empty() ? std::string{root_name} : Basename(d);
        if (name.empty() || std::any_of(port_dirs.begin(), port_dirs.end(), [&](auto& other) { return d != other && IsUnder(d, other); })) {
            continue;
        }
        auto& files = unit(d);
        files.clear();
        for (const auto& p : loose) {
            if (IsUnder(p, d) && p != d) {
                files.push_back({p, "switch/" + name + "/" + p.substr(d.empty() ? 0 : d.size() + 1)});
            }
        }
    }

    std::vector<Option> options;
    for (auto& [root, files] : units) {
        auto [kind, tids] = Classify(files);
        const auto base = Basename(root);
        const auto name = !base.empty() ? base : !root_name.empty() ? std::string{root_name} : std::string{"SD card files"};
        Option o{root.empty() ? "." : root, name, kind, root, std::move(files), {}, std::move(tids)};
        o.lang = DetectLang(name);
        options.push_back(std::move(o));
    }
    // a file still downloading marks only the folder unit it belongs to, not its neighbours
    std::set<std::string> partial_roots;
    for (const auto& [p, why] : rel.skipped) {
        if (why != Skip::Partial) continue;
        const auto parts = Split(p);
        auto sd = SdPath(parts);
        if (!sd) sd = SavePath(parts);
        if (sd) partial_roots.insert(Join(parts, 0, sd->root));
        for (const auto& d : port_dirs) {
            if (!d.empty() && IsUnder(p, d)) partial_roots.insert(d);
        }
    }
    std::set<std::string> port_names;
    // a mod's file names by folder: an archive of bare files with the same names replaces them
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mod_names;
    for (auto& o : options) {
        o.incomplete = partial_roots.contains(o.home);
        for (const auto& [src, dst] : o.files) {
            const auto parts = Split(dst);
            if (o.kind == Kind::Port && parts.size() >= 3 && Lower(parts[0]) == "switch") {
                port_names.insert(Lower(parts[1]));
            }
            if (o.kind == Kind::Mod) {
                mod_names[o.home][Lower(Basename(dst))] = dst;
            }
        }
    }

    for (const auto& a : archives) {
        const auto members = list_archive ? list_archive(a) : std::nullopt;
        if (!members) {
            rel.skipped.push_back({a, Skip::ArchiveUnreadable});
            continue;
        }
        std::vector<FilePair> files;
        bool has_package = false;
        const auto home = Parent(a);
        for (const auto& raw : *members) {
            auto m = Normalize(raw);
            const auto parts = Split(m);
            if (IsPackage(Lower(m))) {
                has_package = true; // a package inside an archive: extract it first
            } else if (auto sd = SdPath(parts)) {
                files.push_back({m, sd->dst});
            } else if (parts.size() > 1 && port_names.contains(Lower(parts[0]))) {
                files.push_back({m, "switch/" + m}); // dusk/texture_replacements/... for the dusk port
            } else if (const auto names = mod_names.find(home); names != mod_names.end()) {
                // DQ XI: an archive with another pakchunk0-Switch_p.pak for the mod beside it
                if (const auto it = names->second.find(Lower(parts.back())); it != names->second.end()) {
                    files.push_back({m, it->second});
                }
            }
        }
        if (files.empty()) {
            rel.skipped.push_back({a, has_package ? Skip::PackageInArchive : Skip::UnknownLayout});
            continue;
        }
        auto [kind, tids] = Classify(files);
        auto stem = Basename(a);
        stem.erase(stem.rfind('.'));
        Option o{a, stem, kind, home, std::move(files), a, std::move(tids)};
        o.lang = DetectLang(stem);
        if (o.lang.empty()) o.lang = DetectLang(Basename(home));
        options.push_back(std::move(o));
    }

    // a mod (or a save) in a folder below another mod, or an archive beside a mod's loose files, is its add-on
    for (auto& o : options) {
        if (o.kind != Kind::Mod && o.kind != Kind::Save) continue;
        const Option* above = nullptr;
        for (const auto& x : options) {
            if (x.kind != Kind::Mod || &x == &o) continue;
            const bool holds = (x.home != o.home && IsUnder(o.home, x.home)) ||
                (x.home == o.home && x.archive.empty() && !o.archive.empty());
            if (holds && (!above || x.home.size() > above->home.size())) above = &x;
        }
        if (above) o.parent = above->key;
    }

    // an update in a mod's folder belongs to that mod and to the add-on mods in folders below it
    std::set<std::string> base_dirs, mod_homes;
    for (const auto& pkg : packages) {
        if (pkg.type() == Package::Type::Base) base_dirs.insert(Parent(pkg.path));
    }
    for (const auto& o : options) {
        if (o.kind == Kind::Mod) mod_homes.insert(o.home);
    }
    std::vector<Game> games;
    for (const auto& pkg : packages) {
        if (!pkg.tid) {
            rel.unknown.push_back(pkg.path);
            continue;
        }
        const auto d = Parent(pkg.path);
        if (pkg.type() == Package::Type::Update && !base_dirs.contains(d) && mod_homes.contains(d)) {
            for (auto& o : options) {
                if (o.kind == Kind::Mod && IsUnder(o.home, d)) o.updates.push_back(pkg);
            }
            continue;
        }
        auto g = std::find_if(games.begin(), games.end(), [&](const Game& x) { return x.tid == pkg.game_tid(); });
        if (g == games.end()) {
            g = games.insert(games.end(), Game{pkg.game_tid(), d});
        }
        if (pkg.type() == Package::Type::Base) {
            g->base = pkg;
            g->root = d;
        } else {
            (pkg.type() == Package::Type::Update ? g->updates : g->dlcs).push_back(pkg);
        }
    }
    // several archives in one mod folder: name them by file; a lone archive takes the folder name
    for (auto& o : options) {
        if (!o.archive.empty() && !o.home.empty() && !base_dirs.contains(o.home) &&
            std::count_if(options.begin(), options.end(), [&](const Option& x) { return x.home == o.home; }) == 1) {
            o.name = Basename(o.home);
            if (o.lang.empty()) o.lang = DetectLang(o.name);
        }
    }

    std::stable_sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
        return std::tie(a.root, a.tid) < std::tie(b.root, b.tid);
    });
    for (auto& o : options) {
        if (o.kind == Kind::Mod) {
            const auto targets = std::count_if(games.begin(), games.end(), [&](const Game& g) { return o.tids.contains(g.tid); });
            if (targets) {
                for (auto& g : games) {
                    if (o.tids.contains(g.tid)) g.options.push_back(ForGame(o, g.tid, targets > 1));
                }
                continue;
            }
        }
        const auto near = Nearest(games, o.home);
        if (!near && o.kind == Kind::Mod) {
            // a mod-only release (the game is already on the console): one entry per title
            for (const auto tid : o.tids) {
                games.push_back(Game{tid, o.home});
                games.back().options.push_back(ForGame(o, tid, o.tids.size() > 1));
            }
        } else if (!near) {
            for (auto& part : SplitPorts(o)) rel.orphans.push_back(std::move(part));
        } else {
            o.tid_mismatch = o.kind == Kind::Mod;
            games[*near].options.push_back(o);
        }
    }

    for (auto& g : games) {
        // copy order: untagged files first, then English, then other languages, then "Update"
        // layers; mods after them, add-ons last
        const auto key = [](const Option& o) {
            return std::tuple{o.kind == Kind::Mod, !o.parent.empty(), IsUpdateWord(o.name), !o.lang.empty(),
                              o.lang != "en", static_cast<int>(o.kind), std::string_view{o.key}};
        };
        std::stable_sort(g.options.begin(), g.options.end(), [&](const Option& a, const Option& b) { return key(a) < key(b); });
        std::stable_sort(g.updates.begin(), g.updates.end(), [](const Package& a, const Package& b) { return a.version < b.version; });
    }
    rel.games = std::move(games);
    return rel;
}

} // namespace sphaira::release
