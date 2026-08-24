#pragma once

#include <cctype>
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
    Invalid
};

// how the MTP Games drive is laid out. stored as OptionLong 0/1/2.
enum class GamesLayout {
    Compatible = 0, // one merged NSP per game, at the drive root
    Separate = 1,   // a folder per game with BASE/UPD/DLC files
    Both = 2,       // root contains Merged/ and Separate/
};

struct ParsedPath {
    PathKind kind{PathKind::Invalid};
    std::string game{};
    std::string filename{};
};

inline ParsedPath ParseGamesPath(std::string_view path, GamesLayout layout = GamesLayout::Both) {
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

    if (equals_ic(segments[0], "Merged")) {
        if (segments.size() == 1) {
            result.kind = PathKind::MergedDir;
        } else if (segments.size() == 2) {
            result.kind = PathKind::MergedFile;
            result.filename = std::string(segments[1]);
        } else {
            result.kind = PathKind::Invalid;
        }
    } else if (equals_ic(segments[0], "Separate")) {
        if (segments.size() == 1) {
            result.kind = PathKind::SeparateDir;
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
