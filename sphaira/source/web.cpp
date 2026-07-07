#include "web.hpp"
#include "log.hpp"
#include "app.hpp"
#include "defines.hpp"
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

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

namespace sphaira {
namespace {

using Socket = int;

constexpr u16 SHARE_PORT_FIRST = 8080;
constexpr u16 SHARE_PORT_LAST = 8090;
constexpr size_t HTTP_READ_LIMIT = 4096;
constexpr size_t HTTP_FILE_CHUNK = 1024 * 32;

enum class ShareMode {
    None,
    Images,
    Folder,
};

std::mutex g_share_mutex{};
std::vector<WebShareEntry> g_share_entries{};
fs::FsPath g_share_folder_root{};
ShareMode g_share_mode{ShareMode::None};
Thread g_share_thread{};
std::atomic_bool g_share_running{false};
Socket g_share_socket{-1};
u16 g_share_port{};

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

auto SanitizeRelativePath(std::string path) -> std::string {
    for (auto& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }

    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }

    std::string out;
    size_t start{};
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        auto part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty() && part != "." && part != ".." && part.find(':') == std::string::npos) {
            if (!out.empty()) {
                out += '/';
            }
            out += part;
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
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

auto JoinSharePath(const fs::FsPath& root, const std::string& rel) -> fs::FsPath {
    if (rel.empty()) {
        return root;
    }

    return fs::AppendPath(root, rel);
}

auto GetShareFolderRoot() -> fs::FsPath {
    std::scoped_lock lock{g_share_mutex};
    return g_share_folder_root;
}

auto GetShareMode() -> ShareMode {
    std::scoped_lock lock{g_share_mutex};
    return g_share_mode;
}

auto SendAll(Socket sock, const void* buf, size_t size) -> bool {
    auto data = static_cast<const char*>(buf);

    while (size) {
        const auto sent = send(sock, data, size, 0);
        if (sent < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                svcSleepThread(1'000'000);
                continue;
            }

            return false;
        }

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

auto GetSharedEntries() -> std::vector<WebShareEntry> {
    std::scoped_lock lock{g_share_mutex};
    return g_share_entries;
}

auto BuildImagesPage() -> std::string {
    const auto entries = GetSharedEntries();
    std::string body;
    body.reserve(4096 + entries.size() * 256);

    body += "<!doctype html><html><head><meta charset=\"utf-8\">";
    body += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    body += "<title>Sphaira Images</title>";
    body += "<style>";
    body += "body{margin:0;font:16px system-ui,-apple-system,Segoe UI,sans-serif;background:#111;color:#f7f7f7}";
    body += "header{position:sticky;top:0;background:#181818;padding:16px 18px;border-bottom:1px solid #333}";
    body += "h1{font-size:20px;margin:0 0 4px}p{margin:0;color:#bbb}";
    body += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px;padding:12px}";
    body += "a{display:block;color:inherit;text-decoration:none;background:#1c1c1c;border:1px solid #333;border-radius:8px;overflow:hidden}";
    body += "img{display:block;width:100%;aspect-ratio:1/1;object-fit:contain;background:#050505}";
    body += "span{display:block;padding:10px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#ddd}";
    body += "</style></head><body>";
    body += "<header><h1>Sphaira Images</h1><p>";
    body += std::to_string(entries.size());
    body += entries.size() == 1 ? " image" : " images";
    body += "</p></header><main class=\"grid\">";

    for (size_t i = 0; i < entries.size(); i++) {
        const auto name = HtmlEscape(entries[i].name);
        body += "<a href=\"/image/";
        body += std::to_string(i);
        body += "\" target=\"_blank\"><img loading=\"lazy\" src=\"/image/";
        body += std::to_string(i);
        body += "\" alt=\"";
        body += name;
        body += "\"><span>";
        body += name;
        body += "</span></a>";
    }

    body += "</main></body></html>";
    return body;
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

auto BuildFolderPage(std::string rel) -> std::string {
    rel = SanitizeRelativePath(std::move(rel));
    const auto root = GetShareFolderRoot();
    const auto dir_path = JoinSharePath(root, rel);

    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(dir_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
        dir.ReadAll(entries);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs){
        if (lhs.type != rhs.type) {
            return lhs.type == FsDirEntryType_Dir;
        }
        return strcasecmp(lhs.name, rhs.name) < 0;
    });

    bool has_images = false;
    for (const auto& entry : entries) {
        if (entry.type == FsDirEntryType_File && IsImagePath(entry.name)) {
            has_images = true;
            break;
        }
    }

    const auto encoded_rel = UrlEncode(rel);
    std::string body;
    body.reserve(8192 + entries.size() * 256);

    body += "<!doctype html><html><head><meta charset=\"utf-8\">";
    body += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    body += "<title>Sphaira Files</title>";
    body += "<style>";
    body += "body{margin:0;font:16px system-ui,-apple-system,Segoe UI,sans-serif;background:#101114;color:#f7f7f7}";
    body += "header{position:sticky;top:0;background:#17191d;padding:16px 18px;border-bottom:1px solid #333;z-index:1}";
    body += "h1{font-size:20px;margin:0 0 6px}.path{color:#b9c2cc;word-break:break-all}.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:14px}";
    body += "button{border:1px solid #50545c;background:#252a32;color:#fff;border-radius:6px;padding:10px 12px;font:inherit;cursor:pointer}";
    body += "input{display:none}.status{color:#9fb1c8}.list{padding:8px 0}.row{display:grid;grid-template-columns:34px 1fr auto;gap:10px;align-items:center;padding:13px 18px;border-bottom:1px solid #25272d;color:inherit;text-decoration:none}";
    body += ".row:hover{background:#191d24}.meta{color:#9aa3ad;font-size:13px}.crumbs a{color:#9fc6ff;text-decoration:none}.empty{padding:26px 18px;color:#98a1aa}";
    body += "</style></head><body><header><h1>Sphaira Files</h1><div class=\"path\">";
    body += HtmlEscape(root);
    if (!rel.empty()) {
        body += "/";
        body += HtmlEscape(rel);
    }
    body += "</div><div class=\"crumbs\"><a href=\"/files\">root</a>";

    std::string crumb_accum;
    size_t start{};
    while (start < rel.size()) {
        const auto end = rel.find('/', start);
        const auto part = rel.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            if (!crumb_accum.empty()) {
                crumb_accum += '/';
            }
            crumb_accum += part;
            body += " / <a href=\"/files?path=";
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

    body += "</div><div class=\"bar\"><button id=\"upload\" onclick=\"document.getElementById('files').click()\">Upload</button>";
    if (has_images) {
        body += "<button onclick=\"location.href='/gallery?path=" + encoded_rel + "'\">Gallery</button>";
    }
    body += "<input id=\"files\" type=\"file\" multiple onchange=\"uploadFiles(this.files)\"><span id=\"status\" class=\"status\"></span></div></header><main class=\"list\">";

    if (!rel.empty()) {
        auto parent = rel;
        if (const auto slash = parent.find_last_of('/'); slash != std::string::npos) {
            parent.resize(slash);
        } else {
            parent.clear();
        }

        body += "<a class=\"row\" href=\"/files?path=";
        body += UrlEncode(parent);
        body += "\"><span>..</span><span>..</span><span class=\"meta\">folder</span></a>";
    }

    if (entries.empty()) {
        body += "<div class=\"empty\">Empty folder</div>";
    }

    for (const auto& entry : entries) {
        const std::string name{entry.name};
        auto child = rel;
        if (!child.empty()) {
            child += '/';
        }
        child += name;

        const auto encoded_child = UrlEncode(child);
        const auto escaped_name = HtmlEscape(name);
        if (entry.type == FsDirEntryType_Dir) {
            body += "<a class=\"row\" href=\"/files?path=";
            body += encoded_child;
            body += "\"><span>[D]</span><span>";
            body += escaped_name;
            body += "</span><span class=\"meta\">folder</span></a>";
        } else {
            const bool is_image = IsImagePath(name);
            body += "<a class=\"row\" href=\"";
            body += is_image ? "/view?path=" : "/download?path=";
            body += encoded_child;
            if (is_image) {
                body += "\" target=\"_blank";
            }
            body += "\"><span>";
            body += is_image ? "[I]" : "[F]";
            body += "</span><span>";
            body += escaped_name;
            body += "</span><span class=\"meta\">";
            if (entry.file_size >= 1024 * 1024) {
                char size_buf[64]{};
                std::snprintf(size_buf, sizeof(size_buf), "%.2f MiB", static_cast<double>(entry.file_size) / 1024.0 / 1024.0);
                body += size_buf;
            } else {
                char size_buf[64]{};
                std::snprintf(size_buf, sizeof(size_buf), "%.2f KiB", static_cast<double>(entry.file_size) / 1024.0);
                body += size_buf;
            }
            body += "</span></a>";
        }
    }

    body += "</main><script>";
    body += "const currentPath='";
    body += encoded_rel;
    body += "';";
    body += "async function uploadFiles(files){const input=document.getElementById('files');const button=document.getElementById('upload');const status=document.getElementById('status');";
    body += "if(!files||!files.length){return;}";
    body += "button.disabled=true;";
    body += "for(const file of files){status.textContent='Uploading '+file.name+'...';";
    body += "const res=await fetch('/upload?path='+currentPath+'&name='+encodeURIComponent(file.name),{method:'PUT',body:file});";
    body += "if(!res.ok){button.disabled=false;status.textContent='Failed: '+await res.text();input.value='';return;}}";
    body += "status.textContent='Done';input.value='';setTimeout(()=>location.reload(),500);}";
    body += "</script></body></html>";

    return body;
}

void SendImage(Socket sock, size_t index) {
    const auto entries = GetSharedEntries();
    if (index >= entries.size()) {
        SendResponse(sock, "404 Not Found", "text/plain", "Image not found");
        return;
    }

    fs::FsNativeSd fs;
    fs::File file;
    if (R_FAILED(fs.OpenFile(entries[index].path, FsOpenMode_Read, &file))) {
        SendResponse(sock, "404 Not Found", "text/plain", "Could not open image");
        return;
    }

    s64 size{};
    if (R_FAILED(file.GetSize(&size))) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not read image size");
        return;
    }

    char header[512]{};
    std::snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zd\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        ContentTypeForPath(entries[index].path), size);

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

void SendDownload(Socket sock, const std::string& rel) {
    const auto root = GetShareFolderRoot();
    const auto clean_rel = SanitizeRelativePath(rel);
    if (clean_rel.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing file path");
        return;
    }

    const auto path = JoinSharePath(root, clean_rel);
    auto name = clean_rel;
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
    const auto root = GetShareFolderRoot();
    const auto clean_rel = SanitizeRelativePath(rel);
    if (clean_rel.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing file path");
        return;
    }

    const auto path = JoinSharePath(root, clean_rel);
    auto name = clean_rel;
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

auto BuildGalleryPage(std::string rel) -> std::string {
    rel = SanitizeRelativePath(std::move(rel));
    const auto root = GetShareFolderRoot();
    const auto dir_path = JoinSharePath(root, rel);

    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(dir_path, FsDirOpenMode_ReadFiles, &dir))) {
        dir.ReadAll(entries);
    }

    std::vector<FsDirectoryEntry> images;
    for (const auto& entry : entries) {
        if (entry.type == FsDirEntryType_File && IsImagePath(entry.name)) {
            images.push_back(entry);
        }
    }

    std::sort(images.begin(), images.end(), [](const auto& lhs, const auto& rhs){
        return strcasecmp(lhs.name, rhs.name) < 0;
    });

    const auto encoded_rel = UrlEncode(rel);
    std::string body;
    body.reserve(4096 + images.size() * 256);

    body += "<!doctype html><html><head><meta charset=\"utf-8\">";
    body += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    body += "<title>Sphaira Gallery</title>";
    body += "<style>";
    body += "body{margin:0;font:16px system-ui,-apple-system,Segoe UI,sans-serif;background:#101114;color:#f7f7f7}";
    body += "header{position:sticky;top:0;background:#17191d;padding:16px 18px;border-bottom:1px solid #333;z-index:1}";
    body += "h1{font-size:20px;margin:0 0 6px}.path{color:#b9c2cc;word-break:break-all}.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:14px}";
    body += "button{border:1px solid #50545c;background:#252a32;color:#fff;border-radius:6px;padding:10px 12px;font:inherit;cursor:pointer}";
    body += ".crumbs a{color:#9fc6ff;text-decoration:none}";
    body += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px;padding:12px}";
    body += "a.card{display:block;color:inherit;text-decoration:none;background:#1c1c1c;border:1px solid #333;border-radius:8px;overflow:hidden}";
    body += "img{display:block;width:100%;aspect-ratio:1/1;object-fit:contain;background:#050505}";
    body += "span{display:block;padding:10px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#ddd}";
    body += ".empty{padding:26px 18px;color:#98a1aa}";
    body += "</style></head><body><header><h1>Sphaira Gallery</h1><div class=\"path\">";
    body += HtmlEscape(root);
    if (!rel.empty()) {
        body += "/";
        body += HtmlEscape(rel);
    }
    body += "</div><div class=\"crumbs\"><a href=\"/files\">root</a>";

