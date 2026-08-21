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

// Safely truncates a UTF-8 string to at most max_bytes without splitting any multi-byte code point.
inline std::string TruncateUtf8(std::string_view input, size_t max_bytes) {
    if (input.size() <= max_bytes) {
        return std::string(input);
    }
    size_t i = 0;
    size_t valid_end = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        size_t char_len = 1;
        if (c < 0x80) {
            char_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            char_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            char_len = 4;
        } else {
            char_len = 1;
        }

        if (i + char_len <= max_bytes && i + char_len <= input.size()) {
            valid_end = i + char_len;
            i += char_len;
        } else {
            break;
        }
    }
    return std::string(input.substr(0, valid_end));
}

// Sanitizes a UTF-8 title name preserving Unicode (Cyrillic, CJK, Emoji, accents),
// replacing only illegal filesystem characters and ASCII control codes with '_'.
inline std::string SanitizeUtf8TitleName(std::string_view input) {
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
                if (!repl) {
                    out.push_back('_');
                    repl = true;
                }
            } else {
                out.append(input.data() + i, len);
                repl = false;
            }
            i += len;
        }
    }
    return out;
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

// Evaluates title candidates for MTP display names preserving UTF-8 Unicode characters:
// 1. Localized / Current name
// 2. American English (slot 0)
// 3. British English (slot 1)
// 4. Title ID fallback
inline std::string ResolveMtpDisplayTitleName(const char* localized_name, const char* name_en_us, const char* name_en_gb, std::uint64_t app_id) {
    std::vector<std::string_view> candidates;
    if (localized_name && localized_name[0] != '\0') {
        candidates.emplace_back(localized_name);
    }
    if (name_en_us && name_en_us[0] != '\0') {
        candidates.emplace_back(name_en_us);
    }
    if (name_en_gb && name_en_gb[0] != '\0') {
        candidates.emplace_back(name_en_gb);
    }

    for (const auto& cand : candidates) {
        std::string sanitized = SanitizeUtf8TitleName(cand);
        while (!sanitized.empty() && (sanitized.front() == ' ' || sanitized.front() == '\t')) {
            sanitized.erase(sanitized.begin());
        }
        while (!sanitized.empty() && (sanitized.back() == ' ' || sanitized.back() == '\t')) {
            sanitized.pop_back();
        }
        if (IsUsableTitleName(sanitized)) {
            return sanitized;
        }
    }
    return FormatTitleId(app_id);
}

inline std::string FormatMtpGameDirName(const char* localized_name, const char* name_en_us, const char* name_en_gb, std::uint64_t app_id, size_t max_entry_len = 255) {
    char suffix[0x20];
    std::snprintf(suffix, sizeof(suffix), "[%016llX]", static_cast<unsigned long long>(app_id));

    std::string base = ResolveMtpDisplayTitleName(localized_name, name_en_us, name_en_gb, app_id);
    if (base == FormatTitleId(app_id) || base.empty()) {
        return std::string(suffix);
    }

    const auto suffix_len = std::strlen(suffix) + 1; // + separating space
    if (base.size() + suffix_len > max_entry_len) {
        base = TruncateUtf8(base, max_entry_len - suffix_len);
        while (!base.empty() && base.back() == ' ') {
            base.pop_back();
        }
    }

    if (base.empty()) {
        return std::string(suffix);
    }
    return base + " " + suffix;
}

} // namespace sphaira::title
