#include "web.hpp"
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

namespace sphaira {
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

struct UploadState {
    std::atomic<bool> active{false};
    std::atomic<s64> bytes{0};
    std::atomic<s64> total{0};
    std::mutex name_mutex{};
    std::string name{};
};
static UploadState g_upload_state;

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

constexpr std::string_view LIGHTBOX_CONTENT = R"HTML(
<style>
.lightbox{display:none;position:fixed;z-index:1000;top:0;left:0;width:100%;height:100%;background-color:rgba(0,0,0,0.95);align-items:center;justify-content:center;user-select:none}
.lightbox-content{position:relative;max-width:90%;max-height:85%;display:flex;flex-direction:column;align-items:center}
.lightbox-img{max-width:100%;max-height:80vh;object-fit:contain;border-radius:4px;box-shadow:0 4px 20px rgba(0,0,0,0.5)}
.lightbox-caption{margin-top:12px;color:#eee;font-size:15px;text-align:center;word-break:break-all;max-width:600px}
.lightbox-close{position:absolute;top:20px;right:25px;color:#bbb;font-size:40px;font-weight:bold;cursor:pointer;transition:color 0.2s;line-height:1}
.lightbox-close:hover{color:#fff}
.lightbox-btn{position:absolute;top:50%;transform:translateY(-50%);background:rgba(40,45,50,0.5);border:1px solid rgba(255,255,255,0.1);color:#fff;font-size:24px;width:50px;height:50px;border-radius:50%;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:background 0.2s,color 0.2s}
.lightbox-btn:hover{background:rgba(60,65,70,0.8)}
.lightbox-prev{left:20px}
.lightbox-next{right:20px}
</style>
<div id="lightbox" class="lightbox">
<span class="lightbox-close" onclick="closeLightbox()">&times;</span>
<button class="lightbox-btn lightbox-prev" onclick="prevImage(event)">&lt;</button>
<button class="lightbox-btn lightbox-next" onclick="nextImage(event)">&gt;</button>
<div class="lightbox-content">
<img id="lightbox-img" class="lightbox-img" src="" alt="">
<div id="lightbox-caption" class="lightbox-caption"></div>
</div></div>
<script>
let imageList=[];let currentImageIndex=-1;
function initLightbox(){
imageList=[];
const links=document.querySelectorAll('a');
for(const link of links){
const href=link.getAttribute('href');
if(href&&href.includes('/view?path=')){
let name='';const span=link.querySelector('span:nth-child(2)')||link.querySelector('span');if(span){name=span.textContent;}else{const img=link.querySelector('img');if(img){name=img.getAttribute('alt')||'';}}
const idx=imageList.length;imageList.push({href:href,name:name});
link.addEventListener('click',function(e){
e.preventDefault();openLightbox(idx);
});
}
}
document.addEventListener('keydown',function(e){
const modal=document.getElementById('lightbox');
if(modal&&modal.style.display==='flex'){
if(e.key==='ArrowLeft')prevImage();
else if(e.key==='ArrowRight')nextImage();
else if(e.key==='Escape')closeLightbox();
}
});
}
function openLightbox(index){
if(index<0||index>=imageList.length)return;
currentImageIndex=index;
const modal=document.getElementById('lightbox');
const img=document.getElementById('lightbox-img');
const caption=document.getElementById('lightbox-caption');
img.src=imageList[index].href;caption.textContent=imageList[index].name;
modal.style.display='flex';
}
function closeLightbox(){document.getElementById('lightbox').style.display='none';}
function prevImage(e){if(e)e.stopPropagation();if(imageList.length<=1)return;let idx=currentImageIndex-1;if(idx<0)idx=imageList.length-1;openLightbox(idx);}
function nextImage(e){if(e)e.stopPropagation();if(imageList.length<=1)return;let idx=currentImageIndex+1;if(idx>=imageList.length)idx=0;openLightbox(idx);}
document.addEventListener('DOMContentLoaded',initLightbox);
initLightbox();
</script>
)HTML";

void AppendLightbox(std::string& body) {
    body += LIGHTBOX_CONTENT;
}

constexpr std::string_view CONFIRM_MODAL_HTML = R"HTML(
<div id="confirm-modal" class="modal" style="display:none;"><div class="modal-content"><div class="modal-text" id="confirm-text">Are you sure?</div><div class="modal-buttons"><button id="confirm-yes-btn" class="modal-btn yes-btn"><span class="key-badge">+</span> Yes</button><button id="confirm-no-btn" class="modal-btn no-btn"><span class="key-badge">B</span> No</button></div></div></div>
)HTML";

constexpr std::string_view CONFIRM_MODAL_JS = R"HTML(
let confirmPromiseResolve=null;
function showConfirmDialog(text){return new Promise(res=>{const m=document.getElementById('confirm-modal');const t=document.getElementById('confirm-text');if(!m||!t){res(confirm(text));return;}t.textContent=text;m.style.display='flex';confirmPromiseResolve=res;});}
function handleConfirmResult(res){const m=document.getElementById('confirm-modal');if(m)m.style.display='none';if(confirmPromiseResolve){const r=confirmPromiseResolve;confirmPromiseResolve=null;r(res);}}
document.addEventListener('keydown',function(e){const m=document.getElementById('confirm-modal');if(!m||m.style.display==='none')return;if(e.key==='+'||e.key==='='||e.key==='Add'){e.preventDefault();e.stopImmediatePropagation();handleConfirmResult(true);}else if(e.key==='b'||e.key==='B'||e.key==='Escape'||e.key==='Backspace'){e.preventDefault();e.stopImmediatePropagation();handleConfirmResult(false);}},true);
document.addEventListener('DOMContentLoaded',()=>{const y=document.getElementById('confirm-yes-btn');if(y)y.onclick=()=>handleConfirmResult(true);const n=document.getElementById('confirm-no-btn');if(n)n.onclick=()=>handleConfirmResult(false);});
)HTML";

void AppendConfirmModal(std::string& body) {
    body += CONFIRM_MODAL_HTML;
}

constexpr std::string_view FOLDER_PAGE_HEADER = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sphaira Files</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f0f12;color:#e2e8f0}
header{position:sticky;top:0;background:rgba(23,25,35,0.85);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);padding:16px 24px;border-bottom:1px solid rgba(255,255,255,0.08);z-index:10}
.header-top{display:flex;justify-content:space-between;align-items:center;gap:16px}
h1{font-size:22px;margin:0;font-weight:600;letter-spacing:-0.5px}
.crumbs{font-size:14px;color:#94a3b8;text-align:right;max-width:60%;word-break:break-all}
.crumbs a{color:#38bdf8;text-decoration:none}
.crumbs a:hover{text-decoration:underline}
.bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:16px}
button{border:1px solid rgba(255,255,255,0.15);background:#1e293b;color:#fff;border-radius:8px;padding:8px 16px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s}
button:hover{background:#334155;border-color:rgba(255,255,255,0.25)}
button:disabled{opacity:0.5;cursor:not-allowed}
input{display:none}.status{color:#38bdf8;font-size:14px}
.container{padding:24px;max-width:1200px;margin:0 auto}
.list{display:flex;flex-direction:column;gap:8px}
.list .item{display:flex;align-items:center;gap:16px;padding:12px 16px;background:#1e1e24;border:1px solid rgba(255,255,255,0.05);border-radius:8px;color:inherit;text-decoration:none;transition:background 0.15s,transform 0.15s;scroll-margin-top:160px}
.list .item:hover{background:#272730;transform:translateY(-1px)}
.list .thumbnail-box{width:40px;height:40px;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,0.2);border-radius:6px;overflow:hidden;flex-shrink:0}
.list .thumbnail-box img{width:100%;height:100%;object-fit:cover}
.list .thumbnail-box svg{width:24px;height:24px}
.list .info{display:flex;flex-grow:1;align-items:center;min-width:0}
.list .name{font-weight:500;margin-right:16px;flex-grow:1;word-break:break-all}
.list .meta{color:#64748b;font-size:13px;flex-shrink:0;width:120px;text-align:right;margin-right:24px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:16px}
.grid .item{display:flex;flex-direction:column;background:#18181f;border:1px solid rgba(255,255,255,0.05);border-radius:12px;color:inherit;text-decoration:none;overflow:hidden;transition:all 0.2s;scroll-margin-top:160px}
.grid .item:hover{background:#202029;transform:translateY(-3px);box-shadow:0 10px 20px rgba(0,0,0,0.3);border-color:rgba(56,189,248,0.3)}
.grid .thumbnail-box{width:100%;aspect-ratio:16/10;display:flex;align-items:center;justify-content:center;background:#0d0d11;overflow:hidden;border-bottom:1px solid rgba(255,255,255,0.03)}
.grid .thumbnail-box img{width:100%;height:100%;object-fit:cover}
.grid .thumbnail-box svg{width:48px;height:48px}
.grid .info{padding:12px;display:flex;flex-direction:column;gap:4px;min-width:0}
.grid .name{font-size:14px;font-weight:500;word-break:break-all}
.grid .meta{color:#64748b;font-size:12px}
.empty{padding:40px;text-align:center;color:#64748b;font-size:16px}
.delete-btn{margin-left:8px;border:1px solid rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171;border-radius:50%;width:28px;height:28px;display:inline-flex;align-items:center;justify-content:center;font-size:18px;font-weight:500;cursor:pointer;flex-shrink:0;transition:all 0.15s;padding:0;line-height:1}
.delete-btn:hover{background:rgba(239,68,68,0.25);border-color:rgba(239,68,68,0.7)}
.grid .delete-btn{position:absolute;top:12px;right:12px;margin:0;z-index:5}
.file-checkbox{-webkit-appearance:none;appearance:none;background:rgba(255,255,255,0.08);border:2px solid rgba(255,255,255,0.3);border-radius:4px;outline:none;cursor:pointer;transition:all 0.15s;display:block}
.file-checkbox:hover{border-color:#38bdf8;background:rgba(56,189,248,0.08)}
.file-checkbox:checked{background:#38bdf8;border-color:#38bdf8}
.file-checkbox:checked::after{content:'';position:absolute;border:solid #0f0f12;border-width:0 2px 2px 0;transform:rotate(45deg)}
.list .file-checkbox{position:relative;width:18px;height:18px;margin-right:12px;flex-shrink:0}
.list .file-checkbox:checked::after{left:5px;top:1px;width:4px;height:9px}
.grid .item{position:relative}
.grid .file-checkbox{position:absolute;top:12px;left:12px;width:20px;height:20px;margin:0;z-index:5}
.grid .file-checkbox:checked::after{left:6px;top:2px;width:4px;height:9px}
.item.focused{outline:2px solid #38bdf8;outline-offset:-2px;box-shadow:0 0 12px rgba(56,189,248,0.25)}
.queue-panel{position:fixed;top:0;right:-380px;width:340px;height:100%;background:rgba(20,20,25,0.95);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);border-left:1px solid rgba(255,255,255,0.08);box-shadow:-10px 0 30px rgba(0,0,0,0.5);transition:right 0.3s cubic-bezier(0.4,0,0.2,1);z-index:100;display:flex;flex-direction:column;padding:20px;box-sizing:border-box}
.queue-panel.open{right:0}
.queue-header{display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid rgba(255,255,255,0.08);padding-bottom:12px;margin-bottom:16px}
.queue-header h2{margin:0;font-size:18px;font-weight:600;color:#f1f5f9}
.queue-close{background:none;border:none;color:#94a3b8;font-size:24px;cursor:pointer;padding:0;line-height:1}
.queue-list{flex-grow:1;overflow-y:auto;display:flex;flex-direction:column;gap:12px;margin-bottom:16px;padding-right:4px}
.queue-item{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.05);border-radius:8px;padding:12px;position:relative;display:flex;flex-direction:column;gap:6px}
.queue-item-header{display:flex;justify-content:space-between;align-items:flex-start;gap:8px}
.queue-item-name{font-size:13px;font-weight:500;word-break:break-all;color:#e2e8f0;flex-grow:1}
.queue-item-cancel{background:none;border:none;color:#f87171;cursor:pointer;padding:0 4px;font-size:16px;font-weight:bold;line-height:1}
.queue-item-progress-bg{height:6px;background:rgba(255,255,255,0.08);border-radius:3px;overflow:hidden}
.queue-item-progress-fill{height:100%;background:#38bdf8;width:0;transition:width 0.1s ease}
.queue-item-meta{display:flex;justify-content:space-between;font-size:11px;color:#94a3b8}
.queue-footer{display:flex;gap:10px}
.queue-footer button{flex:1;padding:10px 14px}
#start-transfers-btn{background:#38bdf8;color:#0f0f12;border-color:#38bdf8}
#start-transfers-btn:hover{background:#0ea5e9;border-color:#0ea5e9}
.queue-item.completed .queue-item-progress-fill{background:#4ade80}
.queue-item.failed .queue-item-progress-fill{background:#f87171}
.queue-item-install-label{display:flex;align-items:center;gap:4px;font-size:11px;color:#cbd5e1;cursor:pointer}
.queue-item-install-label input{display:inline-block;margin:0;cursor:pointer}
.modal{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(15,15,18,0.75);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);z-index:200;display:flex;align-items:center;justify-content:center}
.modal-content{background:#181822;border:1px solid rgba(255,255,255,0.08);border-radius:16px;padding:24px;width:360px;max-width:90%;box-shadow:0 20px 40px rgba(0,0,0,0.6);display:flex;flex-direction:column;gap:20px;transform:scale(0.95);transition:transform 0.15s ease}
.modal-text{font-size:16px;font-weight:500;color:#f1f5f9;text-align:center;line-height:1.5;word-break:break-all}
.modal-buttons{display:flex;gap:12px}
.modal-btn{flex:1;padding:12px;border-radius:10px;font-size:14px;font-weight:600;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:8px;border:1px solid transparent;transition:all 0.15s}
.yes-btn{background:#10b981;color:#fff;border-color:#10b981}
.yes-btn:hover{background:#059669}
.no-btn{background:#ef4444;color:#fff;border-color:#ef4444}
.no-btn:hover{background:#dc2626}
.key-badge{background:rgba(255,255,255,0.25);border-radius:4px;padding:2px 6px;font-size:11px;font-weight:700;border:1px solid rgba(255,255,255,0.4);box-shadow:0 2px 0 rgba(0,0,0,0.2)}
@media (max-width: 600px) {
  header{padding:12px 16px}
  .header-top{flex-direction:column;align-items:flex-start;gap:6px}
  .crumbs{text-align:left;max-width:100%}
  .bar{gap:4px;margin-top:8px}
  button{padding:6px 10px}
  button .text{display:none}
  .list{gap:4px}
  .list .item{padding:8px 10px;gap:8px}
  .list .file-checkbox{margin-right:4px}
  .grid .file-checkbox{top:6px;left:6px}
  .list .name{font-size:13px}
  .meta-folder{display:none !important}
  .meta-size{font-size:10px;margin-right:0 !important;width:auto !important;text-align:right;margin-left:auto}
  .list .thumbnail-box{width:28px;height:28px}
  .delete-btn{display:none !important}
  .grid{grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:8px}
  .grid .info{padding:6px;gap:2px}
  .grid .name{font-size:11px}
  .grid .meta{font-size:9px}
}
</style></head><body>
<div id="transfer-overlay" style="display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(15,15,18,0.7);backdrop-filter:blur(3px);-webkit-backdrop-filter:blur(3px);z-index:90;align-items:center;justify-content:center;flex-direction:column;gap:16px;box-sizing:border-box;">
<div style="font-size:24px;font-weight:600;color:#38bdf8;">Transfer in Progress</div>
<div style="color:#94a3b8;font-size:14px;text-align:center;max-width:400px;padding:0 20px;line-height:1.5;">Please wait while the console is processing file transfers or game installations. You can monitor the progress in the Queue panel on the right.</div>
<div id="transfer-progress-info" style="display:flex;flex-direction:column;align-items:center;"></div>
</div>
<div id="queue-panel" class="queue-panel"><div class="queue-header"><h2>Transfer Queue</h2><button class="queue-close" onclick="toggleQueuePanel()">&times;</button></div><div id="queue-list" class="queue-list"></div><div class="queue-footer"><button id="start-transfers-btn" onclick="startTransfers()">Start transfers</button><button id="clear-queue-btn" onclick="clearCompletedQueue()">Clear completed</button></div></div>
<header>
)HTML";

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
    body += "<div class=\"header-top\"><h1>Sphaira Files</h1><a href=\"/progress\" style=\"text-decoration:none;\"><button><span class=\"icon\">⏳</span> <span class=\"text\">Progress</span></button></a><a href=\"/album\" style=\"text-decoration:none;\"><button><span class=\"icon\">📸</span> <span class=\"text\">Screenshots</span></button></a></div><div class=\"crumbs\"><a href=\"/?path=/\">SD Card</a>";

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

constexpr std::string_view FOLDER_PAGE_JS = R"HTML(
let transferQueue=[];let isTransferring=false;
async function pollServerStatus(){
try{
const res=await fetch('/status');
if(!res.ok)return;
const s=await res.json();
const ov=document.getElementById('transfer-overlay');
const info=document.getElementById('transfer-progress-info');
if(s.active){
if(info){
const pct=s.total>0?Math.min(100,Math.round((s.bytes/s.total)*100)):0;
info.innerHTML='<div style="font-size:14px;color:#e2e8f0;margin-top:4px;word-break:break-all;max-width:340px;text-align:center;">'+escapeHtml(s.name)+'</div>'+
'<div style="width:280px;height:10px;background:rgba(255,255,255,0.1);border-radius:5px;overflow:hidden;margin-top:10px;"><div style="height:100%;background:#38bdf8;width:'+pct+'%;transition:width 0.3s;"></div></div>'+
'<div style="font-size:13px;color:#38bdf8;margin-top:6px;">'+pct+'%</div>';
}
if(ov&&ov.style.display!=='flex')ov.style.display='flex';
}else if(!isTransferring){
if(ov)ov.style.display='none';
if(info)info.innerHTML='';
}
}catch(e){}
}
setInterval(pollServerStatus,1000);
pollServerStatus();
function toggleQueuePanel(){const p=document.getElementById('queue-panel');if(p)p.classList.toggle('open');}
function addFilesToUploadQueue(files){if(!files||!files.length)return;
for(const f of files){const isGame=/\.(nsp|nsz|xci|xcz)$/i.test(f.name);transferQueue.push({id:'up_'+Math.random().toString(36).substr(2,9),type:'upload',file:f,name:f.name,size:f.size,status:'pending',progress:0,speed:'',install:isGame,xhr:null,uploadPath:currentPath});}
updateQueueCount();renderQueue();const p=document.getElementById('queue-panel');if(p&&!p.classList.contains('open'))p.classList.add('open');document.getElementById('files').value='';}
function addSelectedToDownloadQueue(){const ch=document.querySelectorAll('.file-checkbox:checked');if(!ch.length)return;
for(const cb of ch){const p=cb.getAttribute('data-path');const dp=decodeURIComponent(p);const n=dp.split('/').pop()||'file';if(transferQueue.some(item=>item.type==='download'&&item.path===p))continue;transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:p,name:n,size:0,status:'pending',progress:0,speed:'',controller:null});}
for(const cb of ch)cb.checked=false;updateSelectCount();updateQueueCount();renderQueue();toggleQueuePanel();}
function updateQueueCount(){const b=document.getElementById('queue-toggle-btn');if(b){const active=transferQueue.filter(i=>['pending','uploading','downloading','installing'].includes(i.status)).length;const countEl=b.querySelector('.count');if(countEl)countEl.textContent='('+active+')';}}
function renderQueue(){const l=document.getElementById('queue-list');if(!l)return;l.innerHTML='';
for(const i of transferQueue){const el=document.createElement('div');el.className='queue-item '+i.status;const pct=Math.round(i.progress);const speed=i.speed?' · '+i.speed:'';const sizeStr=i.size?formatBytes(i.size):'Unknown size';
let st=i.status;if(i.status==='pending')st='Pending';else if(i.status==='uploading')st='Uploading';else if(i.status==='downloading')st='Downloading';else if(i.status==='installing')st='Installing...';else if(i.status==='completed')st='Completed';else if(i.status==='failed')st='Failed';else if(i.status==='cancelled')st='Cancelled';
let hdr='<div class="queue-item-header"><span class="queue-item-name">'+escapeHtml(i.name)+'</span>';if(['pending','uploading','downloading','installing'].includes(i.status)){hdr+='<button class="queue-item-cancel" onclick="cancelTransfer(\''+i.id+'\')">&times;</button>';}hdr+='</div>';
let inst='';if(i.type==='upload'&&i.status==='pending'&&/\.(nsp|nsz|xci|xcz)$/i.test(i.name)){inst='<label class="queue-item-install-label"><input type="checkbox" '+(i.install?'checked':'')+' onchange="toggleInstallOption(\''+i.id+'\',this.checked)">Install directly</label>';}else if(i.type==='upload'&&i.install){inst='<div style="font-size:11px;color:#c084fc">Direct Install mode</div>';}
el.innerHTML=hdr+'<div style="font-size:11px;color:#94a3b8;margin-bottom:4px">'+(i.type==='upload'?'Upload':'Download')+'</div>'+inst+'<div class="queue-item-progress-bg"><div class="queue-item-progress-fill" style="width:'+pct+'%"></div></div><div class="queue-item-meta"><span>'+pct+'%'+speed+'</span><span>'+sizeStr+'</span></div>';l.appendChild(el);}}
function toggleInstallOption(id,chk){const i=transferQueue.find(item=>item.id===id);if(i)i.install=chk;}
function formatBytes(b){if(b===0)return '0 Bytes';const k=1024;const sizes=['Bytes','KB','MB','GB'];const i=Math.floor(Math.log(b)/Math.log(k));return parseFloat((b/Math.pow(k,i)).toFixed(2))+' '+sizes[i];}
function escapeHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
function cancelTransfer(id){const i=transferQueue.find(item=>item.id===id);if(!i)return;
if(i.status==='uploading'&&i.xhr){i.xhr.abort();}else if(i.status==='downloading'&&i.controller){i.controller.abort();}
i.status='cancelled';i.progress=0;i.speed='';renderQueue();updateQueueCount();}
function clearCompletedQueue(){transferQueue=transferQueue.filter(i=>['pending','uploading','downloading','installing'].includes(i.status));renderQueue();updateQueueCount();}
async function startTransfers(){if(isTransferring)return;isTransferring=true;const btn=document.getElementById('start-transfers-btn');if(btn)btn.disabled=true;const ov=document.getElementById('transfer-overlay');if(ov)ov.style.display='flex';
try{while(true){const next=transferQueue.find(i=>i.status==='pending');if(!next)break;if(next.type==='upload')await uploadFileItem(next);else await downloadFileItem(next);updateQueueCount();renderQueue();}}finally{isTransferring=false;if(btn)btn.disabled=false;if(ov)ov.style.display='none';navigateTo(currentPath,false);}}
function uploadFileItem(item){return new Promise(res=>{item.status='uploading';renderQueue();const xhr=new XMLHttpRequest();item.xhr=xhr;let url='/upload?path='+encodeURIComponent(item.uploadPath)+'&name='+encodeURIComponent(item.name);if(item.install){url+='&install=1';item.status='installing';}xhr.open('PUT',url,true);let startTime=Date.now();let lastTime=startTime;let lastLoaded=0;
xhr.upload.addEventListener('progress',e=>{if(e.lengthComputable){const now=Date.now();item.progress=(e.loaded/e.total)*100;const diff=(now-lastTime)/1000;if(diff>=0.5){const speed=(e.loaded-lastLoaded)/diff;item.speed=formatBytes(speed)+'/s';lastTime=now;lastLoaded=e.loaded;}if(item.install&&item.progress>=99){item.status='installing';}renderQueue();}});
xhr.onload=()=>{if(xhr.status===200){item.status='completed';item.progress=100;item.speed='';}else{item.status='failed';item.speed='Error: '+(xhr.responseText||xhr.statusText);}res();};
xhr.onerror=()=>{item.status='failed';item.speed='Network error';res();};
xhr.onabort=()=>{item.status='cancelled';res();};
xhr.send(item.file);});}
async function downloadFileItem(item){item.status='downloading';renderQueue();const ctrl=new AbortController();item.controller=ctrl;try{const res=await fetch('/download?path='+item.path,{signal:ctrl.signal});if(!res.ok)throw new Error(res.statusText);const len=res.headers.get('content-length');const total=len?parseInt(len,10):0;item.size=total;const reader=res.body.getReader();let loaded=0;let chunks=[];let lastTime=Date.now();let lastLoaded=0;
while(true){const {done,value}=await reader.read();if(done)break;chunks.push(value);loaded+=value.length;if(total)item.progress=(loaded/total)*100;const now=Date.now();const diff=(now-lastTime)/1000;if(diff>=0.5){const speed=(loaded-lastLoaded)/diff;item.speed=formatBytes(speed)+'/s';lastTime=now;lastLoaded=loaded;}renderQueue();}
const blob=new Blob(chunks);const dlUrl=URL.createObjectURL(blob);const a=document.createElement('a');a.href=dlUrl;a.download=item.name;document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(dlUrl);item.status='completed';item.progress=100;item.speed='';}catch(err){if(err.name==='AbortError')item.status='cancelled';else{item.status='failed';item.speed=err.message;}}}
function toggleViewMode(){
const container=document.getElementById('items-container');
const btn=document.getElementById('view-toggle');
if(container.classList.contains('list')){
container.classList.remove('list');
container.classList.add('grid');
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='List View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='☰';
localStorage.setItem('viewMode','grid');
}else{
container.classList.remove('grid');
container.classList.add('list');
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='Grid View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='⊞';
localStorage.setItem('viewMode','list');
}
}
document.addEventListener('DOMContentLoaded',()=>{
const container=document.getElementById('items-container');
const btn=document.getElementById('view-toggle');
const saved=localStorage.getItem('viewMode');
if(saved==='grid'&&container){
container.classList.remove('list');
container.classList.add('grid');
if(btn){
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='List View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='☰';
}
}
});
async function deleteFile(e,path){e.preventDefault();e.stopPropagation();if(!await showConfirmDialog('Delete '+decodeURIComponent(path.split('/').pop())+'?'))return;const res=await fetch('/delete?path='+path,{method:'DELETE'});if(res.ok){navigateTo(currentPath,false);}else{alert('Delete failed: '+await res.text());}}
function updateSelectCount(){
const checked=document.querySelectorAll('.file-checkbox:checked');
const btn=document.getElementById('delete-selected');
if(btn){
btn.disabled=checked.length===0;
const countEl=btn.querySelector('.count');
if(countEl)countEl.textContent='('+checked.length+')';
}
const dlBtn=document.getElementById('download-selected');
if(dlBtn){
dlBtn.disabled=checked.length===0;
const countEl=dlBtn.querySelector('.count');
if(countEl)countEl.textContent='('+checked.length+')';
}
const selectAllBtn=document.getElementById('select-all-btn');
if(selectAllBtn){
const all=document.querySelectorAll('.file-checkbox');
const txt=selectAllBtn.querySelector('.text');
if(txt){
if(all.length&&checked.length===all.length){txt.textContent='Deselect All';}else{txt.textContent='Select All';}
}
}
}
function toggleSelectAll(){
const checkboxes=document.querySelectorAll('.file-checkbox');
const checked=document.querySelectorAll('.file-checkbox:checked');
const targetState=checked.length<checkboxes.length;
for(const cb of checkboxes){cb.checked=targetState;}
updateSelectCount();
}
async function deleteSelected(){
const checked=document.querySelectorAll('.file-checkbox:checked');
if(!checked.length)return;
if(!await showConfirmDialog('Delete '+checked.length+' selected files/folders?'))return;
const btn=document.getElementById('delete-selected');
if(btn)btn.disabled=true;
const status=document.getElementById('status');
let count=0;
for(const cb of checked){
count++;
status.textContent='Deleting ('+count+'/'+checked.length+')...';
const path=cb.getAttribute('data-path');
await fetch('/delete?path='+path,{method:'DELETE'});
}
status.textContent='Done';
navigateTo(currentPath,false);
}
function getFocusedItem(){return document.querySelector('.item.focused');}
function focusItem(item){if(!item)return;const prev=getFocusedItem();if(prev)prev.classList.remove('focused');item.classList.add('focused');item.scrollIntoView({block:'nearest'});}
function navigateGrid(direction){
const current=getFocusedItem();if(!current){focusItem(document.querySelector('.item'));return;}
const items=Array.from(document.querySelectorAll('.item'));
const currRect=current.getBoundingClientRect();
const currCenterX=currRect.left+currRect.width/2;const currCenterY=currRect.top+currRect.height/2;
let bestMatch=null;let minDistance=Infinity;
for(const item of items){
if(item===current)continue;
const rect=item.getBoundingClientRect();
const centerX=rect.left+rect.width/2;const centerY=rect.top+rect.height/2;
if(direction==='down'&&rect.top>=currRect.bottom-5){
const dy=centerY-currCenterY;const dx=centerX-currCenterX;const dist=dy*dy+dx*dx*5;
if(dist<minDistance){minDistance=dist;bestMatch=item;}
}else if(direction==='up'&&rect.bottom<=currRect.top+5){
const dy=currCenterY-centerY;const dx=centerX-currCenterX;const dist=dy*dy+dx*dx*5;
if(dist<minDistance){minDistance=dist;bestMatch=item;}
}
}
if(bestMatch)focusItem(bestMatch);
}
document.addEventListener('mouseover',function(e){const item=e.target.closest('.item');if(item)focusItem(item);});
document.addEventListener('dragstart',function(e){e.preventDefault();});
document.addEventListener('keydown',function(e){
const m=document.getElementById('confirm-modal');if(m&&m.style.display==='flex')return;
if(e.target.tagName==='INPUT'&&e.target.type!=='checkbox')return;
const items=Array.from(document.querySelectorAll('.item'));if(!items.length)return;
const current=getFocusedItem();if(!current){focusItem(items[0]);return;}
const isGrid=document.getElementById('items-container').classList.contains('grid');
if(e.key==='ArrowDown'){
e.preventDefault();
if(isGrid)navigateGrid('down');else{const idx=items.indexOf(current);if(idx<items.length-1)focusItem(items[idx+1]);}
}else if(e.key==='ArrowUp'){
e.preventDefault();
if(isGrid)navigateGrid('up');else{const idx=items.indexOf(current);if(idx>0)focusItem(items[idx-1]);}
}else if(e.key==='ArrowLeft'){
if(isGrid){e.preventDefault();const idx=items.indexOf(current);if(idx>0)focusItem(items[idx-1]);}
}else if(e.key==='ArrowRight'){
if(isGrid){e.preventDefault();const idx=items.indexOf(current);if(idx<items.length-1)focusItem(items[idx+1]);}
}else if(e.key===' '){
e.preventDefault();
const cb=current.querySelector('.file-checkbox');if(cb){cb.checked=!cb.checked;updateSelectCount();}
}else if(e.key==='Escape'){
e.preventDefault();
const checkboxes=document.querySelectorAll('.file-checkbox');for(const cb of checkboxes)cb.checked=false;
updateSelectCount();
}else if(e.key==='Delete'){
e.preventDefault();
const checked=document.querySelectorAll('.file-checkbox:checked');
if(checked.length>0){deleteSelected();}
else{
const path=current.querySelector('.file-checkbox')?.getAttribute('data-path');
if(path){
showConfirmDialog('Delete '+current.querySelector('.name').textContent+'?').then(async (approved)=>{
if(approved){await fetch('/delete?path='+path,{method:'DELETE'});navigateTo(currentPath,false);}
});
}
}
}else if(e.key==='Enter'){
e.preventDefault();
current.click();
}else if(e.key==='Backspace'){
e.preventDefault();
if(currentPath!=='/'){
const parts=currentPath.split('/').filter(Boolean);
parts.pop();
const parentPath='/'+parts.join('/');
navigateTo(parentPath);
}
}
});
function makeCRCTable(){
let c;const crcTable=[];
for(let n=0;n<256;n++){
c=n;
for(let k=0;k<8;k++){
c=((c&1)?(0xEDB88320^(c>>>1)):(c>>>1));
}
crcTable[n]=c;
}
return crcTable;
}
const crcTable=makeCRCTable();
function crc32(arr){
let crc=0^(-1);
for(let i=0;i<arr.length;i++){
crc=(crc>>>8)^crcTable[(crc^arr[i])&0xFF];
}
return (crc^(-1))>>>0;
}
function createZip(files){
const localHeaders=[];const centralHeaders=[];let offset=0;
for(const file of files){
const nameBytes=new TextEncoder().encode(file.name);
const dataBytes=file.data;
const crc=crc32(dataBytes);
const size=dataBytes.length;
const lh=new ArrayBuffer(30+nameBytes.length);
const lhView=new DataView(lh);
lhView.setUint32(0,0x04034b50,true);
lhView.setUint16(4,10,true);
lhView.setUint16(6,0,true);
lhView.setUint16(8,0,true);
lhView.setUint16(10,0,true);
lhView.setUint16(12,0,true);
lhView.setUint32(14,crc,true);
lhView.setUint32(18,size,true);
lhView.setUint32(22,size,true);
lhView.setUint16(26,nameBytes.length,true);
lhView.setUint16(28,0,true);
new Uint8Array(lh,30).set(nameBytes);
localHeaders.push(new Uint8Array(lh));
localHeaders.push(dataBytes);
const ch=new ArrayBuffer(46+nameBytes.length);
const chView=new DataView(ch);
chView.setUint32(0,0x02014b50,true);
chView.setUint16(4,20,true);
chView.setUint16(6,10,true);
chView.setUint16(8,0,true);
chView.setUint16(10,0,true);
chView.setUint16(12,0,true);
chView.setUint16(14,0,true);
chView.setUint32(16,crc,true);
chView.setUint32(20,size,true);
chView.setUint32(24,size,true);
chView.setUint16(28,nameBytes.length,true);
chView.setUint16(30,0,true);
chView.setUint16(32,0,true);
chView.setUint16(34,0,true);
chView.setUint16(36,0,true);
chView.setUint32(38,0,true);
chView.setUint32(42,offset,true);
new Uint8Array(ch,46).set(nameBytes);
centralHeaders.push(new Uint8Array(ch));
offset+=lh.byteLength+size;
}
const cdOffset=offset;let cdSize=0;
for(const ch of centralHeaders)cdSize+=ch.byteLength;
const eocd=new ArrayBuffer(22);
const eocdView=new DataView(eocd);
eocdView.setUint32(0,0x06054b50,true);
eocdView.setUint16(4,0,true);
eocdView.setUint16(6,0,true);
eocdView.setUint16(8,files.length,true);
eocdView.setUint16(10,files.length,true);
eocdView.setUint32(12,cdSize,true);
eocdView.setUint32(16,cdOffset,true);
eocdView.setUint16(20,0,true);
const blobParts=[];
for(const part of localHeaders)blobParts.push(part);
for(const part of centralHeaders)blobParts.push(part);
blobParts.push(new Uint8Array(eocd));
return new Blob(blobParts,{type:'application/zip'});
}
async function addSelectedToDownloadQueue(){
const checked=document.querySelectorAll('.file-checkbox:checked');
if(!checked.length)return;
const status=document.getElementById('status');
if(status)status.textContent='Preparing download queue...';
for(const cb of checked){
const path=cb.getAttribute('data-path');
const decodedPath=decodeURIComponent(path);
const name=decodedPath.split('/').pop()||'file';
const isDir=cb.closest('.item').querySelector('.meta').textContent==='folder';
if(isDir){
if(status)status.textContent='Scanning folder '+name+'...';
try{
const res=await fetch('/list-recursive?path='+path);
if(res.ok){
const nested=await res.json();
for(const f of nested){
const fName=decodeURIComponent(f.path).substring(decodeURIComponent(currentPath).length);
const cleanFName=fName.startsWith('/')?fName.substring(1):fName;
if(!transferQueue.some(item=>item.type==='download'&&item.path===f.path)){
transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:f.path,name:cleanFName,size:f.size,status:'pending',progress:0,speed:'',controller:null});
}
}
}
}catch(e){console.error(e);}
}else{
if(!transferQueue.some(item=>item.type==='download'&&item.path===path)){
let sizeVal=0;
const sizeMeta=cb.closest('.item').querySelector('.meta').textContent;
if(sizeMeta.includes('MiB'))sizeVal=parseFloat(sizeMeta)*1024*1024;
else if(sizeMeta.includes('KiB'))sizeVal=parseFloat(sizeMeta)*1024;
transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:path,name:name,size:sizeVal,status:'pending',progress:0,speed:'',controller:null});
}
}
}
for(const cb of checked)cb.checked=false;
updateSelectCount();updateQueueCount();renderQueue();
if(status)status.textContent='';
const panel=document.getElementById('queue-panel');
if(panel&&!panel.classList.contains('open'))panel.classList.add('open');
}
async function navigateTo(path,shouldPushState=true){
const status=document.getElementById('status');if(status)status.textContent='Loading...';
try{const res=await fetch('/list?path='+encodeURIComponent(path));if(!res.ok)throw new Error(res.statusText);
const data=await res.json();currentPath=data.path;
const pathDiv=document.querySelector('.path');if(pathDiv)pathDiv.textContent=data.path;
renderCrumbs(data.path);renderItems(data.path,data.entries);
if(shouldPushState){const newUrl=window.location.protocol+'//'+window.location.host+'/?path='+encodeURIComponent(data.path);window.history.pushState({path:data.path},'',newUrl);}
updateSelectCount();
const container=document.getElementById('items-container');const saved=localStorage.getItem('viewMode');const btn=document.getElementById('view-toggle');
if(saved==='grid'&&container){container.classList.remove('list');container.classList.add('grid');if(btn){
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='List View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='☰';
}}
else if(container){container.classList.remove('grid');container.classList.add('list');if(btn){
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='Grid View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='⊞';
}}
if(typeof initLightbox==='function')initLightbox();
}catch(err){alert('Failed to load folder: '+err.message);}finally{if(status)status.textContent='';}}
function renderCrumbs(path){
const container=document.querySelector('.crumbs');if(!container)return;
let html='<a href="/?path=/">SD Card</a>';
if(path!=='/'&&path!==''){
const parts=path.split('/').filter(Boolean);let accum='';
for(const part of parts){accum+='/'+part;html+=' / <a href="/?path='+encodeURIComponent(accum)+'">'+escapeHtml(part)+'</a>';}
}
container.innerHTML=html;}
function renderItems(path,entries){
const container=document.getElementById('items-container');if(!container)return;container.innerHTML='';
if(path!=='/'){
let parent=path;const lastSlash=parent.lastIndexOf('/');if(lastSlash!==-1)parent=parent.substring(0,lastSlash);if(parent==='')parent='/';
const el=document.createElement('a');el.className='item';el.href='/?path='+encodeURIComponent(parent);
el.innerHTML='<div class="thumbnail-box"><svg viewBox="0 0 24 24" fill="#ffca28"><path d="M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg></div><div class="info"><span class="name">..</span><span class="meta">parent folder</span></div>';
container.appendChild(el);
}
if(!entries||!entries.length){
const el=document.createElement('div');el.className='empty';el.textContent='Empty folder';container.appendChild(el);return;
}
for(const entry of entries){
let child=path;if(!child.endsWith('/'))child+='/';child+=entry.name;const encChild=encodeURIComponent(child);const nameEsc=escapeHtml(entry.name);
const el=document.createElement('a');el.className='item';let thumb='';
if(entry.type===0){
el.href='/?path='+encChild;
thumb='<svg viewBox="0 0 24 24" fill="#ffca28"><path d="M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg>';
}else{
const ext=entry.name.split('.').pop().toLowerCase();const isImage=['png','jpg','jpeg','gif','bmp'].includes(ext);
el.href=isImage?('/view?path='+encChild):('/download?path='+encChild);
if(isImage)thumb='<img class="thumb" src="/view?path='+encChild+'" alt="" loading="lazy">';
else thumb='<svg viewBox="0 0 24 24" fill="#90a4ae"><path d="M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z"/></svg>';
}
let metaStr='folder';if(entry.type!==0){
if(entry.size>=1024*1024)metaStr=(entry.size/(1024*1024)).toFixed(2)+' MiB';
else metaStr=(entry.size/1024).toFixed(2)+' KiB';
}
el.innerHTML='<input type="checkbox" class="file-checkbox" data-path="'+encChild+'" onclick="event.stopPropagation(); updateSelectCount();">';
el.innerHTML+='<div class="thumbnail-box">'+thumb+'</div>';
el.innerHTML+='<div class="info"><span class="name">'+nameEsc+'</span><span class="meta">'+metaStr+'</span><button class="delete-btn" onclick="deleteFile(event,\''+encChild+'\')">&times;</button></div>';
container.appendChild(el);
}}
document.addEventListener('click',e=>{
const a=e.target.closest('a');if(!a)return;
if(e.target.tagName==='INPUT'||e.target.tagName==='BUTTON'||e.target.closest('.delete-btn'))return;
const href=a.getAttribute('href');
if(href&&href.startsWith('/?path=')){
e.preventDefault();const url=new URL(href,window.location.origin);const path=url.searchParams.get('path')||'/';navigateTo(path);
}
});
window.addEventListener('popstate',e=>{
const url=new URL(window.location.href);const path=url.searchParams.get('path')||'/';navigateTo(path,false);
});
</script>
)HTML";

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
            if (!g_share_running.load()) {
                return -1;
            }
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
                if (idle_count > IDLE_TIMEOUT_MS) {
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

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    u32 idle_count = 0;
    while (offset < content_length) {
        if (!g_share_running.load()) {
            g_upload_state.active.store(false);
            return;
        }
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
            if (idle_count > IDLE_TIMEOUT_MS) {
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

    upload_guard.success = true;
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

constexpr std::string_view PROGRESS_PAGE = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sphaira Progress</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f0f12;color:#e2e8f0;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:24px;box-sizing:border-box}
.card{max-width:420px;width:100%;text-align:center}
h1{font-size:20px;margin:0 0 24px}
.name{font-size:15px;color:#94a3b8;word-break:break-all;margin-bottom:16px;min-height:20px}
.bar-bg{height:14px;background:rgba(255,255,255,0.08);border-radius:7px;overflow:hidden}
.bar-fill{height:100%;background:#38bdf8;width:0%;transition:width 0.3s ease}
.pct{margin-top:12px;font-size:28px;font-weight:600}
.idle{color:#64748b;font-size:15px}
</style></head><body>
<div class="card">
<h1>Sphaira Progress</h1>
<div id="content"><div class="idle">Waiting for activity&hellip;</div></div>
</div>
<script>
async function poll(){
try{
const res=await fetch('/status');
const s=await res.json();
const c=document.getElementById('content');
if(!s.active){c.innerHTML='<div class="idle">No transfer in progress</div>';return;}
const pct=s.total>0?Math.min(100,Math.round((s.bytes/s.total)*100)):0;
c.innerHTML='<div class="name">'+s.name.replace(/[&<>]/g,ch=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[ch]))+'</div>'+
'<div class="bar-bg"><div class="bar-fill" style="width:'+pct+'%"></div></div>'+
'<div class="pct">'+pct+'%</div>';
}catch(e){}
}
poll();
setInterval(poll,1000);
</script>
</body></html>
)HTML";

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
    body += "<title>Sphaira Album</title>";
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
    body += "</style></head><body><header><div class=\"header-top\"><h1>Sphaira Album</h1><a href=\"/files?path=/\" class=\"header-link-btn\" style=\"text-decoration:none;\"><button><span class=\"icon\">📁</span> <span class=\"text\">File Browser</span></button></a></div></header>";

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
