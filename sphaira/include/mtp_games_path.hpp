#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::mtp {

enum class PathKind {
    Root,
    MergedDir,
    MergedFile,
    SeparateDir,
    SeparateGameDir,
    SeparateFile,
    ForwardersDir,
    ForwardersFile,
    // Readme.txt (or the locale's name). `game` is "", "merged", "separate"
    // or "forwarders" so the virtual text can explain that folder.
    InfoFile,
    // <game>/Mods[/sub/path]: the game's /atmosphere/contents/<tid> folder on
    // the SD card. `filename` is the path inside it ("" for the folder itself).
    ModsPath,
    // "Install to SD" / "Install to NAND" and anything dropped into one: `install_sd` says
    // where, `filename` is the path inside the folder ("Game.nsp", "Merged/Mods/x [TID]/romfs");
    // ClassifyInstallDrop(filename) says what it becomes.
    InstallDir,
    InstallFile,
    // Merged/<Mods> (the drive root in the Compatible layout): one folder per game that has
    // /atmosphere/contents/<tid>; the paths below it are ModsPath.
    ModsRootDir,
    Invalid
};

// how the MTP Games drive is laid out. stored as OptionLong 0/1/2.
enum class GamesLayout {
    Compatible = 0, // one merged NSP per game, at the drive root
    Separate = 1,   // a folder per game with BASE/UPD/DLC files
    Both = 2,       // Merged/, Separate/, Forwarders/
};

// folder names as listed over MTP. English is always accepted as well so a
// client that cached "Merged" still opens after a language switch.
struct GamesFolderNames {
    std::string merged{"Merged"};
    std::string separate{"Separate"};
    std::string forwarders{"Forwarders"};
    std::string readme{"Readme.txt"};
    std::string mods{"Mods"};
    std::string install_sd{"Install to SD"};
    std::string install_nand{"Install to NAND"};
};

// owo HOME-menu forwarders (0x05…) and the known HBL 0x03 titles.
inline auto IsForwarderTitleId(std::uint64_t tid) -> bool {
    if ((tid & 0xFF00000000000000ULL) == 0x0500000000000000ULL) {
        return true;
    }
    return tid == 0x03DB1280BD84000ULL || tid == 0x03DB12780BD84000ULL;
}

inline auto EqualsIc(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); ++k) {
        if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k]))) {
            return false;
        }
    }
    return true;
}

// "/a//b/" -> {"a", "b"}.
inline auto SplitPath(std::string_view path) -> std::vector<std::string_view> {
    std::vector<std::string_view> segments;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        if (i >= path.size()) {
            break;
        }
        const size_t start = i;
        while (i < path.size() && path[i] != '/') {
            ++i;
        }
        segments.push_back(path.substr(start, i - start));
    }
    return segments;
}

struct ParsedPath {
    PathKind kind{PathKind::Invalid};
    std::string game{};
    std::string filename{};
    bool install_sd{};
};

// the path an install folder hands to the installer: the English folder name, so
// InstallTargetFromPath() reads the target back whatever the locale.
inline auto InstallQueuePath(bool sd, std::string_view filename) -> std::string {
    return std::string(sd ? "/Install to SD/" : "/Install to NAND/") + std::string(filename);
}

// 1 = microSD, 0 = system memory, -1 = not an install-folder path (the setting decides).
inline auto InstallTargetFromPath(std::string_view path) -> int {
    if (path.starts_with("/Install to SD/")) return 1;
    if (path.starts_with("/Install to NAND/")) return 0;
    return -1;
}

