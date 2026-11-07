#pragma once

// ROM library downloader: the pure part. Collections are archive.org items the user adds, one per
// console folder under /roms/<console>/ (the folder names Tico, HaulNX and RetroArch's scanner share).
// The Hub ships no item ids. No libnx here: tests/test_rom_library.cpp runs it on the host.

#include "bios_plan.hpp" // EncodePath
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::rom {

inline constexpr const char* ROMS_ROOT = "/roms/";
inline constexpr const char* COLLECTIONS_PATH = "/config/kefir/roms.txt";

struct Console {
    const char* folder;
    const char* name;
    const char* terms; // what archive.org calls the system: the title/subject words of the search
};

// the HaulNX folder table (its wiki, Home: "HaulNX folder names"), the systems Switch emulators run
inline const std::vector<Console>& Consoles() {
    static const std::vector<Console> c{
        {"nes", "Nintendo - NES", "nes OR famicom OR \"nintendo entertainment system\""},
        {"fds", "Nintendo - Famicom Disk System", "\"famicom disk system\" OR fds"},
        {"snes", "Nintendo - SNES", "snes OR \"super nintendo\" OR \"super famicom\""},
        {"n64", "Nintendo - 64", "n64 OR \"nintendo 64\""},
        {"gb", "Nintendo - Game Boy", "\"game boy\" OR gameboy"},
        {"gbc", "Nintendo - Game Boy Color", "\"game boy color\" OR gbc"},
        {"gba", "Nintendo - Game Boy Advance", "\"game boy advance\" OR gba"},
        {"nds", "Nintendo - DS", "\"nintendo ds\" OR nds"},
        {"3ds", "Nintendo - 3DS", "\"nintendo 3ds\" OR 3ds"},
        {"gc", "Nintendo - GameCube", "gamecube"},
        {"wii", "Nintendo - Wii", "wii"},
        {"wiiu", "Nintendo - Wii U", "\"wii u\" OR wiiu"},
        {"virtual-boy", "Nintendo - Virtual Boy", "\"virtual boy\""},
        {"pokemon-mini", "Nintendo - Pokemon Mini", "\"pokemon mini\""},
        {"game-and-watch", "Nintendo - Game & Watch", "\"game and watch\" OR \"game & watch\""},
        {"sg-1000", "Sega - SG-1000", "sg-1000 OR sg1000"},
        {"master-system", "Sega - Master System", "\"master system\""},
        {"game-gear", "Sega - Game Gear", "\"game gear\""},
        {"genesis", "Sega - Genesis / Mega Drive", "genesis OR \"mega drive\" OR megadrive"},
        {"sega-cd", "Sega - CD", "\"sega cd\" OR \"mega cd\""},
        {"sega-32x", "Sega - 32X", "32x"},
        {"saturn", "Sega - Saturn", "saturn"},
        {"dc", "Sega - Dreamcast", "dreamcast"},
        {"naomi", "Sega - NAOMI", "naomi"},
        {"atomiswave", "Sammy - Atomiswave", "atomiswave"},
        {"psx", "Sony - PlayStation", "psx OR ps1 OR \"playstation 1\" OR psone"},
        {"ps2", "Sony - PlayStation 2", "ps2 OR \"playstation 2\""},
        {"psp", "Sony - PSP", "psp OR \"playstation portable\""},
        {"vita", "Sony - PS Vita", "\"ps vita\" OR psvita"},
        {"pc-engine", "NEC - PC Engine / TurboGrafx-16", "\"pc engine\" OR turbografx"},
        {"pc-engine-cd", "NEC - PC Engine CD", "\"pc engine cd\" OR \"turbografx cd\""},
        {"supergrafx", "NEC - SuperGrafx", "supergrafx"},
        {"pc-fx", "NEC - PC-FX", "pc-fx OR pcfx"},
        {"neo-geo", "SNK - Neo Geo", "\"neo geo\" OR neogeo"},
        {"neo-geo-cd", "SNK - Neo Geo CD", "\"neo geo cd\""},
        {"neo-geo-pocket", "SNK - Neo Geo Pocket", "\"neo geo pocket\""},
        {"neo-geo-pocket-color", "SNK - Neo Geo Pocket Color", "\"neo geo pocket color\""},
        {"atari-2600", "Atari - 2600", "\"atari 2600\""},
        {"atari-5200", "Atari - 5200", "\"atari 5200\""},
        {"atari-7800", "Atari - 7800", "\"atari 7800\""},
        {"atari-lynx", "Atari - Lynx", "lynx"},
        {"atari-jaguar", "Atari - Jaguar", "jaguar"},
        {"wonderswan", "Bandai - WonderSwan", "wonderswan"},
        {"wonderswan-color", "Bandai - WonderSwan Color", "\"wonderswan color\""},
        {"colecovision", "Coleco - ColecoVision", "colecovision"},
        {"intellivision", "Mattel - Intellivision", "intellivision"},
        {"odyssey2", "Magnavox - Odyssey 2", "odyssey OR videopac"},
        {"vectrex", "GCE - Vectrex", "vectrex"},
        {"channel-f", "Fairchild - Channel F", "\"channel f\""},
        {"supervision", "Watara - Supervision", "supervision"},
        {"3do", "The 3DO Company - 3DO", "3do"},
        {"cd-i", "Philips - CD-i", "cd-i OR cdi"},
        {"arcade", "Arcade (MAME)", "mame OR arcade"},
        {"fbneo", "Arcade (FinalBurn Neo)", "fbneo OR \"finalburn neo\" OR fba"},
        {"amiga", "Commodore - Amiga", "amiga"},
        {"zx-spectrum", "Sinclair - ZX Spectrum", "\"zx spectrum\""},
        {"chip8", "CHIP-8", "chip-8 OR chip8"},
        {"pico8", "PICO-8", "pico-8 OR pico8"},
        {"flash", "Adobe Flash", "flash swf"},
    };
    return c;
}

inline const Console* FindConsole(std::string_view folder) {
    for (const auto& c : Consoles()) {
        if (folder == c.folder) return &c;
    }
    return nullptr;
}

// the archive.org search for a console. Items that need an archive.org login are left out: the Hub cannot download them.
//   no words ("Popular"): the sets of this system only, most downloaded first: the system is named in the title, the title calls it
//     a set or a collection, and the title names no other system of the table nor an emulator, a BIOS pack or a console port;
//   words (what the user typed): the items whose title or subject names the system and that match the words.
inline std::string SearchQuery(std::string_view folder, std::string_view words) {
    std::string q = "mediatype:software AND NOT access-restricted-item:true";
    if (const auto c = FindConsole(folder)) {
        if (words.empty()) {
            q += std::string{" AND title:("} + c->terms + ")"
                 " AND (title:(roms OR romset OR \"rom set\" OR games OR collection OR pack OR set OR library OR archive OR compilation) OR subject:(roms OR romset))"
                 " AND NOT title:(android OR cia OR bios OR emulator OR emulators OR \"virtual console\"";
            for (const auto& other : Consoles()) {
                // a system named inside this one's own name (neo-geo in neo-geo-pocket) is not another system
                if (!folder.starts_with(other.folder)) q += std::string{" OR \""} + other.folder + "\"";
            }
            q += ")";
        } else {
            q += std::string{" AND (title:("} + c->terms + ") OR subject:(" + c->terms + "))";
        }
    }
    if (!words.empty()) {
        q += " AND (" + std::string{words} + ")";
    }
    return q;
}

inline constexpr int SEARCH_ROWS = 50;

inline std::string SearchUrl(std::string_view query) {
    return "https://archive.org/advancedsearch.php?q=" + bios::EncodePath(query)
        + "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=downloads&fl%5B%5D=item_size"
        + "&rows=" + std::to_string(SEARCH_ROWS) + "&sort%5B%5D=downloads+desc&output=json";
}

struct SearchHit {
    std::string identifier;
    std::string title;
    std::uint64_t downloads{};
    std::uint64_t size{};
};

inline const char* ConsoleName(std::string_view folder) {
    const auto c = FindConsole(folder);
    return c ? c->name : "";
}

struct Collection {
    std::string console; // folder under /roms/
    std::string item;    // archive.org identifier
    std::string label;   // the item's title, or what the user typed
};

// roms.txt: one collection a line, "console<TAB>item<TAB>label"; other lines are ignored
inline std::vector<Collection> Parse(std::string_view text) {
    std::vector<Collection> out;
    size_t pos = 0;
    while (pos < text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto t1 = line.find('\t');
        if (t1 == std::string_view::npos) continue;
        const auto t2 = line.find('\t', t1 + 1);
        Collection c;
        c.console = std::string{line.substr(0, t1)};
        c.item = std::string{line.substr(t1 + 1, t2 == std::string_view::npos ? std::string_view::npos : t2 - t1 - 1)};
        c.label = t2 == std::string_view::npos ? c.item : std::string{line.substr(t2 + 1)};
        if (!c.console.empty() && !c.item.empty()) out.push_back(std::move(c));
    }
    return out;
}

inline std::string Format(const std::vector<Collection>& list) {
    std::string out;
    for (const auto& c : list) {
        out += c.console + "\t" + c.item + "\t" + c.label + "\n";
    }
    return out;
}

// "archive.org/details/<id>" or a download url pasted from a browser -> "<id>"; a bare id stays
inline std::string ItemFromInput(std::string_view s) {
    while (!s.empty() && (s.back() == '/' || s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    for (std::string_view key : {"/details/", "/download/", "/metadata/"}) {
        if (const auto at = s.find(key); at != std::string_view::npos) {
            s = s.substr(at + key.size());
            if (const auto slash = s.find('/'); slash != std::string_view::npos) s = s.substr(0, slash);
            break;
        }
    }
    return std::string{s};
}

struct RemoteFile {
    std::string name; // may hold a folder inside the item
    std::string md5;
    std::uint64_t size{};
};

// the files of an item worth offering: the uploader's own files, not archive.org's metadata or derivatives
inline bool IsContent(std::string_view source, std::string_view format, std::uint64_t size) {
    return source == "original" && format != "Metadata" && size > 0;
}

inline std::string MetadataUrl(std::string_view item) {
    return "https://archive.org/metadata/" + std::string{item};
}

inline std::string DownloadUrl(std::string_view item, std::string_view name) {
    return "https://archive.org/download/" + std::string{item} + "/" + bios::EncodePath(name);
}

inline std::string DestPath(std::string_view console, std::string_view name) {
    const auto slash = name.rfind('/');
    const auto base = slash == std::string_view::npos ? name : name.substr(slash + 1);
    return std::string{ROMS_ROOT} + std::string{console} + "/" + std::string{base};
}

// ---- a game's screenshot: github.com/libretro-thumbnails, one repository per system, named by the No-Intro name ----

inline const char* ThumbRepo(std::string_view folder) {
    struct Repo { std::string_view folder; const char* repo; };
    static constexpr Repo REPOS[] = {
        {"nes", "Nintendo_-_Nintendo_Entertainment_System"}, {"snes", "Nintendo_-_Super_Nintendo_Entertainment_System"},
        {"n64", "Nintendo_-_Nintendo_64"}, {"gb", "Nintendo_-_Game_Boy"}, {"gbc", "Nintendo_-_Game_Boy_Color"},
        {"gba", "Nintendo_-_Game_Boy_Advance"}, {"genesis", "Sega_-_Mega_Drive_-_Genesis"},
        {"master-system", "Sega_-_Master_System_-_Mark_III"}, {"game-gear", "Sega_-_Game_Gear"},
    };
    for (const auto& r : REPOS) {
        if (r.folder == folder) return r.repo;
    }
    return "";
}

// the file name without its folder and extension; & * / : ` < > ? \ | " are written as _ in a thumbnail name
inline std::string ThumbName(std::string_view file) {
    const auto slash = file.rfind('/');
    if (slash != std::string_view::npos) file = file.substr(slash + 1);
    const auto dot = file.rfind('.');
    if (dot != std::string_view::npos && dot > 0 && file.size() - dot <= 5) file = file.substr(0, dot);
    std::string out{file};
    for (auto& c : out) {
        if (std::string_view{"&*/:`<>?\\|\""}.find(c) != std::string_view::npos) c = '_';
    }
    return out;
}

// empty for a system without a thumbnail repository
inline std::string ThumbUrl(std::string_view folder, std::string_view file) {
    const std::string_view repo = ThumbRepo(folder);
    if (repo.empty()) return {};
    return "https://raw.githubusercontent.com/libretro-thumbnails/" + std::string{repo} + "/master/Named_Snaps/" + bios::EncodePath(ThumbName(file) + ".png");
}

// ---- the picture of a game by a loose name ----
// A set names its files "Aladdin (U).smc"; the picture server names them "Disney's Aladdin (USA).png". The server lists a
// folder of every system (one page, cached on the card), and the name of a file is matched to a name on that list by its words.

// the folder of a system on thumbnails.libretro.com; kind is "Named_Boxarts" or "Named_Snaps". Empty for a system without pictures.
inline std::string ThumbDirUrl(std::string_view folder, std::string_view kind) {
    std::string system{ThumbRepo(folder)};
    if (system.empty()) return {};
    for (size_t at; (at = system.find("_-_")) != std::string::npos;) system.replace(at, 3, " - ");
    for (auto& c : system) if (c == '_') c = ' ';
    return "https://thumbnails.libretro.com/" + bios::EncodePath(system) + "/" + std::string{kind} + "/";
}

inline std::string UrlDecode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        const auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// the names (no ".png") on an Apache folder page: <a href="Aladdin%20(USA).png">
inline std::vector<std::string> ParseThumbIndex(std::string_view html) {
    std::vector<std::string> out;
    constexpr std::string_view key = "href=\"";
    for (size_t at = html.find(key); at != std::string_view::npos; at = html.find(key, at)) {
        at += key.size();
        const auto end = html.find('"', at);
        if (end == std::string_view::npos) break;
        const auto href = html.substr(at, end - at);
        if (href.size() > 4 && href.substr(href.size() - 4) == ".png" && href.front() != '?' && href.front() != '/') {
            out.push_back(UrlDecode(href.substr(0, href.size() - 4)));
        }
        at = end;
    }
    return out;
}

namespace detail {

struct ThumbKey {
    std::vector<std::string> words; // lowercase words of the title, without "the"
    std::vector<std::string> tags;  // lowercase text of each (...) group
};

inline ThumbKey ThumbKeyOf(std::string_view name) {
    ThumbKey key;
    const auto cut = name.find_first_of("([");
    std::string word;
    const auto flush = [&]() { if (!word.empty() && word != "the") key.words.push_back(word); word.clear(); };
    for (const char raw : name.substr(0, cut)) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw + 32) : raw;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) word += c; else if (c != '\'') flush();
    }
    flush();
    for (size_t at = name.find('('); at != std::string_view::npos; at = name.find('(', at)) {
        const auto end = name.find(')', at);
        if (end == std::string_view::npos) break;
        key.tags.emplace_back(name.substr(at + 1, end - at - 1));
        for (auto& c : key.tags.back()) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
        at = end;
    }
    return key;
}

// the regions a name asks for, in the order of preference; the one-letter codes of GoodTools and the words of No-Intro
inline std::vector<std::string> WantedRegions(const ThumbKey& key) {
    std::vector<std::string> out;
    const auto add = [&](std::string r) { if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(std::move(r)); };
    for (const auto& tag : key.tags) {
        if (tag == "u" || tag == "ue" || tag == "ju" || tag == "usa") add("usa");
        if (tag == "e" || tag == "ue" || tag == "europe") add("europe");
        if (tag == "j" || tag == "ju" || tag == "japan") add("japan");
        if (tag == "w" || tag == "world") add("world");
    }
    for (std::string r : {"usa", "world", "europe", "japan"}) add(r);
    return out;
}

} // namespace detail

