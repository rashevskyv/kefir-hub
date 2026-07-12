#include "web.hpp"
#include "web_pages.hpp"
#include "web_qr.hpp"
#include "web_upload.hpp"
#include "log.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "defines.hpp"
#include "title_info.hpp"
#include "utils/thread.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>
#include <memory>

#include "ui/progress_box.hpp"
#include "yati/yati.hpp"
#include "yati/source/stream.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace sphaira {
using namespace webpages;
namespace {

using Socket = int;

constexpr u16 SHARE_PORT_FIRST = 8080;
constexpr u16 SHARE_PORT_LAST = 8090;
constexpr size_t HTTP_READ_LIMIT = 16384;
constexpr size_t HTTP_FILE_CHUNK = 1024 * 512;
constexpr u32 IDLE_TIMEOUT_MS = 30000;
// Multiple worker threads accept() on the same listening socket so a status
// poll (e.g. from a second device) can still be served while another thread
// is blocked handling a long upload/install request.
constexpr size_t SHARE_WORKER_COUNT = 3;

std::mutex g_share_mutex{};
fs::FsPath g_share_folder_root{};
std::atomic_bool g_title_initialized{false};
Thread g_share_threads[SHARE_WORKER_COUNT]{};
size_t g_share_thread_count{};
std::atomic_bool g_share_running{false};
Socket g_share_socket{-1};
u16 g_share_port{};



// Serializes write operations (upload/install) across the worker threads. Read
// operations (browse/status/download/view) stay concurrent; only one transfer
// touches the shared g_upload_state / install pipeline at a time.
std::atomic_bool g_transfer_busy{false};

auto PathExtension(std::string_view path) -> std::string_view {
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == path.npos || (slash != path.npos && dot < slash)) {
        return {};
    }

    return path.substr(dot + 1);
}

auto ExtensionEquals(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }

    return true;
}

auto ContentTypeForPath(std::string_view path) -> const char* {
    const auto ext = PathExtension(path);
    if (ExtensionEquals(ext, "png")) {
        return "image/png";
    }
    if (ExtensionEquals(ext, "jpg") || ExtensionEquals(ext, "jpeg")) {
        return "image/jpeg";
    }
    if (ExtensionEquals(ext, "gif")) {
        return "image/gif";
    }
    if (ExtensionEquals(ext, "bmp")) {
        return "image/bmp";
    }

    return "application/octet-stream";
}

auto HtmlEscape(std::string_view in) -> std::string {
    std::string out;
    out.reserve(in.size());

    for (const auto c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }

    return out;
}

auto UrlEncode(std::string_view in) -> std::string {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());

    for (const auto c : in) {
        const auto ch = static_cast<unsigned char>(c);
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out += static_cast<char>(ch);
        } else if (ch == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hex[ch >> 4];
            out += hex[ch & 0xF];
        }
    }

    return out;
}

auto HexValue(char c) -> int {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

auto UrlDecode(std::string_view in) -> std::string {
    std::string out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '+' ) {
            out += ' ';
        } else if (in[i] == '%' && i + 2 < in.size()) {
            const auto hi = HexValue(in[i + 1]);
            const auto lo = HexValue(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            }
        } else {
            out += in[i];
        }
    }

    return out;
}

auto GetQueryValue(std::string_view query, std::string_view key) -> std::string {
    while (!query.empty()) {
        const auto amp = query.find('&');
        const auto part = query.substr(0, amp);
        const auto eq = part.find('=');
        if (eq != part.npos && part.substr(0, eq) == key) {
            return UrlDecode(part.substr(eq + 1));
        }

        if (amp == query.npos) {
            break;
        }
        query = query.substr(amp + 1);
    }

    return {};
}

auto SplitPathAndQuery(std::string path, std::string& query) -> std::string {
    query.clear();
    if (const auto pos = path.find('?'); pos != std::string::npos) {
        query = path.substr(pos + 1);
        path.resize(pos);
    }

    return path;
}


auto CanonicalizeAbsolutePath(std::string path) -> std::string {
    for (auto& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }

    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        auto part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty() && part != "." && part.find(':') == std::string::npos) {
            if (part == "..") {
                if (!parts.empty()) {
                    parts.pop_back();
                }
            } else {
                parts.push_back(std::string{part});
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    std::string out = "/";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += "/";
        }
        out += parts[i];
    }
    return out;
}


auto SanitizeFileName(std::string name) -> std::string {
    if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }

    std::string out;
    out.reserve(name.size());
    for (const auto c : name) {
        if (static_cast<unsigned char>(c) < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            out += '_';
        } else {
            out += c;
        }
    }

    while (!out.empty() && (out == "." || out == ".." || out.front() == '.')) {
        out.erase(out.begin());
    }

    if (out.empty()) {
        out = "upload.bin";
    }

    return out;
}

auto GetShareFolderRoot() -> fs::FsPath {
    std::scoped_lock lock{g_share_mutex};
    return g_share_folder_root;
}

auto SendAll(Socket sock, const void* buf, size_t size) -> bool {
    auto data = static_cast<const char*>(buf);
    u32 idle_count = 0;

    while (size) {
        if (!g_share_running.load()) {
            return false;
        }
        const auto sent = send(sock, data, size, 0);
        if (sent < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                idle_count++;
                if (idle_count > IDLE_TIMEOUT_MS) {
                    return false;
                }
                svcSleepThread(1'000'000);
                continue;
            }

            return false;
        }

        idle_count = 0;
        data += sent;
        size -= sent;
    }

    return true;
}

auto SendString(Socket sock, const std::string& str) -> bool {
    return SendAll(sock, str.data(), str.size());
}

void SendResponse(Socket sock, const char* status, const char* content_type, const std::string& body) {
    char header[512]{};
    std::snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body.size());

    SendString(sock, header);
    SendString(sock, body);
}

auto ReadHttpRequest(Socket sock, std::string& out) -> bool {
    out.clear();

    for (u32 attempts = 0; attempts < 5000 && out.size() < HTTP_READ_LIMIT; attempts++) {
        char buf[512];
        const auto got = recv(sock, buf, sizeof(buf), 0);
        if (got > 0) {
            out.append(buf, got);
            if (out.find("\r\n\r\n") != std::string::npos) {
                return true;
            }
        } else if (got == 0) {
            return !out.empty();
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            svcSleepThread(1'000'000);
        } else {
            return false;
        }
    }

    return !out.empty();
}