    std::string crumb_accum;
    size_t start{};
    while (start < rel.size()) {
        const auto end = rel.find('/', start);
        const auto part = rel.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            if (!crumb_accum.empty()) {
                crumb_accum += '/';
            }
            crumb_accum += part;
            body += " / <a href=\"/gallery?path=";
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

    body += "</div><div class=\"bar\">";
    body += "<button onclick=\"location.href='/files?path=" + encoded_rel + "'\">List View</button>";
    body += "</div></header><main class=\"grid\">";

    if (images.empty()) {
        body += "<div class=\"empty\">No images found in this folder</div>";
    }

    for (const auto& entry : images) {
        const std::string name{entry.name};
        auto child = rel;
        if (!child.empty()) {
            child += '/';
        }
        child += name;

        const auto encoded_child = UrlEncode(child);
        const auto escaped_name = HtmlEscape(name);

        body += "<a class=\"card\" href=\"/view?path=";
        body += encoded_child;
        body += "\" target=\"_blank\"><img loading=\"lazy\" src=\"/view?path=";
        body += encoded_child;
        body += "\" alt=\"";
        body += escaped_name;
        body += "\"><span>";
        body += escaped_name;
        body += "</span></a>";
    }

    body += "</main></body></html>";
    return body;
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

    const auto root = GetShareFolderRoot();
    const auto rel = SanitizeRelativePath(GetQueryValue(query, "path"));
    const auto raw_name = GetQueryValue(query, "name");
    const auto name = SanitizeFileName(raw_name);
    const auto dir = JoinSharePath(root, rel);

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

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    while (offset < content_length) {
        const auto want = std::min<s64>(buf.size(), content_length - offset);
        const auto got = recv(sock, buf.data(), want, 0);
        if (got > 0) {
            if (R_FAILED(file.Write(offset, buf.data(), got, FsWriteOption_None))) {
                SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not write file");
                return;
            }
            offset += got;
        } else if (got == 0) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Upload ended early");
            return;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            svcSleepThread(1'000'000);
        } else {
            SendResponse(sock, "500 Internal Server Error", "text/plain", "Socket read failed");
            return;
        }
    }

