#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <cctype>

namespace sphaira::nfs {

// Matches usable capacity of fs::FsPath (FS_MAX_PATH = 769, 768 chars + NUL).
inline constexpr size_t kMaxNfsUrlLength = 768;

struct ParsedUrl {
    std::string server{};
    std::string export_path{};
    uint16_t port{0};
    bool valid{false};
    std::string error{};
};

inline bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

inline int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline ParsedUrl ParseUrl(std::string_view url) {
    ParsedUrl out{};

    if (url.empty()) {
        out.error = "URL is empty";
        return out;
    }

    if (url.size() > kMaxNfsUrlLength) {
        out.error = "URL exceeds maximum length";
        return out;
    }

    // Reject whitespace and control characters anywhere
    for (char c : url) {
        if (static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) == 0x7F) {
            out.error = "URL contains whitespace or control characters";
            return out;
        }
    }

    // Check scheme: must start with exact lowercase "nfs://"
    constexpr std::string_view kScheme = "nfs://";
    if (!url.starts_with(kScheme)) {
        out.error = "URL must begin with lowercase nfs://";
        return out;
    }

    std::string_view rest = url.substr(kScheme.size());

    // Reject credentials (username/password)
    if (rest.find('@') != std::string_view::npos) {
        out.error = "Credentials are not supported in NFS URLs";
        return out;
    }

    // Reject query and fragment
    if (rest.find('?') != std::string_view::npos) {
        out.error = "Query parameters are not supported in NFS URLs";
        return out;
    }
    if (rest.find('#') != std::string_view::npos) {
        out.error = "Fragment identifiers are not supported in NFS URLs";
        return out;
    }

    // Reject backslashes
    if (rest.find('\\') != std::string_view::npos) {
        out.error = "Backslashes are not allowed in NFS URLs";
        return out;
    }

    // Reject IPv6 in v1
    if (rest.find('[') != std::string_view::npos || rest.find(']') != std::string_view::npos) {
        out.error = "IPv6 is not supported in NFS v1";
        return out;
    }

    // Find export path separator
    size_t slash_pos = rest.find('/');
    if (slash_pos == std::string_view::npos) {
        out.error = "Missing export path";
        return out;
    }

    std::string_view authority = rest.substr(0, slash_pos);
    std::string_view path = rest.substr(slash_pos);

    if (authority.empty()) {
        out.error = "Missing host in URL";
        return out;
    }

    // Parse authority: host and optional port
    std::string_view host = authority;
    uint16_t port = 0;

    size_t colon_pos = authority.rfind(':');
    if (colon_pos != std::string_view::npos) {
        host = authority.substr(0, colon_pos);
        std::string_view port_str = authority.substr(colon_pos + 1);

        if (host.empty()) {
            out.error = "Missing host in URL";
            return out;
        }
        if (port_str.empty()) {
            out.error = "Empty port in URL";
            return out;
        }

        uint64_t parsed_port = 0;
        for (char c : port_str) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                out.error = "Malformed port number";
                return out;
            }
            parsed_port = parsed_port * 10 + static_cast<uint64_t>(c - '0');
            if (parsed_port > 65535) {
                out.error = "Port number exceeds 65535";
                return out;
            }
        }

        if (parsed_port == 0) {
            out.error = "Port number cannot be 0";
            return out;
        }

        port = static_cast<uint16_t>(parsed_port);
    }

    // Validate host characters (no percent encoding, valid hostname/IPv4 chars only)
    if (host.front() == '.' || host.front() == '-' || host.back() == '.' || host.back() == '-') {
        out.error = "Hostname cannot start or end with a dot or hyphen";
        return out;
    }

    for (size_t i = 0; i < host.size(); ++i) {
        char c = host[i];
        if (c == '.' && i + 1 < host.size() && host[i + 1] == '.') {
            out.error = "Consecutive dots in hostname";
            return out;
        }
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') {
            out.error = "Invalid characters in hostname";
            return out;
        }
    }

    // Validate export path
    if (path.empty() || path == "/") {
        out.error = "Export path cannot be empty or root-only";
        return out;
    }

    // Check path segments for dot/traversal and percent-encoded dot/traversal/separator
    size_t seg_start = 1; // skip leading '/'
    while (seg_start < path.size()) {
        size_t next_slash = path.find('/', seg_start);
        std::string_view segment = (next_slash == std::string_view::npos)
                                       ? path.substr(seg_start)
                                       : path.substr(seg_start, next_slash - seg_start);

        if (segment.empty()) {
            out.error = "Empty segment in export path";
            return out;
        }
        if (segment == "." || segment == "..") {
            out.error = "Dot and traversal segments are not allowed in export path";
            return out;
        }

        // Check for percent-encoded traversal and separators (%2e, %2E, %2f, %2F, %5c, %5C)
        // and decode segment to check for decoded traversal
        std::string decoded;
        decoded.reserve(segment.size());
        for (size_t i = 0; i < segment.size(); ++i) {
            char c = segment[i];
            if (c == '%') {
                if (i + 2 >= segment.size() || !IsHexDigit(segment[i + 1]) || !IsHexDigit(segment[i + 2])) {
                    out.error = "Malformed percent-encoding in export path";
                    return out;
                }
                int val = (HexValue(segment[i + 1]) << 4) | HexValue(segment[i + 2]);
                char dec = static_cast<char>(val);
                // Explicitly reject percent-encoded dot (0x2E), slash (0x2F), backslash (0x5C), null byte (0x00)
                if (val == 0x2E || val == 0x2F || val == 0x5C || val == 0x00) {
                    out.error = "Encoded dot and separator characters are not allowed in export path";
                    return out;
                }
                decoded.push_back(dec);
                i += 2;
            } else {
                decoded.push_back(c);
            }
        }

        if (decoded == "." || decoded == "..") {
            out.error = "Decoded dot and traversal segments are not allowed in export path";
            return out;
        }

        if (next_slash == std::string_view::npos) {
            break;
        }
        seg_start = next_slash + 1;
    }

    out.server = std::string(host);
    out.export_path = std::string(path);
    out.port = port;
    out.valid = true;
    return out;
}

inline bool ValidateUrl(std::string_view url, std::string* error_out = nullptr) {
    ParsedUrl parsed = ParseUrl(url);
    if (!parsed.valid && error_out) {
        *error_out = parsed.error;
    }
    return parsed.valid;
}

} // namespace sphaira::nfs

#if defined(__SWITCH__)
#include "utils/devoptab_common.hpp"

namespace sphaira::devoptab::nfs {
bool Mount(const common::MountConfig& config, const char* name, const char* mount_name);
Result TestConnection(const std::string& url);
} // namespace sphaira::devoptab::nfs
#endif
