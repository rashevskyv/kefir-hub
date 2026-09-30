#include "i18n.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "app_paths.hpp"
#include <yyjson.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <strings.h>

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

std::vector<LanguageDef> g_available_languages;
bool g_languages_scanned = false;
std::string g_current_lang_code = "en";

std::string get_internal(std::string_view str) {
    if (str.starts_with("__")) {
        return {str.data(), str.length()};
    }

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

void ScanAvailableLanguages() {
    g_available_languages.clear();
    R_TRY_RESULT(romfsInit(), );
    ON_SCOPE_EXIT(romfsExit());

    DIR* dir = opendir("romfs:/i18n");
    if (!dir) {
        dir = opendir("romfs:/i18n/");
    }
    if (!dir) {
        g_languages_scanned = true;
        return;
    }
    ON_SCOPE_EXIT(closedir(dir));

    while (auto d = readdir(dir)) {
        if (d->d_name[0] == '.') {
            continue;
        }

        const std::string filename = d->d_name;
        if (!filename.ends_with(".json") || filename.length() <= 5) {
            continue;
        }

        const std::string code = filename.substr(0, filename.length() - 5);
        if (code.empty() || code == "ru") {
            continue;
        }

        const fs::FsPath full_path = "romfs:/i18n/" + filename;
        std::vector<u8> data;
        if (R_FAILED(fs::FsStdio().read_entire_file(full_path, data)) || data.empty()) {
            continue;
        }

        yyjson_doc* doc = yyjson_read((const char*)data.data(), data.size(),
            YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_INVALID_UNICODE);
        if (!doc) {
            continue;
        }
        ON_SCOPE_EXIT(yyjson_doc_free(doc));

        yyjson_val* root = yyjson_doc_get_root(doc);
        if (!root || !yyjson_is_obj(root)) {
            continue;
        }

        yyjson_val* name_val = yyjson_obj_get(root, "__language_name");
        if (!name_val || !yyjson_is_str(name_val)) {
            continue;
        }

        const char* name_str = yyjson_get_str(name_val);
        const size_t name_len = yyjson_get_len(name_val);
        if (!name_str || name_len == 0) {
            continue;
        }

        bool has_translation = false;
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(root, &iter);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&iter))) {
            const char* text = yyjson_get_str(key);
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (text && strncmp(text, "__", 2) && yyjson_is_str(value) && yyjson_get_len(value)) {
                has_translation = true;
                break;
            }
        }
        if (!has_translation) {
            continue;
        }

        g_available_languages.push_back(LanguageDef{
            .code = code,
            .name = std::string(name_str, name_len)
        });
    }

    std::sort(g_available_languages.begin(), g_available_languages.end(), [](const LanguageDef& a, const LanguageDef& b) {
        const int cmp = strcasecmp(a.name.c_str(), b.name.c_str());
        if (cmp != 0) return cmp < 0;
        return a.name < b.name;
    });

    g_languages_scanned = true;
}

std::span<const LanguageDef> GetSupportedLanguages() {
    if (!g_languages_scanned) {
        ScanAvailableLanguages();
    }
    return g_available_languages;
}

const LanguageDef* FindLanguageByCode(std::string_view code) {
    if (code.empty()) {
        return nullptr;
    }
    const auto languages = GetSupportedLanguages();
    for (const auto& def : languages) {
        if (code == def.code) {
            return &def;
        }
    }
    for (const auto& def : languages) {
        if (!strcasecmp(def.code.c_str(), std::string(code).c_str())) {
            return &def;
        }
    }
    if (code == "ua") return FindLanguageByCode("uk");
    if (code == "jp") return FindLanguageByCode("ja");
    if (code == "zhcn") return FindLanguageByCode("zh");
    return nullptr;
}

std::string MatchSystemLanguage() {
    u64 languageCode = 0;
    SetLanguage setLanguage = SetLanguage_ENUS;
    if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
        setMakeLanguage(languageCode, &setLanguage);
    }

    switch (setLanguage) {
        case SetLanguage_JA: return "ja";
        case SetLanguage_ENUS: return "en";
        case SetLanguage_ENGB: return "engb";
        case SetLanguage_FR: return "fr";
        case SetLanguage_FRCA: return "frca";
        case SetLanguage_DE: return "de";
        case SetLanguage_IT: return "it";
        case SetLanguage_ES: return "es";
        case SetLanguage_ES419: return "es419";
        case SetLanguage_ZHCN:
        case SetLanguage_ZHHANS: return "zh";
        case SetLanguage_ZHTW:
        case SetLanguage_ZHHANT: return "zhtw";
        case SetLanguage_KO: return "ko";
        case SetLanguage_NL: return "nl";
        case SetLanguage_PT: return "pt";
        case SetLanguage_PTBR: return "ptbr";
        case SetLanguage_RU: return "en"; // Russian maps to English fallback
        default: return "en";
    }
}

