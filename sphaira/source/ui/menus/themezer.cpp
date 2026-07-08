#include "ui/menus/themezer.hpp"
#include "ui/menus/file_viewer.hpp"
#include "ui/menus/ghdl.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "ui/nvg_util.hpp"
#include "swkbd.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "title_info.hpp"
#include "nro.hpp"

#include <minIni.h>
#include <stb_image.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>
#include <yyjson.h>
#include "yyjson_helper.hpp"

namespace sphaira::ui::menu::themezer {
namespace {

struct ScreenshotEntry {
    std::string title{};
    std::string url{};
    std::string cache_id{};
};

// format is /themes/sphaira/Theme Name by Author/theme_name-type.nxtheme
constexpr fs::FsPath THEME_FOLDER{"/themes/sphaira/"};
constexpr auto CACHE_PATH = "/switch/sphaira/cache/themezer";
constexpr auto GRAPHQL_URL = "https://api.themezer.net/graphql";

constexpr const char* NRO_URL = "https://github.com/exelix11/SwitchThemeInjector";

constexpr const char* NRO_PATHS[]{
    "/switch/NXThemesInstaller.nro",
    "/switch/NXThemesInstaller/NXThemesInstaller.nro",
    "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
};

constexpr const char* REQUEST_TARGET[]{
    "ResidentMenu",
    "Entrance",
    "Flaunch",
    "Set",
    "Psl",
    "MyPage",
    "Notification"
};

constexpr const char* REQUEST_SORT[]{
    "RISING",
    "TRENDING",
    "CREATED",
    "UPDATED",
    "DOWNLOADS",
    "SAVES",
};

constexpr const char* REQUEST_ORDER[]{
    "DESC",
    "ASC",
};

constexpr const char* PACKS_QUERY =
    "query($paginationArgs:PaginationInput,$sort:ItemSort,$order:SortOrder,$query:String){"
    "switch{packs(paginationArgs:$paginationArgs,sort:$sort,order:$order,query:$query){"
    "nodes{hexId creator{username} name collagePreview{thumbUrl hdUrl} "
    "themes{hexId creator{username} name description updatedAt downloadCount saveCount target screenshotPreview{thumbUrl hdUrl} downloadUrl}}"
    "pageInfo{itemCount limit page pageCount}}}}";

auto GetNroPath() -> const char* {
    fs::FsNativeSd fs;
    for (auto& path : NRO_PATHS) {
        if (fs.FileExists(path)) {
            return path;
        }
    }

    return nullptr;
}

auto HasNro() -> bool {
    return GetNroPath() != nullptr;
}

auto JsonString(std::string_view str) -> std::string {
    std::string out;
    out.reserve(str.size() + 2);
    out += '"';

    for (const auto raw_ch : str) {
        const auto ch = static_cast<unsigned char>(raw_ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += raw_ch;
                }
                break;
        }
    }

    out += '"';
    return out;
}

auto HashString(std::string_view str) -> u32 {
    u32 hash = 2166136261u;
    for (const auto ch : str) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 16777619u;
    }
    return hash;
}

auto ClampArrayIndex(u32 index, u32 count) -> u32 {
    if (count == 0) {
        return 0;
    }
    return index < count ? index : count - 1;
}

auto apiBuildListPacksBody(const Config& e) -> std::string {
    const auto sort_index = ClampArrayIndex(e.sort_index, std::size(REQUEST_SORT));
    const auto order_index = ClampArrayIndex(e.order_index, std::size(REQUEST_ORDER));

    std::string json;
    json += "{\"query\":";
    json += JsonString(PACKS_QUERY);
    json += ",\"variables\":{\"paginationArgs\":{\"page\":";
    json += std::to_string(e.page);
    json += ",\"limit\":";
    json += std::to_string(e.limit);
    json += "},\"sort\":";
    json += JsonString(REQUEST_SORT[sort_index]);
    json += ",\"order\":";
    json += JsonString(REQUEST_ORDER[order_index]);
    json += ",\"query\":";
    json += e.query.empty() ? "null" : JsonString(e.query);
    json += "}}";
    return json;
}

auto apiBuildListPacksCache(const Config& e) -> fs::FsPath {
    fs::FsPath path;
    const auto query_hash = HashString(e.query);
    std::snprintf(path, sizeof(path), "%s/packs_%u_%u_%08x_%u_page.json", CACHE_PATH, e.sort_index, e.order_index, query_hash, e.page);
    return path;
}

auto apiBuildIconCache(std::string_view id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%.*s_thumb.jpg", CACHE_PATH, static_cast<int>(id.size()), id.data());
    return path;
}

auto apiBuildScreenshotCache(std::string_view id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%.*s_screen.jpg", CACHE_PATH, static_cast<int>(id.size()), id.data());
    return path;
}

