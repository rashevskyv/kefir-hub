#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/translation_policy.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "download.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"
#include "i18n.hpp"
#include "app.hpp"
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <strings.h>
#include <yyjson.h>

namespace sphaira::ui::menu::settings::detail {

constexpr std::array TRANSLATION_PATHS{
    "/atmosphere/contents/010000000000080B/",
    "/atmosphere/contents/010000000000080C/",
    "/atmosphere/contents/010000000000100D/",
    "/atmosphere/contents/0100000000000803/",
    "/atmosphere/contents/0100000000000811/",
    "/atmosphere/contents/0100000000001000/romfs/message/",
    "/atmosphere/contents/0100000000001001/",
    "/atmosphere/contents/0100000000001002/",
    "/atmosphere/contents/0100000000001003/",
    "/atmosphere/contents/0100000000001004/",
    "/atmosphere/contents/0100000000001005/",
    "/atmosphere/contents/0100000000001006/",
    "/atmosphere/contents/0100000000001007/",
    "/atmosphere/contents/0100000000001008/",
    "/atmosphere/contents/0100000000001009/",
    "/atmosphere/contents/0100000000001012/",
    "/atmosphere/contents/0100000000001013/",
    "/atmosphere/contents/0100000000001015/",
};

auto DownloadFile(ProgressBox* pbox, const std::string& label, const std::string& url, const fs::FsPath& dst) -> Result {
    R_TRY(EnsureParentDirectory(dst.toString()));
    pbox->NewTransfer(label);

    const auto result = curl::Api().ToFile(
        curl::Url{url},
        curl::Path{dst},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, Result_CurlFailedEasyInit);
    R_SUCCEED();
}

auto UnzipFile(ProgressBox* pbox, const fs::FsPath& zip, const fs::FsPath& dst) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    R_TRY(fs.CreateDirectoryRecursively(dst));
    pbox->NewTransfer("Extracting"_i18n);
    R_TRY(thread::TransferUnzipAll(pbox, zip, &fs, dst));
    R_SUCCEED();
}

void RebootAfterSetting() {
    fsdevCommitDevice("sdmc");
    utils::requestForcedReboot();
}

auto ParseDbiTranslations(const std::string& path) -> std::vector<DbiTranslationEntry> {
    std::vector<DbiTranslationEntry> entries;
    auto lines = ReadLines(path);

    std::string name;
    std::string translation_url;

    const auto flush = [&]() {
        if (!name.empty() && name != "Update list of translations" && !translation_url.empty()) {
            entries.push_back({name, translation_url});
        }
        name.clear();
        translation_url.clear();
    };

    for (const auto& line : lines) {
        if (const auto section = ExtractBracketName(line); !section.empty()) {
            flush();
            name = section;
            continue;
        }

        const auto cmd = SplitCommand(line);
        if (cmd.size() >= 3 && cmd[0] == "download" && cmd[2].find("translation_new.bin") != std::string::npos) {
            translation_url = cmd[1];
        }
    }
    flush();

    std::stable_sort(entries.begin(), entries.end(), [](const DbiTranslationEntry& a, const DbiTranslationEntry& b) {
        const auto cmp = strcasecmp(a.name.c_str(), b.name.c_str());
        return cmp < 0 || (cmp == 0 && a.name < b.name);
    });

    return entries;
}

