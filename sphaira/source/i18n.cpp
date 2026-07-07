#include "i18n.hpp"
#include "fs.hpp"
#include "log.hpp"
#include <yyjson.h>
#include <vector>
#include <unordered_map>

namespace sphaira::i18n {
namespace {

std::vector<u8> g_sdmc_data;
std::vector<u8> g_romfs_data;
yyjson_doc* sdmc_json = nullptr;
yyjson_val* sdmc_root = nullptr;
yyjson_doc* romfs_json = nullptr;
yyjson_val* romfs_root = nullptr;
std::unordered_map<std::string, std::string> g_tr_cache;

std::string get_internal(std::string_view str) {
    const std::string kkey = {str.data(), str.length()};

    if (auto it = g_tr_cache.find(kkey); it != g_tr_cache.end()) {
        return it->second;
    }

    // add default entry in cache
    const auto it = g_tr_cache.emplace(kkey, kkey).first;

    // 1. Try to find the key in SDMC override file first
    if (sdmc_json && sdmc_root) {
        auto key = yyjson_obj_getn(sdmc_root, str.data(), str.length());
        if (key) {
            auto val = yyjson_get_str(key);
            auto val_len = yyjson_get_len(key);
            if (val && val_len) {
                const std::string ret = {val, val_len};
                g_tr_cache.insert_or_assign(it, kkey, ret);
                return ret;
            }
        }
    }

    // 2. Fall back to romfs if key was not found or invalid in SDMC
    if (romfs_json && romfs_root) {
        auto key = yyjson_obj_getn(romfs_root, str.data(), str.length());
        if (key) {
            auto val = yyjson_get_str(key);
            auto val_len = yyjson_get_len(key);
            if (val && val_len) {
                const std::string ret = {val, val_len};
                g_tr_cache.insert_or_assign(it, kkey, ret);
                return ret;
            }
        }
    }

    return kkey;
}

} // namespace

bool init(long index) {
    g_tr_cache.clear();
    R_TRY_RESULT(romfsInit(), false);
    ON_SCOPE_EXIT( romfsExit() );

    u64 languageCode;
    SetLanguage setLanguage = SetLanguage_ENGB;
    std::string lang_name = "en";

    switch (index) {
        case 0: // auto
            if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
                setMakeLanguage(languageCode, &setLanguage);
            }
            break;

        case 1: setLanguage = SetLanguage_ENGB; break; // "English"
        case 2: setLanguage = SetLanguage_JA; break; // "Japanese"
        case 3: setLanguage = SetLanguage_FR; break; // "French"
        case 4: setLanguage = SetLanguage_DE; break; // "German"
        case 5: setLanguage = SetLanguage_IT; break; // "Italian"
        case 6: setLanguage = SetLanguage_ES; break; // "Spanish"
        case 7: setLanguage = SetLanguage_ZHCN; break; // "Chinese"
        case 8: setLanguage = SetLanguage_KO; break; // "Korean"
        case 9: setLanguage = SetLanguage_NL; break; // "Dutch"
        case 10: setLanguage = SetLanguage_PT; break; // "Portuguese"
        case 11: setLanguage = SetLanguage_RU; break; // "Russian"
        case 12: lang_name = "se"; break; // "Swedish"
        case 13: lang_name = "vi"; break; // "Vietnamese"
        case 14: lang_name = "uk"; break; // "Ukrainian"
    }

    switch (setLanguage) {
        case SetLanguage_JA: lang_name = "ja"; break;
        case SetLanguage_FR: lang_name = "fr"; break;
        case SetLanguage_DE: lang_name = "de"; break;
        case SetLanguage_IT: lang_name = "it"; break;
        case SetLanguage_ES: lang_name = "es"; break;
        case SetLanguage_ZHCN: lang_name = "zh"; break; 
        case SetLanguage_KO: lang_name = "ko"; break;
        case SetLanguage_NL: lang_name = "nl"; break;
        case SetLanguage_PT: lang_name = "pt"; break;
        case SetLanguage_RU: lang_name = "ru"; break;
        case SetLanguage_ZHTW: lang_name = "zh"; break;
        default: break;
    }

    const fs::FsPath sdmc_path = "/config/sphaira/i18n/" + lang_name + ".json";
    const fs::FsPath romfs_path = "romfs:/i18n/" + lang_name + ".json";

    // Load romfs built-in translation first (always loaded as fallback)
    Result rc = fs::FsStdio().read_entire_file(romfs_path, g_romfs_data);
    if (R_SUCCEEDED(rc)) {
        romfs_json = yyjson_read((const char*)g_romfs_data.data(), g_romfs_data.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS|YYJSON_READ_ALLOW_COMMENTS|YYJSON_READ_ALLOW_INVALID_UNICODE);
        if (romfs_json) {
            romfs_root = yyjson_doc_get_root(romfs_json);
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

    return (romfs_json != nullptr) || (sdmc_json != nullptr);
}

void exit() {
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
}

std::string get(std::string_view str) {
    return get_internal(str);
}

} // namespace sphaira::i18n

namespace literals {

std::string operator""_i18n(const char* str, size_t len) {
    return sphaira::i18n::get_internal({str, len});
}

} // namespace literals