auto ForceJpegPreviewUrl(std::string url) -> std::string {
    if (!url.starts_with("https://img.themezer.net/")) {
        return url;
    }

    const auto query_pos = url.find('?');
    const auto insert_pos = query_pos == std::string::npos ? url.size() : query_pos;
    if (insert_pos >= 4 && url.compare(insert_pos - 4, 4, "@jpg") == 0) {
        return url;
    }

    url.insert(insert_pos, "@jpg");
    return url;
}

auto GetPreviewUrl(const Preview& preview) -> std::string {
    return preview.full.empty() ? preview.thumb : preview.full;
}

auto loadPreviewImage(Preview& preview, std::string_view id) -> bool {
    auto& image = preview.lazy_image;

    // already have the image
    if (image.image) {
        // log_write("warning, tried to load image: %s when already loaded\n", path.c_str());
        return true;
    }
    auto vg = App::GetVg();

    const auto path = apiBuildIconCache(id);
    TimeStamp ts;
    const auto data = ImageLoadFromFile(path, ImageFlag_JPEG);
    if (!data.data.empty()) {
        image.w = data.w;
        image.h = data.h;
        image.image = nvgCreateImageRGBA(vg, data.w, data.h, 0, data.data.data());
        log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
    }

    if (!image.image) {
        log_write("failed to load image from file: %s\n", path.s);
        return false;
    } else {
        // log_write("loaded image from file: %s\n", path);
        return true;
    }
}

void SetJsonString(std::string& out, yyjson_val* val) {
    if (!yyjson_is_str(val)) {
        return;
    }

    const auto str = yyjson_get_str(val);
    const auto len = yyjson_get_len(val);
    if (str) {
        out.assign(str, len);
    }
}

void from_json(yyjson_val* json, Creator& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(id);
        JSON_SET_STR(display_name);
        case cexprHash("username"): {
            SetJsonString(e.display_name, val);
        } break;
    );
}

void from_json(yyjson_val* json, Details& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(name);
        JSON_SET_STR(description);
    );
}

void from_json(yyjson_val* json, Preview& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(thumb);
        case cexprHash("thumbUrl"): {
            SetJsonString(e.thumb, val);
            e.thumb = ForceJpegPreviewUrl(e.thumb);
        } break;
        case cexprHash("hdUrl"): {
            SetJsonString(e.full, val);
            e.full = ForceJpegPreviewUrl(e.full);
        } break;
    );
}

void from_json(yyjson_val* json, ThemeEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(id);
        JSON_SET_OBJ(creator);
        JSON_SET_OBJ(details);
        JSON_SET_OBJ(preview);
        JSON_SET_STR(target);
        case cexprHash("hexId"): {
            SetJsonString(e.id, val);
        } break;
        case cexprHash("name"): {
            SetJsonString(e.details.name, val);
        } break;
        case cexprHash("description"): {
            SetJsonString(e.details.description, val);
        } break;
        case cexprHash("downloadUrl"): {
            SetJsonString(e.download_url, val);
        } break;
        case cexprHash("screenshotPreview"): {
            if (yyjson_is_obj(val)) {
                from_json(val, e.preview);
            }
        } break;
    );
}

void from_json(yyjson_val* json, PackListEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(id);
        JSON_SET_OBJ(creator);
        JSON_SET_OBJ(details);
        JSON_SET_OBJ(preview);
        JSON_SET_ARR_OBJ(themes);
        case cexprHash("hexId"): {
            SetJsonString(e.id, val);
        } break;
        case cexprHash("name"): {
            SetJsonString(e.details.name, val);
        } break;
        case cexprHash("collagePreview"): {
            if (yyjson_is_obj(val)) {
                from_json(val, e.preview);
            }
        } break;
    );
}

void from_json(yyjson_val* json, Pagination& e) {
    JSON_OBJ_ITR(
        JSON_SET_UINT(page);
        JSON_SET_UINT(limit);
        JSON_SET_UINT(page_count);
        JSON_SET_UINT(item_count);
        case cexprHash("pageCount"): {
            if (yyjson_is_uint(val)) {
                e.page_count = yyjson_get_uint(val);
            }
        } break;
        case cexprHash("itemCount"): {
            if (yyjson_is_uint(val)) {
                e.item_count = yyjson_get_uint(val);
            }
        } break;
    );
}

void from_json(const fs::FsPath& path, PackList& e) {
    JSON_INIT_VEC_FILE(path, nullptr, nullptr);
    JSON_GET_OBJ("data");
    JSON_GET_OBJ("switch");
    JSON_GET_OBJ("packs");
    JSON_OBJ_ITR(
        JSON_SET_ARR_OBJ2(nodes, e.packList);
        case cexprHash("pageInfo"): {
            if (yyjson_is_obj(val)) {
                from_json(val, e.pagination);
            }
        } break;
    );
}

