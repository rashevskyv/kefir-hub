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
#include <memory>

#include "ui/progress_box.hpp"
#include "yati/yati.hpp"
#include "yati/source/stream.hpp"

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

struct UploadState {
    std::atomic<bool> active{false};
    std::atomic<s64> bytes{0};
    std::atomic<s64> total{0};
    std::mutex name_mutex{};
    std::string name{};
};
static UploadState g_upload_state;

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
    u32 idle_count = 0;

    while (size) {
        const auto sent = send(sock, data, size, 0);
        if (sent < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                idle_count++;
                if (idle_count > 30000) { // 30 seconds
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

void AppendLightbox(std::string& body) {
    body += "<style>";
    body += ".lightbox{display:none;position:fixed;z-index:1000;top:0;left:0;width:100%;height:100%;background-color:rgba(0,0,0,0.95);align-items:center;justify-content:center;user-select:none}";
    body += ".lightbox-content{position:relative;max-width:90%;max-height:85%;display:flex;flex-direction:column;align-items:center}";
    body += ".lightbox-img{max-width:100%;max-height:80vh;object-fit:contain;border-radius:4px;box-shadow:0 4px 20px rgba(0,0,0,0.5)}";
    body += ".lightbox-caption{margin-top:12px;color:#eee;font-size:15px;text-align:center;word-break:break-all;max-width:600px}";
    body += ".lightbox-close{position:absolute;top:20px;right:25px;color:#bbb;font-size:40px;font-weight:bold;cursor:pointer;transition:color 0.2s;line-height:1}";
    body += ".lightbox-close:hover{color:#fff}";
    body += ".lightbox-btn{position:absolute;top:50%;transform:translateY(-50%);background:rgba(40,45,50,0.5);border:1px solid rgba(255,255,255,0.1);color:#fff;font-size:24px;width:50px;height:50px;border-radius:50%;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:background 0.2s,color 0.2s}";
    body += ".lightbox-btn:hover{background:rgba(60,65,70,0.8)}";
    body += ".lightbox-prev{left:20px}";
    body += ".lightbox-next{right:20px}";
    body += "</style>";

    body += "<div id=\"lightbox\" class=\"lightbox\">";
    body += "<span class=\"lightbox-close\" onclick=\"closeLightbox()\">&times;</span>";
    body += "<button class=\"lightbox-btn lightbox-prev\" onclick=\"prevImage(event)\">&lt;</button>";
    body += "<button class=\"lightbox-btn lightbox-next\" onclick=\"nextImage(event)\">&gt;</button>";
    body += "<div class=\"lightbox-content\">";
    body += "<img id=\"lightbox-img\" class=\"lightbox-img\" src=\"\" alt=\"\">";
    body += "<div id=\"lightbox-caption\" class=\"lightbox-caption\"></div>";
    body += "</div></div>";

    body += "<script>";
    body += "let imageList=[];let currentImageIndex=-1;";
    body += "function initLightbox(){";
    body += "imageList=[];";
    body += "const links=document.querySelectorAll('a');";
    body += "for(const link of links){";
    body += "const href=link.getAttribute('href');";
    body += "if(href&&href.includes('/view?path=')){";
    body += "let name='';const span=link.querySelector('span:nth-child(2)')||link.querySelector('span');";
    body += "if(span){name=span.textContent;}";
    body += "const idx=imageList.length;imageList.push({href:href,name:name});";
    body += "link.addEventListener('click',function(e){";
    body += "e.preventDefault();openLightbox(idx);";
    body += "});";
    body += "}";
    body += "}";
    body += "document.addEventListener('keydown',function(e){";
    body += "const modal=document.getElementById('lightbox');";
    body += "if(modal&&modal.style.display==='flex'){";
    body += "if(e.key==='ArrowLeft')prevImage();";
    body += "else if(e.key==='ArrowRight')nextImage();";
    body += "else if(e.key==='Escape')closeLightbox();";
    body += "}";
    body += "});";
    body += "}";
    body += "function openLightbox(index){";
    body += "if(index<0||index>=imageList.length)return;";
    body += "currentImageIndex=index;";
    body += "const modal=document.getElementById('lightbox');";
    body += "const img=document.getElementById('lightbox-img');";
    body += "const caption=document.getElementById('lightbox-caption');";
    body += "img.src=imageList[index].href;caption.textContent=imageList[index].name;";
    body += "modal.style.display='flex';";
    body += "}";
    body += "function closeLightbox(){document.getElementById('lightbox').style.display='none';}";
    body += "function prevImage(e){if(e)e.stopPropagation();if(imageList.length<=1)return;let idx=currentImageIndex-1;if(idx<0)idx=imageList.length-1;openLightbox(idx);}";
    body += "function nextImage(e){if(e)e.stopPropagation();if(imageList.length<=1)return;let idx=currentImageIndex+1;if(idx>=imageList.length)idx=0;openLightbox(idx);}";
    body += "document.addEventListener('DOMContentLoaded',initLightbox);";
    body += "initLightbox();";
    body += "</script>";
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

    body += "<!doctype html><html><head><meta charset=\"utf-8\">";
    body += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    body += "<title>Sphaira Files</title>";
    body += "<style>";
    body += "body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f0f12;color:#e2e8f0}";
    body += "header{position:sticky;top:0;background:rgba(23,25,35,0.85);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);padding:16px 24px;border-bottom:1px solid rgba(255,255,255,0.08);z-index:10}";
    body += "h1{font-size:22px;margin:0 0 4px;font-weight:600;letter-spacing:-0.5px}";
    body += ".path{color:#94a3b8;font-size:14px;word-break:break-all}";
    body += ".crumbs a{color:#38bdf8;text-decoration:none}";
    body += ".crumbs a:hover{text-decoration:underline}";
    body += ".bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:16px}";
    body += "button{border:1px solid rgba(255,255,255,0.15);background:#1e293b;color:#fff;border-radius:8px;padding:8px 16px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s}";
    body += "button:hover{background:#334155;border-color:rgba(255,255,255,0.25)}";
    body += "button:disabled{opacity:0.5;cursor:not-allowed}";
    body += "input{display:none}.status{color:#38bdf8;font-size:14px}";
    body += ".container{padding:24px;max-width:1200px;margin:0 auto}";
    
    // List layout CSS
    body += ".list{display:flex;flex-direction:column;gap:8px}";
    body += ".list .item{display:flex;align-items:center;gap:16px;padding:12px 16px;background:#1e1e24;border:1px solid rgba(255,255,255,0.05);border-radius:8px;color:inherit;text-decoration:none;transition:background 0.15s,transform 0.15s}";
    body += ".list .item:hover{background:#272730;transform:translateY(-1px)}";
    body += ".list .thumbnail-box{width:40px;height:40px;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,0.2);border-radius:6px;overflow:hidden;flex-shrink:0}";
    body += ".list .thumbnail-box img{width:100%;height:100%;object-fit:cover}";
    body += ".list .thumbnail-box svg{width:24px;height:24px}";
    body += ".list .info{display:flex;flex-grow:1;align-items:center;min-width:0}";
    body += ".list .name{font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;margin-right:16px;flex-grow:1}";
    body += ".list .meta{color:#64748b;font-size:13px;flex-shrink:0;width:120px;text-align:right;margin-right:24px}";

    // Grid layout CSS
    body += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:16px}";
    body += ".grid .item{display:flex;flex-direction:column;background:#18181f;border:1px solid rgba(255,255,255,0.05);border-radius:12px;color:inherit;text-decoration:none;overflow:hidden;transition:all 0.2s}";
    body += ".grid .item:hover{background:#202029;transform:translateY(-3px);box-shadow:0 10px 20px rgba(0,0,0,0.3);border-color:rgba(56,189,248,0.3)}";
    body += ".grid .thumbnail-box{width:100%;aspect-ratio:16/10;display:flex;align-items:center;justify-content:center;background:#0d0d11;overflow:hidden;border-bottom:1px solid rgba(255,255,255,0.03)}";
    body += ".grid .thumbnail-box img{width:100%;height:100%;object-fit:cover}";
    body += ".grid .thumbnail-box svg{width:48px;height:48px}";
    body += ".grid .info{padding:12px;display:flex;flex-direction:column;gap:4px;min-width:0}";
    body += ".grid .name{font-size:14px;font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}";
    body += ".grid .meta{color:#64748b;font-size:12px}";

    body += ".empty{padding:40px;text-align:center;color:#64748b;font-size:16px}";
    body += ".delete-btn{margin-left:8px;border:1px solid rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171;border-radius:6px;padding:4px 10px;font-size:12px;font-weight:500;cursor:pointer;flex-shrink:0;transition:all 0.15s}";
    body += ".delete-btn:hover{background:rgba(239,68,68,0.25);border-color:rgba(239,68,68,0.7)}";
    
    // Checkbox styling
    body += ".file-checkbox{-webkit-appearance:none;appearance:none;background:rgba(255,255,255,0.08);border:2px solid rgba(255,255,255,0.3);border-radius:4px;outline:none;cursor:pointer;transition:all 0.15s;display:block}";
    body += ".file-checkbox:hover{border-color:#38bdf8;background:rgba(56,189,248,0.08)}";
    body += ".file-checkbox:checked{background:#38bdf8;border-color:#38bdf8}";
    body += ".file-checkbox:checked::after{content:'';position:absolute;border:solid #0f0f12;border-width:0 2px 2px 0;transform:rotate(45deg)}";
    body += ".list .file-checkbox{position:relative;width:18px;height:18px;margin-right:12px;flex-shrink:0}";
    body += ".list .file-checkbox:checked::after{left:5px;top:1px;width:4px;height:9px}";
    body += ".grid .item{position:relative}";
    body += ".grid .file-checkbox{position:absolute;top:12px;left:12px;width:20px;height:20px;margin:0;z-index:5}";
    body += ".grid .file-checkbox:checked::after{left:6px;top:2px;width:4px;height:9px}";
    body += ".item.focused{outline:2px solid #38bdf8;outline-offset:-2px;box-shadow:0 0 12px rgba(56,189,248,0.25)}";
    body += ".queue-panel{position:fixed;top:0;right:-380px;width:340px;height:100%;background:rgba(20,20,25,0.95);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);border-left:1px solid rgba(255,255,255,0.08);box-shadow:-10px 0 30px rgba(0,0,0,0.5);transition:right 0.3s cubic-bezier(0.4,0,0.2,1);z-index:100;display:flex;flex-direction:column;padding:20px;box-sizing:border-box}";
    body += ".queue-panel.open{right:0}";
    body += ".queue-header{display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid rgba(255,255,255,0.08);padding-bottom:12px;margin-bottom:16px}";
    body += ".queue-header h2{margin:0;font-size:18px;font-weight:600;color:#f1f5f9}";
    body += ".queue-close{background:none;border:none;color:#94a3b8;font-size:24px;cursor:pointer;padding:0;line-height:1}";
    body += ".queue-list{flex-grow:1;overflow-y:auto;display:flex;flex-direction:column;gap:12px;margin-bottom:16px;padding-right:4px}";
    body += ".queue-item{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.05);border-radius:8px;padding:12px;position:relative;display:flex;flex-direction:column;gap:6px}";
    body += ".queue-item-header{display:flex;justify-content:space-between;align-items:flex-start;gap:8px}";
    body += ".queue-item-name{font-size:13px;font-weight:500;word-break:break-all;color:#e2e8f0;flex-grow:1}";
    body += ".queue-item-cancel{background:none;border:none;color:#f87171;cursor:pointer;padding:0 4px;font-size:16px;font-weight:bold;line-height:1}";
    body += ".queue-item-progress-bg{height:6px;background:rgba(255,255,255,0.08);border-radius:3px;overflow:hidden}";
    body += ".queue-item-progress-fill{height:100%;background:#38bdf8;width:0;transition:width 0.1s ease}";
    body += ".queue-item-meta{display:flex;justify-content:space-between;font-size:11px;color:#94a3b8}";
    body += ".queue-footer{display:flex;gap:10px}";
    body += ".queue-footer button{flex:1;padding:10px 14px}";
    body += "#start-transfers-btn{background:#38bdf8;color:#0f0f12;border-color:#38bdf8}";
    body += "#start-transfers-btn:hover{background:#0ea5e9;border-color:#0ea5e9}";
    body += ".queue-item.completed .queue-item-progress-fill{background:#4ade80}";
    body += ".queue-item.failed .queue-item-progress-fill{background:#f87171}";
    body += ".queue-item-install-label{display:flex;align-items:center;gap:4px;font-size:11px;color:#cbd5e1;cursor:pointer}";
    body += ".queue-item-install-label input{display:inline-block;margin:0;cursor:pointer}";

    body += "</style></head><body>"
            "<div id=\"transfer-overlay\" style=\"display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(15,15,18,0.7);backdrop-filter:blur(3px);-webkit-backdrop-filter:blur(3px);z-index:90;align-items:center;justify-content:center;flex-direction:column;gap:16px;box-sizing:border-box;\">"
            "<div style=\"font-size:24px;font-weight:600;color:#38bdf8;\">Transfer in Progress</div>"
            "<div style=\"color:#94a3b8;font-size:14px;text-align:center;max-width:400px;padding:0 20px;line-height:1.5;\">Please wait while the console is processing file transfers or game installations. You can monitor the progress in the Queue panel on the right.</div>"
            "</div>"
            "<div id=\"queue-panel\" class=\"queue-panel\"><div class=\"queue-header\"><h2>Transfer Queue</h2><button class=\"queue-close\" onclick=\"toggleQueuePanel()\">&times;</button></div><div id=\"queue-list\" class=\"queue-list\"></div><div class=\"queue-footer\"><button id=\"start-transfers-btn\" onclick=\"startTransfers()\">Start transfers</button><button id=\"clear-queue-btn\" onclick=\"clearCompletedQueue()\">Clear completed</button></div></div><header><h1>Sphaira Files</h1><div class=\"path\">";
    body += HtmlEscape(abs_path);
    body += "</div><div class=\"crumbs\"><a href=\"/?path=/\">SD Card</a>";

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

    body += "</div><div class=\"bar\"><button id=\"upload\" onclick=\"document.getElementById('files').click()\">Add to Upload</button>";
    body += "<button id=\"view-toggle\" onclick=\"toggleViewMode()\">Grid View</button>";
    body += "<button id=\"select-all-btn\" onclick=\"toggleSelectAll()\">Select All</button>";
    body += "<button id=\"download-selected\" onclick=\"addSelectedToDownloadQueue()\" style=\"border-color:rgba(56,189,248,0.4);background:rgba(56,189,248,0.1);color:#38bdf8;\" disabled>Download Selected (0)</button>";
    body += "<button id=\"delete-selected\" onclick=\"deleteSelected()\" style=\"border-color:rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171;\" disabled>Delete Selected (0)</button>";
    body += "<button id=\"queue-toggle-btn\" onclick=\"toggleQueuePanel()\" style=\"border-color:rgba(168,85,247,0.4);background:rgba(168,85,247,0.1);color:#c084fc;\">Queue (0)</button>";
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
            body += "</span><span class=\"meta\">folder</span><button class=\"delete-btn\" onclick=\"deleteFile(event,'";
            body += encoded_child;
            body += "')\">Delete</button></div></a>";
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
            body += "</span><button class=\"delete-btn\" onclick=\"deleteFile(event,'";
            body += encoded_child;
            body += "')\">Delete</button></div></a>";
        }
    }

    body += "<script>let currentPath=decodeURIComponent('";
    body += encoded_path;
    body += "');";
    body += "let transferQueue=[];let isTransferring=false;";
    body += "function toggleQueuePanel(){const p=document.getElementById('queue-panel');if(p)p.classList.toggle('open');}";
    body += "function addFilesToUploadQueue(files){if(!files||!files.length)return;";
    body += "for(const f of files){const isGame=/\\.(nsp|nsz|xci|xcz)$/i.test(f.name);transferQueue.push({id:'up_'+Math.random().toString(36).substr(2,9),type:'upload',file:f,name:f.name,size:f.size,status:'pending',progress:0,speed:'',install:isGame,xhr:null,uploadPath:currentPath});}";
    body += "updateQueueCount();renderQueue();const p=document.getElementById('queue-panel');if(p&&!p.classList.contains('open'))p.classList.add('open');document.getElementById('files').value='';}";
    body += "function addSelectedToDownloadQueue(){const ch=document.querySelectorAll('.file-checkbox:checked');if(!ch.length)return;";
    body += "for(const cb of ch){const p=cb.getAttribute('data-path');const dp=decodeURIComponent(p);const n=dp.split('/').pop()||'file';if(transferQueue.some(item=>item.type==='download'&&item.path===p))continue;transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:p,name:n,size:0,status:'pending',progress:0,speed:'',controller:null});}";
    body += "for(const cb of ch)cb.checked=false;updateSelectCount();updateQueueCount();renderQueue();toggleQueuePanel();}";
    body += "function updateQueueCount(){const b=document.getElementById('queue-toggle-btn');if(b){const active=transferQueue.filter(i=>['pending','uploading','downloading','installing'].includes(i.status)).length;b.textContent='Queue ('+active+')';}}";
    body += "function renderQueue(){const l=document.getElementById('queue-list');if(!l)return;l.innerHTML='';";
    body += "for(const i of transferQueue){const el=document.createElement('div');el.className='queue-item '+i.status;const pct=Math.round(i.progress);const speed=i.speed?' · '+i.speed:'';const sizeStr=i.size?formatBytes(i.size):'Unknown size';";
    body += "let st=i.status;if(i.status==='pending')st='Pending';else if(i.status==='uploading')st='Uploading';else if(i.status==='downloading')st='Downloading';else if(i.status==='installing')st='Installing...';else if(i.status==='completed')st='Completed';else if(i.status==='failed')st='Failed';else if(i.status==='cancelled')st='Cancelled';";
    body += "let hdr='<div class=\"queue-item-header\"><span class=\"queue-item-name\">'+escapeHtml(i.name)+'</span>';if(['pending','uploading','downloading','installing'].includes(i.status)){hdr+='<button class=\"queue-item-cancel\" onclick=\"cancelTransfer(\\''+i.id+'\\')\">&times;</button>';}hdr+='</div>';";
    body += "let inst='';if(i.type==='upload'&&i.status==='pending'&&/\\.(nsp|nsz|xci|xcz)$/i.test(i.name)){inst='<label class=\"queue-item-install-label\"><input type=\"checkbox\" '+(i.install?'checked':'')+' onchange=\"toggleInstallOption(\\''+i.id+'\\',this.checked)\">Install directly</label>';}else if(i.type==='upload'&&i.install){inst='<div style=\"font-size:11px;color:#c084fc\">Direct Install mode</div>';}";
    body += "el.innerHTML=hdr+'<div style=\"font-size:11px;color:#94a3b8;margin-bottom:4px\">'+(i.type==='upload'?'Upload':'Download')+'</div>'+inst+'<div class=\"queue-item-progress-bg\"><div class=\"queue-item-progress-fill\" style=\"width:'+pct+'%\"></div></div><div class=\"queue-item-meta\"><span>'+pct+'%'+speed+'</span><span>'+sizeStr+'</span></div>';l.appendChild(el);}}";
    body += "function toggleInstallOption(id,chk){const i=transferQueue.find(item=>item.id===id);if(i)i.install=chk;}";
    body += "function formatBytes(b){if(b===0)return '0 Bytes';const k=1024;const sizes=['Bytes','KB','MB','GB'];const i=Math.floor(Math.log(b)/Math.log(k));return parseFloat((b/Math.pow(k,i)).toFixed(2))+' '+sizes[i];}";
    body += "function escapeHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}";
    body += "function cancelTransfer(id){const i=transferQueue.find(item=>item.id===id);if(!i)return;";
    body += "if(i.status==='uploading'&&i.xhr){i.xhr.abort();}else if(i.status==='downloading'&&i.controller){i.controller.abort();}";
    body += "i.status='cancelled';i.progress=0;i.speed='';renderQueue();updateQueueCount();}";
    body += "function clearCompletedQueue(){transferQueue=transferQueue.filter(i=>['pending','uploading','downloading','installing'].includes(i.status));renderQueue();updateQueueCount();}";
    body += "async function startTransfers(){if(isTransferring)return;isTransferring=true;const btn=document.getElementById('start-transfers-btn');if(btn)btn.disabled=true;const ov=document.getElementById('transfer-overlay');if(ov)ov.style.display='flex';";
    body += "try{while(true){const next=transferQueue.find(i=>i.status==='pending');if(!next)break;if(next.type==='upload')await uploadFileItem(next);else await downloadFileItem(next);updateQueueCount();renderQueue();}}finally{isTransferring=false;if(btn)btn.disabled=false;if(ov)ov.style.display='none';navigateTo(currentPath,false);}}";
    body += "function uploadFileItem(item){return new Promise(res=>{item.status='uploading';renderQueue();const xhr=new XMLHttpRequest();item.xhr=xhr;let url='/upload?path='+encodeURIComponent(item.uploadPath)+'&name='+encodeURIComponent(item.name);if(item.install){url+='&install=1';item.status='installing';}xhr.open('PUT',url,true);let startTime=Date.now();let lastTime=startTime;let lastLoaded=0;";
    body += "xhr.upload.addEventListener('progress',e=>{if(e.lengthComputable){const now=Date.now();item.progress=(e.loaded/e.total)*100;const diff=(now-lastTime)/1000;if(diff>=0.5){const speed=(e.loaded-lastLoaded)/diff;item.speed=formatBytes(speed)+'/s';lastTime=now;lastLoaded=e.loaded;}if(item.install&&item.progress>=99){item.status='installing';}renderQueue();}});";
    body += "xhr.onload=()=>{if(xhr.status===200){item.status='completed';item.progress=100;item.speed='';}else{item.status='failed';item.speed='Error: '+xhr.statusText;}res();};";
    body += "xhr.onerror=()=>{item.status='failed';item.speed='Network error';res();};";
    body += "xhr.onabort=()=>{item.status='cancelled';res();};";
    body += "xhr.send(item.file);});}";
    body += "async function downloadFileItem(item){item.status='downloading';renderQueue();const ctrl=new AbortController();item.controller=ctrl;try{const res=await fetch('/download?path='+item.path,{signal:ctrl.signal});if(!res.ok)throw new Error(res.statusText);const len=res.headers.get('content-length');const total=len?parseInt(len,10):0;item.size=total;const reader=res.body.getReader();let loaded=0;let chunks=[];let lastTime=Date.now();let lastLoaded=0;";
    body += "while(true){const {done,value}=await reader.read();if(done)break;chunks.push(value);loaded+=value.length;if(total)item.progress=(loaded/total)*100;const now=Date.now();const diff=(now-lastTime)/1000;if(diff>=0.5){const speed=(loaded-lastLoaded)/diff;item.speed=formatBytes(speed)+'/s';lastTime=now;lastLoaded=loaded;}renderQueue();}";
    body += "const blob=new Blob(chunks);const dlUrl=URL.createObjectURL(blob);const a=document.createElement('a');a.href=dlUrl;a.download=item.name;document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(dlUrl);item.status='completed';item.progress=100;item.speed='';}catch(err){if(err.name==='AbortError')item.status='cancelled';else{item.status='failed';item.speed=err.message;}}}";
    
    body += "function toggleViewMode(){";
    body += "const container=document.getElementById('items-container');";
    body += "const btn=document.getElementById('view-toggle');";
    body += "if(container.classList.contains('list')){";
    body += "container.classList.remove('list');";
    body += "container.classList.add('grid');";
    body += "btn.textContent='List View';";
    body += "localStorage.setItem('viewMode','grid');";
    body += "}else{";
    body += "container.classList.remove('grid');";
    body += "container.classList.add('list');";
    body += "btn.textContent='Grid View';";
    body += "localStorage.setItem('viewMode','list');";
    body += "}";
    body += "}";
    
    body += "document.addEventListener('DOMContentLoaded',()=>{";
    body += "const container=document.getElementById('items-container');";
    body += "const btn=document.getElementById('view-toggle');";
    body += "const saved=localStorage.getItem('viewMode');";
    body += "if(saved==='grid'&&container){";
    body += "container.classList.remove('list');";
    body += "container.classList.add('grid');";
    body += "if(btn)btn.textContent='List View';";
    body += "}";
    body += "});";
    
    body += "async function deleteFile(e,path){e.preventDefault();e.stopPropagation();if(!confirm('Delete '+decodeURIComponent(path.split('/').pop())+'?'))return;const res=await fetch('/delete?path='+path,{method:'DELETE'});if(res.ok){navigateTo(currentPath,false);}else{alert('Delete failed: '+await res.text());}}";

    body += "function updateSelectCount(){";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "const btn=document.getElementById('delete-selected');";
    body += "if(btn){btn.disabled=checked.length===0;btn.textContent='Delete Selected ('+checked.length+')';}";
    body += "const dlBtn=document.getElementById('download-selected');";
    body += "if(dlBtn){dlBtn.disabled=checked.length===0;dlBtn.textContent='Download Selected ('+checked.length+')';}";
    body += "const selectAllBtn=document.getElementById('select-all-btn');";
    body += "if(selectAllBtn){";
    body += "const all=document.querySelectorAll('.file-checkbox');";
    body += "if(all.length&&checked.length===all.length){selectAllBtn.textContent='Deselect All';}else{selectAllBtn.textContent='Select All';}";
    body += "}";
    body += "}";

    body += "function toggleSelectAll(){";
    body += "const checkboxes=document.querySelectorAll('.file-checkbox');";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "const targetState=checked.length<checkboxes.length;";
    body += "for(const cb of checkboxes){cb.checked=targetState;}";
    body += "updateSelectCount();";
    body += "}";

    body += "async function deleteSelected(){";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "if(!checked.length)return;";
    body += "if(!confirm('Delete '+checked.length+' selected files/folders?'))return;";
    body += "const btn=document.getElementById('delete-selected');";
    body += "if(btn)btn.disabled=true;";
    body += "const status=document.getElementById('status');";
    body += "let count=0;";
    body += "for(const cb of checked){";
    body += "count++;";
    body += "status.textContent='Deleting ('+count+'/'+checked.length+')...';";
    body += "const path=cb.getAttribute('data-path');";
    body += "await fetch('/delete?path='+path,{method:'DELETE'});";
    body += "}";
    body += "status.textContent='Done';";
    body += "navigateTo(currentPath,false);";
    body += "}";

    body += "function getFocusedItem(){return document.querySelector('.item.focused');}";
    body += "function focusItem(item){if(!item)return;const prev=getFocusedItem();if(prev)prev.classList.remove('focused');item.classList.add('focused');item.scrollIntoView({block:'nearest'});}";
    body += "function navigateGrid(direction){";
    body += "const current=getFocusedItem();if(!current){focusItem(document.querySelector('.item'));return;}";
    body += "const items=Array.from(document.querySelectorAll('.item'));";
    body += "const currRect=current.getBoundingClientRect();";
    body += "const currCenterX=currRect.left+currRect.width/2;const currCenterY=currRect.top+currRect.height/2;";
    body += "let bestMatch=null;let minDistance=Infinity;";
    body += "for(const item of items){";
    body += "if(item===current)continue;";
    body += "const rect=item.getBoundingClientRect();";
    body += "const centerX=rect.left+rect.width/2;const centerY=rect.top+rect.height/2;";
    body += "if(direction==='down'&&rect.top>=currRect.bottom-5){";
    body += "const dy=centerY-currCenterY;const dx=centerX-currCenterX;const dist=dy*dy+dx*dx*5;";
    body += "if(dist<minDistance){minDistance=dist;bestMatch=item;}";
    body += "}else if(direction==='up'&&rect.bottom<=currRect.top+5){";
    body += "const dy=currCenterY-centerY;const dx=centerX-currCenterX;const dist=dy*dy+dx*dx*5;";
    body += "if(dist<minDistance){minDistance=dist;bestMatch=item;}";
    body += "}";
    body += "}";
    body += "if(bestMatch)focusItem(bestMatch);";
    body += "}";
    body += "document.addEventListener('mouseover',function(e){const item=e.target.closest('.item');if(item)focusItem(item);});";
    body += "document.addEventListener('dragstart',function(e){e.preventDefault();});";
    body += "document.addEventListener('keydown',function(e){";
    body += "if(e.target.tagName==='INPUT'&&e.target.type!=='checkbox')return;";
    body += "const items=Array.from(document.querySelectorAll('.item'));if(!items.length)return;";
    body += "const current=getFocusedItem();if(!current){focusItem(items[0]);return;}";
    body += "const isGrid=document.getElementById('items-container').classList.contains('grid');";
    body += "if(e.key==='ArrowDown'){";
    body += "e.preventDefault();";
    body += "if(isGrid)navigateGrid('down');else{const idx=items.indexOf(current);if(idx<items.length-1)focusItem(items[idx+1]);}";
    body += "}else if(e.key==='ArrowUp'){";
    body += "e.preventDefault();";
    body += "if(isGrid)navigateGrid('up');else{const idx=items.indexOf(current);if(idx>0)focusItem(items[idx-1]);}";
    body += "}else if(e.key==='ArrowLeft'){";
    body += "if(isGrid){e.preventDefault();const idx=items.indexOf(current);if(idx>0)focusItem(items[idx-1]);}";
    body += "}else if(e.key==='ArrowRight'){";
    body += "if(isGrid){e.preventDefault();const idx=items.indexOf(current);if(idx<items.length-1)focusItem(items[idx+1]);}";
    body += "}else if(e.key===' '){";
    body += "e.preventDefault();";
    body += "const cb=current.querySelector('.file-checkbox');if(cb){cb.checked=!cb.checked;updateSelectCount();}";
    body += "}else if(e.key==='Escape'){";
    body += "e.preventDefault();";
    body += "const checkboxes=document.querySelectorAll('.file-checkbox');for(const cb of checkboxes)cb.checked=false;";
    body += "updateSelectCount();";
    body += "}else if(e.key==='Delete'){";
    body += "e.preventDefault();";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "if(checked.length>0){deleteSelected();}";
    body += "else{";
    body += "const path=current.querySelector('.file-checkbox')?.getAttribute('data-path');";
    body += "if(path&&confirm('Delete '+current.querySelector('.name').textContent+'?')){";
    body += "fetch('/delete?path='+path,{method:'DELETE'}).then(()=>navigateTo(currentPath,false));";
    body += "}";
    body += "}";
    body += "}else if(e.key==='Enter'){";
    body += "e.preventDefault();";
    body += "current.click();";
    body += "}";
    body += "});";

    body += "function makeCRCTable(){";
    body += "let c;const crcTable=[];";
    body += "for(let n=0;n<256;n++){";
    body += "c=n;";
    body += "for(let k=0;k<8;k++){";
    body += "c=((c&1)?(0xEDB88320^(c>>>1)):(c>>>1));";
    body += "}";
    body += "crcTable[n]=c;";
    body += "}";
    body += "return crcTable;";
    body += "}";
    body += "const crcTable=makeCRCTable();";
    body += "function crc32(arr){";
    body += "let crc=0^(-1);";
    body += "for(let i=0;i<arr.length;i++){";
    body += "crc=(crc>>>8)^crcTable[(crc^arr[i])&0xFF];";
    body += "}";
    body += "return (crc^(-1))>>>0;";
    body += "}";

    body += "function createZip(files){";
    body += "const localHeaders=[];const centralHeaders=[];let offset=0;";
    body += "for(const file of files){";
    body += "const nameBytes=new TextEncoder().encode(file.name);";
    body += "const dataBytes=file.data;";
    body += "const crc=crc32(dataBytes);";
    body += "const size=dataBytes.length;";
    body += "const lh=new ArrayBuffer(30+nameBytes.length);";
    body += "const lhView=new DataView(lh);";
    body += "lhView.setUint32(0,0x04034b50,true);";
    body += "lhView.setUint16(4,10,true);";
    body += "lhView.setUint16(6,0,true);";
    body += "lhView.setUint16(8,0,true);";
    body += "lhView.setUint16(10,0,true);";
    body += "lhView.setUint16(12,0,true);";
    body += "lhView.setUint32(14,crc,true);";
    body += "lhView.setUint32(18,size,true);";
    body += "lhView.setUint32(22,size,true);";
    body += "lhView.setUint16(26,nameBytes.length,true);";
    body += "lhView.setUint16(28,0,true);";
    body += "new Uint8Array(lh,30).set(nameBytes);";
    body += "localHeaders.push(new Uint8Array(lh));";
    body += "localHeaders.push(dataBytes);";
    body += "const ch=new ArrayBuffer(46+nameBytes.length);";
    body += "const chView=new DataView(ch);";
    body += "chView.setUint32(0,0x02014b50,true);";
    body += "chView.setUint16(4,20,true);";
    body += "chView.setUint16(6,10,true);";
    body += "chView.setUint16(8,0,true);";
    body += "chView.setUint16(10,0,true);";
    body += "chView.setUint16(12,0,true);";
    body += "chView.setUint16(14,0,true);";
    body += "chView.setUint32(16,crc,true);";
    body += "chView.setUint32(20,size,true);";
    body += "chView.setUint32(24,size,true);";
    body += "chView.setUint16(28,nameBytes.length,true);";
    body += "chView.setUint16(30,0,true);";
    body += "chView.setUint16(32,0,true);";
    body += "chView.setUint16(34,0,true);";
    body += "chView.setUint16(36,0,true);";
    body += "chView.setUint32(38,0,true);";
    body += "chView.setUint32(42,offset,true);";
    body += "new Uint8Array(ch,46).set(nameBytes);";
    body += "centralHeaders.push(new Uint8Array(ch));";
    body += "offset+=lh.byteLength+size;";
    body += "}";
    body += "const cdOffset=offset;let cdSize=0;";
    body += "for(const ch of centralHeaders)cdSize+=ch.byteLength;";
    body += "const eocd=new ArrayBuffer(22);";
    body += "const eocdView=new DataView(eocd);";
    body += "eocdView.setUint32(0,0x06054b50,true);";
    body += "eocdView.setUint16(4,0,true);";
    body += "eocdView.setUint16(6,0,true);";
    body += "eocdView.setUint16(8,files.length,true);";
    body += "eocdView.setUint16(10,files.length,true);";
    body += "eocdView.setUint32(12,cdSize,true);";
    body += "eocdView.setUint32(16,cdOffset,true);";
    body += "eocdView.setUint16(20,0,true);";
    body += "const blobParts=[];";
    body += "for(const part of localHeaders)blobParts.push(part);";
    body += "for(const part of centralHeaders)blobParts.push(part);";
    body += "blobParts.push(new Uint8Array(eocd));";
    body += "return new Blob(blobParts,{type:'application/zip'});";
    body += "}";

    body += "async function addSelectedToDownloadQueue(){";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "if(!checked.length)return;";
    body += "const status=document.getElementById('status');";
    body += "if(status)status.textContent='Preparing download queue...';";
    body += "for(const cb of checked){";
    body += "const path=cb.getAttribute('data-path');";
    body += "const decodedPath=decodeURIComponent(path);";
    body += "const name=decodedPath.split('/').pop()||'file';";
    body += "const isDir=cb.closest('.item').querySelector('.meta').textContent==='folder';";
    body += "if(isDir){";
    body += "if(status)status.textContent='Scanning folder '+name+'...';";
    body += "try{";
    body += "const res=await fetch('/list-recursive?path='+path);";
    body += "if(res.ok){";
    body += "const nested=await res.json();";
    body += "for(const f of nested){";
    body += "const fName=decodeURIComponent(f.path).substring(decodeURIComponent(currentPath).length);";
    body += "const cleanFName=fName.startsWith('/')?fName.substring(1):fName;";
    body += "if(!transferQueue.some(item=>item.type==='download'&&item.path===f.path)){";
    body += "transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:f.path,name:cleanFName,size:f.size,status:'pending',progress:0,speed:'',controller:null});";
    body += "}";
    body += "}";
    body += "}";
    body += "}catch(e){console.error(e);}";
    body += "}else{";
    body += "if(!transferQueue.some(item=>item.type==='download'&&item.path===path)){";
    body += "let sizeVal=0;";
    body += "const sizeMeta=cb.closest('.item').querySelector('.meta').textContent;";
    body += "if(sizeMeta.includes('MiB'))sizeVal=parseFloat(sizeMeta)*1024*1024;";
    body += "else if(sizeMeta.includes('KiB'))sizeVal=parseFloat(sizeMeta)*1024;";
    body += "transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:path,name:name,size:sizeVal,status:'pending',progress:0,speed:'',controller:null});";
    body += "}";
    body += "}";
    body += "}";
    body += "for(const cb of checked)cb.checked=false;";
    body += "updateSelectCount();updateQueueCount();renderQueue();";
    body += "if(status)status.textContent='';";
    body += "const panel=document.getElementById('queue-panel');";
    body += "if(panel&&!panel.classList.contains('open'))panel.classList.add('open');";
    body += "}";

    body += "async function navigateTo(path,shouldPushState=true){";
    body += "const status=document.getElementById('status');if(status)status.textContent='Loading...';";
    body += "try{const res=await fetch('/list?path='+encodeURIComponent(path));if(!res.ok)throw new Error(res.statusText);";
    body += "const data=await res.json();currentPath=data.path;";
    body += "const pathDiv=document.querySelector('.path');if(pathDiv)pathDiv.textContent=data.path;";
    body += "renderCrumbs(data.path);renderItems(data.path,data.entries);";
    body += "if(shouldPushState){const newUrl=window.location.protocol+'//'+window.location.host+'/?path='+encodeURIComponent(data.path);window.history.pushState({path:data.path},'',newUrl);}";
    body += "updateSelectCount();";
    body += "const container=document.getElementById('items-container');const saved=localStorage.getItem('viewMode');const btn=document.getElementById('view-toggle');";
    body += "if(saved==='grid'&&container){container.classList.remove('list');container.classList.add('grid');if(btn)btn.textContent='List View';}";
    body += "else if(container){container.classList.remove('grid');container.classList.add('list');if(btn)btn.textContent='Grid View';}";
    body += "if(typeof initLightbox==='function')initLightbox();";
    body += "}catch(err){alert('Failed to load folder: '+err.message);}finally{if(status)status.textContent='';}}";

    body += "function renderCrumbs(path){";
    body += "const container=document.querySelector('.crumbs');if(!container)return;";
    body += "let html='<a href=\"/?path=/\">SD Card</a>';";
    body += "if(path!=='/'&&path!==''){";
    body += "const parts=path.split('/').filter(Boolean);let accum='';";
    body += "for(const part of parts){accum+='/'+part;html+=' / <a href=\"/?path='+encodeURIComponent(accum)+'\">'+escapeHtml(part)+'</a>';}";
    body += "}";
    body += "container.innerHTML=html;}";

    body += "function renderItems(path,entries){";
    body += "const container=document.getElementById('items-container');if(!container)return;container.innerHTML='';";
    body += "if(path!=='/'){";
    body += "let parent=path;const lastSlash=parent.lastIndexOf('/');if(lastSlash!==-1)parent=parent.substring(0,lastSlash);if(parent==='')parent='/';";
    body += "const el=document.createElement('a');el.className='item';el.href='/?path='+encodeURIComponent(parent);";
    body += "el.innerHTML='<div class=\"thumbnail-box\"><svg viewBox=\"0 0 24 24\" fill=\"#ffca28\"><path d=\"M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg></div><div class=\"info\"><span class=\"name\">..</span><span class=\"meta\">parent folder</span></div>';";
    body += "container.appendChild(el);";
    body += "}";
    body += "if(!entries||!entries.length){";
    body += "const el=document.createElement('div');el.className='empty';el.textContent='Empty folder';container.appendChild(el);return;";
    body += "}";
    body += "for(const entry of entries){";
    body += "let child=path;if(!child.endsWith('/'))child+='/';child+=entry.name;const encChild=encodeURIComponent(child);const nameEsc=escapeHtml(entry.name);";
    body += "const el=document.createElement('a');el.className='item';let thumb='';";
    body += "if(entry.type===0){";
    body += "el.href='/?path='+encChild;";
    body += "thumb='<svg viewBox=\"0 0 24 24\" fill=\"#ffca28\"><path d=\"M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg>';";
    body += "}else{";
    body += "const ext=entry.name.split('.').pop().toLowerCase();const isImage=['png','jpg','jpeg','gif','bmp'].includes(ext);";
    body += "el.href=isImage?('/view?path='+encChild):('/download?path='+encChild);";
    body += "if(isImage)thumb='<img class=\"thumb\" src=\"/view?path='+encChild+'\" alt=\"\" loading=\"lazy\">';";
    body += "else thumb='<svg viewBox=\"0 0 24 24\" fill=\"#90a4ae\"><path d=\"M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z\"/></svg>';";
    body += "}";
    body += "let metaStr='folder';if(entry.type!==0){";
    body += "if(entry.size>=1024*1024)metaStr=(entry.size/(1024*1024)).toFixed(2)+' MiB';";
    body += "else metaStr=(entry.size/1024).toFixed(2)+' KiB';";
    body += "}";
    body += "el.innerHTML=\'<input type=\"checkbox\" class=\"file-checkbox\" data-path=\"\'+encChild+\'\" onclick=\"event.stopPropagation(); updateSelectCount();\">\';";
    body += "el.innerHTML+=\'<div class=\"thumbnail-box\">\'+thumb+\'</div>\';";
    body += "el.innerHTML+=\'<div class=\"info\"><span class=\"name\">\'+nameEsc+\'</span><span class=\"meta\">\'+metaStr+\'</span><button class=\"delete-btn\" onclick=\"deleteFile(event,\\'\'+encChild+\'\\')\">Delete</button></div>\';";
    body += "container.appendChild(el);";
    body += "}}";

    body += "document.addEventListener('click',e=>{";
    body += "const a=e.target.closest('a');if(!a)return;";
    body += "if(e.target.tagName==='INPUT'||e.target.tagName==='BUTTON'||e.target.closest('.delete-btn'))return;";
    body += "const href=a.getAttribute('href');";
    body += "if(href&&href.startsWith('/?path=')){";
    body += "e.preventDefault();const url=new URL(href,window.location.origin);const path=url.searchParams.get('path')||'/';navigateTo(path);";
    body += "}";
    body += "});";

    body += "window.addEventListener('popstate',e=>{";
    body += "const url=new URL(window.location.href);const path=url.searchParams.get('path')||'/';navigateTo(path,false);";
    body += "});";;

    body += "</script>";

    AppendLightbox(body);

    body += "</body></html>";

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


auto BuildGalleryPage(std::string path_str) -> std::string {
    if (path_str.empty()) {
        path_str = GetShareFolderRoot().toString();
    }
    const auto abs_path = CanonicalizeAbsolutePath(path_str);

    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(abs_path, FsDirOpenMode_ReadFiles, &dir))) {
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

    const auto encoded_path = UrlEncode(abs_path);
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
    body += ".del-btn{display:block;width:calc(100% - 20px);margin:0 10px 10px;border:1px solid rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171;border-radius:6px;padding:6px 0;font-size:13px;font-weight:500;cursor:pointer;transition:all 0.15s}";
    body += ".del-btn:hover{background:rgba(239,68,68,0.25);border-color:rgba(239,68,68,0.7)}";
    body += "</style></head><body><header><h1>Sphaira Gallery</h1><div class=\"path\">";
    body += HtmlEscape(abs_path);
    body += "</div><div class=\"crumbs\"><a href=\"/files?path=/\">SD Card</a>";

    std::string crumb_accum;
    size_t start{};
    while (start < abs_path.size()) {
        const auto end = abs_path.find('/', start);
        const auto part = abs_path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            crumb_accum += '/' + part;
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
    body += "<button onclick=\"location.href='/files?path=" + encoded_path + "'\">List View</button>";
    body += "</div></header><main class=\"grid\">";

    if (images.empty()) {
        body += "<div class=\"empty\">No images found in this folder</div>";
    }

    for (const auto& entry : images) {
        const std::string name{entry.name};
        auto child = abs_path;
        if (child.empty() || child.back() != '/') {
            child += '/';
        }
        child += name;

        const auto encoded_child = UrlEncode(child);
        const auto escaped_name = HtmlEscape(name);

        body += "<a class=\"card\" href=\"/view?path=";
        body += encoded_child;
        body += "\"><img loading=\"lazy\" src=\"/view?path=";
        body += encoded_child;
        body += "\" alt=\"";
        body += escaped_name;
        body += "\"><span>";
        body += escaped_name;
        body += "</span><button class=\"del-btn\" onclick=\"deleteImg(event,'";
        body += encoded_child;
        body += "')\">Delete</button></a>";
    }

    body += "</main>";

    AppendLightbox(body);

    body += "<script>";
    body += "async function deleteImg(e,path){e.preventDefault();e.stopPropagation();if(!confirm('Delete '+decodeURIComponent(path.split('/').pop())+'?'))return;const res=await fetch('/delete?path='+path,{method:'DELETE'});if(res.ok){location.reload();}else{alert('Delete failed: '+await res.text());}}";
    body += "</script>";

    body += "</body></html>";
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

struct SocketStream final : yati::source::Stream {
    SocketStream(Socket sock, const std::string& initial_data, s64 content_length)
        : m_sock(sock), m_initial_data(initial_data), m_content_length(content_length) {
        m_open_result = 0;
    }

    Result ReadChunk(void* buf, s64 size, u64* bytes_read) override {
        *bytes_read = 0;
        if (m_total_read >= m_content_length || size <= 0) {
            return 0;
        }

        s64 want = std::min<s64>(size, m_content_length - m_total_read);
        u8* out_ptr = static_cast<u8*>(buf);
        s64 read_now = 0;

        if (m_initial_offset < static_cast<s64>(m_initial_data.size())) {
            s64 avail = static_cast<s64>(m_initial_data.size()) - m_initial_offset;
            s64 todo = std::min<s64>(want, avail);
            std::memcpy(out_ptr, m_initial_data.data() + m_initial_offset, todo);
            m_initial_offset += todo;
            m_total_read += todo;
            read_now += todo;
            out_ptr += todo;
            want -= todo;
        }

        u32 idle_count = 0;
        while (want > 0) {
            if (auto pbox = WebGetProgressBox()) {
                if (pbox->ShouldExit()) {
                    return -1;
                }
            }
            int got = recv(m_sock, out_ptr, want, 0);
            if (got > 0) {
                idle_count = 0;
                m_total_read += got;
                read_now += got;
                out_ptr += got;
                want -= got;
            } else if (got == 0) {
                break;
            } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
                idle_count++;
                if (idle_count > 30000) { // 30 seconds
                    return -1;
                }
                svcSleepThread(1'000'000);
            } else {
                return -1;
            }
        }

        *bytes_read = read_now;
        g_upload_state.bytes.store(m_total_read);
        return 0;
    }

private:
    Socket m_sock;
    std::string m_initial_data;
    s64 m_initial_offset{0};
    s64 m_content_length;
    s64 m_total_read{0};
};

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
        
        s64 estimated_size = content_length;
        const auto ext = PathExtension(name);
        if (ExtensionEquals(ext, "nsz") || ExtensionEquals(ext, "xcz")) {
            estimated_size = static_cast<s64>(content_length * 1.6);
        }

        s64 free_nand = 0;
        bool install_to_sd = true;
        fs::FsNativeBis fs_nand{FsBisPartitionId_User, "/"};
        if (R_SUCCEEDED(fs_nand.GetFreeSpace("/", &free_nand))) {
            const s64 min_free_nand = 500ULL * 1024ULL * 1024ULL; // 500 MB
            if (free_nand - estimated_size >= min_free_nand) {
                install_to_sd = false;
            }
        }

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

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    u32 idle_count = 0;
    while (offset < content_length) {
        if (auto pbox = WebGetProgressBox()) {
            if (pbox->ShouldExit()) {
                g_upload_state.active.store(false);
                SendResponse(sock, "400 Bad Request", "text/plain", "Cancelled by user");
                return;
            }
        }
        const auto want = std::min<s64>(buf.size(), content_length - offset);
        const auto got = recv(sock, buf.data(), want, 0);
        if (got > 0) {
            idle_count = 0;
            if (R_FAILED(file.Write(offset, buf.data(), got, FsWriteOption_None))) {
                g_upload_state.active.store(false);
                SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not write file");
                return;
            }
            offset += got;
            g_upload_state.bytes.store(offset);
        } else if (got == 0) {
            g_upload_state.active.store(false);
            SendResponse(sock, "400 Bad Request", "text/plain", "Upload ended early");
            return;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            idle_count++;
            if (idle_count > 30000) { // 30 seconds
                g_upload_state.active.store(false);
                SendResponse(sock, "408 Request Timeout", "text/plain", "Receive timeout");
                return;
            }
            svcSleepThread(1'000'000);
        } else {
            g_upload_state.active.store(false);
            SendResponse(sock, "500 Internal Server Error", "text/plain", "Socket read failed");
            return;
        }
    }

    g_upload_state.active.store(false);
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

void ScanDirectoryRecursive(fs::FsNativeSd& fs, const std::string& path, std::vector<std::pair<std::string, s64>>& out_files) {
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
        dir.ReadAll(entries);
        for (const auto& entry : entries) {
            std::string child = path;
            if (child.empty() || child.back() != '/') {
                child += '/';
            }
            child += entry.name;
            if (entry.type == FsDirEntryType_Dir) {
                ScanDirectoryRecursive(fs, child, out_files);
            } else {
                out_files.push_back({child, entry.file_size});
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

    if (path == "/images" || path == "/images/") {
        SendResponse(sock, "200 OK", "text/html", BuildImagesPage());
        return;
    }

    if (path == "/gallery" || path == "/gallery/") {
        SendResponse(sock, "200 OK", "text/html", BuildGalleryPage(GetQueryValue(query, "path")));
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

        threadWaitForExit(&g_share_thread);
        threadClose(&g_share_thread);
        g_share_port = 0;
    }

    std::scoped_lock lock{g_share_mutex};
    g_share_entries.clear();
    g_share_folder_root = {};
    g_share_mode = ShareMode::None;
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

static ui::ProgressBox* g_web_pbox = nullptr;

void WebSetProgressBox(ui::ProgressBox* pbox) {
    g_web_pbox = pbox;
}

ui::ProgressBox* WebGetProgressBox() {
    return g_web_pbox;
}

bool WebShareIsRunning() {
    return g_share_running.load();
}

} // namespace sphaira
