#pragma once

#include <switch.h>
#include <string>
#include <string_view>

namespace sphaira::web::detail {

using Socket = int;

constexpr size_t HTTP_READ_LIMIT = 16384;
constexpr size_t HTTP_FILE_CHUNK = 1024 * 512;
constexpr u32 IDLE_TIMEOUT_MS = 30000;

auto PathExtension(std::string_view path) -> std::string_view;
auto ExtensionEquals(std::string_view a, std::string_view b) -> bool;
auto ContentTypeForPath(std::string_view path) -> const char*;
auto HtmlEscape(std::string_view in) -> std::string;
auto UrlEncode(std::string_view in) -> std::string;
auto HexValue(char c) -> int;
auto UrlDecode(std::string_view in) -> std::string;
auto GetQueryValue(std::string_view query, std::string_view key) -> std::string;
auto SplitPathAndQuery(std::string path, std::string& query) -> std::string;
auto CanonicalizeAbsolutePath(std::string path) -> std::string;
auto SanitizeFileName(std::string name) -> std::string;
auto SendAll(Socket sock, const void* buf, size_t size) -> bool;
auto SendString(Socket sock, const std::string& str) -> bool;
void SendResponse(Socket sock, const char* status, const char* content_type, const std::string& body);
auto ReadHttpRequest(Socket sock, std::string& out, bool* header_too_large = nullptr) -> bool;
auto HeaderValue(const std::string& req, std::string_view name) -> std::string;
auto JsonEscape(std::string_view in) -> std::string;
auto IsImagePath(std::string_view name) -> bool;
void AppendLightbox(std::string& body);
void AppendConfirmModal(std::string& body);

} // namespace sphaira::web::detail