    SendResponse(sock, "200 OK", "text/plain", "Uploaded");
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

    if (method != "GET") {
        SendResponse(sock, "405 Method Not Allowed", "text/plain", "Only GET and PUT are supported");
        return;
    }

    if (path == "/") {
        if (GetShareMode() == ShareMode::Folder) {
            SendResponse(sock, "200 OK", "text/html", BuildFolderPage({}));
        } else {
            SendResponse(sock, "200 OK", "text/html", BuildImagesPage());
        }
        return;
    }

    if (path == "/images" || path == "/images/") {
        SendResponse(sock, "200 OK", "text/html", BuildImagesPage());
        return;
    }

    if (path == "/files" || path == "/files/") {
        SendResponse(sock, "200 OK", "text/html", BuildFolderPage(GetQueryValue(query, "path")));
        return;
    }

    if (path == "/download") {
        SendDownload(sock, GetQueryValue(query, "path"));
        return;
    }

    if (path == "/gallery" || path == "/gallery/") {
        SendResponse(sock, "200 OK", "text/html", BuildGalleryPage(GetQueryValue(query, "path")));
        return;
    }

    if (path == "/view") {
        SendView(sock, GetQueryValue(query, "path"));
        return;
    }

    constexpr std::string_view IMAGE_PREFIX{"/image/"};
    if (path.starts_with(IMAGE_PREFIX)) {
        const auto value = path.substr(IMAGE_PREFIX.size());
        char* end{};
        const auto index = std::strtoull(value.c_str(), &end, 10);
        if (end && *end == '\0') {
            SendImage(sock, index);
            return;
        }
    }

