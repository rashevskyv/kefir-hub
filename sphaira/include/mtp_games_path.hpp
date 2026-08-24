#pragma once

#include <cctype>
#include <cstdint>
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
};

// owo HOME-menu forwarders (0x05…) and the known HBL 0x03 titles.
inline auto IsForwarderTitleId(std::uint64_t tid) -> bool {
    if ((tid & 0xFF00000000000000ULL) == 0x0500000000000000ULL) {
        return true;
    }
    return tid == 0x03DB1280BD84000ULL || tid == 0x03DB12780BD84000ULL;
}

struct ParsedPath {
    PathKind kind{PathKind::Invalid};
    std::string game{};
    std::string filename{};
};

inline ParsedPath ParseGamesPath(std::string_view path, GamesLayout layout = GamesLayout::Both,
    const GamesFolderNames& names = {})
{
    ParsedPath result{};
    std::vector<std::string_view> segments;

    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        if (i >= path.size()) {
            break;
        }
        size_t start = i;
        while (i < path.size() && path[i] != '/') {
            ++i;
        }
        segments.push_back(path.substr(start, i - start));
    }

    if (segments.empty()) {
        result.kind = PathKind::Root;
        return result;
    }

    auto equals_ic = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t k = 0; k < a.size(); ++k) {
            if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k]))) {
                return false;
            }
        }
        return true;
    };

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

    if (segments.size() == 1 && is_readme(segments[0])) {
        result.kind = PathKind::InfoFile;
        result.filename = std::string(segments[0]);
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
        if (segments.size() == 1) {
            result.kind = PathKind::MergedFile;
            result.filename = std::string(segments[0]);
        } else {
            result.kind = PathKind::Invalid;
        }
        return result;
    }

    if (layout == GamesLayout::Separate) {
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

} // namespace sphaira::mtp
