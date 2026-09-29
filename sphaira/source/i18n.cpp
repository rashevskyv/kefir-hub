#include "i18n.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "app_paths.hpp"
#include <yyjson.h>
#include <vector>
#include <unordered_map>

namespace sphaira::i18n {
namespace {

std::vector<u8> g_sdmc_data;
std::vector<u8> g_romfs_data;
std::vector<u8> g_en_data;
yyjson_doc* sdmc_json = nullptr;
yyjson_val* sdmc_root = nullptr;
yyjson_doc* romfs_json = nullptr;
yyjson_val* romfs_root = nullptr;
yyjson_doc* en_json = nullptr;
yyjson_val* en_root = nullptr;
std::unordered_map<std::string, std::string> g_tr_cache;

std::string get_internal(std::string_view str) {
    const std::string kkey = {str.data(), str.length()};

    if (auto it = g_tr_cache.find(kkey); it != g_tr_cache.end()) {
        return it->second;
    }

    // add default entry in cache
    const auto it = g_tr_cache.emplace(kkey, kkey).first;

    // First hit wins, in order:
    //   1. the SDMC override file, so a user translation beats everything.
    //   2. the romfs file for the selected language.
    //   3. built-in English, for keys that are stable across languages
    //      (for example module.<TID>.description).
    for (const auto root : {sdmc_root, romfs_root, en_root}) {
        if (!root) {
            continue;
        }

        const auto key = yyjson_obj_getn(root, str.data(), str.length());
        if (!key) {
            continue;
        }

        const auto val = yyjson_get_str(key);
        const auto val_len = yyjson_get_len(key);
        if (val && val_len) {
            const std::string ret = {val, val_len};
            g_tr_cache.insert_or_assign(it, kkey, ret);
            return ret;
        }
    }

    return kkey;
}

} // namespace

constexpr LanguageDef SUPPORTED_LANGUAGES[] = {
    { 1,  "en",    "en",    "English",                   "English",                   SetLanguage_ENUS },
    { 2,  "ja",    "jp",    "Japanese",                  "日本語",                     SetLanguage_JA },
    { 3,  "fr",    "fr",    "French",                    "Français",                  SetLanguage_FR },
    { 4,  "de",    "de",    "German",                    "Deutsch",                   SetLanguage_DE },
    { 5,  "it",    "it",    "Italian",                   "Italiano",                  SetLanguage_IT },
    { 6,  "es",    "es",    "Spanish",                   "Español",                   SetLanguage_ES },
    { 7,  "zh",    "zhcn",  "Chinese (Simplified)",       "简体中文",                   SetLanguage_ZHCN },
    { 8,  "ko",    "kr",    "Korean",                    "한국어",                     SetLanguage_KO },
    { 9,  "nl",    "nl",    "Dutch",                     "Nederlands",                SetLanguage_NL },
    { 10, "pt",    "pt",    "Portuguese (Portugal)",     "Português",                 SetLanguage_PT },
    { 12, "se",    "se",    "Swedish",                   "Svenska",                   (SetLanguage)-1 },
    { 13, "vi",    "vi",    "Vietnamese",                "Tiếng Việt",                (SetLanguage)-1 },
    { 14, "uk",    "ua",    "Ukrainian",                 "Українська",                (SetLanguage)-1 },
    { 15, "be",    "be",    "Belarusian",                "Беларуская",                (SetLanguage)-1 },
    { 16, "engb",  "engb",  "English (UK)",              "English (UK)",              SetLanguage_ENGB },
    { 17, "es419", "es419", "Spanish (Latin America)",   "Español (Latinoamérica)",   SetLanguage_ES419 },
    { 18, "et",    "et",    "Estonian",                  "Eesti",                     (SetLanguage)-1 },
    { 19, "frca",  "frca",  "French (Canada)",           "Français (Canada)",         SetLanguage_FRCA },
    { 20, "id",    "id",    "Indonesian",                "Bahasa Indonesia",          (SetLanguage)-1 },
    { 21, "kk",    "kk",    "Kazakh",                    "Қазақша (Qazaqsha)",        (SetLanguage)-1 },
    { 22, "lt",    "lt",    "Lithuanian",                "Lietuvių",                  (SetLanguage)-1 },
    { 23, "lv",    "lv",    "Latvian",                   "Latviešu",                  (SetLanguage)-1 },
    { 24, "pl",    "pl",    "Polish",                    "Polski",                    (SetLanguage)-1 },
    { 25, "ptbr",  "ptbr",  "Portuguese (Brazil)",       "Português (Brasil)",        SetLanguage_PTBR },
    { 26, "tr",    "tr",    "Turkish",                   "Türkçe",                    (SetLanguage)-1 },
    { 27, "zhtw",  "zhtw",  "Chinese (Traditional)",      "繁體中文",                   SetLanguage_ZHTW },
};

static std::string g_current_lang_code = "en";
static long g_current_lang_id = 1;

std::span<const LanguageDef> GetSupportedLanguages() {
    return SUPPORTED_LANGUAGES;
}

const LanguageDef* FindLanguage(long id) {
    for (const auto& def : SUPPORTED_LANGUAGES) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

const LanguageDef* FindLanguageByCode(std::string_view code) {
    for (const auto& def : SUPPORTED_LANGUAGES) {
        if (code == def.code) {
            return &def;
        }
    }
    for (const auto& def : SUPPORTED_LANGUAGES) {
        if (code == def.dbi_code) {
            return &def;
        }
    }
    return nullptr;
}

const LanguageDef* FindLanguageByDbiCode(std::string_view dbi_code) {
    for (const auto& def : SUPPORTED_LANGUAGES) {
        if (dbi_code == def.dbi_code || dbi_code == def.code) {
            return &def;
        }
    }
    return nullptr;
}

long MatchSystemLanguage() {
    u64 languageCode = 0;
    SetLanguage setLanguage = SetLanguage_ENUS;
    if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
        setMakeLanguage(languageCode, &setLanguage);
    }

    switch (setLanguage) {
        case SetLanguage_JA: return 2; // Japanese
        case SetLanguage_ENUS: return 1; // English
        case SetLanguage_ENGB: return 16; // English (UK)
        case SetLanguage_FR: return 3; // French
        case SetLanguage_FRCA: return 19; // French (Canada)
        case SetLanguage_DE: return 4; // German
        case SetLanguage_IT: return 5; // Italian
        case SetLanguage_ES: return 6; // Spanish
        case SetLanguage_ES419: return 17; // Spanish (Latin America)
        case SetLanguage_ZHCN:
        case SetLanguage_ZHHANS: return 7; // Chinese (Simplified)
        case SetLanguage_ZHTW:
        case SetLanguage_ZHHANT: return 27; // Chinese (Traditional)
        case SetLanguage_KO: return 8; // Korean
        case SetLanguage_NL: return 9; // Dutch
        case SetLanguage_PT: return 10; // Portuguese (Portugal)
        case SetLanguage_PTBR: return 25; // Portuguese (Brazil)
        case SetLanguage_RU: return 1; // Russian maps to English
        default: return 1; // Default to English
    }
}

long MigrateLegacyLanguage(long old_index, bool has_saved_key) {
    if (!has_saved_key) {
        return MatchSystemLanguage();
    }
    if (old_index == 0) { // Legacy Auto
        return MatchSystemLanguage();
    }
    if (old_index == 11) { // Legacy Russian
        return 1; // English
    }
    if (FindLanguage(old_index) != nullptr) {
        return old_index;
    }
    return 1;
}

bool init(long index) {
    g_tr_cache.clear();
    R_TRY_RESULT(romfsInit(), false);
    ON_SCOPE_EXIT( romfsExit() );

    if (index == 0) {
        index = MatchSystemLanguage();
    } else if (index == 11) {
        index = 1;
    }

    const auto* def = FindLanguage(index);
    if (!def) {
        def = FindLanguage(1);
        index = 1;
    }

    g_current_lang_id = index;
    g_current_lang_code = def ? def->code : "en";
    const std::string lang_name = g_current_lang_code;

    const fs::FsPath sdmc_path = paths::I18N + lang_name + ".json";
    const fs::FsPath romfs_path = "romfs:/i18n/" + lang_name + ".json";

    // Load romfs built-in translation first (always loaded as fallback)
    Result rc = fs::FsStdio().read_entire_file(romfs_path, g_romfs_data);
    if (R_SUCCEEDED(rc)) {
        romfs_json = yyjson_read((const char*)g_romfs_data.data(), g_romfs_data.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS|YYJSON_READ_ALLOW_COMMENTS|YYJSON_READ_ALLOW_INVALID_UNICODE);
        if (romfs_json) {
            romfs_root = yyjson_doc_get_root(romfs_json);
        }
    }

    if (lang_name != "en") {
        const fs::FsPath en_path = "romfs:/i18n/en.json";
        rc = fs::FsStdio().read_entire_file(en_path, g_en_data);
        if (R_SUCCEEDED(rc)) {
            en_json = yyjson_read((const char*)g_en_data.data(), g_en_data.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS|YYJSON_READ_ALLOW_COMMENTS|YYJSON_READ_ALLOW_INVALID_UNICODE);
            if (en_json) {
                en_root = yyjson_doc_get_root(en_json);
            }
        }
    }

    // Try loading SDMC override translation
    rc = fs::FsNativeSd().read_entire_file(sdmc_path, g_sdmc_data);
    if (R_SUCCEEDED(rc)) {
        sdmc_json = yyjson_read((const char*)g_sdmc_data.data(), g_sdmc_data.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS|YYJSON_READ_ALLOW_COMMENTS|YYJSON_READ_ALLOW_INVALID_UNICODE);
        if (sdmc_json) {
            sdmc_root = yyjson_doc_get_root(sdmc_json);
        }
    }

    return (romfs_json != nullptr) || (sdmc_json != nullptr) || (en_json != nullptr);
}

void exit() {
    if (en_json) {
        yyjson_doc_free(en_json);
        en_json = nullptr;
        en_root = nullptr;
    }
    if (sdmc_json) {
        yyjson_doc_free(sdmc_json);
        sdmc_json = nullptr;
        sdmc_root = nullptr;
    }
    if (romfs_json) {
        yyjson_doc_free(romfs_json);
        romfs_json = nullptr;
        romfs_root = nullptr;
    }
    g_sdmc_data.clear();
    g_romfs_data.clear();
    g_en_data.clear();
}

std::string get(std::string_view str) {
    return get_internal(str);
}

std::string_view GetCurrentLanguageCode() {
    return g_current_lang_code;
}

bool IsUkrainian() {
    return g_current_lang_id == 14 || g_current_lang_code == "uk";
}

} // namespace sphaira::i18n

namespace literals {

std::string operator""_i18n(const char* str, size_t len) {
    return sphaira::i18n::get_internal({str, len});
}

} // namespace literals