    SendResponse(sock, "404 Not Found", "text/plain", "Not found");
}

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

        Result rc = utils::CreateThread(&g_share_thread, ShareThreadFunc, nullptr, 1024 * 32, PRIO_PREEMPTIVE);
        if (R_FAILED(rc)) {
            g_share_running = false;
            close(g_share_socket);
            g_share_socket = -1;
            return rc;
        }

        rc = threadStart(&g_share_thread);
        if (R_FAILED(rc)) {
            g_share_running = false;
            threadClose(&g_share_thread);
            close(g_share_socket);
            g_share_socket = -1;
            return rc;
        }

        R_SUCCEED();
    }

    R_THROW(Result_FsUnknownStdioError);
}

class QrCode final {
public:
    static constexpr int VERSION = 4;
    static constexpr int SIZE = VERSION * 4 + 17;
    static constexpr int DATA_CODEWORDS = 80;
    static constexpr int ECC_CODEWORDS = 20;

    static auto Encode(std::string_view text) -> QrCode {
        QrCode qr;
        qr.DrawFunctionPatterns();
        const auto data = MakeDataCodewords(text);
        if (!data.empty()) {
            qr.DrawCodewords(AddEcc(data));
        }
        qr.DrawFormatBits();
        return qr;
    }

    auto Get(int x, int y) const -> bool {
        return m_modules[y * SIZE + x];
    }

private:
    static auto GfMultiply(u8 x, u8 y) -> u8 {
        if (!x || !y) {
            return 0;
        }

        const auto& tables = GetGfTables();
        return tables.exp[tables.log[x] + tables.log[y]];
    }

