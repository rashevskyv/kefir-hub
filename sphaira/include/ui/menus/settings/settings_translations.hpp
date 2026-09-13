#pragma once

#include <string>
#include <vector>
#include <utility>
#include <switch.h>
#include "ui/progress_box.hpp"
#include "fs.hpp"
#include "app_paths.hpp"

namespace sphaira::ui::menu::settings {

struct DbiTranslationEntry {
    std::string name;
    std::string translation_url;
};

struct InterfaceTranslationEntry {
    std::string name;
    std::string id;
    std::string zip_url;
    std::vector<std::pair<std::string, std::string>> replacements;
    bool warning_required{false};
};

namespace detail {

inline const auto TRANSLATIONS_CACHE_PATH = paths::DATA_ROOT + "/cache/translations.json";

auto DownloadFile(ProgressBox* pbox, const std::string& label, const std::string& url, const fs::FsPath& dst) -> Result;
auto UnzipFile(ProgressBox* pbox, const fs::FsPath& zip, const fs::FsPath& dst) -> Result;
auto ParseDbiTranslations(const std::string& path) -> std::vector<DbiTranslationEntry>;
auto FetchAndCacheTranslations(ProgressBox* pbox, const std::string& target_tag, const std::string& metadata_tag, const std::string& fw, bool warning_required) -> Result;
auto LoadTranslationsCache(const std::string& path, const std::string& expected_tag) -> std::vector<InterfaceTranslationEntry>;
auto FileNameFromUrl(const std::string& url) -> std::string;
auto TranslationExtractFolder(const std::string& zip_name) -> std::string;
auto InstallDbiTranslation(ProgressBox* pbox, const DbiTranslationEntry& entry) -> Result;
auto InstallInterfaceTranslation(ProgressBox* pbox, InterfaceTranslationEntry entry, std::string replacement_dir) -> Result;
auto HasInstalledTranslation() -> bool;
auto RemoveInterfaceTranslation(ProgressBox* pbox) -> Result;
// best-effort removal that always reboots: used when replacing a translation
// fails because files are still held open (the reboot releases the locks).
auto RemoveInterfaceTranslationAndReboot(ProgressBox* pbox) -> Result;

void RebootAfterSetting();

} // namespace detail
} // namespace sphaira::ui::menu::settings
