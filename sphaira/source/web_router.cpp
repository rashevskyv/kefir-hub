#include "web_router.hpp"
#include "web.hpp"
#include "web_http.hpp"
#include "web_pages.hpp"
#include "web_screenshots.hpp"
#include "web_upload_routes.hpp"
#include "web_file_routes.hpp"
#include "web_games.hpp"
#include "ui/steamgriddb_icon.hpp"
#include "ui/remote_input.hpp"
#include "i18n.hpp"

#include <switch.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace sphaira {
using namespace webpages;
namespace {

using namespace web::detail;

void HandleStatus(Socket sock) {
    const auto state = WebGetUploadState();
    std::string json = "{\"active\":";
    json += state.active ? "true" : "false";
    json += ",\"name\":\"" + JsonEscape(state.name) + "\"";
    json += ",\"bytes\":" + std::to_string(state.bytes);
    json += ",\"total\":" + std::to_string(state.total);
    json += "}";

    SendResponse(sock, "200 OK", "application/json", json);
}

// the phone posts the SteamGridDB key here as a plain-text body, so nothing has
// to be typed on the console. the key is short, no streaming needed.
void HandleApiKeyPost(Socket sock, const std::string& req) {
    constexpr s64 MAX_KEY_SIZE = 512;

    const auto length_str = HeaderValue(req, "content-length");
    if (length_str.empty()) {
        SendResponse(sock, "411 Length Required", "text/plain", "Missing Content-Length");
        return;
    }

    const auto content_length = std::strtoll(length_str.c_str(), nullptr, 10);
    if (content_length <= 0 || content_length > MAX_KEY_SIZE) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad Content-Length");
        return;
    }

    std::string body;
    if (const auto header_end = req.find("\r\n\r\n"); header_end != std::string::npos) {
        body = req.substr(header_end + 4);
    }
    body.resize(std::min<size_t>(body.size(), content_length));

    for (u32 attempts = 0; attempts < 5000 && (s64)body.size() < content_length; attempts++) {
        char buf[128];
        const auto want = std::min<s64>(sizeof(buf), content_length - body.size());
        const auto got = recv(sock, buf, want, 0);
        if (got > 0) {
            body.append(buf, got);
        } else if (got == 0) {
            break;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            svcSleepThread(1'000'000);
        } else {
            break;
        }
    }

    // keys are hex-ish tokens; anything with whitespace or control bytes in it
    // came from a bad paste rather than from steamgriddb.
    const auto first = body.find_first_not_of(" \t\r\n");
    const auto last = body.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Empty key");
        return;
    }
    const auto key = body.substr(first, last - first + 1);

    const auto invalid = std::ranges::any_of(key, [](unsigned char c){
        return c < 0x21 || c > 0x7E;
    });
    if (invalid) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad key");
        return;
    }

    ui::steamgriddb::SetApiKey(key);
    SendResponse(sock, "200 OK", "text/plain", "OK");
}

void HandleRemoteInputPost(Socket sock, const std::string& req, bool draft = false, bool commit = false) {
    const auto opts = ui::remote_input::GetCurrentOptions();
    const s64 max_size = opts.editor ? 4 * 1024 * 1024 : (opts.multiline ? 256 * 1024 : 64 * 1024);

    const auto length_str = HeaderValue(req, "content-length");
    if (length_str.empty()) {
        SendResponse(sock, "411 Length Required", "text/plain", "Missing Content-Length");
        return;
    }

    const auto content_length = std::strtoll(length_str.c_str(), nullptr, 10);
    if (content_length < 0 || content_length > max_size) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad Content-Length");
        return;
    }

    std::string body;
    if (const auto header_end = req.find("\r\n\r\n"); header_end != std::string::npos) {
        body = req.substr(header_end + 4);
    }
    body.resize(std::min<size_t>(body.size(), static_cast<size_t>(content_length)));

    for (u32 attempts = 0; attempts < 20000 && (s64)body.size() < content_length; attempts++) {
        if (!WebShareIsRunning()) {
            break;
        }
        char buf[4096];
        const auto want = std::min<s64>(sizeof(buf), content_length - (s64)body.size());
        const auto got = recv(sock, buf, want, 0);
        if (got > 0) {
            body.append(buf, got);
        } else if (got == 0) {
            break;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            svcSleepThread(1'000'000);
        } else {
            break;
        }
    }

    if (!opts.multiline && !opts.editor) {
        const auto first = body.find_first_not_of(" \t\r\n");
        const auto last = body.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Empty input");
            return;
        }
        body = body.substr(first, last - first + 1);
    }

    if (body.empty() && !opts.editor) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Empty input");
        return;
    }

    if (draft) {
        ui::remote_input::SetDraftText(body);
    } else if (commit) {
        ui::remote_input::SetCommitText(body);
    } else {
        ui::remote_input::SetReceivedText(body);
    }
    SendResponse(sock, "200 OK", "text/plain", "OK");
}

