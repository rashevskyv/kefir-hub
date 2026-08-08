#pragma once

#include "fs.hpp"
#include <switch.h>
#include <string>
#include <vector>

namespace sphaira {

namespace ui {
class ProgressBox;
}

struct WebShareResult {
    std::string url{};
    int qr_image{};
    bool listener_self_test{};
};

struct WebUploadState {
    bool active{};
    std::string name{};
    s64 bytes{};
    s64 total{};
};

auto WebShow(const std::string& url) -> Result;
auto WebShareFolder(const fs::FsPath& path, WebShareResult& out) -> Result;

// brings the http server up without touching the mounted folder list, and
// returns the url (plus qr) for one of its pages, e.g. "/apikey".
auto WebStartServer(const std::string& page_path, WebShareResult& out) -> Result;
void WebShareStop();
WebUploadState WebGetUploadState();
bool WebShareIsRunning();
void WebSetProgressBox(ui::ProgressBox* pbox);
ui::ProgressBox* WebGetProgressBox();
void WebPushServerProgressBox(const std::string& url, int qr_image, const std::string& title);

} // namespace sphaira
