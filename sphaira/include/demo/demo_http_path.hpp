#pragma once

// DOCS_DEMO builds only: the fixture file name for a request (rules in source/demo/demo_http.cpp).
// libnx-free so tests/test_demo_http_path.cpp can run it on the host.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace sphaira::demo {

inline std::uint32_t Fnv1a32(std::string_view s) {
    std::uint32_t h = 2166136261u;
    for (const unsigned char c : s) {
        h = (h ^ c) * 16777619u;
    }
    return h;
}

// "https://host/a/b?x=1" -> "host/a/b_x=1"; "http://host:8465" -> "host_8465/index"; a trailing '/' -> ".../index";
// a POST body adds ".<fnv1a32 hex>".
inline std::string HttpFixtureName(std::string_view url, std::string_view post) {
    if (const auto scheme = url.find("://"); scheme != url.npos) {
        url.remove_prefix(scheme + 3);
    }

    // "host:port" -> "host_port" (no ':' in file names on Windows), no path -> "host/index".
    const auto q = url.find('?');
    std::string out{url.substr(0, q)};
    const auto slash = out.find('/');
    for (size_t i = 0; i < std::min(slash, out.size()); i++) {
        if (out[i] == ':') out[i] = '_';
    }
    if (slash == std::string::npos) {
        out += '/';
    }
    if (q != url.npos) {
        out += '_';
        for (const char c : url.substr(q + 1)) {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '=' || c == '_' || c == '-';
            out += keep ? c : '_';
        }
    } else if (out.back() == '/') {
        out += "index";
    }

    if (!post.empty()) {
        char hash[16];
        std::snprintf(hash, sizeof(hash), ".%08x", static_cast<unsigned>(Fnv1a32(post)));
        out += hash;
    }
    return out;
}

} // namespace sphaira::demo