auto FetchAndCacheTranslations(ProgressBox* pbox, const std::string& target_tag, const std::string& metadata_tag, const std::string& fw, bool warning_required) -> Result {
    const std::string rel_url = "https://api.github.com/repos/NX-Family/NX-Translation/releases/tags/" + target_tag;
    pbox->NewTransfer("Downloading"_i18n);

    // 1. Fetch exact release JSON from GitHub by target tag
    auto rel_res = curl::Api().ToMemory(
        curl::Url{rel_url},
        curl::Header{
            {"Accept", "application/vnd.github+json"},
        },
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(rel_res.success, Result_CurlFailedEasyInit);

    auto rel_doc = yyjson_read(reinterpret_cast<const char*>(rel_res.data.data()), rel_res.data.size(), 0);
    R_UNLESS(rel_doc, Result_FsEmpty);
    ON_SCOPE_EXIT(yyjson_doc_free(rel_doc));

    auto rel_root = yyjson_doc_get_root(rel_doc);
    R_UNLESS(rel_root && yyjson_is_obj(rel_root), Result_FsEmpty);

    auto assets_val = yyjson_obj_get(rel_root, "assets");
    R_UNLESS(assets_val && yyjson_is_arr(assets_val), Result_FsEmpty);

    std::unordered_map<std::string, std::string> asset_urls;
    size_t a_idx, a_max;
    yyjson_val* a_val;
    yyjson_arr_foreach(assets_val, a_idx, a_max, a_val) {
        if (!yyjson_is_obj(a_val)) continue;
        auto name_val = yyjson_obj_get(a_val, "name");
        auto url_val = yyjson_obj_get(a_val, "browser_download_url");
        if (name_val && url_val && yyjson_is_str(name_val) && yyjson_is_str(url_val)) {
            std::string aname = yyjson_get_str(name_val);
            std::string aurl = yyjson_get_str(url_val);
            std::string lower_name = aname;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c){ return std::tolower(c); });
            asset_urls[lower_name] = aurl;
        }
    }

    // 2. Fetch versioned api.json using metadata tag
    const std::string api_url = "https://raw.githubusercontent.com/NX-Family/NX-Translation/" + metadata_tag + "/api.json";
    pbox->NewTransfer("Downloading"_i18n);
    auto api_res = curl::Api().ToMemory(
        curl::Url{api_url},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(api_res.success, Result_CurlFailedEasyInit);

    auto api_doc = yyjson_read(reinterpret_cast<const char*>(api_res.data.data()), api_res.data.size(), 0);
    R_UNLESS(api_doc, Result_FsEmpty);
    ON_SCOPE_EXIT(yyjson_doc_free(api_doc));

    auto api_root = yyjson_doc_get_root(api_doc);
    R_UNLESS(api_root && yyjson_is_obj(api_root), Result_FsEmpty);

    auto langs_val = yyjson_obj_get(api_root, "languages");
    R_UNLESS(langs_val && yyjson_is_arr(langs_val), Result_FsEmpty);

    std::string legacy_fw_part;
    std::string legacy_tr_part;
    const auto dash = target_tag.find('-');
    if (dash != std::string::npos) {
        legacy_fw_part = target_tag.substr(0, dash);
        legacy_tr_part = target_tag.substr(dash + 1);
        std::transform(legacy_fw_part.begin(), legacy_fw_part.end(), legacy_fw_part.begin(), [](unsigned char c){ return std::tolower(c); });
        std::transform(legacy_tr_part.begin(), legacy_tr_part.end(), legacy_tr_part.begin(), [](unsigned char c){ return std::tolower(c); });
    }

    std::vector<InterfaceTranslationEntry> entries;
    size_t l_idx, l_max;
    yyjson_val* l_val;
    yyjson_arr_foreach(langs_val, l_idx, l_max, l_val) {
        if (!yyjson_is_obj(l_val)) continue;
        auto id_val = yyjson_obj_get(l_val, "id");
        auto name_val = yyjson_obj_get(l_val, "name");
        auto dl_url_val = yyjson_obj_get(l_val, "download_url");
        if (!id_val || !yyjson_is_str(id_val)) continue;

        std::string id = yyjson_get_str(id_val);
        std::string name = (name_val && yyjson_is_str(name_val)) ? yyjson_get_str(name_val) : id;

        std::string id_lower = id;
        std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), [](unsigned char c){ return std::tolower(c); });

        std::string asset_url;

        // 1. Try modern naming: NX-Translation_<id>.zip
        const std::string expected_modern = "nx-translation_" + id_lower + ".zip";
        if (auto it = asset_urls.find(expected_modern); it != asset_urls.end()) {
            asset_url = it->second;
        }

        // 2. Try derived legacy naming: <tr_part>_<id>_<fw_part>.zip
        if (asset_url.empty() && !legacy_fw_part.empty() && !legacy_tr_part.empty()) {
            const std::string expected_legacy = legacy_tr_part + "_" + id_lower + "_" + legacy_fw_part + ".zip";
            if (auto it = asset_urls.find(expected_legacy); it != asset_urls.end()) {
                asset_url = it->second;
            }
        }

        // 3. Fallback to tightly bounded legacy pattern: exactly one asset matching _<id>_.zip
        if (asset_url.empty()) {
            const std::string needle = "_" + id_lower + "_";
            std::string matched_url;
            int match_count = 0;
            for (const auto& [fname, url] : asset_urls) {
                if (fname.size() >= 4 && fname.compare(fname.size() - 4, 4, ".zip") == 0) {
                    if (fname.find(needle) != std::string::npos) {
                        matched_url = url;
                        ++match_count;
                    }
                }
            }
            if (match_count == 1) {
                asset_url = matched_url;
            }
        }

        // 4. Try download_url from api.json if provided and matches an asset filename
        if (asset_url.empty() && dl_url_val && yyjson_is_str(dl_url_val)) {
            std::string dl_url = yyjson_get_str(dl_url_val);
            std::string fname = FileNameFromUrl(dl_url);
            std::transform(fname.begin(), fname.end(), fname.begin(), [](unsigned char c){ return std::tolower(c); });
            if (auto it = asset_urls.find(fname); it != asset_urls.end()) {
                asset_url = it->second;
            }
        }

        if (asset_url.empty()) {
            continue;
        }

        auto replaces_val = yyjson_obj_get(l_val, "replaces");
        if (!replaces_val || !yyjson_is_arr(replaces_val)) continue;

        std::vector<std::pair<std::string, std::string>> replacements;
        size_t rep_idx, rep_max;
        yyjson_val* rep_val;
        yyjson_arr_foreach(replaces_val, rep_idx, rep_max, rep_val) {
            if (!yyjson_is_obj(rep_val)) continue;
            auto path_val = yyjson_obj_get(rep_val, "path");
            if (!path_val || !yyjson_is_str(path_val)) continue;
            std::string path = yyjson_get_str(path_val);

            std::string loc_name;
            if (auto loc_obj = yyjson_obj_get(rep_val, "locale"); loc_obj && yyjson_is_obj(loc_obj)) {
                if (auto n = yyjson_obj_get(loc_obj, "name"); n && yyjson_is_str(n)) {
                    loc_name = yyjson_get_str(n);
                }
            }

            std::string reg_name;
            if (auto reg_obj = yyjson_obj_get(rep_val, "region"); reg_obj && yyjson_is_obj(reg_obj)) {
                if (auto n = yyjson_obj_get(reg_obj, "name"); n && yyjson_is_str(n)) {
                    reg_name = yyjson_get_str(n);
                }
            }

            std::string label;
            if (!loc_name.empty() && !reg_name.empty()) {
                label = loc_name + " for " + reg_name + " region";
            } else if (!loc_name.empty()) {
                label = loc_name;
            } else {
                label = path;
            }

            replacements.emplace_back(std::move(label), std::move(path));
        }

        if (replacements.empty()) {
            continue;
        }

        InterfaceTranslationEntry entry;
        entry.id = std::move(id);
        entry.name = std::move(name);
        entry.zip_url = std::move(asset_url);
        entry.replacements = std::move(replacements);
        entry.warning_required = warning_required;
        entries.push_back(std::move(entry));
    }

    R_UNLESS(!entries.empty(), Result_FsEmpty);

    // Save cache
    auto mut_doc = yyjson_mut_doc_new(nullptr);
    R_UNLESS(mut_doc, Result_FsUnknownStdioError);
    ON_SCOPE_EXIT(yyjson_mut_doc_free(mut_doc));

    auto root = yyjson_mut_obj(mut_doc);
    R_UNLESS(root, Result_FsUnknownStdioError);
    yyjson_mut_doc_set_root(mut_doc, root);

    R_UNLESS(yyjson_mut_obj_add_str(mut_doc, root, "tag", target_tag.c_str()), Result_FsUnknownStdioError);
    R_UNLESS(yyjson_mut_obj_add_str(mut_doc, root, "firmware", fw.c_str()), Result_FsUnknownStdioError);
    R_UNLESS(yyjson_mut_obj_add_bool(mut_doc, root, "warning_required", warning_required), Result_FsUnknownStdioError);

    auto langs_arr = yyjson_mut_arr(mut_doc);
    R_UNLESS(langs_arr, Result_FsUnknownStdioError);
    R_UNLESS(yyjson_mut_obj_add_val(mut_doc, root, "languages", langs_arr), Result_FsUnknownStdioError);

    for (const auto& entry : entries) {
        auto entry_obj = yyjson_mut_arr_add_obj(mut_doc, langs_arr);
        R_UNLESS(entry_obj, Result_FsUnknownStdioError);
        R_UNLESS(yyjson_mut_obj_add_str(mut_doc, entry_obj, "id", entry.id.c_str()), Result_FsUnknownStdioError);
        R_UNLESS(yyjson_mut_obj_add_str(mut_doc, entry_obj, "name", entry.name.c_str()), Result_FsUnknownStdioError);
        R_UNLESS(yyjson_mut_obj_add_str(mut_doc, entry_obj, "zip_url", entry.zip_url.c_str()), Result_FsUnknownStdioError);

        auto reps_arr = yyjson_mut_arr(mut_doc);
        R_UNLESS(reps_arr, Result_FsUnknownStdioError);
        R_UNLESS(yyjson_mut_obj_add_val(mut_doc, entry_obj, "replacements", reps_arr), Result_FsUnknownStdioError);
        for (const auto& [lbl, dir] : entry.replacements) {
            auto rep_obj = yyjson_mut_arr_add_obj(mut_doc, reps_arr);
            R_UNLESS(rep_obj, Result_FsUnknownStdioError);
            R_UNLESS(yyjson_mut_obj_add_str(mut_doc, rep_obj, "label", lbl.c_str()), Result_FsUnknownStdioError);
            R_UNLESS(yyjson_mut_obj_add_str(mut_doc, rep_obj, "dir", dir.c_str()), Result_FsUnknownStdioError);
        }
    }

    R_TRY(EnsureParentDirectory(TRANSLATIONS_CACHE_PATH));
    R_UNLESS(yyjson_mut_write_file(TRANSLATIONS_CACHE_PATH.c_str(), mut_doc, YYJSON_WRITE_PRETTY, nullptr, nullptr), Result_FsUnknownStdioError);

    R_SUCCEED();
}

