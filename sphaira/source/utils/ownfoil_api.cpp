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
namespace {

// every address ends up as the bare root url of its server.
auto WithScheme(const char* scheme, const std::string& address) -> std::string {
    auto url = scheme + address;
    if (!url.ends_with("/")) {
        url += "/";
    }
    return url;
}

// a supplied address rarely carries a scheme, so it is guessed from its shape:
// an ip, a port or a dotless name is a lan shop and plain http, while a domain
// tries https first, since basic auth has no business going out in the clear.
auto BuildUrls(const std::string& address) -> std::vector<std::string> {
    if (address.starts_with("http://") || address.starts_with("https://")) {
        return {WithScheme("", address)};
    }

    const auto host = address.substr(0, address.find('/'));
    const auto is_ip_literal = !host.empty() && ((host.front() >= '0' && host.front() <= '9') || host.front() == '[');

    // the last colon is a port unless it belongs inside a [::1] style address.
    const auto bracket = host.rfind(']');
    const auto colon = host.rfind(':');
    const auto has_port = colon != std::string::npos && (bracket == std::string::npos || colon > bracket);

    // a dotless or .local name is a machine here, not a certificated domain.
    const auto is_lan_name = host.find('.') == std::string::npos || host.ends_with(".local");

    if (is_ip_literal || has_port || is_lan_name) {
        return {WithScheme("http://", address)};
    }
    // A remote hostname with no scheme defaults to HTTPS. Plain HTTP remains
    // available when the user enters it explicitly.
    return {WithScheme("https://", address)};
}

// the url that answered last time, as the entry remembers it: probing costs a
// full connect timeout per address that isn't there.
struct Resolved {
    std::string url{};
    bool used_remote{};
};

// honoured only while the entry's own addresses still produce it, so it can only
// reorder candidates the probe would try anyway - a re-pointed entry can never
// reach the machine it used to name.
auto ResolvedFrom(const Config& config) -> Resolved {
    if (config.resolved_url.empty()) {
        return {};
    }

    for (const auto used_remote : {false, true}) {
        const auto& address = used_remote ? config.remote_address : config.local_address;
        if (address.empty()) {
            continue;
        }
        for (const auto& url : BuildUrls(address)) {
            if (url == config.resolved_url) {
                return {config.resolved_url, used_remote};
            }
        }
    }

    return {};
}

// a non-200 carries the server's own {"error": "..."}, but a reverse proxy in
// front of it answers for itself, so anything unparseable yields nothing.
auto ParseError(const std::vector<u8>& data) -> std::string {
    if (data.empty()) {
        return {};
    }

    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return {};
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return {};
    }

    if (const auto v = yyjson_obj_get(root, "error"); v && yyjson_is_str(v)) {
        return Flatten(yyjson_get_str(v));
    }

    return {};
}

auto ParseHandshake(const std::vector<u8>& data, ConnectResult& out) -> bool {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        out.error = "Could not parse the server's response"_i18n;
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        out.error = "Could not parse the server's response"_i18n;
        return false;
    }

    if (const auto v = yyjson_obj_get(root, "uid"); v && yyjson_is_str(v)) {
        out.info.uid = yyjson_get_str(v);
    }

    const auto name = yyjson_obj_get(root, "name");
    if (!name || !yyjson_is_str(name)) {
        out.error = "This does not look like an Ownfoil server"_i18n;
        return false;
    }
    out.info.name = yyjson_get_str(name);

    if (const auto v = yyjson_obj_get(root, "version"); v && yyjson_is_str(v)) {
        out.info.version = yyjson_get_str(v);
    }
    if (const auto v = yyjson_obj_get(root, "protocol_version"); v && yyjson_is_int(v)) {
        out.info.protocol_version = yyjson_get_sint(v);
    }
    if (const auto v = yyjson_obj_get(root, "motd"); v && yyjson_is_str(v)) {
        out.info.motd = yyjson_get_str(v);
    }
    out.info.is_public = GetBool(root, "public");

    // a shop that moved says so here, which is the only way a console off its
    // network can hear about it.
    if (const auto v = yyjson_obj_get(root, "remote"); v && yyjson_is_str(v)) {
        out.info.remote_address = yyjson_get_str(v);
    }

    if (const auto features = yyjson_obj_get(root, "features"); features && yyjson_is_obj(features)) {
        out.info.features.shop = GetBool(features, "shop");
        out.info.features.dumps_upload = GetBool(features, "dumps_upload");
        out.info.features.save_backup = GetBool(features, "save_backup");
        out.info.features.resumable_download = GetBool(features, "resumable_download");
        out.info.features.resumable_upload = GetBool(features, "resumable_upload");
    }

    return true;
}

