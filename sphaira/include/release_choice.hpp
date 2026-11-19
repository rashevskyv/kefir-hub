#pragma once

// What to install from an analysed release (release_plan.hpp): the default choice, the packages
// that choice needs, and each chosen option as a bundle for the install queue. A port of DBI
// Backend Qt's default_choice()/plan() and of src/release_choice.py, src/release_bundle.py.
// Pure logic, host-tested in tests/test_release_plan.cpp.

#include "release_plan.hpp"
#include "save_bundle_rules.hpp"
#include "tar_stream_layout.hpp"

#include <map>

namespace sphaira::release {

using Keys = std::set<std::string>;
// option keys per game title ID; orphan options (shared by every game) are under ORPHANS.
using Choice = std::map<uint64_t, Keys>;
inline constexpr uint64_t ORPHANS = 0;

// Option keys to install by default (pre-ticked in the list).
// Ports and their files: every option without a language in its name ("Optional ..." left out),
// plus the ones in `lang`, else in English, else (when nothing untagged is there) the first
// language found; an untagged option whose files the chosen language set writes in full is
// dropped. Mods: only when the game has exactly one and its title ID matches; add-ons stay off.
// Options still downloading are never chosen.
inline auto DefaultChoice(const Game& game, std::string_view lang = "en") -> Keys {
    using namespace detail;
    std::vector<const Option*> untagged, tagged, named;
    for (const auto& o : game.options) {
        if (o.kind == Kind::Mod || o.incomplete || !o.parent.empty()) continue;
        if (IsModWord(o.name)) {
            named.push_back(&o); // port files named "... Mod" are alternatives, like LayeredFS mods
        } else if (!o.lang.empty()) {
            tagged.push_back(&o);
        } else if (Lower(o.name).find("optional") == std::string::npos) {
            untagged.push_back(&o);
        }
    }
    const auto in_lang = [&](std::string_view l) {
        std::vector<const Option*> out;
        std::copy_if(tagged.begin(), tagged.end(), std::back_inserter(out), [&](auto o) { return o->lang == l; });
        return out;
    };
    auto pick = in_lang(lang);
    if (pick.empty()) pick = in_lang("en");
    if (pick.empty() && !tagged.empty() &&
        std::none_of(untagged.begin(), untagged.end(), [](auto o) { return o->kind == Kind::Data; })) {
        pick = in_lang(tagged[0]->lang);
    }
    Keys covered, chosen;
    for (const auto o : pick) {
        chosen.insert(o->key);
        for (const auto& f : o->files) covered.insert(Lower(f.second));
    }
    for (const auto o : untagged) {
        const bool all_covered = !pick.empty() &&
            std::all_of(o->files.begin(), o->files.end(), [&](auto& f) { return covered.contains(Lower(f.second)); });
        if (IsUpdateWord(o->name) || !all_covered) chosen.insert(o->key);
    }
    if (named.size() == 1) {
        const auto& l = named[0]->lang;
        const bool own = std::any_of(named.begin(), named.end(), [&](auto o) { return o->lang == lang; });
        if (l.empty() || l == lang || (!own && l == "en")) chosen.insert(named[0]->key);
    }
    const Option* mod = nullptr;
    size_t mods = 0;
    for (const auto& o : game.options) {
        if (o.kind == Kind::Mod && o.parent.empty()) mod = &o, mods++;
    }
    if (mods == 1 && !mod->incomplete && !mod->tid_mismatch) chosen.insert(mod->key);
    return chosen;
}

struct Conflict {
    enum class Type { DifferentUpdates, Incomplete, TidMismatch, NeedsParent } type;
    std::string option;
};

struct SdFile {
    const Option* option;
    std::string src;
    std::string dst;
};

struct Plan {
    std::vector<Package> packages;  // install order: base, one update, DLC
    std::vector<SdFile> sd_files;   // copy order; a file a later option writes again is left out
    std::vector<Conflict> conflicts;
};

inline auto MakePlan(const Game& game, const Keys& chosen) -> Plan {
    Plan plan;
    std::map<uint32_t, Package> mod_updates;
    for (const auto& o : game.options) {
        if (!chosen.contains(o.key)) continue;
        for (const auto& u : o.updates) mod_updates.insert_or_assign(u.version, u);
        if (o.incomplete) plan.conflicts.push_back({Conflict::Type::Incomplete, o.name});
        if (o.tid_mismatch) plan.conflicts.push_back({Conflict::Type::TidMismatch, o.name});
        if (!o.parent.empty() && !chosen.contains(o.parent)) plan.conflicts.push_back({Conflict::Type::NeedsParent, o.name});
    }
    if (mod_updates.size() > 1) plan.conflicts.insert(plan.conflicts.begin(), {Conflict::Type::DifferentUpdates, {}});

    if (game.base) plan.packages.push_back(*game.base);
    if (!mod_updates.empty()) {
        plan.packages.push_back(mod_updates.begin()->second); // the oldest: a newer update breaks the mod
    } else if (!game.updates.empty()) {
        plan.packages.push_back(game.updates.back());
    }
    plan.packages.insert(plan.packages.end(), game.dlcs.begin(), game.dlcs.end());

    std::map<std::string, size_t> seen;
    std::vector<bool> dead;
    for (const auto& o : game.options) {
        if (!chosen.contains(o.key)) continue;
        for (const auto& [src, dst] : o.files) {
            const auto [it, added] = seen.try_emplace(detail::Lower(dst), plan.sd_files.size());
            if (!added) {
                dead[it->second] = true; // a later option writes the same file
                it->second = plan.sd_files.size();
            }
            plan.sd_files.push_back({&o, src, dst});
            dead.push_back(false);
        }
    }
    size_t i = 0;
    std::erase_if(plan.sd_files, [&](const SdFile&) { return dead[i++]; });
    return plan;
}

// true when the release has anything besides packages (ports, data, mods).
inline auto IsSmart(const Release& rel) -> bool {
    return !rel.orphans.empty() || std::any_of(rel.games.begin(), rel.games.end(), [](auto& g) { return !g.options.empty(); });
}

inline auto Defaults(const Release& rel, std::string_view lang = "en") -> Choice {
    Choice choice;
    for (const auto& g : rel.games) choice[g.tid] = DefaultChoice(g, lang);
    // shared ports: all of them; a console save without its game has no title id to go to
    auto& shared = choice[ORPHANS];
    for (const auto& o : rel.orphans) {
        if (o.kind != Kind::Mod && o.kind != Kind::Cache && o.kind != Kind::Save && !o.incomplete) shared.insert(o.key);
    }
    return choice;
}

inline auto Options(const Release& rel, uint64_t tid) -> const std::vector<Option>& {
    static const std::vector<Option> none;
    if (tid == ORPHANS) return rel.orphans;
    const auto g = std::find_if(rel.games.begin(), rel.games.end(), [&](auto& x) { return x.tid == tid; });
    return g == rel.games.end() ? none : g->options;
}

// tick or untick one option. An add-on pulls its mod in; unticking a mod drops its add-ons.
inline void Toggle(const Release& rel, Choice& choice, uint64_t tid, const std::string& key, bool on) {
    auto& chosen = choice[tid];
    for (const auto& o : Options(rel, tid)) {
        if (on && o.key == key && !o.parent.empty()) chosen.insert(o.parent);
        if (!on && o.parent == key) chosen.erase(o.key);
    }
    on ? void(chosen.insert(key)) : void(chosen.erase(key));
}

inline auto Chosen(const Choice& choice, uint64_t tid) -> const Keys& {
    static const Keys none;
    const auto it = choice.find(tid);
    return it == choice.end() ? none : it->second;
}

// packages to queue, in install order: per game base, the update the chosen mods need, DLC;
// then packages without a title ID in the name (the console reads their header).
inline auto Packages(const Release& rel, const Choice& choice) -> std::vector<std::string> {
    std::vector<std::string> out;
    const auto add = [&](const std::string& p) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
    for (const auto& g : rel.games) {
        for (const auto& p : MakePlan(g, Chosen(choice, g.tid)).packages) add(p.path);
    }
    for (const auto& u : rel.unknown) add(u);
    return out;
}

// the choice cannot be installed: two mods of a game need different updates, or a ticked option
// is still downloading. A mod for another title ID is only a warning: the user may mean it.
inline auto Blocking(const Release& rel, const Choice& choice) -> bool {
    const auto bad = [&](uint64_t tid) {
        std::set<uint32_t> versions;
        for (const auto& o : Options(rel, tid)) {
            if (!Chosen(choice, tid).contains(o.key)) continue;
            if (o.incomplete) return true;
            for (const auto& u : o.updates) versions.insert(u.version);
        }
        return versions.size() > 1;
    };
    return bad(ORPHANS) || std::any_of(rel.games.begin(), rel.games.end(), [&](auto& g) { return bad(g.tid); });
}

// ---- bundles: one queue entry per chosen option (sd_bundle_rules.hpp, save_bundle_rules.hpp) ----

// (archive entry, SD path) pairs -> "archive prefix<TAB>SD prefix" rules. Equal trailing path
// parts are cut off, so a whole folder maps with one rule; a renamed file keeps its exact rule.
inline auto ArchiveRules(const std::vector<FilePair>& files) -> std::vector<sd_bundle::Rule> {
    std::vector<sd_bundle::Rule> rules;
    for (const auto& [src, dst] : files) {
        const auto clean = detail::Normalize(src);
        const auto s = detail::Split(clean);
        const auto d = detail::Split(dst);
        size_t n = 0;
        // keep the first part of the entry: a rule for "" would also extract the archive's readme
        while (n + 1 < std::min(s.size(), d.size()) && s[s.size() - 1 - n] == d[d.size() - 1 - n]) n++;
        sd_bundle::Rule rule{clean, dst};
        if (n) rule = {detail::Join(s, 0, s.size() - n) + "/", detail::Join(d, 0, d.size() - n) + "/"};
        if (std::none_of(rules.begin(), rules.end(), [&](auto& r) { return r.src == rule.src && r.dst == rule.dst; })) {
            rules.push_back(std::move(rule));
        }
    }
    return rules;
}

// the queue name of an option. A console save carries the game's title id outside [brackets]
// (the queue reads [<id>] as an installed package's id); empty when it has no game to go to.
inline auto BundleName(std::string_view game, uint64_t tid, const Option& o) -> std::string {
    const auto safe = [](std::string_view name) {
        std::string out{name};
        for (auto& c : out) {
            if (std::string_view{"\\/:*?\"<>|"}.find(c) != std::string_view::npos) c = '_';
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        while (!out.empty() && out.front() == ' ') out.erase(0, 1);
        return out.empty() ? std::string{"files"} : out;
    };
    const auto base = safe(game) + " - " + safe(o.name);
    if (!o.archive.empty() || (o.kind != Kind::Cache && o.kind != Kind::Save)) {
        return base + std::string{sd_bundle::SUFFIX};
    }
    if (!tid) return {};
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string id(16, '0');
    for (int i = 0; i < 16; i++) id[15 - i] = hex[(tid >> (i * 4)) & 0xF];
    return base + " " + id + std::string{o.kind == Kind::Cache ? save_bundle::CACHE_SUFFIX : save_bundle::SAVE_SUFFIX};
}

// the members of a loose-file option's tar, sources relative to the release folder. A cache
// option starts with ".kefir/sizes": what each storage must hold, every file in whole blocks.
inline auto BundleMembers(const Option& o, const std::function<int64_t(const std::string& src)>& size_of) -> std::vector<tar_stream::Member> {
    std::vector<tar_stream::Member> out;
    if (o.kind == Kind::Cache) {
        std::vector<std::tuple<uint32_t, std::string, std::string>> files; // index, name, source
        for (const auto& [src, dst] : o.files) {
            const auto name = dst.substr(CACHE_DST.size());
            files.emplace_back(std::stoul(name.substr(0, name.find('/'))), name, src);
        }
        std::sort(files.begin(), files.end());
        std::map<uint32_t, int64_t> need;
        for (const auto& [index, name, src] : files) {
            const auto n = size_of(src);
            need[index] += (n + save_bundle::BLOCK - 1) / save_bundle::BLOCK * save_bundle::BLOCK;
            out.push_back({name, src, {}, n});
        }
        std::string sizes;
        for (const auto& [index, bytes] : need) sizes += std::to_string(index) + "\t" + std::to_string(bytes) + "\n";
        const auto size = static_cast<int64_t>(sizes.size());
        out.insert(out.begin(), {std::string{save_bundle::SIZES}, {}, std::move(sizes), size});
        return out;
    }
    for (const auto& [src, dst] : o.files) {
        out.push_back({o.kind == Kind::Save ? dst.substr(SAVE_DST.size()) : dst, src, {}, size_of(src)});
    }
    return out;
}

// true when `folder`/`src` on the microSD is the file "/dst" itself (the user picked a folder
// that is already in place, like /switch/<port>): writing it would delete the source first.
inline auto IsSamePath(std::string_view folder, std::string_view src, std::string_view dst) -> bool {
    auto full = std::string{folder};
    while (!full.empty() && full.back() == '/') full.pop_back();
    full += "/" + std::string{src};
    return detail::Lower(full) == detail::Lower("/" + std::string{dst});
}

} // namespace sphaira::release