    struct GfTables {
        std::array<u8, 512> exp{};
        std::array<u8, 256> log{};
    };

    static auto GetGfTables() -> const GfTables& {
        static const auto tables = []{
            GfTables out{};
            int x = 1;
            for (int i = 0; i < 255; i++) {
                out.exp[i] = static_cast<u8>(x);
                out.log[x] = static_cast<u8>(i);
                x <<= 1;
                if (x & 0x100) {
                    x ^= 0x11D;
                }
            }
            for (int i = 255; i < 512; i++) {
                out.exp[i] = out.exp[i - 255];
            }
            return out;
        }();

        return tables;
    }

    static auto MakeDataCodewords(std::string_view text) -> std::vector<u8> {
        if (text.size() > 78) {
            return {};
        }

        std::vector<bool> bits;
        bits.reserve(DATA_CODEWORDS * 8);

        const auto append_bits = [&bits](u32 value, int count) {
            for (int i = count - 1; i >= 0; i--) {
                bits.push_back(((value >> i) & 1) != 0);
            }
        };

        append_bits(0x4, 4);
        append_bits(text.size(), 8);
        for (const auto c : text) {
            append_bits(static_cast<unsigned char>(c), 8);
        }

        const auto capacity = DATA_CODEWORDS * 8;
        const auto terminator = std::min<size_t>(4, capacity - bits.size());
        for (size_t i = 0; i < terminator; i++) {
            bits.push_back(false);
        }
        while (bits.size() % 8) {
            bits.push_back(false);
        }

        std::vector<u8> data;
        data.reserve(DATA_CODEWORDS);
        for (size_t i = 0; i < bits.size(); i += 8) {
            u8 value{};
            for (int j = 0; j < 8; j++) {
                value = static_cast<u8>((value << 1) | (bits[i + j] ? 1 : 0));
            }
            data.push_back(value);
        }

        for (u8 pad = 0xEC; data.size() < DATA_CODEWORDS; pad ^= 0xEC ^ 0x11) {
            data.push_back(pad);
        }

        return data;
    }

