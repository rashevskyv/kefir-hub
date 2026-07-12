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
    std::string json_path;
    std::string zip_url;
};

namespace detail {

inline const auto TRANSLATE_PACKAGE_DIR = paths::PACKAGES + "/Translate Interface";
inline const auto TRANSLATE_PACKAGE = paths::PACKAGES + "/Translate Interface/package.ini";
inline const auto TRANSLATE_PACKAGE_BACKUP = paths::PACKAGES + "/translate_interface.package.ini.bkp";

auto DownloadFile(ProgressBox* pbox, const std::string& label, const std::string& url, const fs::FsPath& dst) -> Result;
auto UnzipFile(ProgressBox* pbox, const fs::FsPath& zip, const fs::FsPath& dst) -> Result;
auto ParseDbiTranslations(const std::string& path) -> std::vector<DbiTranslationEntry>;
auto ParseInterfaceTranslations(const std::string& path) -> std::vector<InterfaceTranslationEntry>;
auto ReadInterfaceReplacementOptions(const InterfaceTranslationEntry& entry) -> std::vector<std::pair<std::string, std::string>>;
auto FileNameFromUrl(const std::string& url) -> std::string;
auto TranslationExtractFolder(const std::string& zip_name) -> std::string;
auto InstallDbiTranslation(ProgressBox* pbox, const DbiTranslationEntry& entry) -> Result;
auto InstallInterfaceTranslation(ProgressBox* pbox, InterfaceTranslationEntry entry, std::string replacement_dir) -> Result;
auto RemoveInterfaceTranslation(ProgressBox* pbox) -> Result;

void RebootAfterSetting();

} // namespace detail
} // namespace sphaira::ui::menu::settings
