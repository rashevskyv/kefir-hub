#pragma once

#include <string>
#include <string_view>
#include <span>
#include <switch.h>

namespace sphaira::i18n {

struct LanguageDef {
    long id;
    const char* code;         // e.g. "en", "uk", "ja", ... (JSON file: romfs:/i18n/<code>.json)
    const char* dbi_code;     // e.g. "en", "ua", "jp", ... (matching DBI patcher codes)
    const char* name_en;      // English display name
    const char* name_native;  // Autonym in native script
    SetLanguage set_language; // Matching Switch system language (or (SetLanguage)-1 if none)
};

std::span<const LanguageDef> GetSupportedLanguages();
const LanguageDef* FindLanguage(long id);
const LanguageDef* FindLanguageByCode(std::string_view code);
const LanguageDef* FindLanguageByDbiCode(std::string_view dbi_code);
long MatchSystemLanguage();
long MigrateLegacyLanguage(long old_index, bool has_saved_key);

bool init(long index);
void exit();

std::string get(std::string_view str);

std::string_view GetCurrentLanguageCode();
bool IsUkrainian();

} // namespace sphaira::i18n

inline namespace literals {

std::string operator""_i18n(const char* str, size_t len);

} // namespace literals
