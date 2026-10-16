#pragma once

// Which NX-Translation folder ("replaces_<lang>-<US|EU>") fits the console's system language, and how that
// system language is named in the console's own language list. The US/EU suffix is the language variant
// (English US / UK, Spanish Latin America / Spain), not the console region.
// Pure; host test tests/test_translation_match.cpp.

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace sphaira::ui::menu::settings::translation {

inline auto Lower(std::string_view s) -> std::string {
    std::string out{s};
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

// code: the system language from setGetSystemLanguage ("en-US", "en-GB", "es-419", "fr-CA", "ru", ...).
inline auto ReplacePathFor(std::string_view code) -> std::string {
    const bool american = code == "en-US" || code == "es-419" || code == "fr-CA" || code == "pt-BR";
    return Lower("replaces_" + std::string(code.substr(0, code.find('-'))) + (american ? "-US" : "-EU"));
}

inline auto Fits(std::string_view replace_path, std::string_view code) -> bool {
    return Lower(replace_path) == ReplacePathFor(code);
}

// The system language a replace folder takes over, as the console lists it; empty when unknown.
inline auto SystemLanguageName(std::string_view replace_path) -> std::string {
    static constexpr std::pair<std::string_view, std::string_view> NAMES[] = {
        {"replaces_en-us", "English (US)"},
        {"replaces_en-eu", "English (UK)"},
        {"replaces_ru-eu", "Русский"},
        {"replaces_de-eu", "Deutsch"},
        {"replaces_it-eu", "Italiano"},
        {"replaces_nl-eu", "Nederlands"},
        {"replaces_es-eu", "Español (España)"},
        {"replaces_es-us", "Español (Latinoamérica)"},
        {"replaces_fr-eu", "Français (France)"},
        {"replaces_fr-us", "Français (Canada)"},
        {"replaces_pt-eu", "Português (Portugal)"},
        {"replaces_pt-us", "Português (Brasil)"},
    };
    const auto path = Lower(replace_path);
    for (const auto& [p, name] : NAMES) {
        if (p == path) {
            return std::string{name};
        }
    }
    return {};
}

// Where the system language is set, in the words the console shows for that system language. English is
// from NX-Translation's english.txt (063c348f20, 5ff374709f, 17708b72ec); the rest are Nintendo's menu names.
inline auto LanguageSettingPath(std::string_view code) -> std::string {
    static constexpr std::pair<std::string_view, std::string_view> PATHS[] = {
        {"ja", "設定 → 本体 → 言語"},
        {"fr", "Paramètres de la console → Console → Langue"},
        {"fr-CA", "Paramètres de la console → Console → Langue"},
        {"de", "Systemeinstellungen → Konsole → Sprache"},
        {"it", "Impostazioni di sistema → Console → Lingua"},
        {"es", "Configuración de la consola → Consola → Idioma"},
        {"es-419", "Configuración de la consola → Consola → Idioma"},
        {"nl", "Systeeminstellingen → Systeem → Taal"},
        {"pt", "Definições da consola → Consola → Idioma"},
        {"pt-BR", "Configurações do console → Console → Idioma"},
        {"ru", "Системные настройки → Система → Язык"},
        {"ko", "설정 → 본체 → 언어"},
        {"zh-CN", "系统设置 → 主机 → 语言"},
        {"zh-Hans", "系统设置 → 主机 → 语言"},
        {"zh-TW", "設定 → 主機 → 語言"},
        {"zh-Hant", "設定 → 主機 → 語言"},
    };
    for (const auto& [c, path] : PATHS) {
        if (c == code) {
            return std::string{path};
        }
    }
    return "System Settings → System → Language";
}

} // namespace sphaira::ui::menu::settings::translation
