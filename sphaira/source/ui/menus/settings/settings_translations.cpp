#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "download.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"
#include "i18n.hpp"
#include "app.hpp"
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cctype>

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
    pbox->NewTransfer("Extracting " + dst.toString());
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

    return entries;
}

auto ParseInterfaceTranslations(const std::string& path) -> std::vector<InterfaceTranslationEntry> {
    std::vector<InterfaceTranslationEntry> entries;
    auto lines = ReadLines(path);

    InterfaceTranslationEntry current;
    bool in_language{};

    const auto flush = [&]() {
        if (in_language && !current.name.empty() && !current.zip_url.empty() && !current.json_path.empty()) {
            if (fs::FileExists(current.json_path) && !ReadInterfaceReplacementOptions(current).empty()) {
                entries.push_back(current);
            }
        }
        current = {};
        in_language = false;
    };

    for (const auto& line : lines) {
        if (const auto section = ExtractBracketName(line); !section.empty()) {
            flush();
            in_language = line.find("[*") != std::string::npos;
            if (in_language) {
                current.name = section;
                current.json_path = std::string{TRANSLATE_PACKAGE_DIR} + "/langs/" + current.name + ".json";
            }
            continue;
        }

        if (!in_language) {
            continue;
        }

        const auto cmd = SplitCommand(::sphaira::utils::Trim(line));
        if (cmd.size() >= 3 && cmd[0] == "download") {
            current.zip_url = cmd[1];
        }
    }
    flush();

    return entries;
}

auto ReadInterfaceReplacementOptions(const InterfaceTranslationEntry& entry) -> std::vector<std::pair<std::string, std::string>> {
    std::vector<std::pair<std::string, std::string>> out;
    const auto json = ReadTextFile(entry.json_path);
    size_t pos{};
    while (true) {
        const auto object_start = json.find('{', pos);
        if (object_start == std::string::npos) {
            break;
        }
        const auto object_end = json.find('}', object_start);
        if (object_end == std::string::npos) {
            break;
        }

        const auto object = json.substr(object_start, object_end - object_start + 1);
        const auto label = ExtractJsonStringField(object, "lang");
        const auto dir = ExtractJsonStringField(object, "dir");
        if (!label.empty() && !dir.empty()) {
            out.emplace_back(label, dir);
        }
        pos = object_end + 1;
    }
    return out;
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
    auto folder = zip_name;
    if (const auto dot = folder.find_last_of('.'); dot != std::string::npos) {
        folder = folder.substr(0, dot);
    }
    if (StartsWith(folder, "NX-")) {
        folder = "Nx-" + folder.substr(3);
    }
    return folder;
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

auto InstallInterfaceTranslation(ProgressBox* pbox, InterfaceTranslationEntry entry, std::string replacement_dir) -> Result {
    const auto zip_name = FileNameFromUrl(entry.zip_url);
    const auto extract_dir = paths::DOWNLOADS + "/translations";
    const auto zip_path = extract_dir + "/" + zip_name;

    R_TRY(DeletePath(extract_dir));
    R_TRY(DownloadFile(pbox, "Downloading " + entry.name + "...", entry.zip_url, zip_path));

    // remove the currently installed translation first. if any of it can't be
    // deleted (e.g. a file is held open by fs.mitm), surface a dedicated code
    // so the ui can offer remove + reboot instead of a raw fs error.
    for (const auto path : TRANSLATION_PATHS) {
        if (R_FAILED(DeletePath(path))) {
            R_THROW(Result_TranslationRemoveExistingFailed);
        }
    }

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

    R_TRY(CopyDirectoryContents(source, "/atmosphere/contents"));
    R_TRY(DeletePath(source));
    R_TRY(DeletePath(extract_dir));
    R_TRY(DeletePath(TRANSLATE_PACKAGE));
    R_TRY(DeletePath(TRANSLATE_PACKAGE_DIR + "/langs"));
    R_TRY(MovePath(TRANSLATE_PACKAGE_BACKUP, TRANSLATE_PACKAGE));
    TryAutoSwitchLanguage(entry.name);
    RebootAfterSetting();
    R_SUCCEED();
}

auto RemoveInterfaceTranslation(ProgressBox* pbox) -> Result {
    pbox->NewTransfer("Removing translations..."_i18n);
    for (const auto path : TRANSLATION_PATHS) {
        if (const auto rc = DeletePath(path); R_FAILED(rc) && rc != FsError_PathNotFoundFsDev && rc != FsError_TargetLocked) {
            R_THROW(rc);
        }
    }
    RebootAfterSetting();
    R_SUCCEED();
}

auto RemoveInterfaceTranslationAndReboot(ProgressBox* pbox) -> Result {
    pbox->NewTransfer("Removing translations..."_i18n);
    for (const auto path : TRANSLATION_PATHS) {
        // best effort: locked files stay behind, but the reboot below releases
        // the locks so the next install can delete them.
        DeletePath(path);
    }
    RebootAfterSetting();
    R_SUCCEED();
}

} // namespace sphaira::ui::menu::settings::detail