std::string MigrateLegacyLanguage(std::string_view saved_val, bool has_saved_key) {
    if (!has_saved_key) {
        return "";
    }

    std::string s_val = std::string(saved_val);
    while (!s_val.empty() && std::isspace(static_cast<unsigned char>(s_val.front()))) s_val.erase(s_val.begin());
    while (!s_val.empty() && std::isspace(static_cast<unsigned char>(s_val.back()))) s_val.pop_back();

    if (s_val.empty() || s_val == "ru") {
        return "";
    }

    char* end = nullptr;
    long num = std::strtol(s_val.c_str(), &end, 10);
    if (end != s_val.c_str() && *end == '\0') {
        if (num == 0 || num == 11) {
            return "";
        }
        const char* code = nullptr;
        switch (num) {
            case 1:  code = "en"; break;
            case 2:  code = "ja"; break;
            case 3:  code = "fr"; break;
            case 4:  code = "de"; break;
            case 5:  code = "it"; break;
            case 6:  code = "es"; break;
            case 7:  code = "zh"; break;
            case 8:  code = "ko"; break;
            case 9:  code = "nl"; break;
            case 10: code = "pt"; break;
            case 12: code = "se"; break;
            case 13: code = "vi"; break;
            case 14: code = "uk"; break;
            case 15: code = "be"; break;
            case 16: code = "engb"; break;
            case 17: code = "es419"; break;
            case 18: code = "et"; break;
            case 19: code = "frca"; break;
            case 20: code = "id"; break;
            case 21: code = "kk"; break;
            case 22: code = "lt"; break;
            case 23: code = "lv"; break;
            case 24: code = "pl"; break;
            case 25: code = "ptbr"; break;
            case 26: code = "tr"; break;
            case 27: code = "zhtw"; break;
            default: return "";
        }
        if (code && FindLanguageByCode(code)) {
            return code;
        }
        return "";
    }

    if (const auto* def = FindLanguageByCode(s_val)) {
        return def->code;
    }

    return "";
}

bool init(std::string_view code) {
    g_tr_cache.clear();
    exit();

    R_TRY_RESULT(romfsInit(), false);
    ON_SCOPE_EXIT(romfsExit());

    // Load romfs built-in English fallback first
    const fs::FsPath en_path = "romfs:/i18n/en.json";
    Result rc = fs::FsStdio().read_entire_file(en_path, g_en_data);
    if (R_SUCCEEDED(rc)) {
        en_json = yyjson_read((const char*)g_en_data.data(), g_en_data.size(),
            YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_INVALID_UNICODE);
        if (en_json) {
            en_root = yyjson_doc_get_root(en_json);
        }
    }

    std::string lang_name = std::string(code);
    const auto* def = FindLanguageByCode(lang_name);
    if (def) {
        lang_name = def->code;
    } else {
        lang_name = "en";
    }

    g_current_lang_code = lang_name;

    if (lang_name != "en") {
        const fs::FsPath romfs_path = "romfs:/i18n/" + lang_name + ".json";
        rc = fs::FsStdio().read_entire_file(romfs_path, g_romfs_data);
        if (R_SUCCEEDED(rc)) {
            romfs_json = yyjson_read((const char*)g_romfs_data.data(), g_romfs_data.size(),
                YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_INVALID_UNICODE);
            if (romfs_json) {
                romfs_root = yyjson_doc_get_root(romfs_json);
            }
        }
    }

    // Try loading SDMC override translation
    const fs::FsPath sdmc_path = paths::I18N + lang_name + ".json";
    rc = fs::FsNativeSd().read_entire_file(sdmc_path, g_sdmc_data);
    if (R_SUCCEEDED(rc)) {
        sdmc_json = yyjson_read((const char*)g_sdmc_data.data(), g_sdmc_data.size(),
            YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_INVALID_UNICODE);
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
    return g_current_lang_code == "uk";
}

} // namespace sphaira::i18n

namespace literals {

std::string operator""_i18n(const char* str, size_t len) {
    return sphaira::i18n::get_internal({str, len});
}

} // namespace literals
