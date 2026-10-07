#include "ui/menus/appstore.hpp"
#include "ui/menus/appstore/appstore_internal.hpp"
#include "ui/menus/appstore_util.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "image.hpp"
#include "i18n.hpp"
#include "yyjson_helper.hpp"
#include <switch.h>
#include <yyjson.h>
#include <stb_image.h>
#include <algorithm>
#include <ranges>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::ui::menu::appstore {
auto LoadSources() -> std::vector<std::string> {
    auto sources = DefaultSources();
    std::vector<u8> text;
    if (R_SUCCEEDED(fs::read_entire_file(SOURCES_PATH, text))) {
        AppendSourceList(std::string_view{(const char*)text.data(), text.size()}, sources);
    }
    return sources;
}

auto BuildRepoCachePath(u32 source) -> fs::FsPath {
    fs::FsPath out;
    if (!source) { // keep the old cache file name for the first source
        std::snprintf(out, sizeof(out), "%s/repo.json", CACHE_PATH.s);
    } else {
        std::snprintf(out, sizeof(out), "%s/repo_%u.json", CACHE_PATH.s, source);
    }
    return out;
}

auto BuildIconUrl(const Entry& e) -> std::string {
    if (!e.icon.empty()) {
        return e.icon;
    }
    return e.base_url + "/packages/" + e.name + "/icon.png";
}

auto BuildBannerUrl(const Entry& e) -> std::string {
    return e.base_url + "/packages/" + e.name + "/screen.png";
}

auto BuildManifestUrl(const Entry& e) -> std::string {
    return e.base_url + "/packages/" + e.name + "/manifest.install";
}

auto IsRetroArchPackage(const Entry& e) -> bool {
    return IsRetroArchPackageName(e.name, e.title);
}

auto BuildZipUrl(const Entry& e) -> std::string {
    return ResolveAppstoreZipUrl(e.name, e.title, e.base_url, e.download);
}

auto BuildIconCachePath(const Entry& e) -> fs::FsPath {
    fs::FsPath out;
    std::snprintf(out, sizeof(out), "%s/icons/%s.png", CACHE_PATH.s, e.name.c_str());
    return out;
}

auto BuildBannerCachePath(const Entry& e) -> fs::FsPath {
    fs::FsPath out;
    std::snprintf(out, sizeof(out), "%s/banners/%s.png", CACHE_PATH.s, e.name.c_str());
    return out;
}

#if 0
auto BuildScreensCachePath(const Entry& e, u8 num) -> fs::FsPath {
    fs::FsPath out;
    std::snprintf(out, sizeof(out), "%s/screens/%s%u.png", CACHE_PATH, e.name.c_str(), num+1);
    return out;
}
#endif

// use appstore path in order to maintain compat with appstore
auto BuildPackageCachePath(const Entry& e) -> fs::FsPath {
    return "/switch/appstore/.get/packages/" + e.name;
}

auto BuildInfoCachePath(const Entry& e) -> fs::FsPath {
    return BuildPackageCachePath(e) + "/info.json";
}

auto BuildManifestCachePath(const Entry& e) -> fs::FsPath {
    return BuildPackageCachePath(e) + "/manifest.install";
}

auto BuildFeedbackCachePath(const Entry& e) -> fs::FsPath {
    return BuildPackageCachePath(e) + "/feedback.json";
}

void from_json(yyjson_val* json, Entry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(category);
        JSON_SET_STR(binary);
        JSON_SET_STR(updated);
        JSON_SET_STR(name);
        JSON_SET_STR(license);
        JSON_SET_STR(title);
        JSON_SET_STR(url);
        JSON_SET_STR(description);
        JSON_SET_STR(author);
        JSON_SET_STR(changelog);
        JSON_SET_UINT(screens);
        JSON_SET_UINT(extracted);
        JSON_SET_STR(version);
        JSON_SET_UINT(filesize);
        JSON_SET_STR(details);
        JSON_SET_UINT(app_dls);
        JSON_SET_STR(md5);
        JSON_SET_STR(download);
        JSON_SET_STR(icon);
    );
}

void from_json(const fs::FsPath& path, std::vector<appstore::Entry>& e) {
    yyjson_read_err err;
    JSON_INIT_VEC_FILE(path, nullptr, &err);
    JSON_OBJ_ITR(
        JSON_SET_ARR_OBJ2(packages, e);
    );
}

auto ParseManifest(std::span<const char> view) -> ManifestEntries {
    ManifestEntries entries;
    // auto view = std::string_view{manifest_data.data(), manifest_data.size()};

    for (const auto line : std::views::split(view, '\n')) {
        if (line.size() <= 3) {
            continue;
        }

        ManifestEntry entry{};
        entry.command = line[0];
        std::strncpy(entry.path, line.data() + 3, line.size() - 3);
        entries.emplace_back(entry);
    }

    return entries;
}

auto LoadAndParseManifest(const Entry& e) -> ManifestEntries {
    const auto path = BuildManifestCachePath(e);

    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(path, data))) {
        return {};
    }

    return ParseManifest(std::span{(const char*)data.data(), data.size()});
}

