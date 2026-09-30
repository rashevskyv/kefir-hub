#pragma once

#include <string>
#include <string_view>
#include <span>
#include <switch.h>

namespace sphaira::i18n {

struct LanguageDef {
    std::string code;         // e.g. "en", "uk", "ja", ... (JSON file: romfs:/i18n/<code>.json)
    std::string name;         // Display name from __language_name in JSON (e.g. "Ukrainian — Українська")
};

void ScanAvailableLanguages();
std::span<const LanguageDef> GetSupportedLanguages();
const LanguageDef* FindLanguageByCode(std::string_view code);
std::string MatchSystemLanguage();
std::string MigrateLegacyLanguage(std::string_view saved_val, bool has_saved_key);

bool init(std::string_view code);
void exit();

std::string get(std::string_view str);

std::string_view GetCurrentLanguageCode();
bool IsUkrainian();

} // namespace sphaira::i18n

inline namespace literals {

std::string operator""_i18n(const char* str, size_t len);

} // namespace literals