// tries a single url, filling `out` either way.
auto TryConnect(const std::string& url, const Config& config, std::stop_token token, ConnectResult& out) -> bool {
    log_write("[OWNFOIL] handshake with %s\n", url.c_str());

    const auto result = curl::Api().ToMemory(
        curl::Url{url},
        curl::CustomRequest{"OPTIONS"},
        curl::UserPass{config.user, config.pass},
        curl::PreemptiveAuth{true},
        // an error status carries the server's own explanation, worth more than
        // the status alone - and with it kept, only an unreachable server fails
        // the transfer, so the status says whether the handshake worked.
        curl::Flags{curl::Flag_KeepErrorBody},
        curl::StopToken{token}
    );

    if (!result.success) {
        out.error = "Could not reach the server"_i18n;
        return false;
    }
    out.reached = true;

    if (result.code != 200) {
        out.error = ParseError(result.data);

        // a refusal the server phrased itself settles the matter: every address
        // and scheme leads to that same server and the same answer.
        out.refused = !out.error.empty() || result.code == 401 || result.code == 403;

        if (out.error.empty()) {
            if (result.code == 401 || result.code == 403) {
                out.error = "Incorrect username or password"_i18n;
            } else {
                out.error = "Server returned an error"_i18n + " (" + std::to_string(result.code) + ")";
            }
        }
        return false;
    }

    if (!ParseHandshake(result.data, out)) {
        return false;
    }

    out.base_url = url;
    out.success = true;
    return true;
}

// tries one address, over each scheme it could plausibly be served on.
auto TryAddress(const std::string& address, const Config& config, std::stop_token token, ConnectResult& out) -> bool {
    for (const auto& url : BuildUrls(address)) {
        ConnectResult attempt{};
        if (TryConnect(url, config, token, attempt)) {
            out = std::move(attempt);
            return true;
        }

        // once something has answered, its reply is the account of what went
        // wrong; trying another scheme only buries it.
        const auto reached = attempt.reached;
        if (reached || !out.reached) {
            out = std::move(attempt);
        }
        if (reached) {
            break;
        }
    }

    return false;
}

// what a card draws, per category, and nothing beyond it. a base game's own
// catalogue row carries the lot; an update's app id has no row, so its game's
// fills in, and `titleId` is what opens that game's page; a dlc has a row but no
// icon in it, and carries its game's `titleId` for the installed check plus its
// own download, which the shop won't answer for under a dlc's id. artwork is
// THUMB, the size a card draws it at.
constexpr const char* BASE_FIELDS = "appId displayVersion latestOwnedVersion{displayVersion} titledb{name publisher icon(size:THUMB){url local} banner(size:THUMB){url local}}";
constexpr const char* UPDATE_FIELDS = "appId titleId displayVersion title{name publisher icon(size:THUMB){url local} banner(size:THUMB){url local}}";
constexpr const char* DLC_FIELDS = "appId titleId displayVersion downloadUrl downloadExtension titledb{name publisher banner(size:THUMB){url local}} title{name icon(size:THUMB){url local}}";

