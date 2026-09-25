#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sphaira::path {

// Forward declarations of case-insensitive string helpers from path_util.hpp
auto EqualsIC(std::string_view a, std::string_view b) -> bool;
auto StartsWithIC(std::string_view s, std::string_view prefix) -> bool;
auto EndsWithIC(std::string_view s, std::string_view suffix) -> bool;

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

// Validates a direct NRO URL
inline auto IsValidDirectNroUrl(std::string_view url) -> bool {
    if (!IsValidDirectAssetUrl(url)) {
        return false;
    }
    const auto q = url.find('?');
    const auto path_only = (q != std::string_view::npos) ? url.substr(0, q) : url;
    return EndsWithIC(path_only, ".nro");
}

// Validates a direct download URL (.zip or .nro)
inline auto IsValidDirectDownloadUrl(std::string_view url) -> bool {
    return IsValidDirectZipUrl(url) || IsValidDirectNroUrl(url);
}

// Trims whitespace and peels stacked http:// / https:// prefixes so
// "https://https://host/file.zip" becomes "https://host/file.zip".
inline auto CollapseRepeatedHttpSchemes(std::string_view url) -> std::string {
    std::string s{url};
    while (!s.empty() && static_cast<unsigned char>(s.front()) <= ' ') {
        s.erase(s.begin());
    }
    while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ') {
        s.pop_back();
    }

    while (true) {
        std::size_t n = 0;
        if (StartsWithIC(s, "https://")) {
            n = 8;
        } else if (StartsWithIC(s, "http://")) {
            n = 7;
        } else {
            break;
        }
        const auto rest = std::string_view{s}.substr(n);
        if (StartsWithIC(rest, "https://") || StartsWithIC(rest, "http://")) {
            s.erase(0, n);
            continue;
        }
        break;
    }
    return s;
}

} // namespace sphaira::path