auto HeaderValue(const std::string& req, std::string_view name) -> std::string {
    auto pos = req.find("\r\n");
    while (pos != std::string::npos) {
        const auto next = req.find("\r\n", pos + 2);
        if (next == std::string::npos || next == pos + 2) {
            break;
        }

        const auto line = std::string_view{req}.substr(pos + 2, next - pos - 2);
        const auto colon = line.find(':');
        if (colon != line.npos) {
            const auto key = line.substr(0, colon);
            if (key.size() == name.size()) {
                bool same = true;
                for (size_t i = 0; i < key.size(); i++) {
                    if (std::tolower(static_cast<unsigned char>(key[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
                        same = false;
                        break;
                    }
                }

                if (same) {
                    auto value = std::string{line.substr(colon + 1)};
                    while (!value.empty() && value.front() == ' ') {
                        value.erase(value.begin());
                    }
                    return value;
                }
            }
        }

        pos = next;
    }

    return {};
}

auto IsImagePath(std::string_view name) -> bool {
    const auto ext = PathExtension(name);
    return ExtensionEquals(ext, "png") || ExtensionEquals(ext, "jpg") || 
           ExtensionEquals(ext, "jpeg") || ExtensionEquals(ext, "gif") || 
           ExtensionEquals(ext, "bmp");
}

void AppendLightbox(std::string& body) {
    body += LIGHTBOX_CONTENT;
}

void AppendConfirmModal(std::string& body) {
    body += CONFIRM_MODAL_HTML;
}

auto BuildFolderPage(std::string path_str) -> std::string {
    if (path_str.empty()) {
        path_str = GetShareFolderRoot().toString();
    }
    const auto abs_path = CanonicalizeAbsolutePath(path_str);

    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(abs_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
        dir.ReadAll(entries);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs){
        if (lhs.type != rhs.type) {
            return lhs.type == FsDirEntryType_Dir;
        }
        return strcasecmp(lhs.name, rhs.name) < 0;
    });

    const auto encoded_path = UrlEncode(abs_path);
    std::string body;
    body.reserve(24576 + entries.size() * 512);

    body += FOLDER_PAGE_HEADER;
    body += "<div class=\"header-top\"><h1>Kefir Hub Files</h1><a href=\"/progress\" style=\"text-decoration:none;\"><button><span class=\"icon\">⏳</span> <span class=\"text\">Progress</span></button></a><a href=\"/album\" style=\"text-decoration:none;\"><button><span class=\"icon\">📸</span> <span class=\"text\">Screenshots</span></button></a></div><div class=\"crumbs\"><a href=\"/?path=/\">SD Card</a>";

    std::string crumb_accum;
    size_t start{};
    while (start < abs_path.size()) {
        const auto end = abs_path.find('/', start);
        const auto part = abs_path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            crumb_accum += '/' + part;
            body += " / <a href=\"/?path=";
            body += UrlEncode(crumb_accum);
            body += "\">";
            body += HtmlEscape(part);
            body += "</a>";
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    body += "</div><div class=\"bar\"><button id=\"upload\" onclick=\"document.getElementById('files').click()\"><span class=\"icon\">↑</span> <span class=\"text\">Add to Upload</span></button>";
    body += "<button id=\"view-toggle\" onclick=\"toggleViewMode()\"><span class=\"icon\">⊞</span> <span class=\"text\">Grid View</span></button>";
    body += "<button id=\"select-all-btn\" onclick=\"toggleSelectAll()\"><span class=\"icon\">✓</span> <span class=\"text\">Select All</span></button>";
    body += "<button id=\"download-selected\" onclick=\"addSelectedToDownloadQueue()\" style=\"border-color:rgba(56,189,248,0.4);background:rgba(56,189,248,0.1);color:#38bdf8;\" disabled><span class=\"icon\">↓</span> <span class=\"text\">Download Selected</span> <span class=\"count\">(0)</span></button>";
    body += "<button id=\"delete-selected\" onclick=\"deleteSelected()\" style=\"border-color:rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171;\" disabled><span class=\"icon\">🗑</span> <span class=\"text\">Delete Selected</span> <span class=\"count\">(0)</span></button>";
    body += "<button id=\"queue-toggle-btn\" onclick=\"toggleQueuePanel()\" style=\"border-color:rgba(168,85,247,0.4);background:rgba(168,85,247,0.1);color:#c084fc;\"><span class=\"icon\">📋</span> <span class=\"text\">Queue</span> <span class=\"count\">(0)</span></button>";
    body += "<input id=\"files\" type=\"file\" multiple onchange=\"addFilesToUploadQueue(this.files)\"><span id=\"status\" class=\"status\"></span></div></header>";
    body += "<div class=\"container\"><main id=\"items-container\" class=\"list\">";

    if (abs_path != "/") {
        auto parent = abs_path;
        if (const auto slash = parent.find_last_of('/'); slash != std::string::npos) {
            parent.resize(slash);
        }
        if (parent.empty()) {
            parent = "/";
        }

        body += "<a class=\"item\" href=\"/?path=";
        body += UrlEncode(parent);
        body += "\"><div class=\"thumbnail-box\">"
                "<svg viewBox=\"0 0 24 24\" fill=\"#ffca28\"><path d=\"M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg>"
                "</div>";
        body += "<div class=\"info\"><span class=\"name\">..</span><span class=\"meta\">parent folder</span></div></a>";
    }

    if (entries.empty()) {
        body += "<div class=\"empty\">Empty folder</div>";
    }

    for (const auto& entry : entries) {
        const std::string name{entry.name};
        auto child = abs_path;
        if (child.empty() || child.back() != '/') {
            child += '/';
        }
        child += name;

        const auto encoded_child = UrlEncode(child);
        const auto escaped_name = HtmlEscape(name);
        if (entry.type == FsDirEntryType_Dir) {
            body += "<a class=\"item\" href=\"/?path=";
            body += encoded_child;
            body += "\">";
            body += "<input type=\"checkbox\" class=\"file-checkbox\" data-path=\"";
            body += encoded_child;
            body += "\" onclick=\"event.stopPropagation(); updateSelectCount();\">";
            body += "<div class=\"thumbnail-box\">"
                    "<svg viewBox=\"0 0 24 24\" fill=\"#ffca28\"><path d=\"M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg>"
                    "</div>";
            body += "<div class=\"info\"><span class=\"name\">";
            body += escaped_name;
            body += "</span><span class=\"meta meta-folder\">folder</span><button class=\"delete-btn\" onclick=\"deleteFile(event,'";
            body += encoded_child;
            body += "')\">&times;</button></div></a>";
        } else {
            const bool is_image = IsImagePath(name);
            body += "<a class=\"item\" href=\"";
            body += is_image ? "/view?path=" : "/download?path=";
            body += encoded_child;
            body += "\">";
            body += "<input type=\"checkbox\" class=\"file-checkbox\" data-path=\"";
            body += encoded_child;
            body += "\" onclick=\"event.stopPropagation(); updateSelectCount();\">";
            body += "<div class=\"thumbnail-box\">";
            if (is_image) {
                body += "<img class=\"thumb\" src=\"/view?path=";
                body += encoded_child;
                body += "\" alt=\"\" loading=\"lazy\">";
            } else {
                body += "<svg viewBox=\"0 0 24 24\" fill=\"#90a4ae\"><path d=\"M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z\"/></svg>";
            }
            body += "</div><div class=\"info\"><span class=\"name\">";
            body += escaped_name;
            body += "</span><span class=\"meta meta-size\">";
            if (entry.file_size >= 1024 * 1024) {
                char size_buf[64]{};
                std::snprintf(size_buf, sizeof(size_buf), "%.2f MiB", static_cast<double>(entry.file_size) / 1024.0 / 1024.0);
                body += size_buf;
            } else {
                char size_buf[64]{};
                std::snprintf(size_buf, sizeof(size_buf), "%.2f KiB", static_cast<double>(entry.file_size) / 1024.0);
                body += size_buf;
            }
            body += "</span><button class=\"delete-btn\" onclick=\"deleteFile(event,'";
            body += encoded_child;
            body += "')\">&times;</button></div></a>";
        }
    }

    body += "<script>let currentPath=decodeURIComponent('";
    body += encoded_path;
    body += "');";
    body += CONFIRM_MODAL_JS;
    body += FOLDER_PAGE_JS;

    AppendConfirmModal(body);
    AppendLightbox(body);

    body += "</body></html>";

    return body;
}



void SendDownload(Socket sock, const std::string& rel) {
    if (rel.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing file path");
        return;
    }

    const auto path = CanonicalizeAbsolutePath(rel);
    auto name = path;
    if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    name = SanitizeFileName(name);

    fs::FsNativeSd fs;
    fs::File file;
    if (R_FAILED(fs.OpenFile(path, FsOpenMode_Read, &file))) {
        SendResponse(sock, "404 Not Found", "text/plain", "Could not open file");
        return;
    }

    s64 size{};
    if (R_FAILED(file.GetSize(&size))) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not read file size");
        return;
    }

    char header[768]{};
    std::snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %zd\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        name.c_str(), size);

    if (!SendString(sock, header)) {
        return;
    }

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    s64 offset{};
    while (offset < size) {
        const auto todo = std::min<s64>(buf.size(), size - offset);
        u64 bytes_read{};
        if (R_FAILED(file.Read(offset, buf.data(), todo, FsReadOption_None, &bytes_read)) || !bytes_read) {
            return;
        }

        if (!SendAll(sock, buf.data(), bytes_read)) {
            return;
        }

        offset += bytes_read;
    }
}

void SendView(Socket sock, const std::string& rel) {
    if (rel.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing file path");
        return;
    }

    const auto path = CanonicalizeAbsolutePath(rel);
    auto name = path;
    if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    name = SanitizeFileName(name);

    fs::FsNativeSd fs;
    fs::File file;
    if (R_FAILED(fs.OpenFile(path, FsOpenMode_Read, &file))) {
        SendResponse(sock, "404 Not Found", "text/plain", "Could not open file");
        return;
    }

    s64 size{};
    if (R_FAILED(file.GetSize(&size))) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not read file size");
        return;
    }

    char header[768]{};
    std::snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Disposition: inline; filename=\"%s\"\r\n"
        "Content-Length: %zd\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        ContentTypeForPath(path), name.c_str(), size);

    if (!SendString(sock, header)) {
        return;
    }

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    s64 offset{};
    while (offset < size) {
        const auto todo = std::min<s64>(buf.size(), size - offset);
        u64 bytes_read{};
        if (R_FAILED(file.Read(offset, buf.data(), todo, FsReadOption_None, &bytes_read)) || !bytes_read) {
            return;
        }

        if (!SendAll(sock, buf.data(), bytes_read)) {
            return;
        }

        offset += bytes_read;
    }
}




