#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace sphaira::forced_language {

// Atmosphère per-game locale override: /atmosphere/contents/<app_id>/config.ini,
// [override_config] override_language=<code>. Indexed like NACP languages, so bit i of
// NacpStruct::supported_language_flag says whether LANGS[i] is in the game.
struct Lang {
    const char* code;
    const char* name; // English; shown through i18n
};

inline constexpr Lang LANGS[16] = {
    {"en-US", "American English"}, {"en-GB", "British English"}, {"ja", "Japanese"}, {"fr", "French"},
    {"de", "German"}, {"es-419", "Latin American Spanish"}, {"es", "Spanish"}, {"it", "Italian"},
    {"nl", "Dutch"}, {"fr-CA", "Canadian French"}, {"pt", "Portuguese"}, {"ru", "Russian"},
    {"ko", "Korean"}, {"zh-Hant", "Traditional Chinese"}, {"zh-Hans", "Simplified Chinese"}, {"pt-BR", "Brazilian Portuguese"},
};

// NACP index of a code, case-insensitive; -1 for an unknown code.
inline auto IndexOf(std::string_view code) -> int {
    for (int i = 0; i < 16; i++) {
        const std::string_view c{LANGS[i].code};
        if (c.size() != code.size()) {
            continue;
        }
        bool same = true;
        for (size_t k = 0; k < c.size() && same; k++) {
            same = std::tolower(static_cast<unsigned char>(c[k])) == std::tolower(static_cast<unsigned char>(code[k]));
        }
        if (same) {
            return i;
        }
    }
    return -1;
}

// translation packs carry kefir_lang.json: {"format":1,"title_id":"0100...000","language":"en-US"}.
struct PackInfo {
    std::uint64_t title_id{};
    std::string language{};
};

inline constexpr int PACK_FORMAT = 1;
inline constexpr std::string_view PACK_FILE = "kefir_lang.json";

// the text of a 16-digit hex title id, or 0 when it is not one.
inline auto ParseTitleId(std::string_view s) -> std::uint64_t {
    if (s.size() != 16) {
        return 0;
    }
    std::uint64_t v{};
    for (const char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return 0;
    }
    return v;
}

// read / write the override; an empty code removes it. Other keys in config.ini are kept.
auto Get(std::uint64_t app_id) -> std::string;
bool Set(std::uint64_t app_id, std::string_view code);
// parse kefir_lang.json; false when the file is not a pack info this build understands.
bool ParsePack(std::string_view json, PackInfo& out);

} // namespace sphaira::forced_language
