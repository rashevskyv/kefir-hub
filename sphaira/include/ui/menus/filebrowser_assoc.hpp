#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include "nro.hpp"
#include "fs.hpp"

namespace sphaira::ui::menu::filebrowser::detail {

using RomDatabaseIndexs = std::vector<size_t>;

struct ExtDbEntry {
    std::string_view db_name;
    std::span<const std::string_view> ext;
};

struct RomDatabaseEntry {
    // uses the naming scheme from retropie.
    std::string_view folder{};
    // uses the naming scheme from Retroarch.
    std::string_view database{};
    // custom alias, to make everyone else happy.
    std::array<std::string_view, 4> alias{};

    // compares against all of the above strings.
    auto IsDatabase(std::string_view name) const -> bool;
};

constexpr std::string_view AUDIO_EXTENSIONS[] = {
    "mp3", "ogg", "flac", "wav", "aac", "ac3", "aif", "asf", "bfwav",
    "bfsar", "bfstm",
};
constexpr std::string_view VIDEO_EXTENSIONS[] = {
    "mp4", "mkv", "m3u", "m3u8", "hls", "vob", "avi", "dv", "flv", "m2ts",
    "m2v", "m4a", "mov", "mpeg", "mpg", "mts", "swf", "ts", "vob", "wma", "wmv",
};
constexpr std::string_view IMAGE_EXTENSIONS[] = {
    "png", "jpg", "jpeg", "bmp", "gif",
};
constexpr std::string_view INSTALL_EXTENSIONS[] = {
    "nsp", "xci", "nsz", "xcz",
};
// these are files that are already compressed or encrypted and should
// be stored raw in a zip file.
constexpr std::string_view COMPRESSED_EXTENSIONS[] = {
    "zip", "xz", "7z", "rar", "tar", "nca", "nsp", "xci", "nsz", "xcz"
};
constexpr std::string_view ZIP_EXTENSIONS[] = {
    "zip",
};

inline constexpr RomDatabaseEntry PATHS[]{
    { "3do", "The 3DO Company - 3DO"},
    { "atari800", "Atari - 8-bit"},
    { "atari2600", "Atari - 2600"},
    { "atari5200", "Atari - 5200"},
    { "atari7800", "Atari - 7800"},
    { "atarilynx", "Atari - Lynx"},
    { "atarijaguar", "Atari - Jaguar"},
    { "atarijaguarcd", ""},
    { "n3ds", "Nintendo - Nintendo 3DS"},
    { "n64", "Nintendo - Nintendo 64"},
    { "nds", "Nintendo - Nintendo DS"},
    { "fds", "Nintendo - Famicom Disk System"},
    { "nes", "Nintendo - Nintendo Entertainment System"},
    { "pokemini", "Nintendo - Pokemon Mini"},
    { "gb", "Nintendo - Game Boy"},
    { "gba", "Nintendo - Game Boy Advance"},
    { "gbc", "Nintendo - Game Boy Color"},
    { "virtualboy", "Nintendo - Virtual Boy"},
    { "gameandwatch", ""},
    { "sega32x", "Sega - 32X"},
    { "segacd", "Sega - Mega CD - Sega CD"},
    { "dreamcast", "Sega - Dreamcast"},
    { "gamegear", "Sega - Game Gear"},
    { "genesis", "Sega - Mega Drive - Genesis"},
    { "mastersystem", "Sega - Master System - Mark III"},
    { "megadrive", "Sega - Mega Drive - Genesis"},
    { "saturn", "Sega - Saturn"},
    { "sg-1000", "Sega - SG-1000"},
    { "psx", "Sony - PlayStation"},
    { "psp", "Sony - PlayStation Portable"},
    { "snes", "Nintendo - Super Nintendo Entertainment System"},
    { "pico8", "Sega - PICO"},
    { "wonderswan", "Bandai - WonderSwan"},
    { "wonderswancolor", "Bandai - WonderSwan Color"},

    { "mame", "MAME 2000", { "MAME", "mame-libretro", } },
    { "mame", "MAME 2003", { "MAME", "mame-libretro", } },
    { "mame", "MAME 2003-Plus", { "MAME", "mame-libretro", } },

    { "neogeo", "SNK - Neo Geo Pocket" },
    { "neogeo", "SNK - Neo Geo Pocket Color" },
    { "neogeo", "SNK - Neo Geo CD" },
};

constexpr fs::FsPath DAYBREAK_PATH{"/switch/daybreak.nro"};


auto GetRomDatabaseFromPath(std::string_view path) -> RomDatabaseIndexs;
auto GetRomIcon(std::string filename, const RomDatabaseIndexs& db_indexs, const NroEntry& nro) -> std::vector<u8>;
auto GetNxmpPath() -> const char*;
auto HasNxmp() -> bool;

} // namespace sphaira::ui::menu::filebrowser::detail
