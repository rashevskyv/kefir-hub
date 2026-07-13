#include "web_http.hpp"
#include "web.hpp"
#include "web_pages.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/socket.h>
#include <errno.h>

namespace sphaira::web::detail {

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

// Note: This UrlDecode is implemented manually for the lightweight embedded web server
// to avoid introducing libcurl dependency to the web server component.
// It differs from MountCurlDevice::url_decode, which uses curl_unescape and html_decode.
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

auto SendAll(Socket sock, const void* buf, size_t size) -> bool {
    auto data = static_cast<const char*>(buf);
    u32 idle_count = 0;

    while (size) {
        if (!sphaira::WebShareIsRunning()) {
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

auto IsImagePath(std::string_view name) -> bool {
    const auto ext = PathExtension(name);
    return ExtensionEquals(ext, "png") || ExtensionEquals(ext, "jpg") || 
           ExtensionEquals(ext, "jpeg") || ExtensionEquals(ext, "gif") || 
           ExtensionEquals(ext, "bmp");
}

void AppendLightbox(std::string& body) {
    body += webpages::LIGHTBOX_CONTENT;
}

void AppendConfirmModal(std::string& body) {
    body += webpages::CONFIRM_MODAL_HTML;
}

} // namespace sphaira::web::detail