auto UniqueUploadPath(fs::FsNativeSd& sd, const fs::FsPath& dir, const std::string& name) -> fs::FsPath {
    auto out = fs::AppendPath(dir, name);
    if (!sd.FileExists(out) && !sd.DirExists(out)) {
        return out;
    }

    auto stem = name;
    std::string ext;
    if (const auto dot = name.find_last_of('.'); dot != std::string::npos) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }

    for (u64 i = 1; ; i++) {
        char buf[FS_MAX_PATH]{};
        std::snprintf(buf, sizeof(buf), "%s (%llu)%s", stem.c_str(), static_cast<unsigned long long>(i), ext.c_str());
        out = fs::AppendPath(dir, buf);
        if (!sd.FileExists(out) && !sd.DirExists(out)) {
            return out;
        }
    }
}



void ReceiveUpload(Socket sock, const std::string& req, const std::string& query) {
    const auto length_str = HeaderValue(req, "content-length");
    if (length_str.empty()) {
        SendResponse(sock, "411 Length Required", "text/plain", "Missing Content-Length");
        return;
    }

    const auto content_length = std::strtoll(length_str.c_str(), nullptr, 10);
    if (content_length < 0) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad Content-Length");
        return;
    }

    // Only one transfer may run at a time; a concurrent upload/install would
    // clobber the shared progress state and install pipeline.
    bool expected = false;
    if (!g_transfer_busy.compare_exchange_strong(expected, true)) {
        SendResponse(sock, "409 Conflict", "text/plain", "Another transfer is already in progress");
        return;
    }
    ON_SCOPE_EXIT(g_transfer_busy.store(false));

    const auto raw_path = GetQueryValue(query, "path");
    const auto dir = CanonicalizeAbsolutePath(raw_path.empty() ? GetShareFolderRoot().toString() : raw_path);
    const auto raw_name = GetQueryValue(query, "name");
    const auto name = SanitizeFileName(raw_name);

    const auto install_param = GetQueryValue(query, "install");
    const bool direct_install = (install_param == "1");

    if (direct_install) {
        auto pbox = WebGetProgressBox();
        if (!pbox) {
            SendResponse(sock, "500 Internal Server Error", "text/plain", "Installation not possible (No active UI)");
            return;
        }

        std::string initial_body;
        const auto header_end = req.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            const auto body_start = header_end + 4;
            const auto available = static_cast<s64>(req.size() - body_start);
            const auto write_size = std::min<s64>(available, content_length);
            if (write_size > 0) {
                initial_body = req.substr(body_start, write_size);
            }
        }

        auto stream_source = std::make_unique<SocketStream>(sock, initial_body, content_length);
        
        const auto ext = PathExtension(name);
        const bool is_compressed = ExtensionEquals(ext, "nsz") || ExtensionEquals(ext, "xcz") || ExtensionEquals(ext, "ncz");
        const bool install_to_sd = yati::ChooseInstallTarget(content_length, is_compressed);

        const std::string dest_str = install_to_sd ? " (SD Card)" : " (System Memory)";
        {
            std::scoped_lock lock{g_upload_state.name_mutex};
            g_upload_state.name = "Installing: " + name + dest_str;
        }
        g_upload_state.total.store(content_length);
        g_upload_state.bytes.store(initial_body.size());
        g_upload_state.active.store(true);

        fs::FsPath dummy_path = "/";
        dummy_path += name;

        yati::ConfigOverride override{};
        override.sd_card_install = install_to_sd;
        if (pbox) {
            pbox->Mute(true);
        }
        const auto rc = yati::InstallFromSource(pbox, stream_source.get(), dummy_path, override);
        if (pbox) {
            pbox->Mute(false);
        }

        g_upload_state.active.store(false);

        if (R_FAILED(rc)) {
            log_write("Direct install failed: 0x%X\n", rc);
            char err_msg[128]{};
            std::snprintf(err_msg, sizeof(err_msg), "Installation failed: 0x%X", rc);
            SendResponse(sock, "500 Internal Server Error", "text/plain", err_msg);
            return;
        }

        SendResponse(sock, "200 OK", "text/plain", "Installed");
        return;
    }

    fs::FsNativeSd fs;
    if (!fs.DirExists(dir)) {
        SendResponse(sock, "404 Not Found", "text/plain", "Upload folder not found");
        return;
    }

    const auto out_path = UniqueUploadPath(fs, dir, name);
    if (auto rc = fs.CreateFile(out_path, content_length, 0); R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not create file");
        return;
    }

    struct UploadGuard {
        fs::FsNativeSd& fs;
        const fs::FsPath& path;
        bool success = false;

        ~UploadGuard() {
            if (!success) {
                fs.DeleteFile(path);
                log_write("Upload aborted or failed. Deleted incomplete file: %s\n", path.toString().c_str());
            }
        }
    } upload_guard{fs, out_path};

    fs::File file;
    if (R_FAILED(fs.OpenFile(out_path, FsOpenMode_Write, &file))) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not open output file");
        return;
    }

    const auto header_end = req.find("\r\n\r\n");
    s64 offset{};
    if (header_end != std::string::npos) {
        const auto body_start = header_end + 4;
        const auto available = static_cast<s64>(req.size() - body_start);
        const auto write_size = std::min<s64>(available, content_length);
        if (write_size > 0) {
            if (R_FAILED(file.Write(offset, req.data() + body_start, write_size, FsWriteOption_None))) {
                SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not write file");
                return;
            }
            offset += write_size;
        }
    }

    {
        std::scoped_lock lock{g_upload_state.name_mutex};
        g_upload_state.name = name;
    }
    g_upload_state.total.store(content_length);
    g_upload_state.bytes.store(offset);
    g_upload_state.active.store(true);

    // Overlap the network receive with SD-card writes: while the writer
    // thread flushes one chunk to the card, this thread keeps draining the
    // socket into the next one. Doing the two sequentially adds their
    // latencies together and leaves the TCP window idle during each write.
    struct UploadWriter {
        static void Func(void* p) {
            auto self = static_cast<UploadWriter*>(p);
            while (true) {
                waitSingle(waiterForUEvent(&self->work_event), UINT64_MAX);
                if (self->exit) {
                    return;
                }
                if (R_SUCCEEDED(self->result)) {
                    self->result = self->file->Write(self->off, self->buf.data(), self->buf.size(), FsWriteOption_None);
                }
                ueventSignal(&self->done_event);
            }
        }

        fs::File* file{};
        std::vector<u8> buf{};
        s64 off{};
        std::atomic<Result> result{};
        std::atomic_bool exit{};
        UEvent work_event{};
        UEvent done_event{};
        Thread thread{};
        bool started{};
        bool busy{};
    } writer{};

    writer.file = &file;
    ueventCreate(&writer.work_event, true);
    ueventCreate(&writer.done_event, true);
    if (R_SUCCEEDED(utils::CreateThread(&writer.thread, UploadWriter::Func, &writer, 1024 * 32, PRIO_PREEMPTIVE))) {
        if (R_SUCCEEDED(threadStart(&writer.thread))) {
            writer.started = true;
        } else {
            threadClose(&writer.thread);
        }
    }

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    size_t fill{};
    s64 block_off = offset;

    // hand the filled buffer over to the writer thread, waiting for its
    // previous write to finish first. falls back to a synchronous write if
    // the writer thread could not be started.
    const auto write_block = [&]() -> Result {
        if (!writer.started) {
            const auto rc = file.Write(block_off, buf.data(), fill, FsWriteOption_None);
            block_off += fill;
            fill = 0;
            return rc;
        }

        if (writer.busy) {
            waitSingle(waiterForUEvent(&writer.done_event), UINT64_MAX);
            writer.busy = false;
        }
        R_TRY(writer.result.load());

        buf.resize(fill);
        std::swap(buf, writer.buf);
        writer.off = block_off;
        buf.resize(HTTP_FILE_CHUNK);

        block_off += fill;
        fill = 0;
        writer.busy = true;
        ueventSignal(&writer.work_event);
        R_SUCCEED();
    };

    // waits for the in-flight write and stops the writer thread. must be
    // called on every exit path so the thread never outlives this frame.
    const auto finish_writer = [&]() -> Result {
        if (!writer.started) {
            R_SUCCEED();
        }
        if (writer.busy) {
            waitSingle(waiterForUEvent(&writer.done_event), UINT64_MAX);
            writer.busy = false;
        }
        writer.exit = true;
        ueventSignal(&writer.work_event);
        threadWaitForExit(&writer.thread);
        threadClose(&writer.thread);
        writer.started = false;
        return writer.result;
    };

    const char* fail_status{};
    const char* fail_msg{};
    bool aborted{};

    u32 idle_count = 0;
    while (offset < content_length) {
        if (!g_share_running.load()) {
            aborted = true;
            break;
        }
        if (auto pbox = WebGetProgressBox()) {
            if (pbox->ShouldExit()) {
                fail_status = "400 Bad Request";
                fail_msg = "Cancelled by user";
                break;
            }
        }
        const auto want = std::min<s64>(buf.size() - fill, content_length - offset);
        const auto got = recv(sock, buf.data() + fill, want, 0);
        if (got > 0) {
            idle_count = 0;
            fill += got;
            offset += got;
            g_upload_state.bytes.store(offset);

            if (fill == buf.size() || offset == content_length) {
                if (R_FAILED(write_block())) {
                    fail_status = "500 Internal Server Error";
                    fail_msg = "Could not write file";
                    break;
                }
            }
        } else if (got == 0) {
            fail_status = "400 Bad Request";
            fail_msg = "Upload ended early";
            break;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            idle_count++;
            if (idle_count > IDLE_TIMEOUT_MS) {
                fail_status = "408 Request Timeout";
                fail_msg = "Receive timeout";
                break;
            }
            svcSleepThread(1'000'000);
        } else {
            fail_status = "500 Internal Server Error";
            fail_msg = "Socket read failed";
            break;
        }
    }

    const auto write_rc = finish_writer();
    g_upload_state.active.store(false);

    if (aborted) {
        return;
    }

    if (!fail_status && R_FAILED(write_rc)) {
        fail_status = "500 Internal Server Error";
        fail_msg = "Could not write file";
    }

    if (fail_status) {
        SendResponse(sock, fail_status, "text/plain", fail_msg);
        return;
    }

    upload_guard.success = true;
    SendResponse(sock, "200 OK", "text/plain", "Uploaded");
}