auto LoadTranslationsCache(const std::string& path, const std::string& expected_tag) -> std::vector<InterfaceTranslationEntry> {
    if (!fs::FileExists(path)) return {};

    auto doc = yyjson_read_file(path.c_str(), YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) return {};
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) return {};

    auto tag_val = yyjson_obj_get(root, "tag");
    if (!tag_val || !yyjson_is_str(tag_val)) return {};
    if (expected_tag != yyjson_get_str(tag_val)) return {};

    auto langs_val = yyjson_obj_get(root, "languages");
    if (!langs_val || !yyjson_is_arr(langs_val)) return {};

    std::vector<InterfaceTranslationEntry> entries;
    size_t l_idx, l_max;
    yyjson_val* l_val;
    yyjson_arr_foreach(langs_val, l_idx, l_max, l_val) {
        if (!yyjson_is_obj(l_val)) continue;
        auto id_val = yyjson_obj_get(l_val, "id");
        auto name_val = yyjson_obj_get(l_val, "name");
        auto url_val = yyjson_obj_get(l_val, "zip_url");
        if (!id_val || !yyjson_is_str(id_val) ||
            !name_val || !yyjson_is_str(name_val) ||
            !url_val || !yyjson_is_str(url_val)) {
            continue;
        }

        InterfaceTranslationEntry entry;
        entry.id = yyjson_get_str(id_val);
        entry.name = yyjson_get_str(name_val);
        entry.zip_url = yyjson_get_str(url_val);

        auto reps_val = yyjson_obj_get(l_val, "replacements");
        if (reps_val && yyjson_is_arr(reps_val)) {
            size_t r_idx, r_max;
            yyjson_val* r_val;
            yyjson_arr_foreach(reps_val, r_idx, r_max, r_val) {
                if (!yyjson_is_obj(r_val)) continue;
                auto lbl_val = yyjson_obj_get(r_val, "label");
                auto dir_val = yyjson_obj_get(r_val, "dir");
                if (lbl_val && yyjson_is_str(lbl_val) && dir_val && yyjson_is_str(dir_val)) {
                    entry.replacements.emplace_back(yyjson_get_str(lbl_val), yyjson_get_str(dir_val));
                }
            }
        }

        if (!entry.replacements.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    std::stable_sort(entries.begin(), entries.end(), [](const InterfaceTranslationEntry& a, const InterfaceTranslationEntry& b) {
        return a.name < b.name;
    });

    return entries;
}

auto FileNameFromUrl(const std::string& url) -> std::string {
    const auto query = url.find_first_of("?#");
    auto clean = query == std::string::npos ? url : url.substr(0, query);
    const auto slash = clean.find_last_of('/');
    if (slash != std::string::npos) {
        clean = clean.substr(slash + 1);
    }
    return clean.empty() ? "translation.zip" : clean;
}

auto TranslationExtractFolder(const std::string& zip_name) -> std::string {
    return ::sphaira::ui::menu::settings::TranslationExtractFolder(zip_name);
}

auto InstallDbiTranslation(ProgressBox* pbox, const DbiTranslationEntry& entry) -> Result {
    R_TRY(DownloadFile(
        pbox,
        "Downloading DBI...",
        "https://github.com/rashevskyv/DBIPatcher/releases/latest/download/DBI.nro",
        "/switch/DBI/DBI_new.nro"
    ));
    R_TRY(DownloadFile(
        pbox,
        "Downloading " + entry.name + " translation...",
        entry.translation_url,
        "/switch/DBI/translation_new.bin"
    ));
    R_TRY(MovePath("/switch/DBI/DBI_new.nro", "/switch/DBI/DBI.nro"));
    R_TRY(MovePath("/switch/DBI/translation_new.bin", "/switch/DBI/translation.bin"));
    R_SUCCEED();
}

void TryAutoSwitchLanguage(const std::string& entry_name) {
    static constexpr std::array<const char*, 15> languages{
        "Auto",
        "English",
        "Japanese",
        "French",
        "German",
        "Italian",
        "Spanish",
        "Chinese",
        "Korean",
        "Dutch",
        "Portuguese",
        "Russian",
        "Swedish",
        "Vietnamese",
        "Ukrainian"
    };

    std::string entry_lower = entry_name;
    std::transform(entry_lower.begin(), entry_lower.end(), entry_lower.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    for (size_t i = 1; i < languages.size(); ++i) {
        std::string lang_lower = languages[i];
        std::transform(lang_lower.begin(), lang_lower.end(), lang_lower.begin(), [](unsigned char c) {
            return std::tolower(c);
        });

        if (entry_lower == lang_lower || entry_lower.find(lang_lower) != std::string::npos || lang_lower.find(entry_lower) != std::string::npos) {
            App::SetLanguage(i, false);
            break;
        }
    }
}

auto HasInstalledTranslation() -> bool {
    for (const auto& raw_path : TRANSLATION_PATHS) {
        std::string clean = raw_path;
        while (clean.size() > 1 && clean.back() == '/') {
            clean.pop_back();
        }
        if (fs::DirExists(clean) || fs::FileExists(clean)) {
            return true;
        }
    }
    return false;
}

auto InstallInterfaceTranslation(ProgressBox* pbox, InterfaceTranslationEntry entry, std::string replacement_dir) -> Result {
    const auto zip_name = FileNameFromUrl(entry.zip_url);
    const auto extract_dir = paths::DOWNLOADS + "/translations";
    const auto zip_path = extract_dir + "/" + zip_name;

    R_TRY(DeletePath(extract_dir));
    R_TRY(DownloadFile(pbox, "Downloading"_i18n, entry.zip_url, zip_path));
    R_TRY(UnzipFile(pbox, zip_path, extract_dir));

    auto folder = TranslationExtractFolder(zip_name);
    auto source = extract_dir + "/" + folder + "/" + replacement_dir + "/contents";
    if (!fs::DirExists(fs::FsPath{source})) {
        if (StartsWith(folder, "Nx-")) {
            folder = "NX-" + folder.substr(3);
        } else if (StartsWith(folder, "NX-")) {
            folder = "Nx-" + folder.substr(3);
        }
        source = extract_dir + "/" + folder + "/" + replacement_dir + "/contents";
    }

    if (!fs::DirExists(fs::FsPath{source})) {
        R_THROW(FsError_PathNotFound);
    }

    // Only remove the installed translation after download, extraction, and
    // source directory validation succeed.
    if (HasInstalledTranslation()) {
        if (pbox) {
            pbox->NewTransfer("Removing previous translation..."_i18n);
        }
        for (const auto path : TRANSLATION_PATHS) {
            if (const auto rc = DeletePath(path); R_FAILED(rc) && rc != FsError_PathNotFound && rc != FsError_PathNotFoundFsDev) {
                R_THROW(rc);
            }
        }
        fsdevCommitDevice("sdmc");
    }

    R_TRY(CopyDirectoryContents(source, "/atmosphere/contents"));
    R_TRY(DeletePath(source));
    R_TRY(DeletePath(extract_dir));
    fsdevCommitDevice("sdmc");
    TryAutoSwitchLanguage(entry.name);
    RebootAfterSetting();
    R_SUCCEED();
}

auto RemoveInterfaceTranslation(ProgressBox* pbox) -> Result {
    if (pbox) {
        pbox->NewTransfer("Removing translations..."_i18n);
    }
    for (const auto path : TRANSLATION_PATHS) {
        if (const auto rc = DeletePath(path); R_FAILED(rc) && rc != FsError_PathNotFound && rc != FsError_PathNotFoundFsDev && rc != FsError_TargetLocked) {
            R_THROW(rc);
        }
    }
    fsdevCommitDevice("sdmc");
    R_SUCCEED();
}

auto RemoveInterfaceTranslationAndReboot(ProgressBox* pbox) -> Result {
    R_TRY(RemoveInterfaceTranslation(pbox));
    RebootAfterSetting();
    R_SUCCEED();
}

} // namespace sphaira::ui::menu::settings::detail