auto ThemeTargetLabel(const ThemeEntry& theme) -> const char* {
    static constexpr const char* TARGET_LABEL[]{
        "Home Menu",
        "Lock Screen",
        "All Apps",
        "Settings",
        "Player Select",
        "User Page",
        "News",
    };

    for (u32 i = 0; i < std::size(REQUEST_TARGET); i++) {
        if (theme.target == REQUEST_TARGET[i]) {
            return TARGET_LABEL[i];
        }
    }

    return theme.target.empty() ? "Theme" : theme.target.c_str();
}

auto BuildScreenshotTitle(const PackListEntry& pack, const ThemeEntry& theme) -> std::string {
    std::string title = ThemeTargetLabel(theme);
    if (!theme.details.name.empty() && theme.details.name != title) {
        title += " - ";
        title += theme.details.name;
    } else if (title.empty()) {
        title = pack.details.name.empty() ? "Screenshot"_i18n : pack.details.name;
    }

    return title;
}

auto SanitizedPathPart(const std::string& value, const char* fallback) -> fs::FsPath {
    fs::FsPath out{value.empty() ? fallback : value.c_str()};
    title::utilsReplaceIllegalCharacters(out, false);
    return out;
}

auto BuildThemePath(const PackListEntry& entry, const ThemeEntry& theme) -> fs::FsPath {
    const auto pack_name = SanitizedPathPart(entry.details.name, entry.id.empty() ? "Themezer Pack" : entry.id.c_str());
    const auto pack_author = SanitizedPathPart(entry.creator.display_name, "Unknown");
    const auto theme_name = SanitizedPathPart(theme.details.name, theme.id.empty() ? "Theme" : theme.id.c_str());
    const auto target = SanitizedPathPart(ThemeTargetLabel(theme), "Theme");
    const auto id = SanitizedPathPart(theme.id, "theme");

    fs::FsPath out;
    std::snprintf(out, sizeof(out), "%s/%s - By %s/%s (%s-%s).nxtheme", THEME_FOLDER.s, pack_name.s, pack_author.s, theme_name.s, target.s, id.s);
    return out;
}

} // namespace

auto InstallTheme(sphaira::ui::ProgressBox* pbox, const PackListEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    std::vector<std::string> nxtheme_paths;
    for (const auto& theme : entry.themes) {
        if (pbox->ShouldExit()) {
            break;
        }
        if (theme.download_url.empty()) {
            continue;
        }

        const auto theme_label = theme.details.name.empty() ? entry.details.name : theme.details.name;
        pbox->NewTransfer("Downloading "_i18n + theme_label);

        const auto out_path = BuildThemePath(entry, theme);
        log_write("starting themezer download: %s -> %s\n", theme.download_url.c_str(), out_path.s);

        const auto result = curl::Api().ToFile(
            curl::Url{theme.download_url},
            curl::Path{out_path},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        R_UNLESS(result.success, Result_ThemezerFailedToDownloadTheme);
        nxtheme_paths.emplace_back(out_path);
    }

    // ensure that we actually downloaded the theme.
    // todo: add new error for this.
    R_UNLESS(!nxtheme_paths.empty(), Result_ThemezerFailedToDownloadTheme);

    // if we have nxtheme installed, prompt the user to install the theme now.
    if (HasNro()) {
        App::Push<OptionBox>(
            "Theme downloaded, install now?"_i18n,
            "Back"_i18n, "Install"_i18n, 1, [nxtheme_paths](auto op_index){
                if (op_index && *op_index) {
                    std::string args;

                    for (const auto& paths : nxtheme_paths) {
                        // add space between each arg.
                        if (!args.empty()) {
                            args += ' ';
                        }

                        // converts path to sdmc:/path.
                        args += nro_add_arg_file(paths);
                    }

                    log_write("themezer nro: %s\n", GetNroPath());
                    log_write("themezer args: %s\n", args.c_str());

                    // launch nro with args to the nxthemes.
                    const auto rc = nro_launch(GetNroPath(), args);
                    App::PushErrorBox(rc, "Failed to launch NXthemes_Installer.nro"_i18n);
                }
            }
        );
    }

    log_write("finished install :)\n");
    R_SUCCEED();
}

auto DelimitedThemesToPackListEntry(const std::string& themes_str, PackListEntry& entry) -> bool {
    size_t start = 0;
    while (start < themes_str.size()) {
        size_t end = themes_str.find(';', start);
        if (end == std::string::npos) end = themes_str.size();
        std::string theme_part = themes_str.substr(start, end - start);
        start = end + 1;

        size_t p1 = theme_part.find('|');
        if (p1 != std::string::npos) {
            size_t p2 = theme_part.find('|', p1 + 1);
            if (p2 != std::string::npos) {
                ThemeEntry theme;
                theme.details.name = theme_part.substr(0, p1);
                theme.target = theme_part.substr(p1 + 1, p2 - p1 - 1);
                theme.download_url = theme_part.substr(p2 + 1);
                entry.themes.push_back(theme);
            }
        }
    }
    return !entry.themes.empty();
}

auto PackListEntryToJson(const PackListEntry& entry) -> std::string {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "id", entry.id.c_str());
    yyjson_mut_obj_add_str(doc, root, "name", entry.details.name.c_str());
    yyjson_mut_obj_add_str(doc, root, "description", entry.details.description.c_str());
    yyjson_mut_obj_add_str(doc, root, "creator", entry.creator.display_name.c_str());

    yyjson_mut_val* themes_arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "themes", themes_arr);

    for (const auto& theme : entry.themes) {
        yyjson_mut_val* theme_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, theme_obj, "id", theme.id.c_str());
        yyjson_mut_obj_add_str(doc, theme_obj, "name", theme.details.name.c_str());
        yyjson_mut_obj_add_str(doc, theme_obj, "description", theme.details.description.c_str());
        yyjson_mut_obj_add_str(doc, theme_obj, "target", theme.target.c_str());
        yyjson_mut_obj_add_str(doc, theme_obj, "downloadUrl", theme.download_url.c_str());
        yyjson_mut_arr_add_val(themes_arr, theme_obj);
    }

    size_t len{};
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
    std::string out;
    if (json) {
        out.assign(json, len);
        std::free(json);
    }

    yyjson_mut_doc_free(doc);
    return out;
}

