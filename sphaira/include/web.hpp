#pragma once

#include "fs.hpp"
#include "option.hpp"
#include <switch.h>
#include <string>
#include <vector>

namespace sphaira {

namespace ui {
class ProgressBox;
}

struct WebShareResult {
    std::string url{};
    std::string ip_url{}; // the same page by IP: many Android phones and PCs cannot resolve kefir.local

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
// an NVG image of the QR code for text (version 4: up to about 78 bytes); the caller deletes it
auto CreateQrImage(const std::string& text) -> int;
auto WebShareFolder(const fs::FsPath& path, WebShareResult& out) -> Result;

// brings the http server up without touching the mounted folder list, and
// returns the url (plus qr) for one of its pages, e.g. "/apikey".
auto WebStartServer(const std::string& page_path, WebShareResult& out) -> Result;
void WebShareStop();
WebUploadState WebGetUploadState();
bool WebShareIsRunning();
// Settings > Network > Web server: called every frame by the main loop. Starts the server when the console is
// online, keeps it bound across sleep and drops it when the address changes. Does nothing when the option is off.
void WebShareBackgroundTick();
// Settings > Network > Upload connections: 0 = off (one connection), 2/3/4/6 = that many parts in parallel, 7 = Auto (the page picks, see web_upload_conns.hpp)
option::OptionLong& WebUploadConnsOption();
void WebSetProgressBox(ui::ProgressBox* pbox);
ui::ProgressBox* WebGetProgressBox();
// the second line of a server window: "Or open: kefir.local/input" (the QR and the first line use the IP); empty when there is no other name
auto WebAltLine(const WebShareResult& share) -> std::string;
// alt_line: shown under the address (see WebAltLine)
void WebPushServerProgressBox(const std::string& url, int qr_image, const std::string& title, const std::string& alt_line = {});

} // namespace sphaira
