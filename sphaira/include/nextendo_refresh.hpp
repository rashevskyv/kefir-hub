#pragma once

// Nextendo server address refresh: the pure decisions (no network here). nextendo_refresh.cpp does the requests.
// The Hub copies what a Prelude update would do: the server IPs are constants of the release, so the newest
// release's nextendo_config.h is read, and only the two IPs are taken from it. Host test tests/test_nextendo_refresh.cpp.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include "nextendo_hosts.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace sphaira::nextendo {

inline constexpr std::string_view kRepo = "NextendoNetwork/Prelude-Nextendo";
inline constexpr std::int64_t kRefreshEverySeconds = 24 * 60 * 60;

// A release tag is used inside a URL: letters, digits, '.', '-', '_' only, 1..40 characters.
inline auto IsSafeTag(std::string_view tag) -> bool {
    if (tag.empty() || tag.size() > 40) {
        return false;
    }
    for (const char c : tag) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

inline auto ConfigUrl(std::string_view tag) -> std::string {
    return "https://raw.githubusercontent.com/" + std::string{kRepo} + "/" + std::string{tag} + "/source/nextendo_config.h";
}

// The server must be a public address: a refresh must never point the hosts file into the user's own network.
inline auto IsPublicIpv4(std::string_view s) -> bool {
    if (!IsIpv4(s)) {
        return false;
    }
    int a = 0;
    int b = 0;
    std::sscanf(std::string{s}.c_str(), "%d.%d", &a, &b);
    if (a == 0 || a == 10 || a == 127 || a >= 224) {
        return false;
    }
    if ((a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254) || (a == 100 && b >= 64 && b <= 127)) {
        return false;
    }
    return true;
}

inline auto ArePublic(const ServerIps& ips) -> bool {
    return IsPublicIpv4(ips.main) && IsPublicIpv4(ips.nncs2);
}

inline auto RefreshDue(std::int64_t now, std::int64_t last) -> bool {
    return last <= 0 || now < last || now - last >= kRefreshEverySeconds; // a clock set back also triggers one check
}

} // namespace sphaira::nextendo
