#pragma once

#include "web_http.hpp"
#include "fs.hpp"
#include <atomic>
#include <string>

namespace sphaira {

void HandleUploadManifest(web::detail::Socket sock, const std::string& req);
void ReceiveUpload(web::detail::Socket sock, const std::string& req, const std::string& query);

// multi-connection upload (web_part_upload.cpp): /upload?part=...
void HandlePartUpload(web::detail::Socket sock, const std::string& req, const std::string& method, const std::string& query);

// one upload or install at a time; the part upload holds it from "part=new" to "done"
extern std::atomic_bool g_transfer_busy;
auto UniqueUploadPath(fs::Fs& sd, const fs::FsPath& dir, const std::string& name) -> fs::FsPath;

} // namespace sphaira
