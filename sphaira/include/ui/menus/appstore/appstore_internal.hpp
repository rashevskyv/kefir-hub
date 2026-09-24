#pragma once

#include "ui/menus/appstore.hpp"
#include "ui/nvg_util.hpp"
#include <string>
#include <vector>
#include <span>

namespace sphaira::ui {
    struct ProgressBox;
}

namespace sphaira::ui::menu::appstore {

inline constexpr fs::FsPath REPO_PATH{"/switch/sphaira/cache/appstore/repo.json"};
inline constexpr fs::FsPath CACHE_PATH{"/switch/sphaira/cache/appstore"};
inline constexpr auto URL_BASE = "https://switch.cdn.fortheusers.org";
inline constexpr auto URL_JSON = "https://switch.cdn.fortheusers.org/repo.json";
inline constexpr auto URL_POST_FEEDBACK = "http://switchbru.com/appstore/feedback";
inline constexpr auto URL_GET_FEEDACK = "http://switchbru.com/appstore/feedback";

inline constexpr const u8 UPDATE_IMAGE_DATA[]{
    #embed <icons/UPDATE.png>
};

inline constexpr const u8 GET_IMAGE_DATA[]{
    #embed <icons/GET.png>
};

inline constexpr const u8 LOCAL_IMAGE_DATA[]{
    #embed <icons/LOCAL.png>
};

inline constexpr const u8 INSTALLED_IMAGE_DATA[]{
    #embed <icons/INSTALLED.png>
};

inline constexpr const char* FILTER_STR[] = {
    "All",
    "Games",
    "Emulators",
    "Tools",
    "Advanced",
    "Themes",
    "Legacy",
    "Misc",
};

inline constexpr const char* SORT_STR[] = {
    "Updated",
    "Downloads",
    "Size",
    "Alphabetical",
};

inline constexpr const char* ORDER_STR[] = {
    "Desc",
    "Asc",
};

// URL builders
auto BuildIconUrl(const Entry& e) -> std::string;
auto BuildBannerUrl(const Entry& e) -> std::string;
auto BuildManifestUrl(const Entry& e) -> std::string;
auto BuildZipUrl(const Entry& e) -> std::string;

// Cache path builders
auto BuildIconCachePath(const Entry& e) -> fs::FsPath;
auto BuildBannerCachePath(const Entry& e) -> fs::FsPath;
auto BuildScreensCachePath(const Entry& e, u8 num) -> fs::FsPath;
auto BuildPackageCachePath(const Entry& e) -> fs::FsPath;
auto BuildInfoCachePath(const Entry& e) -> fs::FsPath;
auto BuildManifestCachePath(const Entry& e) -> fs::FsPath;
auto BuildFeedbackCachePath(const Entry& e) -> fs::FsPath;

// Manifest and json
void from_json(const fs::FsPath& path, std::vector<appstore::Entry>& e);
auto ParseManifest(std::span<const char> view) -> ManifestEntries;
auto LoadAndParseManifest(const Entry& entry) -> ManifestEntries;

// Image loading & drawing
auto EntryLoadImageData(std::span<const u8> image_buf, LazyImage& image) -> bool;
auto EntryLoadImageFile(fs::Fs& fs, const fs::FsPath& path, LazyImage& image) -> bool;
auto EntryLoadImageFile(const fs::FsPath& path, LazyImage& image) -> bool;
auto EntryLoadDefaultImage(LazyImage& image) -> bool;
void DrawIcon(NVGcontext* vg, const LazyImage& l, const LazyImage& d, float x, float y, float w, float h, bool rounded = true, float scale = 1.0);
void DrawIcon(NVGcontext* vg, const LazyImage& l, const LazyImage& d, Vec4 vec, bool rounded = true, float scale = 1.0);

// Info & utilities
auto AppDlToStr(u32 value) -> std::string;
void ReadFromInfoJson(Entry& e);
auto FindCaseInsensitive(std::string_view base, std::string_view term) -> bool;
auto IsRetroArchPackage(const Entry& e) -> bool;

// App operations
auto ExtractPhysfsArchive(ProgressBox* pbox, const fs::FsPath& archive_path, const fs::FsPath& dest_path) -> Result;
auto UninstallApp(ProgressBox* pbox, const Entry& entry) -> Result;
auto InstallApp(ProgressBox* pbox, const Entry& entry) -> Result;

} // namespace sphaira::ui::menu::appstore
