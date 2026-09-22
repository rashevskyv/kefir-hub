#pragma once

#include "web_http.hpp"
#include <string>

namespace sphaira {

void HandleUploadManifest(web::detail::Socket sock, const std::string& req);
void ReceiveUpload(web::detail::Socket sock, const std::string& req, const std::string& query);

} // namespace sphaira
