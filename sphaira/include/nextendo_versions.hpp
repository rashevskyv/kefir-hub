#pragma once

// Nextendo versions shown in Kefir Settings -> Nextendo, and what the Hub does when they differ. Pure rules, no network
// (nextendo_refresh.cpp does the requests). Host test tests/test_nextendo_versions.cpp.
// The newest Prelude tag is read from the network; only the tag this Hub carries (kPinnedTag) is compiled in.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sphaira::nextendo {

inline constexpr std::int64_t kVersionsEverySeconds = 60 * 60;

using Version = std::array<int, 3>;

// "v5.0.2", "5.0", "5.0.2-rc1" -> {5,0,2}. A missing part is 0; a suffix after the third number is ignored.
inline auto ParseVersion(std::string_view s) -> std::optional<Version> {
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
        s.remove_prefix(1);
    }
    Version v{};
    std::size_t i = 0;
    for (int part = 0; part < 3; part++) {
        const auto from = i;
        long n = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && n < 100000) {
            n = n * 10 + (s[i] - '0');
            i++;
        }
        if (i == from) {
            if (part == 0) {
                return std::nullopt;
            }
            break;
        }
        v[part] = static_cast<int>(n);
        if (i < s.size() && s[i] == '.' && part < 2) {
            i++;
        } else {
            break;
        }
    }
    return v;
}

// <0 a older, 0 same, >0 a newer
inline auto Compare(const Version& a, const Version& b) -> int {
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

enum class Verdict {
    Unknown,       // the newest Prelude release could not be read (offline, server down)
    Match,         // this Hub carries the newest Prelude release
    HubBehind,     // a newer Prelude release exists: Hub update brings its patches and server address
    HubAhead,      // this Hub carries a newer tag than the forge shows (forge lag): nothing to do
};

struct Versions {
    std::string hub;      // tag carried by this Hub (compiled in)
    std::string prelude;  // newest Prelude release on the forge, empty = not read yet / failed
};

inline auto Judge(const Versions& v) -> Verdict {
    const auto hub = ParseVersion(v.hub);
    const auto pre = ParseVersion(v.prelude);
    if (!hub || !pre) {
        return Verdict::Unknown;
    }
    const int c = Compare(*hub, *pre);
    return c < 0 ? Verdict::HubBehind : c > 0 ? Verdict::HubAhead : Verdict::Match;
}

// HubBehind: the row says an update exists and A tells to update Kefir Hub. The Hub never changes the card by itself
// on a version difference: patches come only with a Hub release (sync robot).
inline auto UpdateAvailable(const Versions& v) -> bool {
    return Judge(v) == Verdict::HubBehind;
}

inline auto VersionsDue(std::int64_t now, std::int64_t last) -> bool {
    return last <= 0 || now < last || now - last >= kVersionsEverySeconds;
}

} // namespace sphaira::nextendo
