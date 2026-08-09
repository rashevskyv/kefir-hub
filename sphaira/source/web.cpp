#include "web.hpp"
#include "path_util.hpp"
#include "web_http.hpp"
#include "web_screenshots.hpp"
#include "web_pages.hpp"
#include "web_qr.hpp"
#include "web_upload.hpp"
#include "log.hpp"
#include "app.hpp"
#include "net.hpp"
#include "i18n.hpp"
#include "defines.hpp"
#include "title_info.hpp"
#include "location.hpp"
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
#include "ui/steamgriddb_icon.hpp"
#include "ui/menus/homebrew.hpp"
#include "yati/yati.hpp"
#include "yati/source/stream.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace sphaira {
using namespace webpages;
namespace {

using namespace web::detail;

constexpr u16 SHARE_PORT_FIRST = 8080;
constexpr u16 SHARE_PORT_LAST = 8090;
// Multiple worker threads accept() on the same listening socket so a status
// poll (e.g. from a second device) can still be served while another thread
// is blocked handling a long upload/install request.
constexpr size_t SHARE_WORKER_COUNT = 3;
// seconds without an ip before the server gives up and closes itself.
constexpr unsigned SHARE_OFFLINE_GIVEUP = 30;

Thread g_share_threads[SHARE_WORKER_COUNT]{};
size_t g_share_thread_count{};
std::atomic_bool g_share_running{false};
std::atomic_bool g_share_self_test{false};
std::atomic<Socket> g_share_socket{-1};
u16 g_share_port{};
// the ip the listener was bound under, and the applet-hook resume counter it
// was last checked against. see TickShareNetwork().
u32 g_share_ip{};
u32 g_share_resume_gen{};



// Serializes write operations (upload/install) across the worker threads. Read
// operations (browse/status/download/view) stay concurrent; only one transfer
// touches the shared g_upload_state / install pipeline at a time.
std::atomic_bool g_transfer_busy{false};


// the mounted folders, canonicalised. read fresh on every request: the mounts
// belong to the app (App::SetMountedFolders), not to this server, so mounting
// from the file browser takes effect on a server that is already running --
// including one started from Tools, which serves the whole card and never sets
// a mount of its own.
//
// the card root is not a mount: it is what the server already serves, so
// mounting it would only produce a second way to reach the same listing.
auto GetMountRoots() -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const auto& p : App::GetMountedFolders()) {
        const auto raw = p.toString();
        if (raw.empty()) {
            continue;
        }

        auto clean = CanonicalizeAbsolutePath(raw);
        if (clean == "/") {
            continue;
        }

        out.push_back(std::move(clean));
    }
    return out;
}

// the first mount, or empty. only for the one spot that needs a single default:
// the upload target when the request names no folder.
auto GetMountRoot() -> std::string {
    const auto roots = GetMountRoots();
    return roots.empty() ? std::string{} : roots.front();
}

struct RootSource {
    std::string path; // "/", "/config", "ums0:/"
    std::string name;
    std::string meta;
};

// what the root lists, mirroring the file browser's own root: the card plus
// whatever is mounted. network locations (WebDAV / SMB) are left out because
// they are only devoptab-mounted while the browser is sitting inside them --
// listing them here would be links that fail the moment the user leaves.
auto GetRootSources() -> std::vector<RootSource> {
    std::vector<RootSource> out;

    for (const auto& mount : GetMountRoots()) {
        const char* leaf = std::strrchr(mount.c_str(), '/');
        out.push_back({mount, (leaf && leaf[1]) ? leaf + 1 : "Mounted Folder", "Mounted Folder (" + mount + ")"});
    }

    for (const auto& e : location::GetStdio(false)) {
        out.push_back({e.mount + "/", e.name, "USB storage"});
    }

    for (const auto& e : location::GetMtpHostDevices(false)) {
        out.push_back({e.mount + "/", e.name, "MTP device"});
    }

    out.push_back({"/", "sd", "microSD Card"});
    return out;
}

