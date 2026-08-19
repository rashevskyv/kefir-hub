#pragma once

// Case-insensitive string and path bits that had grown five separate copies
// across the tree: EndsWithIC (yati.cpp, dbi_menu.cpp), ExtensionEquals
// (file_viewer.cpp, web_http.cpp), IsExtension and IsSamePath (file_picker.cpp,
// filebrowser_assoc.cpp), PathExtension (file_viewer.cpp, web_http.cpp).
//
// Case-insensitivity is not cosmetic here: FAT32 filenames are case-insensitive,
// so "GAME.NSP" and "game.nsp" are the same file and must compare equal.
//
// Nothing here includes switch.h, so it is testable on the host. fs::FsPath
// converts to std::string_view implicitly, so FsPath callers need no change.

#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <strings.h> // strncasecmp

namespace sphaira::path {

// Whole-string case-insensitive compare. Only reads the bytes it is given, so a
// string_view that is not null-terminated is fine.
inline auto EqualsIC(std::string_view a, std::string_view b) -> bool {
    return a.length() == b.length() && !strncasecmp(a.data(), b.data(), a.length());
}

// True when `s` starts with `prefix`, ignoring case. An empty prefix always
// matches, matching std::string_view::starts_with.
inline auto StartsWithIC(std::string_view s, std::string_view prefix) -> bool {
    if (s.size() < prefix.size()) {
        return false;
    }
    return !strncasecmp(s.data(), prefix.data(), prefix.size());
}

// True when `s` ends with `suffix`, ignoring case. An empty suffix always
// matches, matching std::string_view::ends_with.
inline auto EndsWithIC(std::string_view s, std::string_view suffix) -> bool {
    if (s.size() < suffix.size()) {
        return false;
    }
    return !strncasecmp(s.data() + s.size() - suffix.size(), suffix.data(), suffix.size());
}

// The bit after the last '.', without the dot. Empty when there is no
// extension, and -- importantly -- when the only dot is in a *directory*
// component ("/a.b/file" has no extension, not "b/file").
inline auto Extension(std::string_view path) -> std::string_view {
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == path.npos || (slash != path.npos && dot < slash)) {
        return {};
    }
    return path.substr(dot + 1);
}

// True when `value` case-insensitively equals any entry in `list`.
inline auto IsAnyOfIC(std::string_view value, std::span<const std::string_view> list) -> bool {
    for (const auto e : list) {
        if (EqualsIC(value, e)) {
            return true;
        }
    }
    return false;
}

// A name that is exactly a 16 digit hex title id ("0100000000001000"), as used
// by the folders under /atmosphere/contents. 0 for anything else, so the id is
// also the "is this a title id" answer.
inline auto ParseTitleIdName(std::string_view name) -> std::uint64_t {
    if (name.length() != 16) {
        return 0;
    }

    std::uint64_t id{};
    const auto end = name.data() + name.length();
    const auto r = std::from_chars(name.data(), end, id, 16);
    return r.ec == std::errc{} && r.ptr == end ? id : 0;
}

// Returns true if the archive entry path is safe for extraction:
// - Non-empty relative path (not starting with '/')
// - Does not contain '\\', ':', or control characters (< 0x20, 0x7F)
// - Does not contain '.' or '..' directory traversal components
inline auto IsSafeArchiveEntry(std::string_view path) -> bool {
    if (path.empty() || path.front() == '/') {
        return false;
    }

    for (const char c : path) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\' || c == ':') {
            return false;
        }
    }

    std::size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto comp = (end == std::string_view::npos)
            ? path.substr(start)
            : path.substr(start, end - start);

        if (comp == "." || comp == "..") {
            return false;
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    return true;
}

// Normalizes an absolute SD card path:
// - Must start with '/'
// - Rejects '\', ':', control characters (< 0x20, 0x7F)
// - Collapses repeated slashes
// - Trims trailing slashes (except root "/")
// - Rejects '.' and '..' components (allows dotfiles/dotfolders like .config, ..data)
inline auto NormalizeAbsoluteSdPath(std::string_view path) -> std::optional<std::string> {
    if (path.empty() || path.front() != '/') {
        return std::nullopt;
    }

    for (const char c : path) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\' || c == ':') {
            return std::nullopt;
        }
    }

    std::string normalized;
    normalized.reserve(path.size());

    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') {
            start++;
        }
        if (start >= path.size()) {
            break;
        }

        const auto end = path.find('/', start);
        const auto comp = (end == std::string_view::npos)
            ? path.substr(start)
            : path.substr(start, end - start);

        if (comp == "." || comp == "..") {
            return std::nullopt;
        }

        normalized.push_back('/');
        normalized.append(comp.data(), comp.size());

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    if (normalized.empty()) {
        return "/";
    }

    return normalized;
}

} // namespace sphaira::path