inline ParsedPath ParseGamesPath(std::string_view path, GamesLayout layout = GamesLayout::Both,
    const GamesFolderNames& names = {})
{
    ParsedPath result{};
    const auto segments = SplitPath(path);

    if (segments.empty()) {
        result.kind = PathKind::Root;
        return result;
    }

    const auto equals_ic = EqualsIc;

    auto match_name = [&](std::string_view seg, std::string_view english, const std::string& localized) {
        if (equals_ic(seg, english)) {
            return true;
        }
        return !localized.empty() && equals_ic(seg, localized);
    };

    auto is_merged = [&](std::string_view s) { return match_name(s, "Merged", names.merged); };
    auto is_separate = [&](std::string_view s) { return match_name(s, "Separate", names.separate); };
    auto is_forwarders = [&](std::string_view s) { return match_name(s, "Forwarders", names.forwarders); };
    auto is_readme = [&](std::string_view s) { return match_name(s, "Readme.txt", names.readme); };
    auto is_mods = [&](std::string_view s) { return match_name(s, "Mods", names.mods); };
    auto is_install_sd = [&](std::string_view s) { return match_name(s, "Install to SD", names.install_sd); };
    auto is_install_nand = [&](std::string_view s) { return match_name(s, "Install to NAND", names.install_nand); };

    // a game's mods folder: `game` names the game, segments[from...] is the path inside it.
    auto mods_item = [&](std::string_view game, size_t from) {
        result.kind = PathKind::ModsPath;
        result.game = std::string(game);
        for (size_t k = from; k < segments.size(); ++k) {
            // the path is joined onto a real SD folder: never let it climb out.
            if (segments[k] == "." || segments[k] == "..") {
                result = ParsedPath{};
                return;
            }
            if (!result.filename.empty()) {
                result.filename.push_back('/');
            }
            result.filename += segments[k];
        }
    };

    // segments[game_at] is a game folder; true when the rest is its mods folder.
    auto parse_mods = [&](size_t game_at) {
        if (segments.size() <= game_at + 1 || !is_mods(segments[game_at + 1])) {
            return false;
        }
        mods_item(segments[game_at], game_at + 2);
        return true;
    };

    // segments[at] is the merged mods folder: <Mods>/<game>/<path inside the game's mods folder>.
    auto parse_mods_root = [&](size_t at) {
        if (segments.size() <= at || !is_mods(segments[at])) {
            return false;
        }
        if (segments.size() == at + 1) {
            result.kind = PathKind::ModsRootDir;
        } else {
            mods_item(segments[at + 1], at + 2);
        }
        return true;
    };

    if (segments.size() == 1 && is_readme(segments[0])) {
        result.kind = PathKind::InfoFile;
        result.filename = std::string(segments[0]);
        return result;
    }

    // the install folders sit at the root in every layout.
    if (is_install_sd(segments[0]) || is_install_nand(segments[0])) {
        result.install_sd = is_install_sd(segments[0]);
        if (segments.size() == 1) {
            result.kind = PathKind::InstallDir;
            return result;
        }
        result.kind = PathKind::InstallFile;
        for (size_t k = 1; k < segments.size(); ++k) {
            // a mods file in it is joined onto a real SD folder: never let it climb out.
            if (segments[k] == "." || segments[k] == "..") {
                return ParsedPath{};
            }
            if (!result.filename.empty()) {
                result.filename.push_back('/');
            }
            result.filename += segments[k];
        }
        return result;
    }

    if (is_forwarders(segments[0])) {
        if (segments.size() == 1) {
            result.kind = PathKind::ForwardersDir;
        } else if (segments.size() == 2 && is_readme(segments[1])) {
            result.kind = PathKind::InfoFile;
            result.game = "forwarders";
            result.filename = std::string(segments[1]);
        } else if (segments.size() == 2) {
            result.kind = PathKind::ForwardersFile;
            result.filename = std::string(segments[1]);
        } else {
            result.kind = PathKind::Invalid;
        }
        return result;
    }

    if (layout == GamesLayout::Compatible) {
        if (parse_mods_root(0)) {
            return result;
        }
        if (segments.size() == 1) {
            result.kind = PathKind::MergedFile;
            result.filename = std::string(segments[0]);
        } else {
            result.kind = PathKind::Invalid;
        }
        return result;
    }

    if (layout == GamesLayout::Separate) {
        if (parse_mods(0)) {
            return result;
        }
        if (segments.size() == 1) {
            result.kind = PathKind::SeparateGameDir;
            result.game = std::string(segments[0]);
        } else if (segments.size() == 2) {
            result.kind = PathKind::SeparateFile;
            result.game = std::string(segments[0]);
            result.filename = std::string(segments[1]);
        } else {
            result.kind = PathKind::Invalid;
        }
        return result;
    }

    if (is_merged(segments[0])) {
        if (parse_mods_root(1)) {
            return result;
        }
        if (segments.size() == 1) {
            result.kind = PathKind::MergedDir;
        } else if (segments.size() == 2 && is_readme(segments[1])) {
            result.kind = PathKind::InfoFile;
            result.game = "merged";
            result.filename = std::string(segments[1]);
        } else if (segments.size() == 2) {
            result.kind = PathKind::MergedFile;
            result.filename = std::string(segments[1]);
        } else {
            result.kind = PathKind::Invalid;
        }
    } else if (is_separate(segments[0])) {
        if (parse_mods(1)) {
            return result;
        }
        if (segments.size() == 1) {
            result.kind = PathKind::SeparateDir;
        } else if (segments.size() == 2 && is_readme(segments[1])) {
            result.kind = PathKind::InfoFile;
            result.game = "separate";
            result.filename = std::string(segments[1]);
        } else if (segments.size() == 2) {
            result.kind = PathKind::SeparateGameDir;
            result.game = std::string(segments[1]);
        } else if (segments.size() == 3) {
            result.kind = PathKind::SeparateFile;
            result.game = std::string(segments[1]);
            result.filename = std::string(segments[2]);
        } else {
            result.kind = PathKind::Invalid;
        }
    } else {
        result.kind = PathKind::Invalid;
    }

    return result;
}

