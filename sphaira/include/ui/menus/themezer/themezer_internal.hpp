#pragma once

#include "ui/menus/themezer.hpp"
#include "fs.hpp"
#include <string>
#include <vector>
#include <span>

namespace sphaira::ui::menu::themezer {

struct ScreenshotEntry {
    std::string title{};
    std::string url{};
    std::string cache_id{};
};

// Constants
inline constexpr fs::FsPath THEME_FOLDER{"/themes/sphaira/"};
inline constexpr auto CACHE_PATH = "/switch/sphaira/cache/themezer";
inline constexpr auto GRAPHQL_URL = "https://api.themezer.net/graphql";
inline constexpr const char* NRO_URL = "https://github.com/exelix11/SwitchThemeInjector";

inline constexpr const char* NRO_PATHS[]{
    "/switch/NXThemesInstaller.nro",
    "/switch/NXThemesInstaller/NXThemesInstaller.nro",
    "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
};

inline constexpr const char* REQUEST_TARGET[]{
    "ResidentMenu",
    "Entrance",
    "Flaunch",
    "Set",
    "Psl",
    "MyPage",
    "Notification"
};

inline constexpr const char* REQUEST_SORT[]{
    "RISING",
    "TRENDING",
    "CREATED",
    "UPDATED",
    "DOWNLOADS",
    "SAVES",
};

inline constexpr const char* REQUEST_ORDER[]{
    "DESC",
    "ASC",
};

inline constexpr const char* PACKS_QUERY =
    "query($paginationArgs:PaginationInput,$sort:ItemSort,$order:SortOrder,$query:String,$activeTags:[String!]){"
    "switch{packs(paginationArgs:$paginationArgs,sort:$sort,order:$order,query:$query,activeTags:$activeTags){"
    "nodes{hexId creator{username} name collagePreview{thumbUrl hdUrl} "
    "themes{hexId creator{username} name description updatedAt downloadCount saveCount target screenshotPreview{thumbUrl hdUrl} downloadUrl}}"
    "pageInfo{itemCount limit page pageCount}}}}";

inline constexpr const char* THEMES_QUERY =
    "query($paginationArgs:PaginationInput,$sort:ItemSort,$order:SortOrder,$query:String,$target:Target,$activeTags:[String!]){"
    "switch{themes(paginationArgs:$paginationArgs,sort:$sort,order:$order,query:$query,target:$target,activeTags:$activeTags){"
    "nodes{hexId creator{username} name description updatedAt downloadCount saveCount target screenshotPreview{thumbUrl hdUrl} downloadUrl}"
    "pageInfo{itemCount limit page pageCount}}}}";

// URL / JSON / Caching
auto JsonString(std::string_view str) -> std::string;
auto HashString(std::string_view str) -> u32;
auto apiBuildListPacksBody(const Config& e) -> std::string;
auto apiBuildListPacksCache(const Config& e) -> fs::FsPath;
auto apiBuildIconCache(std::string_view id) -> fs::FsPath;
auto apiBuildScreenshotCache(std::string_view id) -> fs::FsPath;
auto ForceJpegPreviewUrl(std::string url) -> std::string;
auto GetPreviewUrl(const Preview& preview) -> std::string;
auto loadPreviewImage(Preview& preview, std::string_view id) -> bool;
void from_json(const fs::FsPath& path, PackList& e);
auto DelimitedThemesToPackListEntry(const std::string& themes_str, PackListEntry& entry) -> bool;

// Path / naming
auto ThemeTargetLabel(const ThemeEntry& theme) -> const char*;
auto BuildScreenshotTitle(const PackListEntry& pack, const ThemeEntry& theme) -> std::string;
auto SanitizedPathPart(const std::string& value, const char* fallback) -> fs::FsPath;
auto BuildThemePath(const PackListEntry& entry, const ThemeEntry& theme) -> fs::FsPath;

} // namespace sphaira::ui::menu::themezer