auto JsonToPackListEntry(const std::string& json_str, PackListEntry& entry) -> bool {
    yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return false;
    }

    const auto assign_string = [](yyjson_val* obj, const char* key, std::string& out) {
        if (auto val = yyjson_obj_get(obj, key); val && yyjson_is_str(val)) {
            out = yyjson_get_str(val);
        }
    };

    assign_string(root, "id", entry.id);
    assign_string(root, "name", entry.details.name);
    assign_string(root, "description", entry.details.description);
    assign_string(root, "creator", entry.creator.display_name);

    auto themes = yyjson_obj_get(root, "themes");
    if (!themes || !yyjson_is_arr(themes)) {
        return false;
    }

    size_t idx, max;
    yyjson_val* theme_val;
    yyjson_arr_foreach(themes, idx, max, theme_val) {
        if (!yyjson_is_obj(theme_val)) {
            continue;
        }

        ThemeEntry theme;
        assign_string(theme_val, "id", theme.id);
        assign_string(theme_val, "name", theme.details.name);
        assign_string(theme_val, "description", theme.details.description);
        assign_string(theme_val, "target", theme.target);
        assign_string(theme_val, "downloadUrl", theme.download_url);

        if (!theme.download_url.empty()) {
            entry.themes.push_back(std::move(theme));
        }
    }

    return !entry.themes.empty();
}

auto GetFavoriteIds() -> std::vector<std::string> {
    struct Context {
        std::vector<std::string> ids;
    } ctx;

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto* ctx = static_cast<Context*>(UserData);
        if (std::strcmp(Section, "themezer_favorites") == 0) {
            const auto add_id = [ctx](std::string id) {
                if (std::find(ctx->ids.begin(), ctx->ids.end(), id) == ctx->ids.end()) {
                    ctx->ids.push_back(std::move(id));
                }
            };
            std::string key_str(Key);
            if (key_str.ends_with("_name")) {
                add_id(key_str.substr(0, key_str.size() - 5));
            } else if (!key_str.ends_with("_creator") && !key_str.ends_with("_themes")) {
                add_id(std::move(key_str));
            }
        }
        return 1;
    };

    ini_browse(cb, &ctx, App::CONFIG_PATH);
    return ctx.ids;
}

