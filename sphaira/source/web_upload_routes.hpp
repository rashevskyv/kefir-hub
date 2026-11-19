#pragma once

#include "web_http.hpp"
#include "fs.hpp"
#include "ftpsrv_helper.hpp"
#include "yati/source/stream.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira {

void HandleUploadManifest(web::detail::Socket sock, const std::string& req);
void ReceiveUpload(web::detail::Socket sock, const std::string& req, const std::string& query);

// the web install of one package: queues it in the Web install session, runs yati on the source
// make_source returns, and reports the HTTP status and text to answer with.
struct DirectInstallResult {
    const char* status;
    std::string message;
};
auto RunDirectInstall(const std::string& name, s64 content_length, const std::string& item_id,
                      const std::function<std::unique_ptr<yati::source::Stream>()>& make_source) -> DirectInstallResult;

// multi-connection upload (web_part_upload.cpp): /upload?part=...
void HandlePartUpload(web::detail::Socket sock, const std::string& req, const std::string& method, const std::string& query);

// reads one part body (Content-Length len) into buf; nullopt = ok, status null = the server is stopping
struct PartBodyError {
    const char* status;
    const char* message;
};
auto ReadPartBody(web::detail::Socket sock, const std::string& req, s64 len, std::vector<u8>& buf) -> std::optional<PartBodyError>;

// web_part_install.cpp: the same parts, but the package is installed while it arrives
void HandleInstallCreate(web::detail::Socket sock, const std::string& query);
void HandleInstallPut(web::detail::Socket sock, const std::string& req, const std::string& query, const std::string& id);
void HandleInstallCancel(web::detail::Socket sock, const std::string& id);
void HandleInstallState(web::detail::Socket sock, const std::string& id);
auto IsInstallPartId(const std::string& id) -> bool;

// one upload or install at a time; the part upload holds it from "part=new" to "done"
extern std::atomic_bool g_transfer_busy;

// held for the whole web transfer (with g_transfer_busy): FTP closes its listener meanwhile,
// an open one cuts the receive speed of every other socket 3-6x. resumes once, when destroyed.
struct FtpPause {
    FtpPause() { ftpsrv::PauseForTransfer(true); }
    ~FtpPause() { ftpsrv::PauseForTransfer(false); }
    FtpPause(const FtpPause&) = delete;
    FtpPause& operator=(const FtpPause&) = delete;
};
auto UniqueUploadPath(fs::Fs& sd, const fs::FsPath& dir, const std::string& name) -> fs::FsPath;

} // namespace sphaira