// the name on the list that is the same game as `file` (a path or a file name), or empty. All words of the file name
// must be in the listed name ("Aladdin" is in "Disney's Aladdin"); the fewest extra words win, then a release that is not
// a beta or a demo, then the region the file name asks for (U = USA), else USA, World, Europe, Japan.
inline std::string PickThumb(const std::vector<std::string>& names, std::string_view file) {
    const auto slash = file.rfind('/');
    if (slash != std::string_view::npos) file = file.substr(slash + 1);
    const auto dot = file.rfind('.');
    if (dot != std::string_view::npos && dot > 0 && file.size() - dot <= 5) file = file.substr(0, dot);
    const auto want = detail::ThumbKeyOf(file);
    if (want.words.empty()) return {};
    const auto regions = detail::WantedRegions(want);

    const std::string* best = nullptr;
    size_t best_score[4]{};
    for (const auto& name : names) {
        const auto cand = detail::ThumbKeyOf(name);
        size_t extra = 0;
        for (const auto& w : cand.words) extra += std::find(want.words.begin(), want.words.end(), w) == want.words.end();
        const bool all_in = std::all_of(want.words.begin(), want.words.end(), [&](const std::string& w) {
            return std::find(cand.words.begin(), cand.words.end(), w) != cand.words.end();
        });
        if (!all_in || extra > 3) continue;
        size_t odd = 0, region = regions.size();
        for (const auto& tag : cand.tags) {
            for (std::string_view bad : {"beta", "proto", "demo", "sample", "unl", "pirate", "aftermarket", "alt", "kiosk"}) {
                odd += tag.find(bad) != std::string::npos;
            }
            for (size_t i = 0; i < regions.size(); i++) {
                if (tag.find(regions[i]) != std::string::npos) region = std::min(region, i);
            }
        }
        const size_t score[4]{extra, odd, region, name.size()};
        if (!best || std::lexicographical_compare(score, score + 4, best_score, best_score + 4)) {
            best = &name;
            std::copy(score, score + 4, best_score);
        }
    }
    return best ? *best : std::string{};
}

