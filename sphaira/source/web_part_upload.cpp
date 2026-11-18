// Multi-connection file upload: the browser cuts a file into parts and sends several at once.
//   POST /upload?part=new&path=&name=&size=N   -> creates the file at its final size, {"id":..,"name":..}
//   PUT  /upload?part=<id>&offset=N            -> writes the body at offset N (no truncate)
//   POST /upload?part=<id>&done=1|0            -> closes the file; done=0 deletes it
// One transfer at a time (g_transfer_busy), but all connections of that transfer write the same file.
// Horizon refuses a second write handle on one file, so all parts share one fs::File (positional writes).

#include "web_upload_routes.hpp"
#include "web_fs.hpp"
#include "web_upload.hpp"
#include "web.hpp"
#include "web_part_range.hpp"
#include "app.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "defines.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <sys/socket.h>

namespace sphaira {

std::atomic_bool g_transfer_busy{false};

auto UniqueUploadPath(fs::Fs& sd, const fs::FsPath& dir, const std::string& name) -> fs::FsPath {
    auto out = fs::AppendPath(dir, name);
    if (!sd.FileExists(out) && !sd.DirExists(out)) {
        return out;
    }

    auto stem = name;
    std::string ext;
    if (const auto dot = name.find_last_of('.'); dot != std::string::npos) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }

    for (u64 i = 1; ; i++) {
        char buf[FS_MAX_PATH]{};
        std::snprintf(buf, sizeof(buf), "%s (%llu)%s", stem.c_str(), static_cast<unsigned long long>(i), ext.c_str());
        out = fs::AppendPath(dir, buf);
        if (!sd.FileExists(out) && !sd.DirExists(out)) {
            return out;
        }
    }
}

option::OptionLong& WebUploadConnsOption() {
    static option::OptionLong opt{App::INI_SECTION, "web_upload_conns", 0};
    return opt;
}

namespace {

using namespace web::detail;

constexpr u64 STALE_NS = 30'000'000'000ULL; // a browser that went away without "done"

struct PartUpload {
    std::unique_ptr<fs::Fs> fs;
    fs::FsPath path;
    fs::File file;
    std::string id;
    s64 total{};
    std::atomic<u64> last_tick{};
    std::mutex parts_mutex; // guards parts and unique
    std::map<s64, s64> parts; // offset -> length of every part written (a retry rewrites the same key)
    s64 unique{};            // bytes in parts

    // true once the parts cover [0, total) without a hole
    auto Covered() -> bool {
        std::scoped_lock lock{parts_mutex};
        s64 end = 0;
        for (const auto& [off, len] : parts) {
            if (off > end) {
                return false;
            }
            end = std::max(end, off + len);
        }
        return end >= total;
    }
};

std::mutex g_mutex;
std::shared_ptr<PartUpload> g_part;
u32 g_next_id = 1;

auto Now() -> u64 {
    return armTicksToNs(armGetSystemTick());
}

auto Find(const std::string& id) -> std::shared_ptr<PartUpload> {
    std::scoped_lock lock{g_mutex};
    if (g_part && g_part->id == id) {
        return g_part;
    }
    return {};
}

// closes the shared handle (after the last PUT left it) and frees the transfer slot
void Release(const std::shared_ptr<PartUpload>& p, bool keep) {
    {
        std::scoped_lock lock{g_mutex};
        if (g_part != p) {
            return;
        }
        g_part.reset();
    }
    p->file.Close();
    if (!keep) {
        p->fs->DeleteFile(p->path);
        log_write("[WEB] part upload dropped, deleted %s\n", p->path.toString().c_str());
    }
    g_upload_state.active.store(false);
    g_transfer_busy.store(false);
}

void HandleCreate(Socket sock, const std::string& query) {
    // the page falls back to one plain PUT on 403
    const auto conns = std::clamp<long>(WebUploadConnsOption().Get(), 0, 3);
    if (conns < 2) {
        SendResponse(sock, "403 Forbidden", "text/plain", "disabled");
        return;
    }

    bool expected = false;
    if (!g_transfer_busy.compare_exchange_strong(expected, true)) {
        // an abandoned upload (tab closed) must not block the server for ever
        std::shared_ptr<PartUpload> stale;
        {
            std::scoped_lock lock{g_mutex};
            if (g_part && Now() - g_part->last_tick.load() > STALE_NS) {
                stale = g_part;
            }
        }
        if (stale) {
            Release(stale, false);
        }
        expected = false;
        if (!g_transfer_busy.compare_exchange_strong(expected, true)) {
            SendResponse(sock, "409 Conflict", "text/plain", "Another transfer is already in progress");
            return;
        }
    }

    auto p = std::make_shared<PartUpload>();
    bool armed = false;
    // frees the slot on every early return below
    struct Guard {
        bool& armed;
        ~Guard() { if (!armed) g_transfer_busy.store(false); }
    } guard{armed};

    const auto name = SanitizeFileName(GetQueryValue(query, "name"));
    const auto total = std::strtoll(GetQueryValue(query, "size").c_str(), nullptr, 10);
    if (name.empty() || total < 0) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad name or size");
        return;
    }