    static auto ReedSolomonDivisor() -> std::array<u8, ECC_CODEWORDS> {
        std::array<u8, ECC_CODEWORDS> result{};
        result[ECC_CODEWORDS - 1] = 1;

        u8 root = 1;
        for (int i = 0; i < ECC_CODEWORDS; i++) {
            for (int j = 0; j < ECC_CODEWORDS; j++) {
                result[j] = GfMultiply(result[j], root);
                if (j + 1 < ECC_CODEWORDS) {
                    result[j] ^= result[j + 1];
                }
            }
            root = GfMultiply(root, 0x02);
        }

        return result;
    }

    static auto AddEcc(const std::vector<u8>& data) -> std::vector<u8> {
        const auto divisor = ReedSolomonDivisor();
        std::array<u8, ECC_CODEWORDS> remainder{};

        for (const auto b : data) {
            const auto factor = static_cast<u8>(b ^ remainder[0]);
            std::move(remainder.begin() + 1, remainder.end(), remainder.begin());
            remainder.back() = 0;
            for (int i = 0; i < ECC_CODEWORDS; i++) {
                remainder[i] ^= GfMultiply(divisor[i], factor);
            }
        }

        auto out = data;
        out.insert(out.end(), remainder.begin(), remainder.end());
        return out;
    }

    void SetFunction(int x, int y, bool dark) {
        if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) {
            return;
        }

