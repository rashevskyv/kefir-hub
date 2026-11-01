#include "utils/remote_shop_fetch.hpp"

#include "defines.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <yyjson.h>
#include <algorithm>
#include <set>

namespace sphaira::shop {
namespace {

constexpr size_t MAX_ITEMS = 20000;
constexpr int MAX_DEPTH = 2;       // an index and the indexes its directories name
constexpr size_t MAX_INDEXES = 24; // a shop of many folders is read in part, not all night

struct Reply {
    bool reached{};
    long code{};
    std::vector<u8> data{};
};

auto Get(const std::string& url, const std::string& user, const std::string& pass, std::stop_token token) -> Reply {
    auto result = curl::Api().ToMemory(
        curl::Url{url},
        curl::UserPass{user, pass},
        curl::PreemptiveAuth{true},
        curl::Flags{curl::Flag_KeepErrorBody},
        curl::StopToken{token}
    );
    return {result.success, static_cast<long>(result.code), std::move(result.data)};
}

auto GetString(yyjson_val* obj, const char* key) -> std::string {
    const auto v = yyjson_obj_get(obj, key);
    return v && yyjson_is_str(v) ? yyjson_get_str(v) : std::string{};
}

// one file of an index: a bare url, or an object with its url, size and maybe its name
void AddEntry(yyjson_val* entry, const std::string& base, std::set<std::string>& seen, std::vector<Item>& items) {
    std::string url, name;
    std::uint64_t size{};
    if (yyjson_is_str(entry)) {
        url = yyjson_get_str(entry);
    } else if (yyjson_is_obj(entry)) {
        url = GetString(entry, "url");
        name = GetString(entry, "name");
        if (const auto v = yyjson_obj_get(entry, "size"); v && yyjson_is_uint(v)) {
            size = yyjson_get_uint(v);
        }
    }
    // a Google Drive id has no address to give yati
    if (url.empty() || url.starts_with("gdrive:") || items.size() >= MAX_ITEMS) {
        return;
    }

    auto item = MakeItem(base, url, name, size);
    if (item.url.empty() || Extension(item.name).empty() || !seen.insert(item.url).second) {
        return;
    }
    items.push_back(std::move(item));
}

auto ParseSections(const std::vector<u8>& data, const std::string& base, Listing& out, std::string& error) -> bool {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return false;
    }
    if (const auto e = GetString(root, "error"); !e.empty()) {
        error = e;
        return false;
    }

    const auto sections = yyjson_obj_get(root, "sections");
    if (!sections || !yyjson_is_arr(sections)) {
        return false;
    }

    std::set<std::string> seen;
    size_t i, max;
    yyjson_val* section;
    yyjson_arr_foreach(sections, i, max, section) {
        const auto items = yyjson_obj_get(section, "items");
        if (!items || !yyjson_is_arr(items)) {
            continue;
        }
        size_t j, jmax;
        yyjson_val* entry;
        yyjson_arr_foreach(items, j, jmax, entry) {
            AddEntry(entry, base, seen, out.items);
        }
    }
    return true;
}

// a legacy index and, one level down, the indexes of its directories
auto ParseIndex(const std::string& url, const std::string& user, const std::string& pass, std::stop_token token, int depth, size_t& reads,
                std::set<std::string>& seen, Listing& out, std::string& error) -> bool {
    if (reads++ >= MAX_INDEXES || token.stop_requested()) {
        return true;
    }

    const auto reply = Get(url, user, pass, token);
    if (!reply.reached) {
        error = "Could not reach the server"_i18n;
        return false;
    }
    if (reply.code == 401 || reply.code == 403) {
        error = "Incorrect username or password"_i18n;
        return false;
    }
    if (reply.code != 200) {
        error = "Server returned an error"_i18n + " (" + std::to_string(reply.code) + ")";
        return false;
    }

    auto doc = yyjson_read(reinterpret_cast<const char*>(reply.data.data()), reply.data.size(), YYJSON_READ_NOFLAG);
    const auto root = doc ? yyjson_doc_get_root(doc) : nullptr;
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        error = "This is not a shop index"_i18n;
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto files = yyjson_obj_get(root, "files");
    const auto directories = yyjson_obj_get(root, "directories");
    if (depth == 0 && !(files && yyjson_is_arr(files)) && !(directories && yyjson_is_arr(directories))) {
        error = "This is not a shop index"_i18n;
        return false;
    }

    if (depth == 0) {
        out.motd = GetString(root, "success");
    }

    if (files && yyjson_is_arr(files)) {
        size_t i, max;
        yyjson_val* entry;
        yyjson_arr_foreach(files, i, max, entry) {
            AddEntry(entry, url, seen, out.items);
        }
    }

    if (depth < MAX_DEPTH && directories && yyjson_is_arr(directories)) {
        size_t i, max;
        yyjson_val* entry;
        yyjson_arr_foreach(directories, i, max, entry) {
            const auto dir = yyjson_is_str(entry) ? ResolveUrl(url, yyjson_get_str(entry)) : std::string{};
            std::string ignored;
            if (!dir.empty()) {
                ParseIndex(dir, user, pass, token, depth + 1, reads, seen, out, ignored); // a folder that fails is only left out
            }
        }
    }
    return true;
}

// the api lives at the root of the server: the url itself when it is a folder, the folder of the file when it names one
auto ApiRoot(std::string url) -> std::string {
    url = url.substr(0, url.find_first_of("?#"));
    if (url.ends_with('/')) {
        return url;
    }

    const auto scheme = url.find("://");
    const auto path_start = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    if (path_start != std::string::npos) {
        const auto last = url.rfind('/');
        if (url.find('.', last) != std::string::npos) {
            return url.substr(0, last + 1);
        }
    }
    return url + "/";
}

} // namespace

auto Fetch(const std::string& url, const std::string& user, const std::string& pass, std::stop_token token, Listing& out, std::string& error) -> bool {
    out = {};

    const auto root = ApiRoot(url);

    for (const char* prefix : {"api/remote/sections", "api/shop/sections"}) {
        const auto api = root + prefix;
        const auto reply = Get(api, user, pass, token);
        log_write("[SHOP] %s -> %ld\n", api.c_str(), reply.code);
        if (!reply.reached) {
            error = "Could not reach the server"_i18n;
            return false;
        }
        if (reply.code == 401 || reply.code == 403) {
            error = "Incorrect username or password"_i18n;
            return false;
        }
        std::string api_error;
        if (reply.code == 200 && ParseSections(reply.data, root, out, api_error)) {
            out.aerofoil = true;
            out.source_url = api;
            return true;
        }
        if (!api_error.empty()) {
            error = api_error;
            return false;
        }
        out.items.clear();
    }

    // an ordinary Tinfoil shop: the index is the address itself
    size_t reads{};
    std::set<std::string> seen;
    if (!ParseIndex(url, user, pass, token, 0, reads, seen, out, error)) {
        return false;
    }
    out.source_url = url;
    return true;
}

} // namespace sphaira::shop