auto GetFavorites() -> std::vector<PackListEntry> {
    struct Context {
        std::vector<PackListEntry> favorites;

        void Add(PackListEntry entry) {
            const auto found = std::find_if(favorites.begin(), favorites.end(), [&entry](const auto& favorite) {
                return favorite.id == entry.id;
            });
            if (found == favorites.end()) {
                favorites.push_back(std::move(entry));
            }
        }
    } ctx;

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto* ctx = static_cast<Context*>(UserData);
        if (std::strcmp(Section, "themezer_favorites") == 0) {
            std::string key_str(Key);
            if (key_str.ends_with("_creator") || key_str.ends_with("_themes")) {
                return 1;
            }

            if (!key_str.ends_with("_name")) {
                PackListEntry entry;
                if (JsonToPackListEntry(Value, entry)) {
                    ctx->Add(std::move(entry));
                }
                return 1;
            }

            if (key_str.ends_with("_name")) {
                std::string id = key_str.substr(0, key_str.size() - 5);
                PackListEntry entry;
                entry.id = id;
                entry.details.name = Value;

                char creator_buf[256];
                ini_gets("themezer_favorites", (id + "_creator").c_str(), "Unknown", creator_buf, sizeof(creator_buf), App::CONFIG_PATH);
                entry.creator.display_name = creator_buf;

                char themes_buf[1024];
                ini_gets("themezer_favorites", (id + "_themes").c_str(), "", themes_buf, sizeof(themes_buf), App::CONFIG_PATH);
                if (DelimitedThemesToPackListEntry(themes_buf, entry)) {
                    ctx->Add(std::move(entry));
                }
            }
        }
        return 1;
    };

    ini_browse(cb, &ctx, App::CONFIG_PATH);
    return ctx.favorites;
}

void Menu::ToggleFavorite() {
    if (m_pages.empty() || m_page_index < 0 || m_page_index >= static_cast<s64>(m_pages.size())) {
        return;
    }
    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done || m_index < 0 || m_index >= static_cast<s64>(page.m_packList.size())) {
        return;
    }
    const auto& entry = page.m_packList[m_index];
    const auto& id = entry.id;

    auto it = std::find(m_favorite_ids.begin(), m_favorite_ids.end(), id);
    if (it != m_favorite_ids.end()) {
        m_favorite_ids.erase(it);
        ini_puts("themezer_favorites", id.c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_name").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_creator").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_themes").c_str(), nullptr, App::CONFIG_PATH);
        App::Notify("Removed from Favorites"_i18n);
    } else {
        m_favorite_ids.push_back(id);
        ini_puts("themezer_favorites", id.c_str(), PackListEntryToJson(entry).c_str(), App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_name").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_creator").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_themes").c_str(), nullptr, App::CONFIG_PATH);
        App::Notify("Added to Favorites"_i18n);
    }
    UpdateFavoriteAction();
}

void Menu::UpdateFavoriteAction() {
    if (m_pages.empty() || m_page_index < 0 || m_page_index >= static_cast<s64>(m_pages.size())) {
        RemoveAction(Button::R3);
        return;
    }
    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done || m_index < 0 || m_index >= static_cast<s64>(page.m_packList.size())) {
        RemoveAction(Button::R3);
        return;
    }
    const auto& entry = page.m_packList[m_index];
    if (IsFavorite(entry.id)) {
        SetAction(Button::R3, Action{"Unstar"_i18n, [this](){ ToggleFavorite(); }});
    } else {
        SetAction(Button::R3, Action{"Star"_i18n, [this](){ ToggleFavorite(); }});
    }
}

bool Menu::IsFavorite(const std::string& id) const {
    return std::find(m_favorite_ids.begin(), m_favorite_ids.end(), id) != m_favorite_ids.end();
}

Menu::Menu(u32 flags) : MenuBase{"Themezer"_i18n, flags} {
    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_PATH);
    m_favorite_ids = GetFavoriteIds();
    UpdateFavoriteAction();

    SetAction(Button::B, Action{"Back"_i18n, [this]{
        // if search is valid, then we are in search mode, return back to normal.
        if (!m_search.empty()) {
            m_search.clear();
            InvalidateAllPages();
        } else {
            SetPop();
        }
    }});

    this->SetActions(
        std::make_pair(Button::A, Action{"Download"_i18n, [this](){
            App::Push<OptionBox>(
                "Download theme?"_i18n,
                "Back"_i18n, "Download"_i18n, 1, [this](auto op_index){
                    if (op_index && *op_index) {
                        const auto& page = m_pages[m_page_index];
                        if (page.m_packList.size() && page.m_ready == PageLoadState::Done) {
                            const auto& entry = page.m_packList[m_index];

                            App::Push<ProgressBox>(entry.preview.lazy_image.image, "Downloading "_i18n, entry.details.name, [this, &entry](auto pbox) -> Result {
                                return InstallTheme(pbox, entry);
                            }, [this, &entry](Result rc){
                                App::PushErrorBox(rc, "Failed to download theme"_i18n);

                                if (R_SUCCEEDED(rc)) {
                                    App::Notify("Downloaded "_i18n + entry.details.name);
                                }
                            });
                        }
                    }
                }
            );
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }}),
        std::make_pair(Button::Y, Action{"Screenshots"_i18n, [this](){
            DisplayScreenshots();
        }}),
        std::make_pair(Button::R2, Action{"Next Page"_i18n, [this](){
            m_page_index++;
            if (m_page_index >= m_page_index_max) {
                m_page_index = m_page_index_max - 1;
            } else {
                PackListDownload();
            }
        }}),
        std::make_pair(Button::L2, Action{"Previous Page"_i18n, [this](){
            if (m_page_index) {
                m_page_index--;
                PackListDownload();
            }
        }})
    );

    const Vec4 v{75, 110, 350, 250};
    const Vec2 pad{10, 10};
    m_list = std::make_unique<List>(3, 6, m_pos, v, pad);

    m_page_index = 0;
    m_pages.resize(1);
    PackListDownload();
}