void HandleRemoteInputConfig(Socket sock) {
    const auto opts = ui::remote_input::GetCurrentOptions();
    std::string json = "{";
    json += "\"title\":\"" + JsonEscape(opts.title) + "\",";
    json += "\"guide\":\"" + JsonEscape(opts.guide) + "\",";
    json += "\"placeholder\":\"" + JsonEscape(opts.placeholder) + "\",";
    json += "\"default_text\":\"" + JsonEscape(opts.editor ? std::string{} : opts.default_text) + "\",";
    json += "\"multiline\":" + std::string(opts.multiline ? "true" : "false") + ",";
    json += "\"secret\":" + std::string(opts.secret ? "true" : "false") + ",";
    json += "\"editor\":" + std::string(opts.editor ? "true" : "false") + ",";
    // the page is static HTML; its own words come in the console's language.
    const std::pair<const char*, std::string> ui[] = {
        {"hint", "Type or paste the text, then press Send."_i18n},
        {"send", "Send"_i18n},
        {"paste", "Paste"_i18n},
        {"sending", "Sending to the console..."_i18n},
        {"sent", "Sent. You can close this page."_i18n},
        {"rejected", "The console did not accept the text."_i18n},
        {"offline", "Could not reach the console."_i18n},
        {"empty", "Type or paste the text first."_i18n},
    };
    json += "\"ui\":{";
    for (size_t i = 0; i < std::size(ui); i++) {
        json += std::string(i ? "," : "") + "\"" + ui[i].first + "\":\"" + JsonEscape(ui[i].second) + "\"";
    }
    json += "}}";

    SendResponse(sock, "200 OK", "application/json", json);
}

} // namespace

using namespace web::detail;