// ---- inside a whole-set archive (zip, 7z, rar) ----
// archive.org lists the files of an archive at <download url>/ and serves each one on its own, so one ROM
// of a 300 MB set is fetched without the rest.

inline std::string Lower(std::string_view s) {
    std::string out{s};
    for (auto& c : out) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    return out;
}

inline bool IsArchiveName(std::string_view name) {
    const auto n = Lower(name);
    for (std::string_view ext : {".zip", ".7z", ".rar"}) {
        if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) return true;
    }
    return false;
}

// An archive is opened (its files listed, one picked) when it is a set: 32 MB or more, or 1 MB or more in an item that holds only a few
// archives (a set of a console as one or two zips). A catalogue of many small archives (one game each) downloads them as they are.
inline constexpr std::uint64_t LOOK_INSIDE_MIN = 32ull * 1024 * 1024;
inline constexpr std::uint64_t LOOK_INSIDE_SMALL_MIN = 1024ull * 1024;
inline constexpr size_t LOOK_INSIDE_FEW_ARCHIVES = 20;

inline bool LookInside(std::string_view name, std::uint64_t size, size_t archives_in_item) {
    if (!IsArchiveName(name)) return false;
    return size >= LOOK_INSIDE_MIN || (size >= LOOK_INSIDE_SMALL_MIN && archives_in_item <= LOOK_INSIDE_FEW_ARCHIVES);
}

