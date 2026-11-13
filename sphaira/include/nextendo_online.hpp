#pragma once

// Nextendo online player counts for the games list badge: the pure part (no network here).
// nextendo_refresh.cpp does the request. Host test tests/test_nextendo_online.cpp.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace sphaira::nextendo {

// {"counts":{"<16 hex title id>":<players>, ...}}: every supported game is a key, 0 players included.
inline constexpr std::string_view kOnlineCountsUrl = "https://nextendo.network/api/online-counts";
inline constexpr std::int64_t kOnlineCountsEverySeconds = 60;
inline constexpr std::int64_t kOnlineCountsTimeoutSeconds = 15;
inline constexpr std::size_t kOnlineCountsMaxBody = 256 * 1024;
inline constexpr std::size_t kOnlineCountsMaxGames = 4096;

using OnlineCounts = std::unordered_map<std::uint64_t, int>;

// nullopt when the text is not that shape. A key that is not 16 hex digits is skipped, not an error.
inline auto ParseOnlineCounts(std::string_view s) -> std::optional<OnlineCounts> {
    if (s.size() > kOnlineCountsMaxBody) {
        return std::nullopt;
    }
    std::size_t i = s.find("\"counts\"");
    if (i == std::string_view::npos) {
        return std::nullopt;
    }
    i += 8;
    const auto ws = [&] {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
            i++;
        }
    };
    const auto eat = [&](char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            i++;
            return true;
        }
        return false;
    };
    if (!eat(':') || !eat('{')) {
        return std::nullopt;
    }
    OnlineCounts out;
    if (eat('}')) {
        return out;
    }
    for (;;) {
        if (!eat('"')) {
            return std::nullopt;
        }
        const auto end = s.find('"', i);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        const auto key = s.substr(i, end - i);
        i = end + 1;
        if (!eat(':')) {
            return std::nullopt;
        }
        ws();
        std::int64_t players = 0;
        const auto digits_from = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            players = std::min<std::int64_t>(players * 10 + (s[i] - '0'), 999999);
            i++;
        }
        if (i == digits_from) {
            return std::nullopt; // not a non-negative integer
        }
        std::uint64_t tid = 0;
        bool hex = key.size() == 16;
        for (const char c : key) {
            const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            hex = hex && d >= 0;
            tid = (tid << 4) | static_cast<std::uint64_t>(d < 0 ? 0 : d);
        }
        if (hex && out.size() < kOnlineCountsMaxGames) {
            out[tid] = static_cast<int>(players);
        }
        if (eat('}')) {
            return out;
        }
        if (!eat(',')) {
            return std::nullopt;
        }
    }
}

// Time to ask again: never asked, the interval passed, or the clock went back.
inline auto OnlineCountsDue(std::int64_t now, std::int64_t last) -> bool {
    return last <= 0 || now < last || now - last >= kOnlineCountsEverySeconds;
}

} // namespace sphaira::nextendo
