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
#include <span>
#include <string_view>

#include <strings.h> // strncasecmp

namespace sphaira::path {

// Whole-string case-insensitive compare. Only reads the bytes it is given, so a
// string_view that is not null-terminated is fine.
inline auto EqualsIC(std::string_view a, std::string_view b) -> bool {
    return a.length() == b.length() && !strncasecmp(a.data(), b.data(), a.length());
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

} // namespace sphaira::path
