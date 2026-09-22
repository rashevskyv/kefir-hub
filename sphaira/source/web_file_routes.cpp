#include "web_file_routes.hpp"
#include "web_fs.hpp"
#include "web_http.hpp"
#include "web_pages.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "ui/menus/homebrew.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira {
using namespace webpages;
namespace {

using namespace web::detail;

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

} // namespace

using namespace web::detail;

auto BuildFolderPage(std::string path_str) -> std::string {
    const auto mounts = GetMountRoots();
    // With one app mount, the server root is that mount rather than a source
    // selector; there is nowhere useful for its parent link to go.
    const bool has_root = mounts.size() != 1 && GetRootSources().size() > 1;
    if (path_str.empty()) {
        if (mounts.size() == 1) {
            path_str = mounts.front();
        } else if (has_root) {
            return BuildRootSelectionPage(GetRootSources());
        } else {
            path_str = "/";
        }
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
        ui::menu::homebrew::NotifyDirectoryDeleted(path);
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

    ui::menu::homebrew::NotifyFileDeleted(path);
    SendResponse(sock, "200 OK", "text/plain", "Deleted");
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

} // namespace sphaira
