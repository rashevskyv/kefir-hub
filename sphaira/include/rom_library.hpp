#pragma once

// ROM library downloader: the pure part. Collections are archive.org items the user adds, one per
// console folder under /roms/<console>/ (the folder names Tico, HaulNX and RetroArch's scanner share).
// The Hub ships no item ids. No libnx here: tests/test_rom_library.cpp runs it on the host.

#include "bios_plan.hpp" // EncodePath
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

// the archive.org search for a console: software items whose title or subject names the system;
// `words` (what the user typed) narrows it further. Sorted by downloads, so the first rows are the
// sets most people took.
inline std::string SearchQuery(std::string_view folder, std::string_view words) {
    std::string q = "mediatype:software";
    if (const auto c = FindConsole(folder)) {
        q += std::string{" AND (title:("} + c->terms + ") OR subject:(" + c->terms + "))";
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

} // namespace sphaira::rom