void HandleDelete(Socket sock, const std::string& query) {
    const auto raw_path = GetQueryValue(query, "path");
    if (raw_path.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing file path");
        return;
    }

    const auto path = CanonicalizeAbsolutePath(raw_path);

    fs::FsNativeSd sd;
    if (sd.DirExists(path)) {
        log_write("Web UI deleting directory recursively: %s\n", path.c_str());
        if (R_FAILED(sd.DeleteDirectoryRecursively(path))) {
            SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not delete folder recursively");
            return;
        }
        SendResponse(sock, "200 OK", "text/plain", "Deleted");
        return;
    }

    if (!sd.FileExists(path)) {
        SendResponse(sock, "404 Not Found", "text/plain", "File not found");
        return;
    }

    log_write("Web UI deleting file: %s\n", path.c_str());
    if (R_FAILED(sd.DeleteFile(path))) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not delete file");
        return;
    }

    SendResponse(sock, "200 OK", "text/plain", "Deleted");
}


auto JsonEscape(std::string_view in) -> std::string {
    std::string out;
    out.reserve(in.size());
    for (const auto c : in) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out += c;
        }
    }
    return out;
}

void ScanDirectoryRecursive(fs::FsNativeSd& fs, const std::string& start_path, std::vector<std::pair<std::string, s64>>& out_files) {
    struct StackEntry {
        std::string path;
        int depth;
    };
    std::vector<StackEntry> stack;
    stack.push_back({start_path, 0});

    while (!stack.empty()) {
        auto entry = stack.back();
        stack.pop_back();

        if (entry.depth > 64) {
            continue;
        }

        fs::Dir dir;
        std::vector<FsDirectoryEntry> entries;
        if (R_SUCCEEDED(fs.OpenDirectory(entry.path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
            dir.ReadAll(entries);
            for (const auto& d_entry : entries) {
                std::string child = entry.path;
                if (child.empty() || child.back() != '/') {
                    child += '/';
                }
                child += d_entry.name;
                if (d_entry.type == FsDirEntryType_Dir) {
                    stack.push_back({child, entry.depth + 1});
                } else {
                    out_files.push_back({child, d_entry.file_size});
                }
            }
        }
    }
}

void HandleListRecursive(Socket sock, const std::string& query) {
    const auto raw_path = GetQueryValue(query, "path");
    if (raw_path.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing path");
        return;
    }

    const auto path = CanonicalizeAbsolutePath(raw_path);

    fs::FsNativeSd fs;
    std::vector<std::pair<std::string, s64>> files;
    if (fs.DirExists(path)) {
        ScanDirectoryRecursive(fs, path, files);
    } else if (fs.FileExists(path)) {
        fs::File file;
        s64 size = 0;
        if (R_SUCCEEDED(fs.OpenFile(path, FsOpenMode_Read, &file))) {
            file.GetSize(&size);
        }
        files.push_back({path, size});
    }

    std::string json = "[";
    for (size_t i = 0; i < files.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"path\":\"" + JsonEscape(files[i].first) + "\",\"size\":" + std::to_string(files[i].second) + "}";
    }
    json += "]";

    SendResponse(sock, "200 OK", "application/json", json);
}

void HandleList(Socket sock, const std::string& query) {
    const auto raw_path = GetQueryValue(query, "path");
    const auto path = CanonicalizeAbsolutePath(raw_path.empty() ? GetShareFolderRoot().toString() : raw_path);

    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
        dir.ReadAll(entries);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs){
        if (lhs.type != rhs.type) {
            return lhs.type == FsDirEntryType_Dir;
        }
        return strcasecmp(lhs.name, rhs.name) < 0;
    });

    std::string json = "{\"path\":\"" + JsonEscape(path) + "\",\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"name\":\"" + JsonEscape(entries[i].name) + "\",\"type\":" + std::to_string(entries[i].type) + ",\"size\":" + std::to_string(entries[i].file_size) + "}";
    }
    json += "]}";

    SendResponse(sock, "200 OK", "application/json", json);
}

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



