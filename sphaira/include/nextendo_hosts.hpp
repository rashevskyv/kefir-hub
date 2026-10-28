#pragma once

// Nextendo (Prelude-Nextendo) rules, as pure data and functions: build the Atmosphere dns_mitm hosts text from the
// pinned template, decide what to do with a hosts file already on the SD card, read server IPs from a
// nextendo_config.h text, and the firmware guard. No file or network access here; host test tests/test_nextendo_hosts.cpp.
// The template and the pinned tag come from nextendo_snapshot.hpp (generated, Kefirosphere/utilities/nextendo_extract.py).
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include "nextendo_snapshot.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace sphaira::nextendo {

// First line of every hosts file we write. The same text Prelude writes, so both tools recognise each other's file,
// and the Kefir dns_mitm patch (0010) switches its Nintendo fallback on when it sees it.
inline constexpr std::string_view kHostsMark = "NEXTENDO NETWORK - Atmosphere DNS-MITM";
inline constexpr std::size_t kMarkWindow = 512; // the mark is searched only at the start, like Prelude does

// "1.2.3.4": four parts of 1..3 digits, each <= 255. Anything else (names, ports, spaces) is refused.
inline auto IsIpv4(std::string_view s) -> bool {
    int parts = 0;
    std::size_t i = 0;
    while (i <= s.size()) {
        int digits = 0;
        int value = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            value = value * 10 + (s[i] - '0');
            digits++;
            i++;
            if (digits > 3) {
                return false;
            }
        }
        if (digits == 0 || value > 255) {
            return false;
        }
        parts++;
        if (i == s.size()) {
            break;
        }
        if (s[i] != '.') {
            return false;
        }
        i++;
    }
    return parts == 4;
}

struct ServerIps {
    std::string main;
    std::string nncs2;
};

inline auto SnapshotIps() -> ServerIps {
    return {kSnapshotIp, kSnapshotIpNncs2};
}

// The template with {IP} and {IP_NNCS2} filled in. Refuses a bad IP: it returns an empty string, never a half file.
inline auto BuildHosts(const ServerIps& ips) -> std::string {
    if (!IsIpv4(ips.main) || !IsIpv4(ips.nncs2)) {
        return {};
    }
    std::string out;
    const std::string_view tmpl = kHostsTemplate;
    out.reserve(tmpl.size() + 512);
    for (std::size_t i = 0; i < tmpl.size();) {
        if (tmpl.compare(i, 4, "{IP}") == 0) {
            out += ips.main;
            i += 4;
        } else if (tmpl.compare(i, 10, "{IP_NNCS2}") == 0) {
            out += ips.nncs2;
            i += 10;
        } else {
            out += tmpl[i++];
        }
    }
    return out;
}

inline auto HasMark(std::string_view text) -> bool {
    return text.substr(0, kMarkWindow).find(kHostsMark) != std::string_view::npos;
}

// What a hosts file on the card is, compared with what we would write now.
enum class FileState {
    Missing,   // no file, or only white space: nothing to lose
    Current,   // ours and identical to the expected text
    Stale,     // ours (has the mark) but different: old IP or older template
    UserOwned, // not ours: the user's own entries
};

inline auto Classify(const std::optional<std::string>& current, std::string_view expected) -> FileState {
    if (!current) {
        return FileState::Missing;
    }
    bool blank = true;
    for (const char c : *current) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            blank = false;
            break;
        }
    }
    if (blank) {
        return FileState::Missing;
    }
    if (!HasMark(*current)) {
        return FileState::UserOwned;
    }
    return *current == expected ? FileState::Current : FileState::Stale;
}

enum class ApplyAction { Skip, Write, BackupThenWrite };

inline auto PlanApply(FileState s) -> ApplyAction {
    switch (s) {
        case FileState::Current: return ApplyAction::Skip;
        case FileState::UserOwned: return ApplyAction::BackupThenWrite;
        default: return ApplyAction::Write;
    }
}

enum class RemoveAction { Leave, Delete };

// Switching Nextendo off removes only a file that carries our mark. A user file stays untouched.
inline auto PlanRemove(FileState s) -> RemoveAction {
    return (s == FileState::Current || s == FileState::Stale) ? RemoveAction::Delete : RemoveAction::Leave;
}

// Server IPs from the text of nextendo_config.h: the two #define lines NEXTENDO_SERVER_IP_DEFAULT and
// NEXTENDO_SERVER_IP_NNCSD2. nullopt when either is missing or not a plain IPv4: the caller keeps the snapshot.
inline auto ParseConfigIps(std::string_view text) -> std::optional<ServerIps> {
    const auto find = [&](std::string_view name) -> std::optional<std::string> {
        std::size_t pos = 0;
        while ((pos = text.find(name, pos)) != std::string_view::npos) {
            const auto line_start = text.rfind('\n', pos);
            const auto prefix = text.substr(line_start == std::string_view::npos ? 0 : line_start + 1,
                                            pos - (line_start == std::string_view::npos ? 0 : line_start + 1));
            pos += name.size();
            if (prefix.find("#define") == std::string_view::npos || prefix.find("//") != std::string_view::npos) {
                continue;
            }
            const auto q1 = text.find('"', pos);
            const auto eol = text.find('\n', pos);
            if (q1 == std::string_view::npos || (eol != std::string_view::npos && q1 > eol)) {
                continue;
            }
            const auto q2 = text.find('"', q1 + 1);
            if (q2 == std::string_view::npos) {
                continue;
            }
            const auto value = text.substr(q1 + 1, q2 - q1 - 1);
            if (IsIpv4(value)) {
                return std::string{value};
            }
        }
        return std::nullopt;
    };
    const auto main = find("NEXTENDO_SERVER_IP_DEFAULT");
    const auto nncs2 = find("NEXTENDO_SERVER_IP_NNCSD2");
    if (!main || !nncs2) {
        return std::nullopt;
    }
    return ServerIps{*main, *nncs2};
}

// Firmware versions the pinned tag ships patches for (ssl / bcat build ids). Update by hand when the pinned tag
// is raised; an unknown firmware is refused, never guessed.
inline constexpr std::array<std::array<int, 3>, 2> kSupportedFirmware{{{22, 5, 0}, {23, 0, 1}}};

inline auto FirmwareSupported(int major, int minor, int micro) -> bool {
    for (const auto& v : kSupportedFirmware) {
        if (v[0] == major && v[1] == minor && v[2] == micro) {
            return true;
        }
    }
    return false;
}

// Prelude refuses its Account Link fallback (network_mitm) from HOS 23.0.0 on.
inline auto AccountLinkAvailable(int major) -> bool {
    return major < 23;
}

} // namespace sphaira::nextendo
