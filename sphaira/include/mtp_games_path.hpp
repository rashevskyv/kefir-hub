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

struct ParsedPath {
    PathKind kind{PathKind::Invalid};
    std::string game{};
    std::string filename{};
};

inline ParsedPath ParseGamesPath(std::string_view path) {
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
