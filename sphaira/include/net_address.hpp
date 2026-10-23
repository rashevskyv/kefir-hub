#pragma once

// "host:port" typed into a server field: split it so the port goes where it belongs. No libnx: host-tested by tests/test_net_address.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sphaira::net {

struct HostPort {
    std::string host{};
    std::optional<std::uint16_t> port{};
};

// "192.168.1.5:2121" -> {"192.168.1.5", 2121}; "ftp://nas.local:21/" -> {"nas.local", 21}; "[fe80::1]:21" -> {"[fe80::1]", 21}.
// A text with no usable port (none, 0, over 65535, a bare IPv6 address, a path after the host) comes back unchanged.
inline auto SplitHostPort(std::string_view in) -> HostPort {
    while (!in.empty() && (in.front() == ' ' || in.front() == '\t')) in.remove_prefix(1);
    while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n')) in.remove_suffix(1);
    const std::string original{in};

    if (const auto scheme = in.find("://"); scheme != std::string_view::npos) {
        in.remove_prefix(scheme + 3);
    }
    if (!in.empty() && in.back() == '/') {
        in.remove_suffix(1);
    }
    if (in.empty() || in.find('/') != std::string_view::npos) {
        return {original, std::nullopt};
    }

    std::string_view host = in;
    std::string_view digits;
    if (in.front() == '[') {
        const auto close = in.find(']');
        if (close == std::string_view::npos) {
            return {original, std::nullopt};
        }
        host = in.substr(0, close + 1);
        const auto rest = in.substr(close + 1);
        if (rest.empty()) {
            return {original, std::nullopt};
        }
        if (rest.front() != ':') {
            return {original, std::nullopt};
        }
        digits = rest.substr(1);
    } else {
        const auto colon = in.find(':');
        if (colon == std::string_view::npos || in.find(':', colon + 1) != std::string_view::npos) {
            return {original, std::nullopt}; // no port, or a bare IPv6 address
        }
        host = in.substr(0, colon);
        digits = in.substr(colon + 1);
    }

    if (host.empty() || digits.empty() || digits.size() > 5) {
        return {original, std::nullopt};
    }
    unsigned value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return {original, std::nullopt};
        }
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value < 1 || value > 65535) {
        return {original, std::nullopt};
    }
    return {std::string{host}, static_cast<std::uint16_t>(value)};
}

} // namespace sphaira::net
