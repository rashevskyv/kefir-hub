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
};

// the HaulNX folder table (its wiki, Home: "HaulNX folder names"), the systems Switch emulators run
inline const std::vector<Console>& Consoles() {
    static const std::vector<Console> c{
        {"nes", "Nintendo - NES"}, {"fds", "Nintendo - Famicom Disk System"}, {"snes", "Nintendo - SNES"},
        {"n64", "Nintendo - 64"}, {"gb", "Nintendo - Game Boy"}, {"gbc", "Nintendo - Game Boy Color"},
        {"gba", "Nintendo - Game Boy Advance"}, {"nds", "Nintendo - DS"}, {"3ds", "Nintendo - 3DS"},
        {"gc", "Nintendo - GameCube"}, {"wii", "Nintendo - Wii"}, {"wiiu", "Nintendo - Wii U"},
        {"virtual-boy", "Nintendo - Virtual Boy"}, {"pokemon-mini", "Nintendo - Pokemon Mini"},
        {"game-and-watch", "Nintendo - Game & Watch"},
        {"sg-1000", "Sega - SG-1000"}, {"master-system", "Sega - Master System"}, {"game-gear", "Sega - Game Gear"},
        {"genesis", "Sega - Genesis / Mega Drive"}, {"sega-cd", "Sega - CD"}, {"sega-32x", "Sega - 32X"},
        {"saturn", "Sega - Saturn"}, {"dc", "Sega - Dreamcast"}, {"naomi", "Sega - NAOMI"}, {"atomiswave", "Sammy - Atomiswave"},
        {"psx", "Sony - PlayStation"}, {"ps2", "Sony - PlayStation 2"}, {"psp", "Sony - PSP"}, {"vita", "Sony - PS Vita"},
        {"pc-engine", "NEC - PC Engine / TurboGrafx-16"}, {"pc-engine-cd", "NEC - PC Engine CD"},
        {"supergrafx", "NEC - SuperGrafx"}, {"pc-fx", "NEC - PC-FX"},
        {"neo-geo", "SNK - Neo Geo"}, {"neo-geo-cd", "SNK - Neo Geo CD"},
        {"neo-geo-pocket", "SNK - Neo Geo Pocket"}, {"neo-geo-pocket-color", "SNK - Neo Geo Pocket Color"},
        {"atari-2600", "Atari - 2600"}, {"atari-5200", "Atari - 5200"}, {"atari-7800", "Atari - 7800"},
        {"atari-lynx", "Atari - Lynx"}, {"atari-jaguar", "Atari - Jaguar"},
        {"wonderswan", "Bandai - WonderSwan"}, {"wonderswan-color", "Bandai - WonderSwan Color"},
        {"colecovision", "Coleco - ColecoVision"}, {"intellivision", "Mattel - Intellivision"},
        {"odyssey2", "Magnavox - Odyssey 2"}, {"vectrex", "GCE - Vectrex"}, {"channel-f", "Fairchild - Channel F"},
        {"supervision", "Watara - Supervision"}, {"3do", "The 3DO Company - 3DO"}, {"cd-i", "Philips - CD-i"},
        {"arcade", "Arcade (MAME)"}, {"fbneo", "Arcade (FinalBurn Neo)"},
        {"amiga", "Commodore - Amiga"}, {"zx-spectrum", "Sinclair - ZX Spectrum"},
        {"chip8", "CHIP-8"}, {"pico8", "PICO-8"}, {"flash", "Adobe Flash"},
    };
    return c;
}

inline const char* ConsoleName(std::string_view folder) {
    for (const auto& c : Consoles()) {
        if (folder == c.folder) return c.name;
    }
    return "";
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
