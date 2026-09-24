#include "ui/menus/themezer.hpp"
#include "ui/menus/themezer/themezer_internal.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "download.hpp"
#include "image.hpp"
#include "i18n.hpp"
#include "yyjson_helper.hpp"
#include <switch.h>
#include <yyjson.h>
#include <stb_image.h>
#include <minIni.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::ui::menu::themezer {
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
    if (!e.target.empty()) {
        json += JsonString(THEMES_QUERY);
    } else {
        json += JsonString(PACKS_QUERY);
    }
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

    if (!e.target.empty()) {
        json += ",\"target\":";
        json += JsonString(e.target);
    }

    json += ",\"activeTags\":";
    if (e.tags.empty()) {
        json += "null";
    } else {
        json += "[";
        for (size_t i = 0; i < e.tags.size(); ++i) {
            if (i > 0) json += ",";
            json += JsonString(e.tags[i]);
        }
        json += "]";
    }

    json += "}}";
    return json;
}

auto apiBuildListPacksCache(const Config& e) -> fs::FsPath {
    fs::FsPath path;
    const auto query_hash = HashString(e.query);
    std::string tags_str;
    for (const auto& tag : e.tags) {
        tags_str += tag + ";";
    }
    const auto target_hash = HashString(e.target);
    const auto tags_hash = HashString(tags_str);
    std::snprintf(path, sizeof(path), "%s/packs_%u_%u_%08x_%08x_%08x_%u_page.json", CACHE_PATH, e.sort_index, e.order_index, query_hash, target_hash, tags_hash, e.page);
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

    yyjson_val* packs_val = yyjson_obj_get(json, "packs");
    if (packs_val) {
        yyjson_val* nodes_val = yyjson_obj_get(packs_val, "nodes");
        if (nodes_val && yyjson_is_arr(nodes_val)) {
            const auto arr_size = yyjson_arr_size(nodes_val);
            e.packList.resize(arr_size);
            size_t idx, max;
            yyjson_val* val;
            yyjson_arr_foreach(nodes_val, idx, max, val) {
                from_json(val, e.packList[idx]);
            }
        }
        yyjson_val* page_info_val = yyjson_obj_get(packs_val, "pageInfo");
        if (page_info_val && yyjson_is_obj(page_info_val)) {
            from_json(page_info_val, e.pagination);
        }
    } else {
        yyjson_val* themes_val = yyjson_obj_get(json, "themes");
        if (themes_val) {
            yyjson_val* nodes_val = yyjson_obj_get(themes_val, "nodes");
            if (nodes_val && yyjson_is_arr(nodes_val)) {
                const auto arr_size = yyjson_arr_size(nodes_val);
                e.packList.resize(arr_size);
                size_t idx, max;
                yyjson_val* val;
                yyjson_arr_foreach(nodes_val, idx, max, val) {
                    ThemeEntry theme;
                    from_json(val, theme);

                    PackListEntry& entry = e.packList[idx];
                    entry.id = theme.id;
                    entry.creator = theme.creator;
                    entry.details = theme.details;
                    entry.preview = theme.preview;
                    entry.themes.push_back(std::move(theme));
                }
            }
            yyjson_val* page_info_val = yyjson_obj_get(themes_val, "pageInfo");
            if (page_info_val && yyjson_is_obj(page_info_val)) {
                from_json(page_info_val, e.pagination);
            }
        }
    }
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


} // namespace sphaira::ui::menu::themezer