inline constexpr const char* INSTALL_EXT[] = { ".nsp", ".xci", ".nsz", ".xcz" };

inline auto IsInstallName(std::string_view name) -> bool {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    for (const auto ext : INSTALL_EXT) {
        if (EqualsIc(name.substr(dot), ext)) {
            return true;
        }
    }
    return false;
}

// "Zelda [0100F2C0115B6000]" -> 0x0100F2C0115B6000: the last "[16 hex digits]" in a name
// (the Games drive names game folders "<name> [TID]").
inline auto TitleIdFromName(std::string_view name) -> std::optional<std::uint64_t> {
    std::optional<std::uint64_t> out;
    for (size_t open = 0; open + 18 <= name.size(); ++open) {
        if (name[open] != '[' || name[open + 17] != ']') {
            continue;
        }
        std::uint64_t tid = 0;
        bool hex = true;
        for (size_t k = open + 1; k < open + 17 && hex; ++k) {
            const auto c = static_cast<unsigned char>(name[k]);
            hex = std::isxdigit(c);
            tid = (tid << 4) | static_cast<std::uint64_t>(std::isdigit(c) ? c - '0' : std::tolower(c) - 'a' + 10);
        }
        if (hex) {
            out = tid;
        }
    }
    return out;
}

// what a file dropped on the MTP Install drive (alone or inside a folder) becomes.
enum class DropKind {
    Install, // an NSP/XCI/NSZ/XCZ: streams to the installer.
    Mod,     // a file in a game's mods folder: written to `sd_path` on the SD card.
    Skip,    // any other file inside a dropped folder (a readme): accepted and discarded.
    Reject,  // any other file at the drive root: refused, as before folders were accepted.
};

struct Drop {
    DropKind kind{DropKind::Reject};
    std::string sd_path{};
};

// "/atmosphere/contents/" + 16 hex digits: a game's mods folder itself, not a file in it.
inline constexpr size_t CONTENTS_TID_PATH_LEN = 37;

// a path in a game's mods folder as the Games drive lays it out: ".../<Mods>/<name [TID]>/<rest>"
// (Merged) or ".../<name [TID]>/<Mods>/<rest>" (Separate). <Mods> is "Mods" or the locale's name.
// returns "/atmosphere/contents/<TID>[/<rest>]", nullopt for any other path.
inline auto ModsDropSdPath(std::string_view path, std::string_view mods_name) -> std::optional<std::string> {
    const auto segments = SplitPath(path);
    for (const auto seg : segments) {
        // the result is a real SD path: never let it climb out.
        if (seg == "." || seg == "..") {
            return std::nullopt;
        }
    }
    for (size_t k = 0; k < segments.size(); ++k) {
        if (!EqualsIc(segments[k], "Mods") && (mods_name.empty() || !EqualsIc(segments[k], mods_name))) {
            continue;
        }
        size_t rest = 0;
        std::optional<std::uint64_t> tid;
        if (k + 1 < segments.size() && (tid = TitleIdFromName(segments[k + 1]))) {
            rest = k + 2;
        } else if (k > 0 && (tid = TitleIdFromName(segments[k - 1]))) {
            rest = k + 1;
        } else {
            continue;
        }
        char base[48];
        std::snprintf(base, sizeof(base), "/atmosphere/contents/%016llX", static_cast<unsigned long long>(*tid));
        std::string out = base;
        for (size_t r = rest; r < segments.size(); ++r) {
            out += '/';
            out += segments[r];
        }
        return out;
    }
    return std::nullopt;
}

inline auto ClassifyInstallDrop(std::string_view path, std::string_view mods_name) -> Drop {
    const auto segments = SplitPath(path);
    if (segments.empty()) {
        return {};
    }
    // the mods rule comes first: a mod may hold any file, even an nsp.
    if (auto sd = ModsDropSdPath(path, mods_name); sd && sd->size() > CONTENTS_TID_PATH_LEN) {
        return { DropKind::Mod, std::move(*sd) };
    }
    if (IsInstallName(segments.back())) {
        return { DropKind::Install };
    }
    return { segments.size() > 1 ? DropKind::Skip : DropKind::Reject };
}

} // namespace sphaira::mtp
