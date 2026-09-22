#include "web_upload_routes.hpp"
#include "web_fs.hpp"
#include "web_http.hpp"
#include "web_upload.hpp"
#include "web.hpp"
#include "app.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "i18n.hpp"
#include "defines.hpp"
#include "utils/thread.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/homebrew.hpp"
#include "yati/yati.hpp"
#include "yyjson.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

namespace sphaira {
namespace {

using namespace web::detail;

// Serializes write operations (upload/install) across the worker threads. Read
// operations (browse/status/download/view) stay concurrent; only one transfer
// touches the shared g_upload_state / install pipeline at a time.
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

} // namespace

void HandleUploadManifest(Socket sock, const std::string& req) {
    const auto length_str = HeaderValue(req, "content-length");
    if (length_str.empty()) {
        SendResponse(sock, "411 Length Required", "text/plain", "Missing Content-Length");
        return;
    }

    const auto content_length = std::strtoll(length_str.c_str(), nullptr, 10);
    if (content_length <= 0 || content_length > 1024 * 1024) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad Content-Length");
        return;
    }

    std::string body;
    if (const auto header_end = req.find("\r\n\r\n"); header_end != std::string::npos) {
        body = req.substr(header_end + 4);
    }
    body.resize(std::min<size_t>(body.size(), static_cast<size_t>(content_length)));

    for (u32 attempts = 0; attempts < 20000 && (s64)body.size() < content_length; attempts++) {
        if (!WebShareIsRunning()) {
            break;
        }
        char buf[4096];
        const auto want = std::min<s64>(sizeof(buf), content_length - (s64)body.size());
        const auto got = recv(sock, buf, want, 0);
        if (got > 0) {
            body.append(buf, got);
        } else if (got == 0) {
            break;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            svcSleepThread(1'000'000);
        } else {
            break;
        }
    }

    if ((s64)body.size() < content_length) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Truncated manifest body");
        return;
    }

    yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Invalid JSON");
        return;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Expected JSON array");
        return;
    }

    const size_t count = yyjson_arr_size(root);
    if (count == 0 || count > 128) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Invalid item count");
        return;
    }

    struct ManifestItem {
        std::string id;
        std::string name;
        s64 size{0};
        bool to_sd{true};
    };
    std::vector<ManifestItem> items;
    items.reserve(count);
    std::unordered_set<std::string> item_ids;

    size_t idx, max;
    yyjson_val* val;
    yyjson_arr_foreach(root, idx, max, val) {
        if (!yyjson_is_obj(val)) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Invalid manifest item");
            return;
        }
        yyjson_val* id_val = yyjson_obj_get(val, "id");
        yyjson_val* name_val = yyjson_obj_get(val, "name");
        yyjson_val* size_val = yyjson_obj_get(val, "size");
        if (!yyjson_is_str(name_val)) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Missing or invalid item name");
            return;
        }
        const char* raw_name = yyjson_get_str(name_val);
        const std::string name = SanitizeFileName(raw_name ? raw_name : "");
        if (name.empty() || name.size() > 256) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Invalid item name length");
            return;
        }
        const auto ext = path::Extension(name);
        if (!path::EqualsIC(ext, "nsp") && !path::EqualsIC(ext, "nsz") &&
            !path::EqualsIC(ext, "xci") && !path::EqualsIC(ext, "xcz") &&
            !path::EqualsIC(ext, "nro")) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Unsupported extension");
            return;
        }

        s64 size = 0;
        if (yyjson_is_num(size_val)) {
            size = yyjson_get_sint(size_val);
            if (size < 0 || size > 100LL * 1024 * 1024 * 1024) {
                SendResponse(sock, "400 Bad Request", "text/plain", "Invalid item size");
                return;
            }
        }

        std::string item_id;
        if (yyjson_is_str(id_val)) {
            const char* s = yyjson_get_str(id_val);
            if (s) item_id = s;
        }
        if (item_id.empty() || item_id.size() > 64 || !item_ids.emplace(item_id).second) {
            SendResponse(sock, "400 Bad Request", "text/plain", "Invalid or duplicate item id");
            return;
        }

        const bool is_compressed = path::EqualsIC(ext, "nsz") || path::EqualsIC(ext, "xcz") || path::EqualsIC(ext, "ncz");
        const bool to_sd = yati::ChooseInstallTarget(size, is_compressed);

        items.push_back({item_id, name, size, to_sd});
    }

    auto session = App::GetActiveInstallSession();
    if (session && session->GetOrigin() != ui::menu::dbi::TransportOrigin::Web) {
        SendResponse(sock, "409 Conflict", "text/plain", "Another transport installation is in progress");
        return;
    }

    if (!session) {
        session = std::make_shared<ui::menu::dbi::InstallSession>("Web install"_i18n, 0, ui::menu::dbi::TransportOrigin::Web);
        for (const auto& item : items) {
            session->EnqueueFile(item.name, item.size, item.to_sd, item.id);
        }
        if (!App::PushInstallSession(session)) {
            SendResponse(sock, "409 Conflict", "text/plain", "Failed to start install session");
            return;
        }
    } else {
        for (const auto& item : items) {
            if (!session->HasQueuedItem(item.id, item.name)) {
                session->EnqueueFile(item.name, item.size, item.to_sd, item.id);
            }
        }
    }

    SendResponse(sock, "200 OK", "application/json", "{\"status\":\"ok\"}");
}

