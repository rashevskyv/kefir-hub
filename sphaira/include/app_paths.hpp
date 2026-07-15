#pragma once

#include <string>

namespace sphaira::paths {

inline const std::string DATA_ROOT = "/config/kefir";
inline const std::string LEGACY_DATA_ROOT = "/config/sphaira";
inline const std::string CONFIG = DATA_ROOT + "/config.ini";
inline const std::string PLAYLOG = DATA_ROOT + "/playlog.ini";
inline const std::string LOCATIONS = DATA_ROOT + "/locations.ini";
inline const std::string MODULE_INDEX = DATA_ROOT + "/cache/homebrew_sysmodules.txt";
inline const std::string MODULE_INDEX_DOWNLOAD = MODULE_INDEX + ".download";
inline const std::string LOG = DATA_ROOT + "/log.txt";
inline const std::string ASSOC = DATA_ROOT + "/assoc/";
inline const std::string THEMES = DATA_ROOT + "/themes/";
inline const std::string GITHUB = DATA_ROOT + "/github/";
inline const std::string I18N = DATA_ROOT + "/i18n/";
inline const std::string DOWNLOADS = DATA_ROOT + "/downloads";
inline const std::string PACKAGES = DATA_ROOT + "/packages";
inline const std::string LOGO = DATA_ROOT + "/logo";

} // namespace sphaira::paths
