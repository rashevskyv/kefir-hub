// Multi-connection web install: the browser sends a package in parts over several connections,
// yati reads it in order through a PartBuffer window.
//   POST /upload?part=new&install=1&id=<item>&name=&size=N   answers {"conns":n} at once, then installs on this worker
//   PUT  /upload?part=<item>&offset=N                        a part (same PUT as the file upload)
//   GET  /upload?part=<item>&state=1                         {"state":"running|ok|failed","message":..}
// Installing is one at a time (g_transfer_busy), like the single connection install.

#include "web_upload_routes.hpp"
#include "web_upload.hpp"
#include "web.hpp"
#include "web_part_buffer.hpp"
#include "web_part_range.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira {
namespace {

using namespace web::detail;

// parts waiting for yati: ahead of the reader by at most this much (sender slowed by TCP beyond it)
constexpr s64 WINDOW = 6 * 1024 * 1024;

enum State : int { Idle, Running, Ok, Failed };

std::mutex g_mutex; // guards g_buf, g_id, g_message, g_total
std::shared_ptr<PartBuffer> g_buf;
std::string g_id;
std::string g_message;
s64 g_total{};
std::atomic<int> g_state{Idle};
std::atomic<bool> g_cancel{false}; // the page asked to stop: the reader gives up at once

auto Aborted() -> bool {
    if (!WebShareIsRunning()) {
        return true;
    }
    auto pbox = WebGetProgressBox();
    return pbox && pbox->ShouldExit();
}

struct PartStream final : yati::source::Stream {
    explicit PartStream(std::shared_ptr<PartBuffer> buf) : m_buf{std::move(buf)} {
        m_open_result = 0;
    }

    Result ReadChunk(void* buf, s64 size, u64* bytes_read) override {
        *bytes_read = 0;
        const auto n = m_buf->Read(buf, size, [] { return Aborted() || g_cancel.load() || g_state.load() != Running; });
        if (n < 0) {
            return Result_StreamUnexpectedEof;
        }
        *bytes_read = n;
        g_upload_state.bytes.store(m_buf->Pos());
        return 0;
    }

private:
    std::shared_ptr<PartBuffer> m_buf;
};

void Finish(State state, const std::string& message) {
    std::scoped_lock lock{g_mutex};
    g_message = message;
    g_buf.reset();
    g_state.store(state);
}

} // namespace

auto IsInstallPartId(const std::string& id) -> bool {
    std::scoped_lock lock{g_mutex};
    return !id.empty() && id == g_id;
}

void HandleInstallCreate(Socket sock, const std::string& query) {
    // the page falls back to one plain PUT on 403; applet mode has only 2 workers
    const auto conns = std::clamp<long>(WebUploadConnsOption().Get(), 0, 3);
    if (conns < 2 || App::IsApplet()) {
        SendResponse(sock, "403 Forbidden", "text/plain", "disabled");
        return;
    }

    const auto name = SanitizeFileName(GetQueryValue(query, "name"));
    const auto item_id = GetQueryValue(query, "id");
    const auto size = std::strtoll(GetQueryValue(query, "size").c_str(), nullptr, 10);
    const auto ext = path::Extension(name);
    const bool package = path::EqualsIC(ext, "nsp") || path::EqualsIC(ext, "nsz") ||
                         path::EqualsIC(ext, "xci") || path::EqualsIC(ext, "xcz");
    if (!package || item_id.empty() || item_id.size() > 64 || size <= 0) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad name, id or size");
        return;
    }

    bool expected = false;
    if (!g_transfer_busy.compare_exchange_strong(expected, true)) {
        SendResponse(sock, "409 Conflict", "text/plain", "Another transfer is already in progress");
        return;
    }
    ON_SCOPE_EXIT(g_transfer_busy.store(false));

    auto buf = std::make_shared<PartBuffer>(size, WINDOW);
    {
        std::scoped_lock lock{g_mutex};
        g_buf = buf;
        g_id = item_id;
        g_message.clear();
        g_total = size;
        g_cancel.store(false);
        g_state.store(Running);
    }

    // answer now: the page starts sending parts, this worker installs until the package is done
    SendResponse(sock, "200 OK", "application/json", "{\"conns\":" + std::to_string(conns) + "}");

    const auto res = RunDirectInstall(name, size, item_id, [&]() -> std::unique_ptr<yati::source::Stream> {
        return std::make_unique<PartStream>(buf);
    });
    const bool ok = std::string_view{res.status}.starts_with("200");
    log_write("[WEB] part install %s: %s\n", name.c_str(), res.message.c_str());
    Finish(ok ? Ok : Failed, g_cancel.load() ? std::string{"Cancelled"} : res.message);
}

void HandleInstallPut(Socket sock, const std::string& req, const std::string& query, const std::string& id) {
    std::shared_ptr<PartBuffer> buf;
    s64 total{};
    {
        std::scoped_lock lock{g_mutex};
        if (id == g_id && g_state.load() == Running) {
            buf = g_buf;
            total = g_total;
        }
    }
    if (!buf) {
        // the install ended (failed or cancelled): the page stops
        SendResponse(sock, "410 Gone", "text/plain", "Install is not running");
        return;
    }

    const auto len_str = HeaderValue(req, "content-length");
    const auto len = len_str.empty() ? -1 : std::strtoll(len_str.c_str(), nullptr, 10);
    const auto offset = std::strtoll(GetQueryValue(query, "offset").c_str(), nullptr, 10);
    if (!WebPartRangeOk(offset, len, total)) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad part range");
        return;
    }

    std::vector<u8> data;
    if (const auto err = ReadPartBody(sock, req, len, data)) {
        if (err->status) {
            SendResponse(sock, err->status, "text/plain", err->message);
        }
        return;
    }

    if (!buf->Put(offset, std::move(data), [] { return Aborted() || g_cancel.load() || g_state.load() != Running; })) {
        SendResponse(sock, "410 Gone", "text/plain", "Install is not running");
        return;
    }
    SendResponse(sock, "200 OK", "text/plain", "ok");
}

void HandleInstallCancel(Socket sock, const std::string& id) {
    if (IsInstallPartId(id) && g_state.load() == Running) {
        g_cancel.store(true); // the reader and the waiting parts stop; yati ends the install with an error
        log_write("[WEB] part install: cancelled by the page\n");
    }
    SendResponse(sock, "200 OK", "text/plain", "cancelling");
}

void HandleInstallState(Socket sock, const std::string& id) {
    std::string state, message;
    {
        std::scoped_lock lock{g_mutex};
        if (id.empty() || id != g_id) {
            SendResponse(sock, "404 Not Found", "text/plain", "Unknown install");
            return;
        }
        switch (g_state.load()) {
            case Running: state = g_cancel.load() ? "cancelling" : "running"; break;
            case Ok: state = "ok"; break;
            default: state = "failed"; break;
        }
        message = g_message;
    }
    SendResponse(sock, "200 OK", "application/json", "{\"state\":\"" + state + "\",\"message\":\"" + JsonEscape(message) + "\"}");
}

} // namespace sphaira