struct ScreenshotEntry {
    std::string path;
    std::string filename;
    std::string timestamp; // YYYY-MM-DD HH:MM:SS
    std::string game_name;
    s64 file_size{};
    std::string raw_timestamp; // YYYYMMDDHHMMSS00 (for sorting)
    bool is_video{};
};

bool TryParseScreenshotEntry(const FsDirectoryEntry& d_entry, const std::string& full_path, ScreenshotEntry& se) {
    std::string ext = d_entry.name;
    if (const auto dot = ext.find_last_of('.'); dot != std::string::npos) {
        ext = ext.substr(dot + 1);
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext != "jpg" && ext != "png" && ext != "mp4" && ext != "jpeg") {
        return false;
    }

    se.path = full_path;
    se.filename = d_entry.name;
    se.file_size = d_entry.file_size;
    se.is_video = (ext == "mp4");

    std::string stem = d_entry.name;
    if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }

    bool parsed = false;
    if (stem.size() >= 33 && stem[16] == '-') {
        bool all_digits = true;
        for (int i = 0; i < 16; i++) {
            if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            se.raw_timestamp = stem.substr(0, 16);
            se.timestamp = stem.substr(0, 4) + "-" + stem.substr(4, 2) + "-" + stem.substr(6, 2) + " " +
                           stem.substr(8, 2) + ":" + stem.substr(10, 2) + ":" + stem.substr(12, 2);

            std::string title_id_hex = stem.substr(17, 16);
            char* endptr = nullptr;
            u64 title_id = std::strtoull(title_id_hex.c_str(), &endptr, 16);
            if (endptr != nullptr && *endptr == '\0') {
                if (auto info = title::Get(title_id)) {
                    if (title::IsPlaceholderName(info->lang.name)) {
                        se.game_name = "Title: " + title_id_hex;
                    } else {
                        se.game_name = info->lang.name;
                    }
                } else {
                    se.game_name = "Title: " + title_id_hex;
                }
            } else {
                se.game_name = "Unknown Game";
            }
            parsed = true;
        }
    }

    if (!parsed) {
        se.raw_timestamp = "";
        se.timestamp = "Unknown Date";
        se.game_name = "Unknown Game";
    }

    return true;
}

bool CompareScreenshotEntries(const ScreenshotEntry& lhs, const ScreenshotEntry& rhs) {
    if (lhs.raw_timestamp.empty() && !rhs.raw_timestamp.empty()) return false;
    if (!lhs.raw_timestamp.empty() && rhs.raw_timestamp.empty()) return true;
    return lhs.raw_timestamp > rhs.raw_timestamp;
}

void ScanScreenshots(std::vector<ScreenshotEntry>& out_entries) {
    fs::FsNativeSd fs;
    if (!fs.DirExists("/Nintendo/Album")) {
        return;
    }

    struct StackEntry {
        std::string path;
        int depth;
    };
    std::vector<StackEntry> stack;
    stack.push_back({"/Nintendo/Album", 0});

    while (!stack.empty()) {
        auto entry = stack.back();
        stack.pop_back();

        if (entry.depth > 64) {
            continue;
        }

        fs::Dir dir;
        std::vector<FsDirectoryEntry> entries;
        if (R_SUCCEEDED(fs.OpenDirectory(entry.path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
            dir.ReadAll(entries);
            for (const auto& d_entry : entries) {
                std::string child = entry.path;
                if (child.empty() || child.back() != '/') {
                    child += '/';
                }
                child += d_entry.name;

                if (d_entry.type == FsDirEntryType_Dir) {
                    stack.push_back({child, entry.depth + 1});
                } else {
                    ScreenshotEntry se;
                    if (TryParseScreenshotEntry(d_entry, child, se)) {
                        out_entries.push_back(se);
                    }
                }
            }
        }
    }

    std::sort(out_entries.begin(), out_entries.end(), CompareScreenshotEntries);
}

bool IsValidAlbumPath(const std::string& path) {
    if (path.empty() || path[0] != '/') {
        return false;
    }
    for (char c : path) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '/') {
            return false;
        }
    }
    return true;
}

void ScanFolders(const std::string& parent_path, std::vector<std::string>& out_folders) {
    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(parent_path, FsDirOpenMode_ReadDirs, &dir))) {
        dir.ReadAll(entries);
        for (const auto& entry : entries) {
            if (entry.type == FsDirEntryType_Dir) {
                bool all_digits = true;
                for (size_t i = 0; entry.name[i] != '\0'; ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(entry.name[i]))) {
                        all_digits = false;
                        break;
                    }
                }
                if (all_digits) {
                    out_folders.push_back(entry.name);
                }
            }
        }
    }
    std::sort(out_folders.begin(), out_folders.end(), std::greater<std::string>());
}

void ScanFolderFiles(const std::string& folder_path, std::vector<ScreenshotEntry>& out_entries) {
    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(folder_path, FsDirOpenMode_ReadFiles, &dir))) {
        dir.ReadAll(entries);
        for (const auto& d_entry : entries) {
            std::string child = folder_path;
            if (child.back() != '/') {
                child += '/';
            }
            child += d_entry.name;

            ScreenshotEntry se;
            if (TryParseScreenshotEntry(d_entry, child, se)) {
                out_entries.push_back(se);
            }
        }
    }

    std::sort(out_entries.begin(), out_entries.end(), CompareScreenshotEntries);
}