// one shape for every category, with the content type, fields and filter each
// needs. the ids ride as variables, not in the document, so the shop's parser
// and validation caches keep hitting however long the console's library gets.
// fills `variables` with this page's, in `FetchGraphql`'s bare-json shape.
auto BuildPageQuery(const CatalogQuery& query, std::string& variables) -> std::string {
    const char* app_type{"BASE"};
    const char* fields{BASE_FIELDS};
    std::string params{"$page:Int!,$pageSize:Int!"};
    // the arguments this category adds to the apps() call, spliced in whole.
    std::string args{};
    variables = "\"page\":" + std::to_string(query.page) + ",\"pageSize\":" + std::to_string(query.page_size);

    switch (query.category) {
        case Category::All:
            break;

        case Category::NewGames:
            params += ",$appIds:[String!]";
            args = ",filter:{appId:{notIn:$appIds}}";
            variables += ",\"appIds\":" + IdArray(query.app_ids);
            break;

        case Category::Updates:
            app_type = "UPDATE";
            fields = UPDATE_FIELDS;
            params += ",$appIds:[String!]";
            args = ",filter:{appId:{in:$appIds}}";
            variables += ",\"appIds\":" + IdArray(query.app_ids);
            break;

        case Category::Dlc:
            app_type = "DLC";
            fields = DLC_FIELDS;
            params += ",$titleIds:[String!],$appIds:[String!]";
            args = ",filter:{titleId:{in:$titleIds},appId:{notIn:$appIds}}";
            variables += ",\"titleIds\":" + IdArray(query.title_ids) + ",\"appIds\":" + IdArray(query.app_ids);
            break;

        // an argument of its own, not a filter field: it is an OR across the
        // name and either id, which a filter's all-AND fields can't express.
        case Category::Search:
            params += ",$search:String";
            args = ",search:$search";
            variables += ",\"search\":" + JsonString(query.search);
            break;
    }

    // `total` is cheap enough for every page to ask: a HAVING would make the
    // server count a grouped query as a derived table, but `owned:true` is
    // filtered before the grouping, so it is a flat COUNT(DISTINCT appId).
    return "query(" + params + "){apps(owned:true,appType:[" + app_type + "],groupByAppId:true" + args
        + ",orderBy:{field:" + query.order_field + ",direction:" + (query.descending ? "DESC" : "ASC")
        + "},page:$page,pageSize:$pageSize){total items{" + fields + "}}}";
}

} // namespace

bool PostQuery(const std::string& url, const Config& config, std::stop_token token, const char* body, std::vector<u8>& data, std::string& error) {
    auto result = curl::Api().ToMemory(
        curl::Url{url},
        curl::Fields{body},
        curl::Header{{"Content-Type", "application/json"}},
        curl::UserPass{config.user, config.pass},
        curl::PreemptiveAuth{true},
        curl::Flags{curl::Flag_KeepErrorBody},
        curl::StopToken{token}
    );

    if (!result.success) {
        error = "Could not reach the server"_i18n;
        return false;
    }

    if (result.code != 200) {
        error = ParseError(result.data);
        if (error.empty()) {
            if (result.code == 401 || result.code == 403) {
                error = "Incorrect username or password"_i18n;
            } else {
                error = "Server returned an error"_i18n + " (" + std::to_string(result.code) + ")";
            }
        }
        return false;
    }

    data = std::move(result.data);
    return true;
}

bool FetchGraphql(const std::string& base_url, const Config& config, std::stop_token token, const std::string& query, const std::string& variables, std::vector<u8>& data, std::string& error) {
    std::string body = "{\"query\":" + JsonString(query);
    if (!variables.empty()) {
        body += ",\"variables\":{" + variables + "}";
    }
    body += "}";

    const auto url = base_url + "graphql";
    return PostQuery(url, config, token, body.c_str(), data, error);
}