auto EntryLoadImageData(std::span<const u8> image_buf, LazyImage& image) -> bool {
    // already have the image
    if (image.image) {
        // log_write("warning, tried to load image: %s when already loaded\n", path);
        return true;
    }
    auto vg = App::GetVg();

    int channels_in_file;
    auto buf = stbi_load_from_memory(image_buf.data(), image_buf.size(), &image.w, &image.h, &channels_in_file, 4);
    if (buf) {
        ON_SCOPE_EXIT(stbi_image_free(buf));
        std::memcpy(image.first_pixel, buf, sizeof(image.first_pixel));
        image.image = nvgCreateImageRGBA(vg, image.w, image.h, 0, buf);
    }

    return image.image;
}

auto EntryLoadImageFile(fs::Fs& fs, const fs::FsPath& path, LazyImage& image) -> bool {
    // already have the image
    if (image.image) {
        // log_write("warning, tried to load image: %s when already loaded\n", path);
        return true;
    }

    std::vector<u8> image_buf;
    if (R_FAILED(fs.read_entire_file(path, image_buf))) {
        log_write("failed to load image from file: %s\n", path.s);
    } else {
        EntryLoadImageData(image_buf, image);
    }

    if (!image.image) {
        log_write("failed to load image from file: %s\n", path.s);
        return false;
    } else {
        // log_write("loaded image from file: %s\n", path);
        return true;
    }
}

auto EntryLoadImageFile(const fs::FsPath& path, LazyImage& image) -> bool {
    if (!strncasecmp("romfs:/", path, 7)) {
        fs::FsStdio fs;
        return EntryLoadImageFile(fs, path, image);
    } else {
        fs::FsNativeSd fs;
        return EntryLoadImageFile(fs, path, image);
    }
}

void DrawIcon(NVGcontext* vg, const LazyImage& l, const LazyImage& d, float x, float y, float w, float h, bool rounded, float scale) {
    const auto& i = l.image ? l : d;

    const float iw = (float)i.w / scale;
    const float ih = (float)i.h / scale;
    float ix = x;
    float iy = y;
    bool rounded_image = rounded;

    if (w > iw) {
        ix = x + abs((w - iw) / 2);
    } else if (w < iw) {
        ix = x - abs((w - iw) / 2);
    }
    if (h > ih) {
        iy = y + abs((h - ih) / 2);
    } else if (h < ih) {
        iy = y - abs((h - ih) / 2);
    }

    bool crop = false;
    if (iw < w || ih < h) {
        rounded_image = false;
        gfx::drawRect(vg, x, y, w, h, nvgRGB(i.first_pixel[0], i.first_pixel[1], i.first_pixel[2]), rounded ? 5 : 0);
    }
    if (iw > w || ih > h) {
        crop = true;
        nvgSave(vg);
        nvgIntersectScissor(vg, x, y, w, h);
    }

    gfx::drawImage(vg, ix, iy, iw, ih, i.image, rounded_image ? 5 : 0);
    if (crop) {
        nvgRestore(vg);
    }
}

void DrawIcon(NVGcontext* vg, const LazyImage& l, const LazyImage& d, Vec4 vec, bool rounded, float scale) {
    DrawIcon(vg, l, d, vec.x, vec.y, vec.w, vec.h, rounded, scale);
}

auto AppDlToStr(u32 value) -> std::string {
    auto str = std::to_string(value);
    u32 inc = 3;
    for (u32 i = inc; i < str.size(); i += inc) {
        str.insert(str.cend() - i , ',');
        inc++;
    }
    return str;
}

void ReadFromInfoJson(Entry& e) {
    const auto info_path = BuildInfoCachePath(e);

    yyjson_read_err err;
    auto doc = yyjson_read_file(info_path, YYJSON_READ_NOFLAG, nullptr, &err);
    if (doc) {
        const auto root = yyjson_doc_get_root(doc);
        const auto version = yyjson_obj_get(root, "version");
        if (version) {
            const char* v_str = yyjson_get_str(version);
            if (v_str) {
                e.installed_version = v_str;
            }
            if (IsRetroArchPackage(e)) {
                if (e.installed_version == "Nightly") {
                    e.status = EntryStatus::Installed;
                } else {
                    e.status = EntryStatus::Update;
                    log_write("RetroArch needs Nightly update: %s\n", e.installed_version.c_str());
                }
            } else if (!std::strcmp(yyjson_get_str(version), e.version.c_str())) {
                e.status = EntryStatus::Installed;
            } else {
                e.status = EntryStatus::Update;
                log_write("info.json said %s needs update: %s vs %s\n", e.name.c_str(), yyjson_get_str(version), e.version.c_str());
            }
        }
        // log_write("got info for: %s\n", e.name.c_str());
        yyjson_doc_free(doc);
    }
}

// this ignores ShouldExit() as leaving somthing in a half
// deleted state is a bad idea :)

auto FindCaseInsensitive(std::string_view base, std::string_view term) -> bool {
    const auto it = std::search(base.cbegin(), base.cend(), term.cbegin(), term.cend(), [](char a, char b){
        return std::toupper(a) == std::toupper(b);
    });
    return it != base.cend();
}

} // namespace sphaira::ui::menu::appstore