    // same routing as a plain upload: homebrew goes to /switch/<name>/, the rest to the browsed folder
    const bool is_nro = path::EqualsIC(path::Extension(name), "nro");
    std::string dir;
    if (is_nro) {
        auto stem = name;
        if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
            stem.resize(dot);
        }
        dir = "/switch/" + stem;
    } else {
        const auto raw_path = GetQueryValue(query, "path");
        dir = CanonicalizeAbsolutePath(raw_path.empty() ? GetMountRoot() : raw_path);
    }

    p->fs = OpenFs(dir);
    auto& fs = *p->fs;
    if (is_nro) {
        fs.CreateDirectoryRecursively(dir);
        p->path = fs::AppendPath(dir, name);
        fs.DeleteFile(p->path);
    } else if (!fs.DirExists(dir)) {
        SendResponse(sock, "404 Not Found", "text/plain", "Upload folder not found");
        return;
    } else {
        p->path = UniqueUploadPath(fs, dir, name);
    }

    if (auto rc = fs.CreateFile(p->path, total, 0); R_FAILED(rc)) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not create file");
        return;
    }
    if (R_FAILED(fs.OpenFile(p->path, FsOpenMode_Write, &p->file))) {
        fs.DeleteFile(p->path);
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not open output file");
        return;
    }

    p->total = total;
    p->last_tick.store(Now());
    {
        std::scoped_lock lock{g_mutex};
        p->id = std::to_string(g_next_id++);
        g_part = p;
    }
    armed = true;

    {
        std::scoped_lock lock{g_upload_state.name_mutex};
        g_upload_state.name = name;
    }
    g_upload_state.total.store(total);
    g_upload_state.bytes.store(0);
    g_upload_state.active.store(true);

    // 1 in applet mode: it has only 2 workers
    const auto final_name = p->path.toString();
    SendResponse(sock, "200 OK", "application/json",
        "{\"id\":\"" + p->id + "\",\"name\":\"" + JsonEscape(final_name.substr(final_name.find_last_of('/') + 1)) +
        "\",\"conns\":" + (App::IsApplet() ? std::string("1") : std::to_string(conns)) + "}");
}

void HandlePut(Socket sock, const std::string& req, const std::string& query, const std::string& id) {
    const auto p = Find(id);
    if (!p) {
        SendResponse(sock, "404 Not Found", "text/plain", "Unknown upload");
        return;
    }

    const auto len_str = HeaderValue(req, "content-length");
    const auto len = len_str.empty() ? -1 : std::strtoll(len_str.c_str(), nullptr, 10);
    const auto offset = std::strtoll(GetQueryValue(query, "offset").c_str(), nullptr, 10);
    if (!WebPartRangeOk(offset, len, p->total)) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad part range");
        return;
    }

    std::vector<u8> buf(len);
    s64 fill = 0;
    if (const auto header_end = req.find("\r\n\r\n"); header_end != std::string::npos) {
        const auto body_start = header_end + 4;
        fill = std::min<s64>(static_cast<s64>(req.size() - body_start), len);
        if (fill > 0) {
            std::memcpy(buf.data(), req.data() + body_start, fill);
        }
    }

    u32 idle_count = 0;
    while (fill < len) {
        if (!WebShareIsRunning()) {
            return;
        }
        if (auto pbox = WebGetProgressBox(); pbox && pbox->ShouldExit()) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Cancelled by user");
            return;
        }
        const auto got = recv(sock, buf.data() + fill, len - fill, 0);
        if (got > 0) {
            idle_count = 0;
            fill += got;
            p->last_tick.store(Now());
        } else if (got == 0) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Part ended early");
            return;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            if (++idle_count > IDLE_TIMEOUT_MS) {
                SendResponse(sock, "408 Request Timeout", "text/plain", "Receive timeout");
                return;
            }
            svcSleepThread(1'000'000);
        } else {
            SendResponse(sock, "500 Internal Server Error", "text/plain", "Socket read failed");
            return;
        }
    }

    if (R_FAILED(p->file.Write(offset, buf.data(), len, FsWriteOption_None))) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not write file");
        return;
    }

    p->last_tick.store(Now());
    {
        std::scoped_lock lock{p->parts_mutex};
        auto& slot = p->parts[offset];
        p->unique += len - slot;
        slot = len;
        g_upload_state.bytes.store(p->unique);
    }
    SendResponse(sock, "200 OK", "text/plain", "ok");
}

void HandleDone(Socket sock, const std::string& id, bool ok) {
    const auto p = Find(id);
    if (!p) {
        SendResponse(sock, "404 Not Found", "text/plain", "Unknown upload");
        return;
    }

    const bool complete = ok && p->Covered();
    Release(p, complete);
    if (!complete) {
        SendResponse(sock, ok ? "409 Conflict" : "200 OK", "text/plain", ok ? "Upload incomplete" : "dropped");
        return;
    }
    SendResponse(sock, "200 OK", "text/plain", "ok");
}

} // namespace

void HandlePartUpload(Socket sock, const std::string& req, const std::string& method, const std::string& query) {
    const auto part = GetQueryValue(query, "part");
    if (method == "POST" && part == "new") {
        HandleCreate(sock, query);
    } else if (method == "POST") {
        HandleDone(sock, part, GetQueryValue(query, "done") == "1");
    } else if (method == "PUT") {
        HandlePut(sock, req, query, part);
    } else {
        SendResponse(sock, "405 Method Not Allowed", "text/plain", "Bad method");
    }
}

} // namespace sphaira
