// DOCS_DEMO builds only: every http(s) request is answered from sdmc:/config/kefir/demo/http/, nothing goes to
// the network (Eden fails the TLS handshake anyway). Missing fixture = the reply an offline console gets.
//   https://host/a/b?x=1     -> http/host/a/b_x=1        (query: characters outside [A-Za-z0-9.=_-] become '_')
//   POST body                -> <that path>.<fnv1a32 of the body, 8 hex digits>
//   a path that is a folder  -> <folder>/index
// The Hub log lists each URL with the file it looked for ("[demo] http").

#include "demo/demo_http.hpp"
#include "demo/demo_http_path.hpp"
#include "log.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace sphaira::demo {
namespace {

const std::string HTTP_DIR = "/config/kefir/demo/http/";

// the fixture name, or <folder>/index when the name is a folder on the SD.
auto FixturePath(const std::string& url, const std::string& post) -> std::string {
    auto out = HTTP_DIR + HttpFixtureName(url, "");
    struct stat st;
    if (!stat(out.c_str(), &st) && S_ISDIR(st.st_mode)) {
        out += "/index";
    }
    if (!post.empty()) {
        out = HTTP_DIR + HttpFixtureName(out.substr(HTTP_DIR.size()), post);
    }
    return out;
}

auto ReadFile(const std::string& path, std::vector<u8>& out) -> bool {
    auto f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    u8 buf[0x4000];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0; ) {
        out.insert(out.end(), buf, buf + n);
    }
    std::fclose(f);
    return true;
}

} // namespace

auto HttpReply(const curl::Api& e) -> std::optional<curl::ApiResult> {
    const auto& url = e.GetUrl();
    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        return std::nullopt;
    }

    const auto path = FixturePath(url, e.GetFields());
    std::vector<u8> data;
    const bool found = ReadFile(path, data);
    log_write("[demo] http %s -> %s%s\n", url.c_str(), path.c_str(), found ? "" : " (missing)");
    if (!found) {
        if (!e.GetFields().empty()) {
            log_write("[demo] http body: %.600s\n", e.GetFields().c_str());
        }
        return curl::ApiResult{false, 0, {}, {}, e.GetPath()};
    }

    // a download to a file gets the fixture copied there, like a finished transfer.
    if (!e.GetPath().empty()) {
        fs::FsNativeSd fs;
        fs.DeleteFile(e.GetPath());
        fs.CreateDirectoryRecursivelyWithPath(e.GetPath());
        if (R_FAILED(fs.CreateFile(e.GetPath(), data.size(), 0))) {
            return curl::ApiResult{false, 0, {}, {}, e.GetPath()};
        }
        fs::File file;
        if (R_FAILED(fs.OpenFile(e.GetPath(), FsOpenMode_Write, &file)) || R_FAILED(file.Write(0, data.data(), data.size(), FsWriteOption_None))) {
            return curl::ApiResult{false, 0, {}, {}, e.GetPath()};
        }
        return curl::ApiResult{true, 200, {}, {}, e.GetPath()};
    }
    return curl::ApiResult{true, 200, {}, std::move(data), {}};
}

} // namespace sphaira::demo