// which source a path lives in: the longest source root containing it. drives
// the "up" link, so leaving a source lands on the root page rather than on the
// card folder that happens to sit above a mount.
auto SourceRootFor(const std::string& path) -> std::string {
    std::string best = "/";
    for (const auto& s : GetRootSources()) {
        if (s.path == "/" || s.path.size() <= best.size()) {
            continue;
        }
        if (path == s.path || path.starts_with(s.path.back() == '/' ? s.path : s.path + "/")) {
            best = s.path;
        }
    }
    return best;
}

auto SourceNameFor(const std::string& root) -> std::string {
    for (const auto& s : GetRootSources()) {
        if (s.path == root) {
            return s.name;
        }
    }
    return root;
}

// a devoptab prefix ("ums0:/…") names a mounted source and is served through
// stdio; everything else is a plain microSD path.
auto OpenFs(std::string_view path) -> std::unique_ptr<fs::Fs> {
    if (path.find(':') != std::string_view::npos) {
        return std::make_unique<fs::FsStdio>();
    }
    return std::make_unique<fs::FsNativeSd>();
}

// the landing page when more than the card is mounted. deliberately
// script-free, so every link here is a full page load and comes back through
// the server rather than through the client-side router.
auto BuildRootSelectionPage(const std::vector<RootSource>& sources) -> std::string {
    std::string body;
    body.reserve(8192);
    body += FOLDER_PAGE_HEADER;
    body += "<div class=\"header-top\"><h1>Kefir Hub Files</h1><a href=\"/album\" style=\"text-decoration:none;\"><button><span class=\"icon\">📸</span> <span class=\"text\">Screenshots</span></button></a></div>";
    body += "<div class=\"crumbs\"><a href=\"/\">Root</a></div></header>";
    body += "<div class=\"container\"><main id=\"items-container\" class=\"list\">";

    for (const auto& s : sources) {
        const bool is_sd = s.path == "/";
        body += "<a class=\"item\" href=\"/?path=";
        body += UrlEncode(s.path);
        body += "\"><div class=\"thumbnail-box\">"
                "<svg viewBox=\"0 0 24 24\" fill=\"";
        body += is_sd ? "#4ade80" : "#38bdf8";
        body += "\"><path d=\"M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg>"
                "</div>";
        body += "<div class=\"info\"><span class=\"name\">";
        body += HtmlEscape(s.name);
        body += "</span><span class=\"meta\">";
        body += HtmlEscape(s.meta);
        body += "</span></div></a>";
    }

    body += "</main></div></body></html>";
    return body;
}

