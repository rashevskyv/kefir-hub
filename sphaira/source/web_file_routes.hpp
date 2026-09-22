#pragma once

#include "web_http.hpp"
#include <string>

namespace sphaira {

auto BuildFolderPage(std::string path_str) -> std::string;
void SendDownload(web::detail::Socket sock, const std::string& rel);
void SendView(web::detail::Socket sock, const std::string& rel);
void HandleDelete(web::detail::Socket sock, const std::string& query);
void HandleListRecursive(web::detail::Socket sock, const std::string& query);
void HandleList(web::detail::Socket sock, const std::string& query);

} // namespace sphaira