void HandleRequest(Socket sock) {
    std::string req;
    bool header_too_large = false;
    if (!ReadHttpRequest(sock, req, &header_too_large)) {
        if (header_too_large) {
            SendResponse(sock, "431 Request Header Fields Too Large", "text/plain", "Request headers are too large");
        }
        return;
    }

    const auto line_end = req.find("\r\n");
    const auto first_line = req.substr(0, line_end);
    const auto method_end = first_line.find(' ');
    if (method_end == std::string::npos) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad request");
        return;
    }

    const auto method = first_line.substr(0, method_end);
    const auto path_start = method_end + 1;
    const auto path_end = first_line.find(' ', path_start);
    if (path_end == std::string::npos) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad request");
        return;
    }

    std::string query;
    auto path = SplitPathAndQuery(first_line.substr(path_start, path_end - path_start), query);

    if (method == "POST") {
        if (path == "/upload" && GetQueryValue(query, "manifest") == "1") {
            HandleUploadManifest(sock, req);
            return;
        }
    }

    if (method == "PUT") {
        if (path == "/upload") {
            ReceiveUpload(sock, req, query);
            return;
        }

        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (method == "DELETE") {
        if (path == "/delete") {
            HandleDelete(sock, query);
            return;
        }

        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/apikey") {
        if (!ui::steamgriddb::IsApiKeyWebRequestActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Not found");
            return;
        }
        if (method == "POST") {
            HandleApiKeyPost(sock, req);
            return;
        } else if (method == "GET") {
            SendResponse(sock, "200 OK", "text/html", std::string{APIKEY_PAGE});
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/input" || path == "/remote-input") {
        if (!ui::remote_input::IsRemoteInputActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Remote input not active");
            return;
        }
        if (method == "POST") {
            HandleRemoteInputPost(sock, req);
            return;
        } else if (method == "GET") {
            // ponytail: editor HTML is a thin shell; CodeMirror loads from CDN in the browser.
            const auto editor = ui::remote_input::GetCurrentOptions().editor;
            if (editor) {
                ui::remote_input::SetClientSeen();
            }
            const auto page = editor ? REMOTE_EDITOR_PAGE : REMOTE_INPUT_PAGE;
            SendResponse(sock, "200 OK", "text/html", std::string{page});
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/input/config") {
        if (!ui::remote_input::IsRemoteInputActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Remote input not active");
            return;
        }
        if (method == "GET") {
            HandleRemoteInputConfig(sock);
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/input/draft") {
        if (!ui::remote_input::IsRemoteInputActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Remote input not active");
            return;
        }
        if (method == "POST") {
            HandleRemoteInputPost(sock, req, true);
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/input/save") {
        if (!ui::remote_input::IsRemoteInputActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Remote input not active");
            return;
        }
        if (method == "POST") {
            HandleRemoteInputPost(sock, req, false, true);
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/input/close") {
        if (!ui::remote_input::IsRemoteInputActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Remote input not active");
            return;
        }
        if (method == "POST") {
            ui::remote_input::SetClientClosed(GetQueryValue(query, "discard") == "1");
            SendResponse(sock, "200 OK", "text/plain", "OK");
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/input/status") {
        if (!ui::remote_input::IsRemoteInputActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Remote input not active");
            return;
        }
        if (method == "GET") {
            const auto json = std::string{"{\"closing\":"} +
                (ui::remote_input::IsClosing() ? "true" : "false") + "}";
            SendResponse(sock, "200 OK", "application/json", json);
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (path == "/input/body") {
        if (!ui::remote_input::IsRemoteInputActive()) {
            SendResponse(sock, "404 Not Found", "text/plain", "Remote input not active");
            return;
        }
        if (method == "GET") {
            SendResponse(sock, "200 OK", "text/plain; charset=utf-8",
                ui::remote_input::GetCurrentOptions().default_text);
            return;
        }
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (method == "POST") {
        SendResponse(sock, "404 Not Found", "text/plain", "Not found");
        return;
    }

    if (method != "GET") {
        SendResponse(sock, "405 Method Not Allowed", "text/plain", "Only GET, POST, PUT and DELETE are supported");
        return;
    }

    if (path == "/" || path == "/files" || path == "/files/") {
        SendResponse(sock, "200 OK", "text/html", BuildFolderPage(GetQueryValue(query, "path")));
        return;
    }

    if (path == "/album" || path == "/album/") {
        SendResponse(sock, "200 OK", "text/html", BuildScreenshotGalleryPage(query));
        return;
    }

    if (path == "/download") {
        SendDownload(sock, GetQueryValue(query, "path"));
        return;
    }

    if (path == "/list") {
        HandleList(sock, query);
        return;
    }

    // Console Transfer → Send installed games.
    if (path == "/games") {
        HandleGamesList(sock);
        return;
    }

    if (path == "/games/file") {
        SendGameFile(sock, req, query);
        return;
    }

    if (path == "/list-recursive") {
        HandleListRecursive(sock, query);
        return;
    }

    if (path == "/view") {
        SendView(sock, GetQueryValue(query, "path"));
        return;
    }

    if (path == "/status") {
        HandleStatus(sock);
        return;
    }

    if (path == "/progress") {
        SendResponse(sock, "200 OK", "text/html", std::string{PROGRESS_PAGE});
        return;
    }

    SendResponse(sock, "404 Not Found", "text/plain", "Not found");
}

} // namespace sphaira