auto BuildScreenshotGalleryPage(const std::string& query) -> std::string {
    std::string req_path = GetQueryValue(query, "path");
    
    bool browse_mode = query.find("path=") != std::string::npos;
    if (browse_mode && !req_path.empty() && !IsValidAlbumPath(req_path)) {
        req_path = "/";
    }

    std::string body;
    body.reserve(16384);

    body += "<!doctype html><html><head><meta charset=\"utf-8\">";
    body += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    body += "<title>Kefir Hub Album</title>";
    body += "<style>";
    body += "body{margin:0;font:16px system-ui,-apple-system,Segoe UI,sans-serif;background:#101114;color:#f7f7f7}";
    body += "header{position:sticky;top:0;background:#17191d;padding:16px 18px;border-bottom:1px solid #333;z-index:10}";
    body += "h1{font-size:20px;margin:0 0 6px}.subtitle{color:#b9c2cc;font-size:14px}";
    body += ".crumbs{margin-top:8px}.crumbs a{color:#38bdf8;text-decoration:none}";
    body += ".crumbs a:hover{text-decoration:underline}";
    body += ".tabs{display:flex;gap:4px;padding:0 18px;margin-top:16px;border-bottom:1px solid #333}";
    body += ".tab{padding:8px 16px;text-decoration:none;color:#98a1aa;border:1px solid transparent;border-bottom:none;border-radius:6px 6px 0 0;font-size:14px;font-weight:500;transition:all 0.15s}";
    body += ".tab:hover{color:#fff;background:#17191d}";
    body += ".tab.active{color:#fff;background:#1c1c1c;border-color:#333}";
    body += ".album-crumbs{padding:12px 18px 0;font-size:14px;color:#8a939e}";
    body += ".album-crumbs a{color:#38bdf8;text-decoration:none}";
    body += ".album-crumbs a:hover{text-decoration:underline}";
    body += ".folder-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:16px;padding:18px}";
    body += ".folder-card{display:flex;flex-direction:column;align-items:center;justify-content:center;background:#1c1c1c;border:1px solid #333;border-radius:10px;padding:20px;text-decoration:none;color:#fff;box-shadow:0 4px 6px rgba(0,0,0,0.3);transition:all 0.15s}";
    body += ".folder-card:hover{transform:translateY(-2px);background:#252a32;border-color:#4f535c}";
    body += ".folder-icon{width:48px;height:48px;margin-bottom:12px;color:#ffca28}";
    body += ".folder-name{font-weight:600;font-size:15px}";
    body += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:16px;padding:18px}";
    body += ".card{display:flex;flex-direction:column;background:#1c1c1c;border:1px solid #333;border-radius:10px;overflow:hidden;box-shadow:0 4px 6px rgba(0,0,0,0.3);transition:transform 0.15s}";
    body += ".card:hover{transform:translateY(-2px)}";
    body += "img,video{display:block;width:100%;aspect-ratio:16/9;object-fit:cover;background:#050505;border-bottom:1px solid #2d2d2d}";
    body += ".info{padding:12px;display:flex;flex-direction:column;gap:6px;flex-grow:1}";
    body += ".game-name{font-weight:600;color:#fff;font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}";
    body += ".timestamp{color:#8a939e;font-size:12px}";
    body += ".size{color:#6c757d;font-size:11px}";
    body += ".actions{display:flex;gap:8px;margin-top:auto;padding-top:10px}";
    body += "a.btn,button.btn{flex:1;text-align:center;text-decoration:none;border:1px solid #4f535c;background:#252a32;color:#fff;border-radius:6px;padding:8px 0;font-size:12px;font-weight:500;cursor:pointer;transition:all 0.15s}";
    body += "a.btn:hover,button.btn:hover{background:#303640;border-color:#656b77}";
    body += "button.del-btn{border-color:rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171}";
    body += "button.del-btn:hover{background:rgba(239,68,68,0.25);border-color:rgba(239,68,68,0.7)}";
    body += ".empty{grid-column:1/-1;text-align:center;padding:50px 20px;color:#98a1aa;font-style:italic}";
    body += ".header-top{display:flex;justify-content:space-between;align-items:center;gap:16px}";
    body += "a.header-link-btn button{border:1px solid rgba(255,255,255,0.15);background:#1e293b;color:#fff;border-radius:8px;padding:8px 16px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s}";
    body += "a.header-link-btn button:hover{background:#334155;border-color:rgba(255,255,255,0.25)}";
    body += "@media (max-width: 600px) {";
    body += "  header{padding:12px 16px}";
    body += "  .header-top{flex-direction:column;align-items:flex-start;gap:6px}";
    body += "  .crumbs{text-align:left;max-width:100%}";
    body += "  a.header-link-btn button{padding:8px 12px}";
    body += "  a.header-link-btn button .text{display:none}";
    body += "  .folder-grid{grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:8px;padding:12px}";
    body += "  .grid{grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:8px;padding:12px}";
    body += "  .grid .info{padding:6px;gap:2px}";
    body += "  .game-name{font-size:11px}";
    body += "  .timestamp{font-size:9px}";
    body += "  .size{font-size:8px}";
    body += "  .folder-card{padding:10px}";
    body += "  .folder-icon{width:32px;height:32px;margin-bottom:6px}";
    body += "  .folder-name{font-size:11px}";
    body += "}";
    body += "</style></head><body><header><div class=\"header-top\"><h1>Kefir Hub Album</h1><a href=\"/files?path=/\" class=\"header-link-btn\" style=\"text-decoration:none;\"><button><span class=\"icon\">📁</span> <span class=\"text\">File Browser</span></button></a></div></header>";

    body += "<div class=\"tabs\">";
    body += "<a href=\"/album\" class=\"tab" + std::string(!browse_mode ? " active" : "") + "\">All Screenshots</a>";
    body += "<a href=\"/album?path=/\" class=\"tab" + std::string(browse_mode ? " active" : "") + "\">Browse by Date</a>";
    body += "</div>";

    if (!browse_mode) {
        std::vector<ScreenshotEntry> entries;
        ScanScreenshots(entries);

        body += "<main class=\"grid\">";
        if (entries.empty()) {
            body += "<div class=\"empty\">No screenshots or videos found in /Nintendo/Album</div>";
        } else {
            for (const auto& entry : entries) {
                const auto encoded_path = UrlEncode(entry.path);
                const auto escaped_game = HtmlEscape(entry.game_name);
                const auto escaped_time = HtmlEscape(entry.timestamp);
                const auto escaped_filename = HtmlEscape(entry.filename);

                body += "<div class=\"card\">";
                if (entry.is_video) {
                    body += "<video controls preload=\"none\" src=\"/view?path=" + encoded_path + "\"></video>";
                } else {
                    body += "<a href=\"/view?path=" + encoded_path + "\"><img loading=\"lazy\" src=\"/view?path=" + encoded_path + "\" alt=\"" + escaped_game + "\"></a>";
                }
                body += "<div class=\"info\">";
                body += "<div class=\"game-name\" title=\"" + escaped_game + "\">" + escaped_game + "</div>";
                body += "<div class=\"timestamp\">" + escaped_time + "</div>";

                char size_buf[32]{};
                if (entry.file_size >= 1024 * 1024) {
                    std::snprintf(size_buf, sizeof(size_buf), "%.2f MB", static_cast<double>(entry.file_size) / (1024.0 * 1024.0));
                } else {
                    std::snprintf(size_buf, sizeof(size_buf), "%.1f KB", static_cast<double>(entry.file_size) / 1024.0);
                }
                body += "<div class=\"size\">" + std::string(size_buf) + " (" + escaped_filename + ")</div>";
                body += "<div class=\"actions\">";
                body += "<button class=\"btn del-btn\" onclick=\"deleteItem(event,'" + encoded_path + "')\">Delete</button>";
                body += "</div></div></div>";
            }
        }
        body += "</main>";
    } else {
        body += "<div class=\"album-crumbs\"><a href=\"/album?path=/\">Album</a>";
        std::vector<std::string> crumb_parts;
        size_t start_pos = 1;
        while (start_pos < req_path.size()) {
            const auto slash = req_path.find('/', start_pos);
            const auto part = req_path.substr(start_pos, slash == std::string::npos ? std::string::npos : slash - start_pos);
            if (!part.empty()) {
                crumb_parts.push_back(part);
            }
            if (slash == std::string::npos) break;
            start_pos = slash + 1;
        }

        std::string current_accum;
        for (size_t i = 0; i < crumb_parts.size(); i++) {
            current_accum += "/" + crumb_parts[i];
            if (i == crumb_parts.size() - 1) {
                body += " / <span>" + HtmlEscape(crumb_parts[i]) + "</span>";
            } else {
                body += " / <a href=\"/album?path=" + UrlEncode(current_accum) + "\">" + HtmlEscape(crumb_parts[i]) + "</a>";
            }
        }
        body += "</div>";

        int depth = 0;
        if (req_path != "/" && !req_path.empty()) {
            depth = std::count(req_path.begin(), req_path.end(), '/');
        }

        if (depth < 3) {
            std::string parent_disk_path = "/Nintendo/Album" + (req_path == "/" ? "" : req_path);
            std::vector<std::string> folders;
            ScanFolders(parent_disk_path, folders);

            body += "<main class=\"folder-grid\">";
            if (folders.empty()) {
                body += "<div class=\"empty\">No folders found</div>";
            } else {
                for (const auto& folder : folders) {
                    std::string child_path = (req_path == "/" ? "" : req_path) + "/" + folder;
                    body += "<a href=\"/album?path=" + UrlEncode(child_path) + "\" class=\"folder-card\">";
                    body += "<div class=\"folder-icon\">";
                    body += "<svg viewBox=\"0 0 24 24\" width=\"100%\" height=\"100%\" fill=\"currentColor\"><path d=\"M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg>";
                    body += "</div>";
                    body += "<div class=\"folder-name\">" + HtmlEscape(folder) + "</div>";
                    body += "</a>";
                }
            }
            body += "</main>";
        } else {
            std::string disk_path = "/Nintendo/Album" + req_path;
            std::vector<ScreenshotEntry> entries;
            ScanFolderFiles(disk_path, entries);

            body += "<main class=\"grid\">";
            if (entries.empty()) {
                body += "<div class=\"empty\">No screenshots found for this day</div>";
            } else {
                for (const auto& entry : entries) {
                    const auto encoded_path = UrlEncode(entry.path);
                    const auto escaped_game = HtmlEscape(entry.game_name);
                    const auto escaped_time = HtmlEscape(entry.timestamp);
                    const auto escaped_filename = HtmlEscape(entry.filename);

                    body += "<div class=\"card\">";
                    if (entry.is_video) {
                        body += "<video controls preload=\"none\" src=\"/view?path=" + encoded_path + "\"></video>";
                    } else {
                        body += "<a href=\"/view?path=" + encoded_path + "\"><img loading=\"lazy\" src=\"/view?path=" + encoded_path + "\" alt=\"" + escaped_game + "\"></a>";
                    }
                    body += "<div class=\"info\">";
                    body += "<div class=\"game-name\" title=\"" + escaped_game + "\">" + escaped_game + "</div>";
                    body += "<div class=\"timestamp\">" + escaped_time + "</div>";

                    char size_buf[32]{};
                    if (entry.file_size >= 1024 * 1024) {
                        std::snprintf(size_buf, sizeof(size_buf), "%.2f MB", static_cast<double>(entry.file_size) / (1024.0 * 1024.0));
                    } else {
                        std::snprintf(size_buf, sizeof(size_buf), "%.1f KB", static_cast<double>(entry.file_size) / 1024.0);
                    }
                    body += "<div class=\"size\">" + std::string(size_buf) + " (" + escaped_filename + ")</div>";
                    body += "<div class=\"actions\">";
                    body += "<button class=\"btn del-btn\" onclick=\"deleteItem(event,'" + encoded_path + "')\">Delete</button>";
                    body += "</div></div></div>";
                }
            }
            body += "</main>";
        }
    }

    body += "<script>";
    body += CONFIRM_MODAL_JS;
    body += "document.addEventListener('keydown',function(e){";
    body += "const m=document.getElementById('confirm-modal');";
    body += "if(m&&m.style.display==='flex') return;";
    body += "if(e.key==='Backspace'){";
    body += "e.preventDefault();";
    body += "const urlParams=new URLSearchParams(window.location.search);";
    body += "const path=urlParams.get('path');";
    body += "if(path&&path!=='/'){";
    body += "const parts=path.split('/').filter(Boolean);";
    body += "parts.pop();";
    body += "const parentPath='/'+parts.join('/');";
    body += "window.location.href=window.location.pathname+'?path='+encodeURIComponent(parentPath);";
    body += "}";
    body += "}";
    body += "});";
    body += "async function deleteItem(e,path){";
    body += "e.preventDefault();e.stopPropagation();";
    body += "if(!await showConfirmDialog('Delete '+decodeURIComponent(path.split('/').pop())+'?')) return;";
    body += "const res=await fetch('/delete?path='+path,{method:'DELETE'});";
    body += "if(res.ok){ location.reload(); }else{ alert('Delete failed: '+await res.text()); }";
    body += "}";
    body += "</script>";

    AppendConfirmModal(body);

    AppendLightbox(body);

    body += "</body></html>";
    return body;
}