Menu::~Menu() {

}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_pages.empty()) {
        return;
    }

    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done) {
        return;
    }

    m_list->OnUpdate(controller, touch, m_index, page.m_packList.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_pages.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty!"_i18n.c_str());
        return;
    }

    auto& page = m_pages[m_page_index];

    switch (page.m_ready) {
        case PageLoadState::None:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Not Ready..."_i18n.c_str());
            return;
        case PageLoadState::Loading:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading"_i18n.c_str());
            return;
        case PageLoadState::Done:
            break;
        case PageLoadState::Error:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Error loading page!"_i18n.c_str());
            return;
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    m_list->Draw(vg, theme, page.m_packList.size(), [this, &page, &image_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto& [x, y, w, h] = v;
        auto& e = page.m_packList[pos];

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = pos == m_index;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            DrawElement(x, y, w, h, ThemeEntryID_GRID);
        }

        const float xoff = (350 - 320) / 2;

        // lazy load image
        Preview* preview = &e.preview;
        std::string_view preview_id{e.id.c_str(), e.id.size()};
        if (preview->thumb.empty() && e.themes.size()) {
            preview = &e.themes[0].preview;
            preview_id = std::string_view{e.themes[0].id.c_str(), e.themes[0].id.size()};
        }

        if (!preview->thumb.empty()) {
            auto& image = preview->lazy_image;
            const auto page_generation_for_image = m_page_generation;
            const auto page_index_for_image = m_page_index;
            const auto entry_index_for_image = pos;
            const bool use_pack_preview = preview == &e.preview;

            // try and load cached image.
            if (image_load_count < image_load_max && !image.image && !image.tried_cache) {
                image.tried_cache = true;
                image.cached = loadPreviewImage(*preview, preview_id);
                if (image.cached) {
                    image_load_count++;
                }
            }

            if (!image.image || image.cached) {
                switch (image.state) {
                    case ImageDownloadState::None: {
                        const auto path = apiBuildIconCache(preview_id);
                        log_write("downloading theme!: %s\n", path.s);

                        const auto url = preview->thumb;
                        log_write("downloading url: %s\n", url.c_str());
                        image.state = ImageDownloadState::Progress;
                        curl::Api().ToFileAsync(
                            curl::Url{url},
                            curl::Path{path},
                            curl::Flags{curl::Flag_Cache},
                            curl::StopToken{this->GetToken()},
                            curl::Priority::Normal,
                            curl::OnComplete{[this, page_generation_for_image, page_index_for_image, entry_index_for_image, use_pack_preview](auto& result) {
                                if (page_generation_for_image != m_page_generation) {
                                    return;
                                }
                                if (page_index_for_image >= m_pages.size()) {
                                    return;
                                }

                                auto& page = m_pages[page_index_for_image];
                                if (entry_index_for_image >= page.m_packList.size()) {
                                    return;
                                }

                                auto& entry = page.m_packList[entry_index_for_image];
                                auto* preview = &entry.preview;
                                if (!use_pack_preview) {
                                    if (entry.themes.empty()) {
                                        return;
                                    }
                                    preview = &entry.themes[0].preview;
                                }

                                auto& image = preview->lazy_image;
                                if (result.success) {
                                    image.state = ImageDownloadState::Done;
                                    // data hasn't changed
                                    if (result.code == 304) {
                                        image.cached = false;
                                    }
                                } else {
                                    image.state = ImageDownloadState::Failed;
                                    log_write("failed to download image\n");
                                }
                            }
                        });
                    }   break;
                    case ImageDownloadState::Progress: {

                    }   break;
                    case ImageDownloadState::Done: {
                        image.cached = false;
                        if (!loadPreviewImage(*preview, preview_id)) {
                            image.state = ImageDownloadState::Failed;
                        } else {
                            image_load_count++;
                        }
                    }   break;
                    case ImageDownloadState::Failed: {
                    }   break;
                }
            }

            gfx::drawImage(vg, x + xoff, y, 320, 180, image.image ? image.image : App::GetDefaultImage(), 5);
        }

        const auto text_x = x + xoff;
        const auto text_clip_w = w - 30.f - xoff;
        const float font_size = 18;
        m_scroll_name.Draw(vg, selected, text_x, y + 180 + 20, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), e.details.name.c_str());
        m_scroll_author.Draw(vg, selected, text_x, y + 180 + 55, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), e.creator.display_name.c_str());
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_checked_for_nro) {
        m_checked_for_nro = true;

        // check if we have the nro, if not, then prompt the user to download from the appstore.
        if (!HasNro()) {
            App::Push<OptionBox>(
                "NXthemes_Installer.nro not found, download now?"_i18n,
                "Back"_i18n, "Download"_i18n, 1, [this](auto op_index){
                    if (op_index && *op_index) {
                        const gh::AssetEntry asset{
                            .name = "NXThemesInstaller.nro",
                            // same path as appstore
                            .path = "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
                        };

                        gh::Download(NRO_URL, asset);
                    }
                }
            );
        }
    }
}

