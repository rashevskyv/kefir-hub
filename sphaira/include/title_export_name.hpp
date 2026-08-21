#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::title {

// Checks whether a candidate name has usable semantic content (not empty and
// not solely comprised of whitespace, periods, or underscores).
inline bool IsUsableTitleName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        if (c != ' ' && c != '.' && c != '_') {
            return true;
        }
    }
    return false;
}

// Formats a 64-bit Title ID as a 16-character uppercase hex string.
inline std::string FormatTitleId(std::uint64_t id) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(id));
    return std::string(buf);
}

// Sanitizes a title name string for ASCII-safe filesystem NSP export, replacing
// illegal filesystem characters and non-ASCII sequences with a single underscore.
inline std::string SanitizeAsciiTitleName(std::string_view input) {
    static const char g_illegal[] = "\\/:*?\"<>|";
    std::string out;
    out.reserve(input.size());
    bool repl = false;

    for (size_t i = 0; i < input.size(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7F || std::strchr(g_illegal, c) != nullptr) {
                if (!repl) {
                    out.push_back('_');
                    repl = true;
                }
            } else {
                out.push_back(static_cast<char>(c));
                repl = false;
            }
            i++;
        } else {
            size_t len = 1;
            if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;

            if (i + len > input.size()) {
                len = 1;
            }
            if (!repl) {
                out.push_back('_');
                repl = true;
            }
            i += len;
        }
    }
    return out;
}

// Truncates a title name to max_len characters if necessary.
inline std::string TruncateTitleName(std::string_view name, size_t max_len = 160) {
    if (name.size() <= max_len) {
        return std::string(name);
    }
    return std::string(name.substr(0, max_len));
}

// Evaluates an ordered list of title candidates for ASCII-safe NSP export:
// Sanitizes each candidate, checks usability, truncates if valid, and falls back
// to formatted Title ID if no candidate is usable.
inline std::string ResolveExportTitleNameFromCandidates(std::span<const std::string_view> candidates, std::uint64_t app_id, size_t max_len = 160) {
    for (const auto& cand : candidates) {
        if (cand.empty()) {
            continue;
        }
        std::string sanitized = SanitizeAsciiTitleName(cand);
        if (IsUsableTitleName(sanitized)) {
            return TruncateTitleName(sanitized, max_len);
        }
    }
    return FormatTitleId(app_id);
}

// Standard helper evaluating the hierarchy:
// 1. American English (slot 0)
// 2. British English (slot 1)
// 3. Localized / Current name
// 4. Title ID hex fallback
inline std::string ResolveExportTitleName(const char* name_en_us, const char* name_en_gb, const char* localized_name, std::uint64_t app_id, size_t max_len = 160) {
    std::vector<std::string_view> candidates;
    if (name_en_us && name_en_us[0] != '\0') {
        candidates.emplace_back(name_en_us);
    }
    if (name_en_gb && name_en_gb[0] != '\0') {
        candidates.emplace_back(name_en_gb);
    }
    if (localized_name && localized_name[0] != '\0') {
        candidates.emplace_back(localized_name);
    }
    return ResolveExportTitleNameFromCandidates(candidates, app_id, max_len);
}

inline std::string ResolveExportTitleName(const char* localized_name, std::uint64_t app_id, size_t max_len = 160) {
    return ResolveExportTitleName(nullptr, nullptr, localized_name, app_id, max_len);
}

} // namespace sphaira::title