void HandleRequest(Socket sock) {
    std::string req;
    if (!ReadHttpRequest(sock, req)) {
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

    if (method != "GET") {
        SendResponse(sock, "405 Method Not Allowed", "text/plain", "Only GET, PUT and DELETE are supported");
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

// The advertised TCP window equals the socket buffer size, and the default
// initial buffer (64K, see SocketInitConfig in main.cpp) caps throughput at
// window/RTT — with the Switch's high wifi power-save latency that lands at
// only a few Mbit/s. Request bigger buffers, falling back if the bsd service
// rejects the size (applet mode has a much lower tcp_*_buf_max_size).
void TuneShareSocket(Socket sock) {
    for (int size = 1024 * 1024; size >= 1024 * 128; size /= 2) {
        if (!setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size))) {
            break;
        }
    }

    for (int size = 1024 * 1024; size >= 1024 * 128; size /= 2) {
        if (!setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size))) {
            break;
        }
    }

    const int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
}

// Multiple instances of this run concurrently (see SHARE_WORKER_COUNT), each
// accept()-ing on the shared listening socket, so one client's long-running
// request (e.g. install) doesn't block other clients (e.g. a status poll).
void ShareThreadFunc(void*) {
    while (g_share_running) {
        sockaddr_in remote{};
        socklen_t remote_len = sizeof(remote);
        const auto client = accept(g_share_socket, reinterpret_cast<sockaddr*>(&remote), &remote_len);
        if (client < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                svcSleepThread(5'000'000);
                continue;
            }

            svcSleepThread(50'000'000);
            continue;
        }

        fcntl(client, F_SETFL, fcntl(client, F_GETFL) | O_NONBLOCK);
        TuneShareSocket(client);
        HandleRequest(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}

auto StartShareServer() -> Result {
    if (g_share_running) {
        R_SUCCEED();
    }

    for (u16 port = SHARE_PORT_FIRST; port <= SHARE_PORT_LAST; port++) {
        const auto sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            continue;
        }

        const int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // set before listen() so the window scale factor negotiated during the
        // handshake accounts for the enlarged buffer (inherited by accept()).
        TuneShareSocket(sock);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            continue;
        }

        if (listen(sock, 4) < 0) {
            close(sock);
            continue;
        }

        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
        g_share_socket = sock;
        g_share_port = port;
        g_share_running = true;

        size_t started = 0;
        for (; started < SHARE_WORKER_COUNT; started++) {
            Result rc = utils::CreateThread(&g_share_threads[started], ShareThreadFunc, nullptr, 1024 * 128, PRIO_PREEMPTIVE);
            if (R_SUCCEEDED(rc)) {
                rc = threadStart(&g_share_threads[started]);
                if (R_FAILED(rc)) {
                    threadClose(&g_share_threads[started]);
                }
            }
            if (R_FAILED(rc)) {
                break;
            }
        }

        if (!started) {
            g_share_running = false;
            close(g_share_socket);
            g_share_socket = -1;
            return Result_FsUnknownStdioError;
        }

        g_share_thread_count = started;
        R_SUCCEED();
    }

    R_THROW(Result_FsUnknownStdioError);
}



auto CreateQrImage(const std::string& url) -> int {
    const auto qr = QrCode::Encode(url);
    constexpr int border = 4;
    constexpr int scale = 8;
    constexpr int qr_size = QrCode::SIZE + border * 2;
    constexpr int image_size = qr_size * scale;

    std::vector<u8> rgba(image_size * image_size * 4);
    for (int y = 0; y < image_size; y++) {
        for (int x = 0; x < image_size; x++) {
            const auto module_x = x / scale - border;
            const auto module_y = y / scale - border;
            const auto dark = module_x >= 0 && module_y >= 0 && module_x < QrCode::SIZE && module_y < QrCode::SIZE && qr.Get(module_x, module_y);
            const auto off = (y * image_size + x) * 4;
            const u8 value = dark ? 0 : 255;
            rgba[off + 0] = value;
            rgba[off + 1] = value;
            rgba[off + 2] = value;
            rgba[off + 3] = 255;
        }
    }

    return nvgCreateImageRGBA(App::GetVg(), image_size, image_size, 0, rgba.data());
}

} // namespace

