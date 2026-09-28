#include "utils/ownfoil_api.hpp"
#include "utils/ownfoil_api_internal.hpp"

#include "download.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"

#include <yyjson.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sphaira::ownfoil::api {

// a toast draws one line, so a multi-line server message is folded into one.
auto Flatten(const std::string& str) -> std::string {
    std::string out;
    out.reserve(str.size());

    for (const auto c : str) {
        const auto ch = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        if (ch == ' ' && (out.empty() || out.back() == ' ')) {
            continue;
        }
        out += ch;
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

auto GetBool(yyjson_val* obj, const char* key) -> bool {
    const auto v = yyjson_obj_get(obj, key);
    return v && yyjson_is_bool(v) && yyjson_get_bool(v);
}

// empty when missing or null, which the shop's catalogue fields often are.
auto GetString(yyjson_val* obj, const char* key) -> std::string {
    const auto v = yyjson_obj_get(obj, key);
    return v && yyjson_is_str(v) ? yyjson_get_str(v) : "";
}

// every id was formatted here from a u64, so there is nothing to escape.
auto IdArray(const std::vector<std::string>& ids) -> std::string {
    std::string out{"["};
    for (const auto& id : ids) {
        if (out.size() > 1) {
            out += ',';
        }
        out += '"';
        out += id;
        out += '"';
    }
    out += ']';
    return out;
}

// text somebody typed, so escaped rather than merely quoted. json takes utf-8
// raw, leaving the quote, the backslash and the control characters.
auto JsonString(const std::string& str) -> std::string {
    std::string out{"\""};
    for (const auto c : str) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", u);
            out += escaped;
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

// the shop returns its own stored copies as a path relative to its root, so
// they only become fetchable joined onto the address that answered.
void ParseImage(yyjson_val* obj, const std::string& base_url, ShopImage& out) {
    if (!obj || !yyjson_is_obj(obj)) {
        return;
    }

    const auto url = yyjson_obj_get(obj, "url");
    if (!url || !yyjson_is_str(url) || !*yyjson_get_str(url)) {
        return;
    }

    const auto local = yyjson_obj_get(obj, "local");
    out.local = local && yyjson_is_bool(local) && yyjson_get_bool(local);
    out.url = yyjson_get_str(url);

    // base_url already carries its trailing '/'.
    if (out.local && out.url.front() == '/') {
        out.url = base_url + out.url.substr(1);
    }
}

void ParseImage(yyjson_val* src, const char* key, const std::string& base_url, ShopImage& out) {
    ParseImage(yyjson_obj_get(src, key), base_url, out);
}

// a path under the shop's own root, as its images are, so joined onto it.
auto ParseDownload(yyjson_val* item, const std::string& base_url) -> ShopDownload {
    ShopDownload out{};
    out.url = GetString(item, "downloadUrl");
    out.extension = GetString(item, "downloadExtension");
    if (!out.url.empty() && out.url.front() == '/') {
        out.url = base_url + out.url.substr(1);
    }
    return out;
}

// the newest update the shop can serve, falling back to what the base file
// reports. either can be absent until metadata extraction has run, and a title
// naming no version is left blank: the raw number is 0 on nearly every game.
auto ParseVersion(yyjson_val* item) -> std::string {
    for (const auto src : {yyjson_obj_get(item, "latestOwnedVersion"), item}) {
        if (!src || !yyjson_is_obj(src)) {
            continue;
        }
        if (const auto v = yyjson_obj_get(src, "displayVersion"); v && yyjson_is_str(v) && *yyjson_get_str(v)) {
            return "v" + std::string{yyjson_get_str(v)};
        }
    }
    return {};
}

// reads one page of the apps connection into `out`.
bool ParseApps(const std::vector<u8>& data, const std::string& base_url, AppType type, std::vector<ShopApp>& out, s64& total, std::string& error) {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto items = OpenApps(doc, total, error);
    if (!items) {
        return false;
    }

    size_t idx, max;
    yyjson_val* item;
    yyjson_arr_foreach(items, idx, max, item) {
        if (!yyjson_is_obj(item)) {
            continue;
        }

        ShopApp app{};
        app.type = type;
        app.app_id = GetString(item, "appId");
        // only update and dlc cards ask: a base game's app id is its title id.
        app.title_id = GetString(item, "titleId");
        if (app.title_id.empty()) {
            app.title_id = app.app_id;
        }
        app.version = ParseVersion(item);
        // only a dlc card asks for it.
        app.download = ParseDownload(item, base_url);

        // `titledb` is the app's own catalogue row, `title` the game's. a base
        // game's app id *is* its title id, so that query asks for only the
        // first; updates and dlc need both, each filling what the other lacks.
        for (const auto key : {"titledb", "title"}) {
            const auto src = yyjson_obj_get(item, key);
            if (!src || !yyjson_is_obj(src)) {
                continue;
            }

            if (app.name.empty()) {
                if (const auto v = yyjson_obj_get(src, "name"); v && yyjson_is_str(v)) {
                    app.name = yyjson_get_str(v);
                }
            }
            if (app.publisher.empty()) {
                if (const auto v = yyjson_obj_get(src, "publisher"); v && yyjson_is_str(v)) {
                    app.publisher = yyjson_get_str(v);
                }
            }
            if (app.icon.url.empty()) {
                ParseImage(src, "icon", base_url, app.icon);
            }
            if (app.banner.url.empty()) {
                ParseImage(src, "banner", base_url, app.banner);
            }
        }

        // the dlc's own name won above, so its game's is kept apart for the page
        // that says what the dlc needs.
        if (type == AppType::Dlc) {
            if (const auto game = yyjson_obj_get(item, "title"); game && yyjson_is_obj(game)) {
                app.game_name = GetString(game, "name");
            }
        }

        if (app.name.empty()) {
            app.name = app.app_id;
        }

        out.emplace_back(std::move(app));
    }

    return true;
}

// reads the id-and-version rows the update pre-pass asks for.
bool ParseUpdates(const std::vector<u8>& data, std::vector<ShopUpdate>& out, s64& total, std::string& error) {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto items = OpenApps(doc, total, error);
    if (!items) {
        return false;
    }

    size_t idx, max;
    yyjson_val* item;
    yyjson_arr_foreach(items, idx, max, item) {
        if (!yyjson_is_obj(item)) {
            continue;
        }

        ShopUpdate update{};
        if (const auto v = yyjson_obj_get(item, "appId"); v && yyjson_is_str(v)) {
            update.app_id = yyjson_get_str(v);
        }
        if (const auto v = yyjson_obj_get(item, "titleId"); v && yyjson_is_str(v)) {
            update.title_id = yyjson_get_str(v);
        }
        if (const auto v = yyjson_obj_get(item, "appVersion"); v && yyjson_is_int(v)) {
            update.version = yyjson_get_sint(v);
        }

        out.emplace_back(std::move(update));
    }

    return true;
}

// reads the one title a page asks for.
bool ParseTitle(const std::vector<u8>& data, const std::string& base_url, ShopTitle& out, std::string& error) {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto data_obj = OpenData(doc, error);
    if (!data_obj) {
        return false;
    }

    const auto title = yyjson_obj_get(data_obj, "title");
    if (!title || !yyjson_is_obj(title)) {
        error = "The shop has no details for this title"_i18n;
        return false;
    }

    out.name = GetString(title, "name");
    out.publisher = GetString(title, "publisher");
    ParseImage(title, "banner", base_url, out.banner);
    out.intro = GetString(title, "intro");
    out.description = GetString(title, "description");
    out.release_date = GetString(title, "releaseDate");
    out.players = GetString(title, "numberOfPlayers");
    out.rating = GetString(title, "rating");
    out.region = GetString(title, "region");
    // a string in the catalogue, not reliably numeric: unparseable means no size.
    out.size = std::strtoll(GetString(title, "size").c_str(), nullptr, 10);

    size_t idx, max;
    yyjson_val* val;

    if (const auto categories = yyjson_obj_get(title, "category"); categories && yyjson_is_arr(categories)) {
        yyjson_arr_foreach(categories, idx, max, val) {
            if (!yyjson_is_str(val)) {
                continue;
            }
            if (!out.genre.empty()) {
                out.genre += ", ";
            }
            out.genre += yyjson_get_str(val);
        }
    }

    ParseImage(title, "fullBanner", base_url, out.full_banner);

    const auto parse_screenshots = [&](const char* key, std::vector<ShopImage>& dest) {
        if (const auto arr = yyjson_obj_get(title, key); arr && yyjson_is_arr(arr)) {
            yyjson_arr_foreach(arr, idx, max, val) {
                ShopImage image{};
                ParseImage(val, base_url, image);
                if (!image.url.empty()) {
                    dest.emplace_back(std::move(image));
                }
            }
        }
    };
    parse_screenshots("screenshots", out.screenshots);
    parse_screenshots("full", out.full_screenshots);

    if (const auto versions = yyjson_obj_get(title, "availableVersions"); versions && yyjson_is_arr(versions) && yyjson_arr_size(versions)) {
        if (const auto newest = yyjson_arr_get_last(versions); newest && yyjson_is_obj(newest)) {
            if (const auto v = yyjson_obj_get(newest, "version"); v && yyjson_is_int(v)) {
                out.latest_version = yyjson_get_sint(v);
            }
            out.latest_date = GetString(newest, "releaseDate");
        }
    }

    if (const auto base = yyjson_obj_get(title, "base"); base && yyjson_is_arr(base) && yyjson_arr_size(base)) {
        if (const auto first = yyjson_arr_get_first(base); first && yyjson_is_obj(first)) {
            out.available_version = 0;
            out.available_display = GetString(first, "displayVersion");
            out.download_size = std::strtoll(GetString(first, "downloadSize").c_str(), nullptr, 10);

            if (const auto latest = yyjson_obj_get(first, "latestOwnedVersion"); latest && yyjson_is_obj(latest)) {
                if (const auto v = yyjson_obj_get(latest, "version"); v && yyjson_is_int(v)) {
                    out.available_version = yyjson_get_sint(v);
                }
                if (const auto v = yyjson_obj_get(latest, "displayVersion"); v && yyjson_is_str(v) && *yyjson_get_str(v)) {
                    out.available_display = yyjson_get_str(v);
                }
            }

            out.versions.emplace_back(ShopVersion{0, out.available_display, ParseDownload(first, base_url)});
        }
    }

    if (const auto updates = yyjson_obj_get(title, "updates"); updates && yyjson_is_arr(updates)) {
        yyjson_arr_foreach(updates, idx, max, val) {
            if (!yyjson_is_obj(val)) {
                continue;
            }
            const auto v = yyjson_obj_get(val, "appVersion");
            if (!v || !yyjson_is_int(v)) {
                continue;
            }
            const auto version = yyjson_get_sint(v);
            const auto seen = std::any_of(out.versions.begin(), out.versions.end(), [version](const auto& e) {
                return e.version == version;
            });
            if (!seen) {
                out.versions.emplace_back(ShopVersion{version, GetString(val, "displayVersion"), ParseDownload(val, base_url)});
            }
        }
    }

    // the shop answers in no particular order; the picker offers oldest first.
    std::sort(out.versions.begin(), out.versions.end(), [](const auto& a, const auto& b) {
        return a.version < b.version;
    });

    // a row per version held, so a dlc held at two arrives twice and is listed
    // once, with the newer to install.
    if (const auto dlc = yyjson_obj_get(title, "dlc"); dlc && yyjson_is_arr(dlc)) {
        yyjson_arr_foreach(dlc, idx, max, val) {
            if (!yyjson_is_obj(val)) {
                continue;
            }

            ShopContent content{};
            content.app_id = GetString(val, "appId");
            if (content.app_id.empty()) {
                continue;
            }

            if (const auto v = yyjson_obj_get(val, "appVersion"); v && yyjson_is_int(v)) {
                content.version = yyjson_get_sint(v);
            }
            content.download = ParseDownload(val, base_url);

            const auto seen = std::find_if(out.dlc.begin(), out.dlc.end(), [&](const auto& e) {
                return e.app_id == content.app_id;
            });
            if (seen != out.dlc.end()) {
                if (content.version > seen->version) {
                    seen->version = content.version;
                    seen->download = std::move(content.download);
                }
                continue;
            }

            if (const auto db = yyjson_obj_get(val, "titledb"); db && yyjson_is_obj(db)) {
                content.name = GetString(db, "name");
                content.intro = GetString(db, "intro");
                ParseImage(db, "banner", base_url, content.banner);
            }
            if (content.name.empty()) {
                content.name = content.app_id;
            }

            out.dlc.emplace_back(std::move(content));
        }
    }

    return true;
}

constexpr const char* TITLE_QUERY = "query($id:ID!){title(titleId:$id){name publisher intro description releaseDate category numberOfPlayers size rating region banner(size:CLIENT){url local} fullBanner:banner(size:SCREEN){url local} availableVersions{version releaseDate} screenshots(size:CLIENT){url local} full:screenshots(size:SCREEN){url local} base:apps(owned:true,appType:[BASE]){displayVersion downloadSize downloadUrl downloadExtension latestOwnedVersion{version displayVersion}} updates:apps(owned:true,appType:[UPDATE]){appVersion displayVersion downloadUrl downloadExtension} dlc:apps(owned:true,appType:[DLC]){appId appVersion downloadUrl downloadExtension titledb{name intro banner(size:THUMB){url local}}}}}";

constexpr const char* UPDATES_QUERY = "query($page:Int!,$pageSize:Int!,$titleIds:[String!]){apps(owned:true,appType:[UPDATE],groupByAppId:true,filter:{titleId:{in:$titleIds}},page:$page,pageSize:$pageSize){total items{appId titleId appVersion}}}";

auto FetchUpdates(const std::string& base_url, const Config& config, std::stop_token token, const std::vector<std::string>& title_ids, std::vector<ShopUpdate>& out, std::string& error) -> bool {
    out.clear();

    if (title_ids.empty()) {
        return true;
    }

    const auto ids = IdArray(title_ids);
    static constexpr s64 PAGE_SIZE = 1000;

    for (s64 page = 1; ; page++) {
        const auto variables = "\"page\":" + std::to_string(page) + ",\"pageSize\":" + std::to_string(PAGE_SIZE) + ",\"titleIds\":" + ids;

        std::vector<u8> data;
        if (!FetchGraphql(base_url, config, token, UPDATES_QUERY, variables, data, error)) {
            return false;
        }

        const auto before = out.size();
        s64 total{};
        if (!ParseUpdates(data, out, total, error)) {
            return false;
        }

        if (out.size() == before || static_cast<s64>(out.size()) >= total) {
            break;
        }
    }

    log_write("[OWNFOIL] shop has updates for %zu of %zu installed titles\n", out.size(), title_ids.size());
    return true;
}

auto FetchTitle(const std::string& base_url, const Config& config, std::stop_token token, const std::string& id, ShopTitle& out, std::string& error) -> bool {
    out = {};

    log_write("[OWNFOIL] fetching title %s\n", id.c_str());

    const auto variables = "\"id\":" + JsonString(id);

    std::vector<u8> data;
    if (!FetchGraphql(base_url, config, token, TITLE_QUERY, variables, data, error)) {
        return false;
    }

    return ParseTitle(data, base_url, out, error);
}

} // namespace sphaira::ownfoil::api