inline std::string ArchiveListUrl(std::string_view item, std::string_view name) {
    return DownloadUrl(item, name) + "/";
}

struct ArchiveEntry {
    std::string name; // the path inside the archive
    std::string url;  // serves this one file
    std::uint64_t size{};
};

inline std::string HtmlUnescape(std::string_view s) {
    struct Entity { std::string_view text; char ch; };
    static constexpr Entity ENTITIES[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''}, {"&#039;", '\''}};
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '&') {
            for (const auto& e : ENTITIES) {
                if (s.substr(i, e.text.size()) == e.text) {
                    c = e.ch;
                    i += e.text.size() - 1;
                    break;
                }
            }
        }
        out += c;
    }
    return out;
}

// the "View Archive" page: rows "<tr><td><a href="//archive.org/download/..">name</a><td><td>date<td id="size">bytes</tr>";
// a row without a size (a folder) is skipped
inline std::vector<ArchiveEntry> ParseArchiveListing(std::string_view html) {
    constexpr std::string_view ROW = "<tr><td><a href=\"";
    std::vector<ArchiveEntry> out;
    size_t pos = 0;
    while ((pos = html.find(ROW, pos)) != std::string_view::npos) {
        pos += ROW.size();
        const auto href_end = html.find('"', pos);
        const auto row_end = html.find("</tr>", pos);
        if (href_end == std::string_view::npos || row_end == std::string_view::npos) break;
        auto href = html.substr(pos, href_end - pos);
        const auto text_begin = html.find('>', href_end);
        const auto text_end = html.find("</a>", href_end);
        const auto size_at = html.find("id=\"size\">", href_end);
        pos = row_end;
        if (href.empty() || text_begin == std::string_view::npos || text_end == std::string_view::npos || text_begin >= text_end ||
            size_at == std::string_view::npos || size_at > row_end) {
            continue;
        }
        std::uint64_t size = 0;
        bool digits = false;
        for (auto i = size_at + 10; i < row_end && html[i] >= '0' && html[i] <= '9'; i++) {
            size = size * 10 + static_cast<std::uint64_t>(html[i] - '0');
            digits = true;
        }
        if (!digits || size == 0) continue;
        ArchiveEntry e;
        e.name = HtmlUnescape(html.substr(text_begin + 1, text_end - text_begin - 1));
        e.url = href.starts_with("//") ? "https:" + std::string{href} : href.starts_with("/") ? "https://archive.org" + std::string{href} : std::string{href};
        e.size = size;
        out.push_back(std::move(e));
    }
    return out;
}

// every word of `words` (split on spaces) is part of `name`, any case; no words matches everything
inline bool MatchesWords(std::string_view name, std::string_view words) {
    const auto n = Lower(name);
    const auto w = Lower(words);
    size_t pos = 0;
    while (pos < w.size()) {
        const auto end = w.find(' ', pos);
        const auto word = w.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? w.size() : end + 1;
        if (!word.empty() && n.find(word) == std::string::npos) return false;
    }
    return true;
}

} // namespace sphaira::rom