auto WebShow(const std::string& url) -> Result {
    WebCommonConfig config{};
    WebCommonReply reply{};
    WebExitReason reason{};
    AccountUid account_uid{};
    char last_url[FS_MAX_PATH]{};
    size_t last_url_len{};

    // WebBackgroundKind_Unknown1 = shows background
    // WebBackgroundKind_Unknown2 = shows background faded

    if (R_FAILED(accountGetPreselectedUser(&account_uid))) {
        log_write("failed: accountGetPreselectedUser\n");
        if (R_FAILED(accountTrySelectUserWithoutInteraction(&account_uid, false))) {
            log_write("failed: accountTrySelectUserWithoutInteraction\n");
            if (R_FAILED(accountGetLastOpenedUser(&account_uid))) {
                log_write("failed: accountGetLastOpenedUser\n");
            }
        }
    }

    if (R_FAILED(webPageCreate(&config, url.c_str()))) { log_write("failed: webPageCreate\n"); }
    if (R_FAILED(webConfigSetWhitelist(&config, ".*"))) { log_write("failed: webConfigSetWhitelist\n"); }
    if (R_FAILED(webConfigSetEcClientCert(&config, true))) { log_write("failed: webConfigSetEcClientCert\n"); }
    if (R_FAILED(webConfigSetScreenShot(&config, true))) { log_write("failed: webConfigSetScreenShot\n"); }
    if (R_FAILED(webConfigSetBootDisplayKind(&config, WebBootDisplayKind_Black))) { log_write("failed: webConfigSetBootDisplayKind\n"); }
    if (R_FAILED(webConfigSetBackgroundKind(&config, WebBackgroundKind_Default))) { log_write("failed: webConfigSetBackgroundKind\n"); }
    if (R_FAILED(webConfigSetPointer(&config, true))) { log_write("failed: webConfigSetPointer\n"); }
    if (R_FAILED(webConfigSetLeftStickMode(&config, WebLeftStickMode_Pointer))) { log_write("failed: webConfigSetLeftStickMode\n"); }
    if (R_FAILED(webConfigSetBootAsMediaPlayer(&config, false))) { log_write("failed: webConfigSetBootAsMediaPlayer\n"); }
    if (R_FAILED(webConfigSetJsExtension(&config, true))) { log_write("failed: webConfigSetJsExtension\n"); }
    if (R_FAILED(webConfigSetMediaPlayerAutoClose(&config, false))) { log_write("failed: webConfigSetMediaPlayerAutoClose\n"); }
    if (R_FAILED(webConfigSetPageCache(&config, true))) { log_write("failed: webConfigSetPageCache\n"); }
    if (R_FAILED(webConfigSetFooterFixedKind(&config, WebFooterFixedKind_Default))) { log_write("failed: webConfigSetFooterFixedKind\n"); }
    if (R_FAILED(webConfigSetPageFade(&config, true))) { log_write("failed: webConfigSetPageFade\n"); }
    if (R_FAILED(webConfigSetPageScrollIndicator(&config, true))) { log_write("failed: webConfigSetPageScrollIndicator\n"); }
    // if (R_FAILED(webConfigSetMediaPlayerSpeedControl(&config, true))) { log_write("failed: webConfigSetMediaPlayerSpeedControl\n"); }
    if (R_FAILED(webConfigSetBootMode(&config, WebSessionBootMode_AllForeground))) { log_write("failed: webConfigSetBootMode\n"); }
    if (R_FAILED(webConfigSetTransferMemory(&config, true))) { log_write("failed: webConfigSetTransferMemory\n"); }
    if (R_FAILED(webConfigSetTouchEnabledOnContents(&config, true))) { log_write("failed: webConfigSetTouchEnabledOnContents\n"); }
    // if (R_FAILED(webConfigSetMediaPlayerUi(&config, true))) { log_write("failed: webConfigSetMediaPlayerUi\n"); }
    if (R_FAILED(webConfigSetWebAudio(&config, false))) { log_write("failed: webConfigSetWebAudio\n"); }
    if (R_FAILED(webConfigSetPageCache(&config, true))) { log_write("failed: webConfigSetPageCache\n"); }
    // if (R_FAILED(webConfigSetBootLoadingIcon(&config, true))) { log_write("failed: webConfigSetBootLoadingIcon\n"); }
    if (R_FAILED(webConfigSetUid(&config, account_uid))) { log_write("failed: webConfigSetUid\n"); }

    if (R_FAILED(webConfigShow(&config, &reply))) { log_write("failed: webConfigShow\n"); }
    if (R_FAILED(webReplyGetExitReason(&reply, &reason))) { log_write("failed: webReplyGetExitReason\n"); }
    if (R_FAILED(webReplyGetLastUrl(&reply, last_url, sizeof(last_url), &last_url_len))) { log_write("failed: webReplyGetLastUrl\n"); }
    log_write("last url: %s\n", last_url);
    R_SUCCEED();
}



auto WebShareFolder(const fs::FsPath& path, WebShareResult& out) -> Result {
    R_UNLESS(!path.empty(), Result_FsEmpty);

    fs::FsNativeSd fs;
    R_UNLESS(fs.DirExists(path), Result_FsInvalidType);

    u32 ip{};
    R_TRY(nifmGetCurrentIpAddress(&ip));
    R_UNLESS(ip != 0, Result_FsNotActive);

    {
        std::scoped_lock lock{g_share_mutex};
        g_share_folder_root = path;
    }

    if (const auto rc = StartShareServer(); R_FAILED(rc)) {
        std::scoped_lock lock{g_share_mutex};
        g_share_folder_root = {};
        R_THROW(rc);
    }

    if (!g_title_initialized.exchange(true)) {
        title::Init();
    }

    char url[128]{};
    std::snprintf(url, sizeof(url), "http://%u.%u.%u.%u:%u",
        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, g_share_port);

    out.url = url;
    out.qr_image = CreateQrImage(out.url);

    R_SUCCEED();
}

void WebShareStop() {
    const auto was_running = g_share_running.exchange(false);

    if (was_running) {
        if (g_share_socket >= 0) {
            shutdown(g_share_socket, SHUT_RDWR);
            close(g_share_socket);
            g_share_socket = -1;
        }

        for (size_t i = 0; i < g_share_thread_count; i++) {
            threadWaitForExit(&g_share_threads[i]);
            threadClose(&g_share_threads[i]);
        }
        g_share_thread_count = 0;
        g_share_port = 0;
    }

    if (g_title_initialized.exchange(false)) {
        title::Exit();
    }

    std::scoped_lock lock{g_share_mutex};
    g_share_folder_root = {};
}

WebUploadState WebGetUploadState() {
    WebUploadState out;
    out.active = g_upload_state.active.load();
    out.bytes = g_upload_state.bytes.load();
    out.total = g_upload_state.total.load();
    std::scoped_lock lock{g_upload_state.name_mutex};
    out.name = g_upload_state.name;
    return out;
}

static std::atomic<ui::ProgressBox*> g_web_pbox = nullptr;

void WebSetProgressBox(ui::ProgressBox* pbox) {
    g_web_pbox.store(pbox);
}

ui::ProgressBox* WebGetProgressBox() {
    return g_web_pbox.load();
}

void WebPushServerProgressBox(const std::string& url, int qr_image, const std::string& title) {
    App::Push<ui::ProgressBox>(qr_image, title, url,
        [url](ui::ProgressBox* pbox) -> Result {
            pbox->NewTransferForce("Press B to Stop Server"_i18n);
            WebSetProgressBox(pbox);
            ON_SCOPE_EXIT(WebSetProgressBox(nullptr));
            std::string last_name;
            while (!pbox->ShouldExit() && WebShareIsRunning()) {
                const auto state = WebGetUploadState();
                if (state.active) {
                    if (state.name != last_name) {
                        last_name = state.name;
                        pbox->NewTransferForce(state.name);
                    }
                    pbox->UpdateTransferForce(state.bytes, state.total);
                } else if (!last_name.empty()) {
                    const std::string completed_name = last_name;
                    last_name.clear();
                    pbox->ResetTransferProgress();
                    pbox->SetTitle(url);
                    if (completed_name.starts_with("Installing:")) {
                        pbox->NewTransferForce("Installation completed"_i18n);
                    } else {
                        pbox->NewTransferForce("Upload completed"_i18n);
                    }
                    for (int i = 0; i < 30 && !WebGetUploadState().active && !pbox->ShouldExit(); ++i) {
                        svcSleepThread(100'000'000LL);
                    }
                    if (!WebGetUploadState().active && !pbox->ShouldExit()) {
                        pbox->NewTransferForce("Press B to Stop Server"_i18n);
                    }
                }
                svcSleepThread(100'000'000LL);
            }
            WebShareStop();
            R_SUCCEED();
        },
        [qr_image](Result) {
            nvgDeleteImage(App::GetVg(), qr_image);
        }
    );
}

bool WebShareIsRunning() {
    return g_share_running.load();
}

} // namespace sphaira