void ReceiveUpload(Socket sock, const std::string& req, const std::string& query) {
    const auto length_str = HeaderValue(req, "content-length");
    if (length_str.empty()) {
        SendResponse(sock, "411 Length Required", "text/plain", "Missing Content-Length");
        return;
    }

    const auto content_length = std::strtoll(length_str.c_str(), nullptr, 10);
    if (content_length < 0) {
        SendResponse(sock, "400 Bad Request", "text/plain", "Bad Content-Length");
        return;
    }

    // Only one transfer may run at a time; a concurrent upload/install would
    // clobber the shared progress state and install pipeline.
    bool expected = false;
    if (!g_transfer_busy.compare_exchange_strong(expected, true)) {
        SendResponse(sock, "409 Conflict", "text/plain", "Another transfer is already in progress");
        return;
    }
    ON_SCOPE_EXIT(g_transfer_busy.store(false));

    const auto raw_path = GetQueryValue(query, "path");
    const auto raw_name = GetQueryValue(query, "name");
    const auto name = SanitizeFileName(raw_name);

    // homebrew (.nro) is routed to /switch/<name>/ so it lands in the homebrew
    // menu, mirroring the MTP/USB root-drop behaviour.
    const bool is_nro = path::EqualsIC(path::Extension(name), "nro");
    std::string dir;
    if (is_nro) {
        auto stem = name;
        if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
            stem.resize(dot);
        }
        dir = "/switch/" + stem;
    } else {
        dir = CanonicalizeAbsolutePath(raw_path.empty() ? GetMountRoot() : raw_path);
    }

    const auto install_param = GetQueryValue(query, "install");
    const bool direct_install = (install_param == "1");

    if (direct_install) {
        const auto item_id = GetQueryValue(query, "id");
        std::shared_ptr<ui::menu::dbi::InstallSession> session = App::GetActiveInstallSession();
        if (session && session->GetOrigin() != ui::menu::dbi::TransportOrigin::Web) {
            SendResponse(sock, "409 Conflict", "text/plain", "Another transport installation is in progress");
            return;
        }

        const auto ext = path::Extension(name);
        const bool is_compressed = path::EqualsIC(ext, "nsz") || path::EqualsIC(ext, "xcz") || path::EqualsIC(ext, "ncz");
        const bool install_to_sd = yati::ChooseInstallTarget(content_length, is_compressed);

        if (!session) {
            session = std::make_shared<ui::menu::dbi::InstallSession>("Web install"_i18n, 0, ui::menu::dbi::TransportOrigin::Web);
            session->EnqueueFile(name, content_length, install_to_sd, item_id);
            if (!App::PushInstallSession(session)) {
                SendResponse(sock, "500 Internal Server Error", "text/plain", "Installation not possible (Cannot start install session)");
                return;
            }
        } else {
            if (!session->HasQueuedItem(item_id, name)) {
                session->EnqueueFile(name, content_length, install_to_sd, item_id);
            }
        }

        std::string initial_body;
        const auto header_end = req.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            const auto body_start = header_end + 4;
            const auto available = static_cast<s64>(req.size() - body_start);
            const auto write_size = std::min<s64>(available, content_length);
            if (write_size > 0) {
                initial_body = req.substr(body_start, write_size);
            }
        }

        auto stream_source = std::make_unique<SocketStream>(sock, initial_body, content_length);

        const std::string dest_str = install_to_sd ? " (SD Card)" : " (System Memory)";
        {
            std::scoped_lock lock{g_upload_state.name_mutex};
            g_upload_state.name = "Installing: " + name + dest_str;
        }
        g_upload_state.total.store(content_length);
        g_upload_state.bytes.store(initial_body.size());
        g_upload_state.active.store(true);

        session->SetCurrentPackage(item_id, name, content_length, install_to_sd);
        session->SetState(ui::menu::dbi::State::Installing);

        fs::FsPath dummy_path = "/";
        dummy_path += name;

        yati::ConfigOverride override{};
        override.sd_card_install = install_to_sd;

        const auto rc = yati::InstallFromSource(session.get(), stream_source.get(), dummy_path, override);

        const auto cur_pkg = session->GetCurrentPackageIndex();
        session->MarkPackageComplete(cur_pkg, rc);

        if (session->AllPackagesTerminal()) {
            session->TransitionToSummary();
        }

        g_upload_state.active.store(false);

        if (R_FAILED(rc)) {
            log_write("Direct install failed: 0x%X\n", rc);
            char err_msg[128]{};
            std::snprintf(err_msg, sizeof(err_msg), "Installation failed: 0x%X", rc);
            SendResponse(sock, "500 Internal Server Error", "text/plain", err_msg);
            return;
        }

        SendResponse(sock, "200 OK", "text/plain", "Installed");
        return;
    }

    // the upload target follows the folder the browser is in, which may be a
    // mounted source rather than the card.
    auto fsp = OpenFs(dir);
    auto& fs = *fsp;
    fs::FsPath out_path;
    if (is_nro) {
        fs.CreateDirectoryRecursively(dir); // ensure /switch/<name>/ exists.
        // replace any previous copy rather than adding a "name (1).nro"
        // sibling, which would show up as a duplicate homebrew entry.
        out_path = fs::AppendPath(dir, name);
        fs.DeleteFile(out_path);
    } else if (!fs.DirExists(dir)) {
        SendResponse(sock, "404 Not Found", "text/plain", "Upload folder not found");
        return;
    } else {
        out_path = UniqueUploadPath(fs, dir, name);
    }

    if (auto rc = fs.CreateFile(out_path, content_length, 0); R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not create file");
        return;
    }

    struct UploadGuard {
        fs::Fs& fs;
        const fs::FsPath& path;
        bool success = false;

        ~UploadGuard() {
            if (!success) {
                fs.DeleteFile(path);
                log_write("Upload aborted or failed. Deleted incomplete file: %s\n", path.toString().c_str());
            }
        }
    } upload_guard{fs, out_path};

    fs::File file;
    if (R_FAILED(fs.OpenFile(out_path, FsOpenMode_Write, &file))) {
        SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not open output file");
        return;
    }

    const auto header_end = req.find("\r\n\r\n");
    s64 offset{};
    if (header_end != std::string::npos) {
        const auto body_start = header_end + 4;
        const auto available = static_cast<s64>(req.size() - body_start);
        const auto write_size = std::min<s64>(available, content_length);
        if (write_size > 0) {
            if (R_FAILED(file.Write(offset, req.data() + body_start, write_size, FsWriteOption_None))) {
                SendResponse(sock, "500 Internal Server Error", "text/plain", "Could not write file");
                return;
            }
            offset += write_size;
        }
    }

    {
        std::scoped_lock lock{g_upload_state.name_mutex};
        g_upload_state.name = name;
    }
    g_upload_state.total.store(content_length);
    g_upload_state.bytes.store(offset);
    g_upload_state.active.store(true);

    // Overlap the network receive with SD-card writes: while the writer
    // thread flushes one chunk to the card, this thread keeps draining the
    // socket into the next one. Doing the two sequentially adds their
    // latencies together and leaves the TCP window idle during each write.
    struct UploadWriter {
        static void Func(void* p) {
            auto self = static_cast<UploadWriter*>(p);
            while (true) {
                waitSingle(waiterForUEvent(&self->work_event), UINT64_MAX);
                if (self->exit) {
                    return;
                }
                if (R_SUCCEEDED(self->result)) {
                    self->result = self->file->Write(self->off, self->buf.data(), self->buf.size(), FsWriteOption_None);
                }
                ueventSignal(&self->done_event);
            }
        }

        fs::File* file{};
        std::vector<u8> buf{};
        s64 off{};
        std::atomic<Result> result{};
        std::atomic_bool exit{};
        UEvent work_event{};
        UEvent done_event{};
        Thread thread{};
        bool started{};
        bool busy{};
    } writer{};

    writer.file = &file;
    ueventCreate(&writer.work_event, true);
    ueventCreate(&writer.done_event, true);
    if (R_SUCCEEDED(utils::CreateThread(&writer.thread, UploadWriter::Func, &writer, 1024 * 32, PRIO_PREEMPTIVE))) {
        if (R_SUCCEEDED(threadStart(&writer.thread))) {
            writer.started = true;
        } else {
            threadClose(&writer.thread);
        }
    }

    std::vector<u8> buf(HTTP_FILE_CHUNK);
    size_t fill{};
    s64 block_off = offset;

    // hand the filled buffer over to the writer thread, waiting for its
    // previous write to finish first. falls back to a synchronous write if
    // the writer thread could not be started.
    const auto write_block = [&]() -> Result {
        if (!writer.started) {
            const auto rc = file.Write(block_off, buf.data(), fill, FsWriteOption_None);
            block_off += fill;
            fill = 0;
            return rc;
        }

        if (writer.busy) {
            waitSingle(waiterForUEvent(&writer.done_event), UINT64_MAX);
            writer.busy = false;
        }
        R_TRY(writer.result.load());

        buf.resize(fill);
        std::swap(buf, writer.buf);
        writer.off = block_off;
        buf.resize(HTTP_FILE_CHUNK);

        block_off += fill;
        fill = 0;
        writer.busy = true;
        ueventSignal(&writer.work_event);
        R_SUCCEED();
    };

    // waits for the in-flight write and stops the writer thread. must be
    // called on every exit path so the thread never outlives this frame.
    const auto finish_writer = [&]() -> Result {
        if (!writer.started) {
            R_SUCCEED();
        }
        if (writer.busy) {
            waitSingle(waiterForUEvent(&writer.done_event), UINT64_MAX);
            writer.busy = false;
        }
        writer.exit = true;
        ueventSignal(&writer.work_event);
        threadWaitForExit(&writer.thread);
        threadClose(&writer.thread);
        writer.started = false;
        return writer.result;
    };

    const char* fail_status{};
    const char* fail_msg{};
    bool aborted{};

    u32 idle_count = 0;
    while (offset < content_length) {
        if (!WebShareIsRunning()) {
            aborted = true;
            break;
        }
        if (auto pbox = WebGetProgressBox()) {
            if (pbox->ShouldExit()) {
                fail_status = "400 Bad Request";
                fail_msg = "Cancelled by user";
                break;
            }
        }
        const auto want = std::min<s64>(buf.size() - fill, content_length - offset);
        const auto got = recv(sock, buf.data() + fill, want, 0);
        if (got > 0) {
            idle_count = 0;
            fill += got;
            offset += got;
            g_upload_state.bytes.store(offset);

            if (fill == buf.size() || offset == content_length) {
                if (R_FAILED(write_block())) {
                    fail_status = "500 Internal Server Error";
                    fail_msg = "Could not write file";
                    break;
                }
            }
        } else if (got == 0) {
            fail_status = "400 Bad Request";
            fail_msg = "Upload ended early";
            break;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            idle_count++;
            if (idle_count > IDLE_TIMEOUT_MS) {
                fail_status = "408 Request Timeout";
                fail_msg = "Receive timeout";
                break;
            }
            svcSleepThread(1'000'000);
        } else {
            fail_status = "500 Internal Server Error";
            fail_msg = "Socket read failed";
            break;
        }
    }

    const auto write_rc = finish_writer();
    g_upload_state.active.store(false);

    if (aborted) {
        return;
    }

    if (!fail_status && R_FAILED(write_rc)) {
        fail_status = "500 Internal Server Error";
        fail_msg = "Could not write file";
    }

    if (fail_status) {
        SendResponse(sock, fail_status, "text/plain", fail_msg);
        return;
    }

    upload_guard.success = true;
    fs.Commit();
    ui::menu::homebrew::NotifyFileCreated(out_path.s);

    if (is_nro) {
        log_write("[WEB] installed homebrew to %s\n", out_path.s);
        SendResponse(sock, "200 OK", "text/plain", "Installed");
        return;
    }

    SendResponse(sock, "200 OK", "text/plain", "Uploaded");
}

} // namespace sphaira