void Menu::InvalidateAllPages() {
    m_page_generation++;
    m_pages.clear();
    m_pages.resize(1);
    m_page_index = 0;
    PackListDownload();
}

void Menu::PackListDownload() {
    const auto page_index = m_page_index + 1;
    char subheading[128];
    std::snprintf(subheading, sizeof(subheading), "Page %zu / %zu"_i18n.c_str(), m_page_index+1, m_page_index_max);
    SetSubHeading(subheading);

    m_index = 0;
    m_list->SetYoff(0);

    // already downloaded
    if (m_pages[m_page_index].m_ready != PageLoadState::None) {
        return;
    }
    m_pages[m_page_index].m_ready = PageLoadState::Loading;

    Config config;
    config.page = page_index;
    config.SetQuery(m_search);
    config.sort_index = m_sort.Get();
    config.order_index = m_order.Get();
    const auto packList_body = apiBuildListPacksBody(config);
    const auto packlist_path = apiBuildListPacksCache(config);
    const auto page_generation = m_page_generation;

    log_write("\npackList_body: %s\n\n", packList_body.c_str());

    curl::Api().ToFileAsync(
        curl::Url{GRAPHQL_URL},
        curl::Path{packlist_path},
        curl::Fields{packList_body},
        curl::Header{
            { "Accept", "application/json" },
            { "Content-Type", "application/json" },
        },
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this, page_index, page_generation](auto& result){
            App::SetBoostMode(true);
            ON_SCOPE_EXIT(App::SetBoostMode(false));

            if (page_generation != m_page_generation) {
                log_write("ignoring stale themezer generation\n");
                return;
            }

            if (page_index == 0 || page_index > m_pages.size()) {
                log_write("ignoring stale themezer page response: %zu\n", static_cast<size_t>(page_index));
                return;
            }

            log_write("got themezer data\n");
            if (!result.success) {
                auto& page = m_pages[page_index-1];
                page.m_ready = PageLoadState::Error;
                log_write("failed to get themezer data...\n");
                return;
            }

            PackList a;
            from_json(result.path, a);

            if (!a.pagination.page_count || page_index > a.pagination.page_count) {
                auto& page = m_pages[page_index-1];
                page.m_ready = PageLoadState::Error;
                log_write("failed to parse themezer data...\n");
                return;
            }

            m_pages.resize(a.pagination.page_count);
            auto& page = m_pages[page_index-1];

            page.m_packList = a.packList;
            page.m_pagination = a.pagination;
            page.m_ready = PageLoadState::Done;
            m_page_index_max = a.pagination.page_count;
            UpdateFavoriteAction();

            char subheading[128];
            std::snprintf(subheading, sizeof(subheading), "Page %zu / %zu"_i18n.c_str(), m_page_index+1, m_page_index_max);
            SetSubHeading(subheading);

            log_write("a.pagination.page: %zu\n", a.pagination.page);
            log_write("a.pagination.page_count: %zu\n", a.pagination.page_count);
        }
    });
}