auto OpenData(yyjson_doc* doc, std::string& error) -> yyjson_val* {
    const auto root = doc ? yyjson_doc_get_root(doc) : nullptr;
    if (!root || !yyjson_is_obj(root)) {
        error = "Could not parse the server's response"_i18n;
        return nullptr;
    }

    if (const auto errors = yyjson_obj_get(root, "errors"); errors && yyjson_is_arr(errors) && yyjson_arr_size(errors)) {
        const auto first = yyjson_arr_get_first(errors);
        const auto message = first ? yyjson_obj_get(first, "message") : nullptr;
        error = message && yyjson_is_str(message) ? Flatten(yyjson_get_str(message)) : "The server rejected the request"_i18n;
        return nullptr;
    }

    const auto data_obj = yyjson_obj_get(root, "data");
    if (!data_obj || !yyjson_is_obj(data_obj)) {
        error = "Unexpected response from the server"_i18n;
        return nullptr;
    }

    return data_obj;
}

auto OpenApps(yyjson_doc* doc, s64& total, std::string& error) -> yyjson_val* {
    const auto data_obj = OpenData(doc, error);
    if (!data_obj) {
        return nullptr;
    }

    const auto conn = yyjson_obj_get(data_obj, "apps");
    const auto items = conn && yyjson_is_obj(conn) ? yyjson_obj_get(conn, "items") : nullptr;
    if (!items || !yyjson_is_arr(items)) {
        error = "Unexpected response from the server"_i18n;
        return nullptr;
    }

    if (const auto v = yyjson_obj_get(conn, "total"); v && yyjson_is_int(v)) {
        total = yyjson_get_sint(v);
    }

    return items;
}

auto Connect(const Config& config, std::stop_token token) -> ConnectResult {
    ConnectResult out{};
    out.error = "No address configured"_i18n;

    // whatever answered last time goes first, so a reconnect costs one request
    // rather than a walk down every address and scheme again.
    if (const auto resolved = ResolvedFrom(config); !resolved.url.empty()) {
        ConnectResult attempt{};
        if (TryConnect(resolved.url, config, token, attempt)) {
            attempt.used_remote = resolved.used_remote;
            return attempt;
        }

        if (attempt.refused) {
            return attempt;
        }
    }

    if (!config.local_address.empty() && TryAddress(config.local_address, config, token, out)) {
        out.used_remote = false;
        return out;
    }

    // the remote address is the same shop and would answer the same way, while
    // whatever proxies it may answer less helpfully and bury the reason.
    if (out.refused) {
        return out;
    }

    if (!config.remote_address.empty()) {
        ConnectResult remote_attempt{};
        if (TryAddress(config.remote_address, config, token, remote_attempt)) {
            remote_attempt.used_remote = true;
            return remote_attempt;
        }

        // whichever address got an answer explains the failure better than the
        // one that stayed silent.
        if (remote_attempt.reached || !out.reached) {
            return remote_attempt;
        }
    }

    return out;
}

auto FetchApps(const std::string& base_url, const Config& config, std::stop_token token, const CatalogQuery& query, std::vector<ShopApp>& out, s64& total, std::string& error) -> bool {
    out.clear();
    total = 0;

    // an empty list is no constraint to the shop, so a category narrowed by one
    // the console can't supply would answer with the whole catalog rather than
    // the nothing it is; an empty search term matches everything the same way.
    // New games is the exception: with nothing installed, every game is new.
    if ((query.category == Category::Updates && query.app_ids.empty())
        || (query.category == Category::Dlc && query.title_ids.empty())
        || (query.category == Category::Search && query.search.empty())) {
        return true;
    }

    std::string variables;
    const auto doc = BuildPageQuery(query, variables);

    log_write("[OWNFOIL] fetching apps page %d (%d per page, category %d, %s %s)\n",
        static_cast<int>(query.page), static_cast<int>(query.page_size),
        static_cast<int>(query.category), query.order_field, query.descending ? "desc" : "asc");

    std::vector<u8> data;
    if (!FetchGraphql(base_url, config, token, doc, variables, data, error)) {
        return false;
    }

    // each category asks for one kind of content, so the query says what every
    // card on the page is.
    const auto type = query.category == Category::Updates ? AppType::Update
        : query.category == Category::Dlc ? AppType::Dlc
        : AppType::Base;
    return ParseApps(data, base_url, type, out, total, error);
}

} // namespace sphaira::ownfoil::api
