#pragma once

// Path matching for WebDAV listings. Nothing here touches switch.h, so it is tested on the host.
//
// A PROPFIND answer lists the requested folder itself as its first entry. Its href holds the base path
// of the mount url (mounting https://host/base answers /base/ and /base/file, while the Hub asks for
// / and /file), may be a full url, may hold double slashes and ends in a slash for folders.
// NormalizeUrlPath makes both sides comparable.

#include <string>
#include <string_view>

namespace sphaira::webdav {

inline auto NormalizeUrlPath(std::string_view str) -> std::string {
    // strip the scheme and host from absolute urls: https://example.com/base/dir/ -> /base/dir/
    if (const auto scheme = str.find("://"); scheme != std::string_view::npos) {
        const auto path = str.find('/', scheme + 3);
        str = path != std::string_view::npos ? str.substr(path) : std::string_view{"/"};
    }

    std::string out;
    out.reserve(str.size());
    for (const auto c : str) {
        if (c == '/' && !out.empty() && out.back() == '/') {
            continue; // duplicate slash
        }
        out += c;
    }

    // the trailing slash does not count, and the root is the empty path
    if (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    if (out == "/") {
        out.clear();
    }
    return out;
}

} // namespace sphaira::webdav
