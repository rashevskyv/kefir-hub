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

// Returns true if the content type or filename/URL indicates a ZIP archive.
// - Content type contains "zip" (case-insensitive)
// - Filename or URL path ends with ".zip" (case-insensitive, URL query/fragment ignored)
inline auto IsZipAsset(std::string_view content_type, std::string_view filename, std::string_view url = {}) -> bool {
    if (!content_type.empty()) {
        for (std::size_t i = 0; i + 3 <= content_type.size(); ++i) {
            if ((content_type[i] == 'z' || content_type[i] == 'Z') &&
                (content_type[i + 1] == 'i' || content_type[i + 1] == 'I') &&
                (content_type[i + 2] == 'p' || content_type[i + 2] == 'P')) {
                return true;
            }
        }
    }
    if (EndsWithIC(filename, ".zip")) {
        return true;
    }
    if (!url.empty()) {
        const auto q = url.find_first_of("?#");
        const auto url_path = (q != std::string_view::npos) ? url.substr(0, q) : url;
        if (EndsWithIC(url_path, ".zip")) {
            return true;
        }
    }
    return false;
}

// Validates a filename basename (rejects slashes, backslashes, colons, traversal '..' and control chars)
inline auto IsSafeFilename(std::string_view name) -> bool {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (const char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    return true;
}

// Extracts the basename from a path or URL (e.g. "https://foo/bar.nro?v=1" -> "bar.nro", "/switch/app.nro" -> "app.nro")
inline auto ExtractBasename(std::string_view path_or_url) -> std::string_view {
    if (path_or_url.empty()) {
        return {};
    }
    const auto q = path_or_url.find_first_of("?#");
    const auto clean = (q != std::string_view::npos) ? path_or_url.substr(0, q) : path_or_url;
    const auto slash = clean.find_last_of('/');
    if (slash == clean.npos) {
        return clean;
    }
    return clean.substr(slash + 1);
}

struct GitHubRepo {
    std::string owner;
    std::string repo;
};

// Parses and validates a GitHub repository URL:
// - http:// or https:// scheme
// - Host is github.com or www.github.com
// - Exactly two path segments: /owner/repo (optional single trailing slash allowed)
// - Optional ".git" suffix in repo name stripped
// - Owner and repo only contain ASCII alphanumeric, '-', '_', '.'
// - Rejects credentials (@), port (:), query (?), fragment (#), empty/traversal segments
inline auto ParseGitHubRepoUrl(std::string_view url) -> std::optional<GitHubRepo> {
    if (url.empty()) {
        return std::nullopt;
    }

    // Must not contain userinfo, query, or fragment
    if (url.find('@') != std::string_view::npos ||
        url.find('?') != std::string_view::npos ||
        url.find('#') != std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view rest;
    if (StartsWithIC(url, "https://")) {
        rest = url.substr(8);
    } else if (StartsWithIC(url, "http://")) {
        rest = url.substr(7);
    } else {
        return std::nullopt;
    }

    const auto slash_pos = rest.find('/');
    if (slash_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto host = rest.substr(0, slash_pos);
    if (!EqualsIC(host, "github.com") && !EqualsIC(host, "www.github.com")) {
        return std::nullopt;
    }

    auto path_part = rest.substr(slash_pos + 1);
    if (path_part.empty()) {
        return std::nullopt;
    }

    // Allow single trailing slash
    if (path_part.back() == '/') {
        path_part.remove_suffix(1);
    }

    if (path_part.empty()) {
        return std::nullopt;
    }

    const auto second_slash = path_part.find('/');
    if (second_slash == std::string_view::npos) {
        return std::nullopt; // only owner, no repo
    }

    const auto owner_part = path_part.substr(0, second_slash);
    auto repo_part = path_part.substr(second_slash + 1);

    // No extra path segments allowed
    if (repo_part.find('/') != std::string_view::npos) {
        return std::nullopt;
    }

    // Strip optional .git suffix from repo
    if (EndsWithIC(repo_part, ".git")) {
        repo_part.remove_suffix(4);
    }

    const auto is_valid_ident = [](std::string_view s) -> bool {
        if (s.empty() || s == "." || s == "..") {
            return false;
        }
        for (const char c : s) {
            const auto uc = static_cast<unsigned char>(c);
            const bool ok = (uc >= 'a' && uc <= 'z') ||
                            (uc >= 'A' && uc <= 'Z') ||
                            (uc >= '0' && uc <= '9') ||
                            c == '-' || c == '_' || c == '.';
            if (!ok) {
                return false;
            }
        }
        return true;
    };

    if (!is_valid_ident(owner_part) || !is_valid_ident(repo_part)) {
        return std::nullopt;
    }

    return GitHubRepo{std::string(owner_part), std::string(repo_part)};
}

// Validates a direct HTTP/HTTPS asset download URL
inline auto IsValidDirectAssetUrl(std::string_view url) -> bool {
    if (url.empty()) {
        return false;
    }

    // Disallow user credentials and fragments
    if (url.find('@') != std::string_view::npos ||
        url.find('#') != std::string_view::npos) {
        return false;
    }

    std::string_view rest;
    if (StartsWithIC(url, "https://")) {
        rest = url.substr(8);
    } else if (StartsWithIC(url, "http://")) {
        rest = url.substr(7);
    } else {
        return false;
    }

    if (rest.empty()) {
        return false;
    }

    const auto slash_pos = rest.find('/');
    const auto host = (slash_pos != std::string_view::npos) ? rest.substr(0, slash_pos) : rest;
    if (host.empty()) {
        return false;
    }

    // Validate host
    for (const char c : host) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc >= 0x7F || c == ' ' || c == '\\') {
            return false;
        }
    }

    // Path must not contain spaces or control chars
    if (slash_pos != std::string_view::npos) {
        const auto path_and_query = rest.substr(slash_pos);
        for (const char c : path_and_query) {
            const auto uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || uc >= 0x7F || c == ' ' || c == '\\') {
                return false;
            }
        }
    }

    return true;
}

// Validates a direct ZIP URL
inline auto IsValidDirectZipUrl(std::string_view url) -> bool {
    if (!IsValidDirectAssetUrl(url)) {
        return false;
    }
    const auto q = url.find('?');
    const auto path_only = (q != std::string_view::npos) ? url.substr(0, q) : url;
    return EndsWithIC(path_only, ".zip");
}

} // namespace sphaira::path

