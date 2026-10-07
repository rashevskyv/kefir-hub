#include "web_games.hpp"
#include "web_http.hpp"
#include "web_upload.hpp"
#include "web.hpp"
#include "title_info.hpp"
#include "title_nsp.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace sphaira {
namespace {

using namespace web::detail;

struct SharedGame {
    u64 app_id{};
    std::string name{};
    // built on first request, kept for the life of the share.
    std::shared_ptr<std::vector<title::NspEntry>> entries{};
};

std::mutex g_mutex;
std::vector<SharedGame> g_shared;
bool g_title_init{};

// ncm content reads are not safe to issue concurrently (see FsGameProxy), and
// the server has several worker threads: one nsp read at a time.
std::mutex g_read_mutex;

auto GameName(u64 app_id) -> std::string {
    if (const auto data = title::Get(app_id); data && data->status == title::NacpLoadStatus::Loaded && data->lang.name[0]) {
        return data->lang.name;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016lX", app_id);
    return buf;
}

// the entries of one shared game, built under the lock on first use.
auto GetEntries(u64 app_id) -> std::shared_ptr<std::vector<title::NspEntry>> {
    std::scoped_lock lock{g_mutex};
    const auto it = std::find_if(g_shared.begin(), g_shared.end(), [app_id](const SharedGame& g){ return g.app_id == app_id; });
    if (it == g_shared.end()) {
        return nullptr;
    }
    if (!it->entries) {
        auto entries = std::make_shared<std::vector<title::NspEntry>>();
        if (const auto rc = title::BuildNspEntries(app_id, it->name.c_str(), title::ContentFlag_All, false, *entries); R_FAILED(rc)) {
            log_write("[WEB-GAMES] BuildNspEntries %016lX failed 0x%X\n", app_id, rc);
            return nullptr;
        }
        it->entries = std::move(entries);
    }
    return it->entries;
}

// "bytes=START-" or "bytes=START-END"; false when absent or malformed.
auto ParseRange(const std::string& header, s64 size, s64& start, s64& end) -> bool {
    if (!header.starts_with("bytes=")) {
        return false;
    }
    const char* p = header.c_str() + 6;
    char* stop{};
    start = std::strtoll(p, &stop, 10);
    if (stop == p || *stop != '-' || start < 0 || start >= size) {
        return false;
    }
    p = stop + 1;
    end = *p ? std::strtoll(p, &stop, 10) : size - 1;
    if (end < start || end >= size) {
        end = size - 1;
    }
    return true;
}

} // namespace

void WebGamesSetShared(std::vector<u64> app_ids) {
    std::scoped_lock lock{g_mutex};
    if (!g_title_init) {
        g_title_init = R_SUCCEEDED(title::Init());
    }
    g_shared.clear();
    for (const auto id : app_ids) {
        g_shared.push_back({id, GameName(id), nullptr});
    }
}

void WebGamesClear() {
    std::scoped_lock lock{g_mutex};
    g_shared.clear();
    if (g_title_init) {
        title::Exit();
        g_title_init = false;
    }
}

auto WebGamesIsSharing() -> bool {
    std::scoped_lock lock{g_mutex};
    return !g_shared.empty();
}

void HandleGamesList(Socket sock) {
    std::vector<u64> ids;
    {
        std::scoped_lock lock{g_mutex};
        for (const auto& g : g_shared) {
            ids.push_back(g.app_id);
        }
    }

    std::string json = "{\"games\":[";
    bool first_game = true;
    for (const auto id : ids) {
        const auto entries = GetEntries(id);
        if (!entries || entries->empty()) {
            continue;
        }
        std::string name;
        {
            std::scoped_lock lock{g_mutex};
            const auto it = std::find_if(g_shared.begin(), g_shared.end(), [id](const SharedGame& g){ return g.app_id == id; });
            name = it == g_shared.end() ? std::string{} : it->name;
        }
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016lX", id);
        json += first_game ? "" : ",";
        first_game = false;
        json += "{\"id\":\"" + std::string{hex} + "\",\"name\":\"" + JsonEscape(name) + "\",\"files\":[";
        for (size_t n = 0; n < entries->size(); n++) {
            const auto& e = (*entries)[n];
            json += n ? "," : "";
            json += "{\"n\":" + std::to_string(n) + ",\"name\":\"" + JsonEscape(e.path.s) + "\",\"size\":" + std::to_string(e.nsp_size) + "}";
        }
        json += "]}";
    }
    json += "]}";
    SendResponse(sock, "200 OK", "application/json", json);
}

void SendGameFile(Socket sock, const std::string& req, const std::string& query) {
    const auto id_str = GetQueryValue(query, "id");
    const auto n_str = GetQueryValue(query, "n");
    if (id_str.empty() || n_str.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing id or n");
        return;
    }
    const auto app_id = std::strtoull(id_str.c_str(), nullptr, 16);
    const auto n = std::strtoul(n_str.c_str(), nullptr, 10);

    const auto entries = GetEntries(app_id);
    if (!entries || n >= entries->size()) {
        SendResponse(sock, "404 Not Found", "text/plain", "No such game file");
        return;
    }
    auto& entry = (*entries)[n];
    const s64 size = entry.nsp_size;

    s64 start = 0, end = size - 1;
    const bool ranged = ParseRange(HeaderValue(req, "Range"), size, start, end);
    const s64 length = end - start + 1;

    char header[768]{};
    if (ranged) {
        std::snprintf(header, sizeof(header),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Range: bytes %ld-%ld/%ld\r\n"
            "Content-Length: %ld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "\r\n",
            start, end, size, length);
    } else {
        std::snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Content-Length: %ld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "\r\n",
            SanitizeFileName(entry.path.s).c_str(), length);
    }
    if (!SendString(sock, header)) {
        return;
    }

    // the server box shows what is going out, the way it shows an upload.
    {
        std::scoped_lock lock{g_upload_state.name_mutex};
        g_upload_state.name = "Sending: " + std::string{entry.path.s};
    }
    g_upload_state.total.store(size);
    g_upload_state.bytes.store(start);
    g_upload_state.active.store(true);
    ON_SCOPE_EXIT(g_upload_state.active.store(false));

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    s64 offset = start;
    while (offset <= end) {
        const auto todo = std::min<s64>(buf.size(), end - offset + 1);
        u64 bytes_read{};
        {
            std::scoped_lock lock{g_read_mutex};
            if (R_FAILED(entry.Read(buf.data(), offset, todo, &bytes_read)) || !bytes_read) {
                log_write("[WEB-GAMES] read failed at %ld of %s\n", offset, entry.path.s);
                return;
            }
        }
        if (!SendAll(sock, buf.data(), bytes_read)) {
            return;
        }
        offset += bytes_read;
        g_upload_state.bytes.store(offset);
    }
}

} // namespace sphaira