        m_modules[y * SIZE + x] = dark;
        m_is_function[y * SIZE + x] = true;
    }

    void SetModule(int x, int y, bool dark) {
        m_modules[y * SIZE + x] = dark;
    }

    auto IsFunction(int x, int y) const -> bool {
        return m_is_function[y * SIZE + x];
    }

    void DrawFinder(int x, int y) {
        for (int dy = -1; dy <= 7; dy++) {
            for (int dx = -1; dx <= 7; dx++) {
                const auto xx = x + dx;
                const auto yy = y + dy;
                const auto dist = std::max(std::abs(dx - 3), std::abs(dy - 3));
                SetFunction(xx, yy, dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 && dist != 2);
            }
        }
    }

    void DrawAlignment(int x, int y) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                SetFunction(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        }
    }

    void ReserveFormatBits() {
        for (int i = 0; i <= 8; i++) {
            if (i != 6) {
                SetFunction(8, i, false);
                SetFunction(i, 8, false);
            }
        }

        for (int i = 0; i < 8; i++) {
            SetFunction(SIZE - 1 - i, 8, false);
        }
        for (int i = 0; i < 7; i++) {
            SetFunction(8, SIZE - 1 - i, false);
        }

        SetFunction(8, SIZE - 8, true);
    }

    void DrawFunctionPatterns() {
        DrawFinder(0, 0);
        DrawFinder(SIZE - 7, 0);
        DrawFinder(0, SIZE - 7);
        DrawAlignment(26, 26);

        for (int i = 0; i < SIZE; i++) {
            if (!IsFunction(i, 6)) {
                SetFunction(i, 6, i % 2 == 0);
            }
            if (!IsFunction(6, i)) {
                SetFunction(6, i, i % 2 == 0);
            }
        }

        ReserveFormatBits();
    }

    static auto Mask(int x, int y) -> bool {
        return ((x + y) & 1) == 0;
    }

    void DrawCodewords(const std::vector<u8>& codewords) {
        size_t bit_index{};
        bool upward = true;

        for (int right = SIZE - 1; right >= 1; right -= 2) {
            if (right == 6) {
                right--;
            }

            for (int vert = 0; vert < SIZE; vert++) {
                const auto y = upward ? SIZE - 1 - vert : vert;
                for (int j = 0; j < 2; j++) {
                    const auto x = right - j;
                    if (IsFunction(x, y)) {
                        continue;
                    }

                    bool dark{};
                    if (bit_index < codewords.size() * 8) {
                        dark = ((codewords[bit_index >> 3] >> (7 - (bit_index & 7))) & 1) != 0;
                        bit_index++;
                    }
                    if (Mask(x, y)) {
                        dark = !dark;
                    }
                    SetModule(x, y, dark);
                }
            }

            upward = !upward;
        }
    }

    static auto FormatBits() -> int {
        const int data = (0x1 << 3) | 0x0; // Error level L, mask 0.
        int rem = data << 10;
        for (int i = 14; i >= 10; i--) {
            if ((rem >> i) & 1) {
                rem ^= 0x537 << (i - 10);
            }
        }

        return ((data << 10) | rem) ^ 0x5412;
    }

    void DrawFormatBits() {
        const auto bits = FormatBits();
        const auto bit = [bits](int i) {
            return ((bits >> i) & 1) != 0;
        };

        for (int i = 0; i <= 5; i++) {
            SetFunction(8, i, bit(i));
        }
        SetFunction(8, 7, bit(6));
        SetFunction(8, 8, bit(7));
        SetFunction(7, 8, bit(8));
        for (int i = 9; i < 15; i++) {
            SetFunction(14 - i, 8, bit(i));
        }

        for (int i = 0; i < 8; i++) {
            SetFunction(SIZE - 1 - i, 8, bit(i));
        }
        for (int i = 8; i < 15; i++) {
            SetFunction(8, SIZE - 15 + i, bit(i));
        }

        SetFunction(8, SIZE - 8, true);
    }

private:
    std::array<bool, SIZE * SIZE> m_modules{};
    std::array<bool, SIZE * SIZE> m_is_function{};
};

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

auto WebShareImages(const std::vector<WebShareEntry>& entries, WebShareResult& out) -> Result {
    R_UNLESS(!entries.empty(), Result_FsEmpty);

    u32 ip{};
    R_TRY(nifmGetCurrentIpAddress(&ip));
    R_UNLESS(ip != 0, Result_FsNotActive);

    {
        std::scoped_lock lock{g_share_mutex};
        g_share_entries = entries;
        g_share_folder_root = {};
        g_share_mode = ShareMode::Images;
    }

    if (const auto rc = StartShareServer(); R_FAILED(rc)) {
        std::scoped_lock lock{g_share_mutex};
        g_share_entries.clear();
        g_share_folder_root = {};
        g_share_mode = ShareMode::None;
        R_THROW(rc);
    }

    char url[128]{};
    std::snprintf(url, sizeof(url), "http://%u.%u.%u.%u:%u/images",
        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, g_share_port);

    out.url = url;
    out.qr_image = CreateQrImage(out.url);

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
        g_share_entries.clear();
        g_share_folder_root = path;
        g_share_mode = ShareMode::Folder;
    }

    if (const auto rc = StartShareServer(); R_FAILED(rc)) {
        std::scoped_lock lock{g_share_mutex};
        g_share_entries.clear();
        g_share_folder_root = {};
        g_share_mode = ShareMode::None;
        R_THROW(rc);
    }

    char url[128]{};
    std::snprintf(url, sizeof(url), "http://%u.%u.%u.%u:%u/files",
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

        threadWaitForExit(&g_share_thread);
        threadClose(&g_share_thread);
        g_share_port = 0;
    }

    std::scoped_lock lock{g_share_mutex};
    g_share_entries.clear();
    g_share_folder_root = {};
    g_share_mode = ShareMode::None;
}

} // namespace sphaira