void Menu::DisplayScreenshots() {
    if (m_pages.empty() || m_page_index < 0 || m_page_index >= static_cast<s64>(m_pages.size())) {
        return;
    }

    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done || m_index < 0 || m_index >= static_cast<s64>(page.m_packList.size())) {
        return;
    }

    const auto& pack = page.m_packList[m_index];
    std::vector<ScreenshotEntry> screenshots;
    screenshots.reserve(pack.themes.size());

    for (size_t i = 0; i < pack.themes.size(); i++) {
        const auto& theme = pack.themes[i];
        const auto url = GetPreviewUrl(theme.preview);
        if (url.empty()) {
            continue;
        }

        auto cache_id = theme.id;
        if (cache_id.empty()) {
            cache_id = pack.id + "_" + std::to_string(i);
        }

        screenshots.push_back({BuildScreenshotTitle(pack, theme), url, cache_id});
    }

    if (screenshots.empty()) {
        const auto url = GetPreviewUrl(pack.preview);
        if (url.empty()) {
            App::Notify("No screenshots"_i18n);
            return;
        }

        const auto title = pack.details.name.empty() ? "Screenshot"_i18n : pack.details.name;
        const auto cache_id = pack.id.empty() ? std::to_string(HashString(url)) : pack.id + "_collage";
        screenshots.push_back({title, url, cache_id});
    }

    auto paths = std::make_shared<std::vector<fs::FsPath>>();
    auto titles = std::make_shared<std::vector<std::string>>();
    paths->reserve(screenshots.size());
    titles->reserve(screenshots.size());

    bool needs_download = false;
    for (auto& screenshot : screenshots) {
        if (screenshot.cache_id.empty()) {
            screenshot.cache_id = std::to_string(HashString(screenshot.url));
        }

        const auto path = apiBuildScreenshotCache(screenshot.cache_id);
        needs_download |= !fs::FileExists(path);
        paths->emplace_back(path);
        titles->emplace_back(screenshot.title);
    }

    const auto open_gallery = [paths, titles](){
        if (!paths->empty()) {
            App::Push<fileview::Menu>((*paths)[0], *paths, 0, *titles);
        }
    };

    if (!needs_download) {
        open_gallery();
        return;
    }

    App::Push<ProgressBox>(
        0, "Downloading "_i18n, pack.details.name, [screenshots, paths](auto pbox) -> Result {
            for (size_t i = 0; i < screenshots.size(); i++) {
                if (pbox->ShouldExit()) {
                    return pbox->ShouldExitResult();
                }

                const auto& path = (*paths)[i];
                if (fs::FileExists(path)) {
                    continue;
                }

                pbox->NewTransfer(screenshots[i].title);
                const auto result = curl::Api().ToFile(
                    curl::Url{screenshots[i].url},
                    curl::Path{path},
                    curl::Flags{curl::Flag_Cache},
                    curl::OnProgress{pbox->OnDownloadProgressCallback()}
                );

                R_UNLESS(result.success, Result_ThemezerFailedToDownloadThemeMeta);
            }

            R_SUCCEED();
        }, [paths, titles](Result rc){
            App::PushErrorBox(rc, "Failed to download screenshot"_i18n);
            if (R_SUCCEEDED(rc) && !paths->empty()) {
                App::Push<fileview::Menu>((*paths)[0], *paths, 0, *titles);
            }
        }
    );
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Themezer Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items sort_items;
    sort_items.push_back("Rising"_i18n);
    sort_items.push_back("Trending"_i18n);
    sort_items.push_back("Created"_i18n);
    sort_items.push_back("Updated"_i18n);
    sort_items.push_back("Downloads"_i18n);
    sort_items.push_back("Saves"_i18n);

    SidebarEntryArray::Items order_items;
    order_items.push_back("Descending"_i18n);
    order_items.push_back("Ascending"_i18n);

    options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
        if (m_sort.Get() != index_out) {
            m_sort.Set(index_out);
            InvalidateAllPages();
        }
    }, m_sort.Get(), "Select how themes are sorted in the list."_i18n);

    options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
        if (m_order.Get() != index_out) {
            m_order.Set(index_out);
            InvalidateAllPages();
        }
    }, m_order.Get(), "Sort themes in ascending or descending order."_i18n);

    options->Add<SidebarEntryCallback>("Page"_i18n, [this](){
        s64 out;
        if (R_SUCCEEDED(swkbd::ShowNumPad(out, "Enter Page Number"_i18n.c_str(), nullptr, -1, 3))) {
            if (out > 0 && out <= m_page_index_max) {
                m_page_index = out - 1;
                PackListDownload();
            } else {
                log_write("invalid page number\n");
                App::Notify("Bad Page"_i18n);
            }
        }
    }, "Jump to a specific page number in the theme list."_i18n);

    options->Add<SidebarEntryCallback>("Search"_i18n, [this](){
        std::string out;
        if (R_SUCCEEDED(swkbd::ShowText(out)) && !out.empty()) {
            m_search = out;
            // PackListDownload();
            InvalidateAllPages();
        }
    }, "Search for themes by name or keyword."_i18n);

    if (HasNro()) {
        options->Add<SidebarEntryCallback>("Launch NXthemes_Installer.nro"_i18n, [](){
            const auto rc = nro_launch(GetNroPath());
            App::PushErrorBox(rc, "Failed to launch NXthemes_Installer.nro"_i18n);
        }, "Open the NXthemes Installer to apply downloaded themes."_i18n);
    }
}

sphaira::ui::menu::themezer::LazyImage::~LazyImage() {
    if (image) {
        nvgDeleteImage(App::GetVg(), image);
    }
}

} // namespace sphaira::ui::menu::themezer