auto BuildFolderPage(std::string path_str) -> std::string {
    const auto sources = GetRootSources();
    // one source is not a choice: with only the card mounted the root page is a
    // click in the way, so the card *is* the root.
    const bool has_root = sources.size() > 1;

    if (path_str.empty()) {
        if (has_root) {
            return BuildRootSelectionPage(sources);
        }
        path_str = "/";
    }
    const auto abs_path = CanonicalizeAbsolutePath(path_str);
    const auto source_root = SourceRootFor(abs_path);

    auto fsp = OpenFs(abs_path);
    auto& fs = *fsp;
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

    std::string body;
    body.reserve(24576 + entries.size() * 512);

    body += FOLDER_PAGE_HEADER;
    body += "<div class=\"header-top\"><h1>Kefir Hub Files</h1><a href=\"/album\" style=\"text-decoration:none;\"><button><span class=\"icon\">📸</span> <span class=\"text\">Screenshots</span></button></a></div><div class=\"crumbs\"><a href=\"/\">Root</a>";

    // the crumb trail starts at the source root ("ums0:/" / "/config"), then
    // walks the path inside it, so a device prefix never gets split into a
    // crumb of its own that links nowhere.
    std::string crumb_accum = source_root == "/" ? "" : source_root;
    size_t start = crumb_accum.size();
    if (!crumb_accum.empty()) {
        body += " / <a href=\"/?path=";
        body += UrlEncode(crumb_accum);
        body += "\">";
        body += HtmlEscape(SourceNameFor(source_root));
        body += "</a>";
    }
    while (start < abs_path.size()) {
        const auto end = abs_path.find('/', start);
        const auto part = abs_path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            if (crumb_accum.empty() || crumb_accum.back() != '/') {
                crumb_accum += '/';
            }
            crumb_accum += part;
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

    // "up" out of a source root goes to the root page; there is no "up" at all
    // when the card is the only source and we are sitting at its top.
    if (abs_path != source_root || has_root) {
        std::string parent_href = "/";
        if (abs_path != source_root) {
            auto parent = abs_path;
            if (const auto slash = parent.find_last_of('/'); slash != std::string::npos) {
                parent.resize(slash);
            }
            if (parent.size() < source_root.size()) {
                parent = source_root;
            }
            parent_href = "/?path=" + UrlEncode(parent);
        }

        body += "<a class=\"item\" href=\"";
        body += parent_href;
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

    // both are consumed by the client-side router in FOLDER_PAGE_JS. quoted as
    // json rather than url-encoded: UrlEncode() turns a space into '+', which
    // decodeURIComponent() leaves as a literal '+', so any path with a space in
    // it used to come out mangled -- and shareRoot has to compare equal to
    // currentPath for the "up" link out of the mount to be found.
    // consumed by the client-side router in FOLDER_PAGE_JS: srcRoot is the top
    // of the source we are in, hasRoot says whether there is a root page above
    // it. quoted as json rather than url-encoded -- UrlEncode() turns a space
    // into '+', which decodeURIComponent() leaves as a literal '+', and srcRoot
    // has to compare equal to currentPath for the "up" link to be found.
    body += "<script>let currentPath=\"";
    body += JsonEscape(abs_path);
    body += "\";let srcRoot=\"";
    body += JsonEscape(source_root);
    body += "\";let hasRoot=";
    body += has_root ? "true" : "false";
    body += ";";
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

    auto fsp = OpenFs(path);
    auto& fs = *fsp;
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

    auto fsp = OpenFs(path);
    auto& fs = *fsp;
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




auto UniqueUploadPath(fs::Fs& sd, const fs::FsPath& dir, const std::string& name) -> fs::FsPath {
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
    const auto raw_name = GetQueryValue(query, "name");
    const auto name = SanitizeFileName(raw_name);

    // homebrew (.nro) is routed to /switch/<name>/ so it lands in the homebrew
    // menu, mirroring the MTP/USB root-drop behaviour.
    const bool is_nro = path::EqualsIC(path::Extension(name), "nro");
    std::string dir;
    if (is_nro) {
        auto stem = name;
        if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
            stem.resize(dot);
        }
        dir = "/switch/" + stem;
    } else {
        dir = CanonicalizeAbsolutePath(raw_path.empty() ? GetMountRoot() : raw_path);
    }

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
        
        const auto ext = path::Extension(name);
        const bool is_compressed = path::EqualsIC(ext, "nsz") || path::EqualsIC(ext, "xcz") || path::EqualsIC(ext, "ncz");
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

    // the upload target follows the folder the browser is in, which may be a
    // mounted source rather than the card.
    auto fsp = OpenFs(dir);
    auto& fs = *fsp;
    fs::FsPath out_path;
    if (is_nro) {
        fs.CreateDirectoryRecursively(dir); // ensure /switch/<name>/ exists.
        // replace any previous copy rather than adding a "name (1).nro"
        // sibling, which would show up as a duplicate homebrew entry.
        out_path = fs::AppendPath(dir, name);
        fs.DeleteFile(out_path);
    } else if (!fs.DirExists(dir)) {
        SendResponse(sock, "404 Not Found", "text/plain", "Upload folder not found");
        return;
    } else {
        out_path = UniqueUploadPath(fs, dir, name);
    }

    if (auto rc = fs.CreateFile(out_path, content_length, 0); R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not create file");
        return;
    }

    struct UploadGuard {
        fs::Fs& fs;
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

    if (is_nro) {
        fs.Commit();
        log_write("[WEB] installed homebrew to %s\n", out_path.s);
        // the homebrew menu rescans /switch on its next frame.
        ui::menu::homebrew::SignalChange();
        SendResponse(sock, "200 OK", "text/plain", "Installed");
        return;
    }

    SendResponse(sock, "200 OK", "text/plain", "Uploaded");
}

void HandleDelete(Socket sock, const std::string& query) {
    const auto raw_path = GetQueryValue(query, "path");
    if (raw_path.empty()) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Missing file path");
        return;
    }

    const auto path = CanonicalizeAbsolutePath(raw_path);

    auto sdp = OpenFs(path);
    auto& sd = *sdp;
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


void ScanDirectoryRecursive(fs::Fs& fs, const std::string& start_path, std::vector<std::pair<std::string, s64>>& out_files) {
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

    auto fsp = OpenFs(path);
    auto& fs = *fsp;
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
    const auto path = CanonicalizeAbsolutePath(raw_path.empty() ? GetMountRoot() : raw_path);

    auto fsp = OpenFs(path);
    auto& fs = *fsp;
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

auto CreateShareListener(u16 port) -> Socket {
    const auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        log_write("[WEB] socket() failed for port %u: %d %s\n", port, errno, std::strerror(errno));
        return -1;
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
        log_write("[WEB] bind() failed for port %u: %d %s\n", port, errno, std::strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 4) < 0) {
        log_write("[WEB] listen() failed for port %u: %d %s\n", port, errno, std::strerror(errno));
        close(sock);
        return -1;
    }

    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    return sock;
}

// Called once a second from the server's progress box -- the one thread that
// owns the server's lifetime -- so it needs no locking of its own. Returns
// false when the server should be stopped.
//
// A sleep takes the network interface down with it: the listening socket
// survives the wake as a valid fd that is no longer attached to anything, so
// accept() just goes quiet and the server is unreachable while looking healthy
// from here. Same address after the wake means the url and qr on screen still
// point here and a fresh bind() is enough. A different address (or none at all)
// means they do not, and there is nothing worth keeping alive.
TimeStamp g_share_net_ts{};
unsigned g_share_offline{};

auto TickShareNetwork() -> bool {
    if (g_share_net_ts.GetMs() < 1000) {
        return true;
    }
    g_share_net_ts.Update();

    u32 ip{};
    if (R_FAILED(nifmGetCurrentIpAddress(&ip)) || !ip) {
        // wi-fi re-associates a few seconds after a wake, so being offline is
        // only conclusive once it has had time to come back.
        if (++g_share_offline < SHARE_OFFLINE_GIVEUP) {
            return true;
        }

        log_write("[WEB] no ip for %us, stopping server\n", g_share_offline);
        App::Notify("Web server stopped: the console went offline"_i18n);
        return false;
    }
    g_share_offline = 0;

    if (ip != g_share_ip) {
        log_write("[WEB] ip changed %08X -> %08X, stopping server\n", g_share_ip, ip);
        App::Notify("Web server stopped: the console's IP address changed"_i18n);
        return false;
    }

    const auto gen = net::ResumeGeneration();
    if (gen == g_share_resume_gen) {
        return true;
    }
    g_share_resume_gen = gen;

    const auto old = g_share_socket.exchange(-1);
    if (old >= 0) {
        shutdown(old, SHUT_RDWR);
        close(old);
    }

    // same port, so the url and qr code already on screen stay valid.
    const auto sock = CreateShareListener(g_share_port);
    if (sock < 0) {
        log_write("[WEB] rebind failed on port %u, stopping server\n", g_share_port);
        return false;
    }

    g_share_socket = sock;
    log_write("[WEB] listener rebound on port %u after resume\n", g_share_port);
    return true;
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
            // EBADF means a rebind is swapping the listener under us: back off
            // like any other hard error and pick the new fd up next time round.
            svcSleepThread(errno == EWOULDBLOCK || errno == EAGAIN ? 5'000'000 : 50'000'000);
            continue;
        }

        fcntl(client, F_SETFL, fcntl(client, F_GETFL) | O_NONBLOCK);
        TuneShareSocket(client);
        HandleRequest(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}

auto TestShareServerLoopback(u16 port) -> bool {
    const auto client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        log_write("[WEB] loopback socket() failed: %d %s\n", errno, std::strerror(errno));
        return false;
    }
    ON_SCOPE_EXIT(close(client));

    const timeval timeout{2, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(client, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_write("[WEB] loopback connect() failed on port %u: %d %s\n", port, errno, std::strerror(errno));
        return false;
    }

    constexpr std::string_view request{"GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"};
    if (!SendAll(client, request.data(), request.size())) {
        log_write("[WEB] loopback request send failed on port %u\n", port);
        return false;
    }

    char response[64]{};
    const auto received = recv(client, response, sizeof(response) - 1, 0);
    const bool passed = received > 0 && std::string_view{response, static_cast<size_t>(received)}.starts_with("HTTP/1.1 200");
    log_write("[WEB] loopback listener self-test %s on port %u\n", passed ? "passed" : "failed", port);
    return passed;
}

auto StartShareServer() -> Result {
    if (g_share_running) {
        R_SUCCEED();
    }

    for (u16 port = SHARE_PORT_FIRST; port <= SHARE_PORT_LAST; port++) {
        const auto sock = CreateShareListener(port);
        if (sock < 0) {
            continue;
        }

        g_share_socket = sock;
        g_share_port = port;
        g_share_offline = 0;
        g_share_resume_gen = net::ResumeGeneration();
        g_share_net_ts.Update();
        nifmGetCurrentIpAddress(&g_share_ip);
        g_share_running = true;

        const size_t target_worker_count = App::IsApplet() ? 2 : SHARE_WORKER_COUNT;
        size_t started = 0;
        for (; started < target_worker_count; started++) {
            Result rc = utils::CreateThread(&g_share_threads[started], ShareThreadFunc, nullptr, 1024 * 128, PRIO_PREEMPTIVE);
            if (R_SUCCEEDED(rc)) {
                rc = threadStart(&g_share_threads[started]);
                if (R_FAILED(rc)) {
                    threadClose(&g_share_threads[started]);
                }
            }
            if (R_FAILED(rc)) {
                log_write("[WEB] failed to start worker %u/%u: 0x%X\n",
                    static_cast<unsigned>(started + 1), static_cast<unsigned>(target_worker_count), rc);
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
        g_share_self_test = TestShareServerLoopback(port);
        log_write("[WEB] listening on port %u with %u worker(s), mode=%s\n", port,
            static_cast<unsigned>(started), App::IsApplet() ? "applet" : "title");
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



auto WebStartServer(const std::string& page_path, WebShareResult& out) -> Result {
    u32 ip{};
    R_TRY(nifmGetCurrentIpAddress(&ip));
    R_UNLESS(ip != 0, Result_FsNotActive);

    // note: this only brings the server up (and is a no-op if it already is).
    // what the root page shows comes from App::GetMountedFolders(), so stopping
    // and starting the server never disturbs a mount, and a mount made while the
    // server runs takes effect on the next request.
    R_TRY(StartShareServer());

    char url[128]{};
    std::snprintf(url, sizeof(url), "http://%u.%u.%u.%u:%u",
        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, g_share_port);

    out.url = std::string{url} + page_path;
    out.qr_image = CreateQrImage(out.url);
    out.listener_self_test = g_share_self_test;

    R_SUCCEED();
}

auto WebShareFolder(const fs::FsPath& path, WebShareResult& out) -> Result {
    R_UNLESS(!path.empty(), Result_FsEmpty);

    fs::FsNativeSd fs;
    R_UNLESS(fs.DirExists(path), Result_FsInvalidType);

    R_TRY(WebStartServer({}, out));

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
        g_share_self_test = false;
    }
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
    App::PopToMenu();
    // Route the server box through the detached-transfer path (like MTP) instead
    // of pushing it as a blocking widget. This grants it the same UX as other
    // transfers: L3 minimises it to a corner badge (so the menu stays usable
    // while the server / an install runs) and B / Stop cancels it.
    App::PushTransfer(std::make_unique<ui::ProgressBox>(qr_image, title, url,
        [url](ui::ProgressBox* pbox) -> Result {
            pbox->NewTransferForce(App::IsApplet()
                ? "Applet Mode: keep this screen open; use the same non-guest Wi-Fi. Press B to stop."_i18n
                : "Press B to Stop Server"_i18n);
            WebSetProgressBox(pbox);
            ON_SCOPE_EXIT(WebSetProgressBox(nullptr));
            std::string last_name;
            while (!pbox->ShouldExit() && WebShareIsRunning() && TickShareNetwork()) {
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
                        pbox->NewTransferForce(App::IsApplet()
                            ? "Applet Mode: keep this screen open; use the same non-guest Wi-Fi. Press B to stop."_i18n
                            : "Press B to Stop Server"_i18n);
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
    ));
}

bool WebShareIsRunning() {
    return g_share_running.load();
}

} // namespace sphaira
