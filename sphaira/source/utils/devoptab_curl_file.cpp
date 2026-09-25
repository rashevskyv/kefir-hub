#include "utils/devoptab_curl_device.hpp"
#include "log.hpp"
#include "defines.hpp"
#include <yyjson.h>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

namespace sphaira::devoptab::common {

PushThreadData* MountCurlDevice::CreatePushData(CURL* curl_handle, const std::string& url, size_t offset) {
    auto data = new PushThreadData{curl_handle};
    if (!data) {
        log_write("[PUSH:PULL] Failed to allocate PushThreadData\n");
        return nullptr;
    }

    curl_set_common_options(curl_handle, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, PushThreadData::push_thread_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)data);

    if (offset > 0) {
        char range[64];
        std::snprintf(range, sizeof(range), "%zu-", offset);
        log_write("[PUSH:PULL] Requesting range: %s\n", range);
        curl_easy_setopt(curl_handle, CURLOPT_RANGE, range);
    }

    if (R_FAILED(data->CreateAndStart())) {
        log_write("[PUSH:PULL] Failed to create and start push thread\n");
        delete data;
        return nullptr;
    }

    return data;
}

PullThreadData* MountCurlDevice::CreatePullData(CURL* curl_handle, const std::string& url, bool append) {
    auto data = new PullThreadData{curl_handle};
    if (!data) {
        log_write("[PUSH:PULL] Failed to allocate PullThreadData\n");
        return nullptr;
    }

    curl_set_common_options(curl_handle, url);
    curl_easy_setopt(curl_handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION, PullThreadData::pull_thread_callback);
    curl_easy_setopt(curl_handle, CURLOPT_READDATA, (void *)data);

    if (append) {
        log_write("[PUSH:PULL] Setting append mode for upload\n");
        curl_easy_setopt(curl_handle, CURLOPT_APPEND, 1L);
    }

    if (R_FAILED(data->CreateAndStart())) {
        log_write("[PUSH:PULL] Failed to create and start pull thread\n");
        delete data;
        return nullptr;
    }

    return data;
}

namespace {

// abortive close on every socket: without this, closing a connection whose
// receive queue still holds a fast in-flight stream can block forever inside
// the bsd sysmodule, freezing the thread in a way no callback can interrupt.
int curl_sockopt_callback(void*, curl_socket_t fd, curlsocktype) {
    struct linger sl{};
    sl.l_onoff = 1;
    sl.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
    return CURL_SOCKOPT_OK;
}

} // namespace

void MountCurlDevice::curl_set_common_options(CURL* curl_handle, const std::string& url) {
    // NOTE: port, user and pass are set in the curl_url.
    curl_easy_reset(curl_handle);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, APP_USER_AGENT);
    curl_easy_setopt(curl_handle, CURLOPT_SOCKOPTFUNCTION, curl_sockopt_callback);
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_AUTOREFERER, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 15L);
    // keep sending credentials when the server redirects, e.g. an
    // http:// source that 301s to https://. Without this the redirected
    // request is sent without auth and the server replies 401.
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, nullptr);
    curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, CurlShutdownProgressCallback);
    // fail on http >= 400: otherwise a 404/500 error page is streamed back
    // as if it were the file contents.
    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
    // negotiate the auth scheme with the server. Without this, curl only
    // sends Basic, and servers requiring Digest reply 401 to every request
    // (download.cpp does the same for probe/sync, which is why Test
    // Connection passes while browsing fails).
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, (long)CURLAUTH_ANY);
    // credentials as explicit options (not in the url): the device's curl only
    // re-sends these across a redirect (eg http->https) when UNRESTRICTED_AUTH
    // is set, whereas url-embedded creds are dropped, giving 401. See Mount().
    if (!config.user.empty()) {
        curl_easy_setopt(curl_handle, CURLOPT_USERNAME, config.user.c_str());
    }
    if (!config.pass.empty()) {
        curl_easy_setopt(curl_handle, CURLOPT_PASSWORD, config.pass.c_str());
    }
    // bigger receive buffer: curl reads the socket in larger slices, which
    // cuts write-callback round-trips and helps the streaming download keep up
    // with the link instead of dribbling data in 64 KiB pieces.
    curl_easy_setopt(curl_handle, CURLOPT_BUFFERSIZE, 1024L * 256L);
    curl_easy_setopt(curl_handle, CURLOPT_UPLOAD_BUFFERSIZE, 1024L * 256L);
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    if (config.timeout > 0) {
        // cancel if speed is less than 1 bytes/sec for timeout seconds.
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        // todo: change config to accept seconds rather than ms.
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, config.timeout / 1000L);
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT_MS, config.timeout);
    } else {
        // no timeout configured: still bound connection setup and stalled
        // transfers, otherwise a dead host/server blocks the calling thread
        // indefinitely with no way to cancel.
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, 60L);
    }

    if (m_curl_share) {
        curl_easy_setopt(curl_handle, CURLOPT_SHARE, m_curl_share);
    }
}

CURLcode MountCurlDevice::curl_perform_cancellable(CURL* curl_handle) {
    // stack lifetime is fine: the callback only reads it during this perform,
    // and every caller re-sets XFERINFODATA before the next request.
    u64 generation = GetCurlCancelGeneration();
    curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, &generation);
    curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, CurlOpCancelProgressCallback);
    return curl_easy_perform(curl_handle);
}

size_t MountCurlDevice::write_memory_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto data = static_cast<std::vector<char>*>(userdata);

    // increase by chunk size.
    const auto realsize = size * nmemb;
    if (data->capacity() < data->size() + realsize) {
        const auto rsize = std::max(realsize, data->size() + 1024 * 1024);
        data->reserve(rsize);
    }

    // store the data.
    const auto offset = data->size();
    data->resize(offset + realsize);
    std::memcpy(data->data() + offset, ptr, realsize);

    return realsize;
}

// libcurl doesn't handle html encodings, so we have to do it manually.
std::string MountCurlDevice::html_decode(const std::string_view& str) {
    struct Entry {
        std::string_view key;
        char value;
    };

    static constexpr Entry map[]{
        { "&amp;", '&' },
        { "&lt;", '<' },
        { "&gt;", '>' },
        { "&quot;", '"' },
        { "&apos;", '\'' },
        { "&nbsp;", ' ' },
        { "&#38;", '&' },
        { "&#60;", '<' },
        { "&#62;", '>' },
        { "&#34;", '"' },
        { "&#39;", '\'' },
        { "&#160;", ' ' },
        { "&#35;", '#' },
        { "&#37;", '%' },
        { "&#43;", '+' },
        { "&#61;", '=' },
        { "&#64;", '@' },
        { "&#91;", '[' },
        { "&#93;", ']' },
        { "&#123;", '{' },
        { "&#125;", '}' },
        { "&#126;", '~' },
    };

    std::string output{};
    output.reserve(str.size());

    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '&') {
            bool found = false;
            for (const auto& e : map) {
                if (!str.compare(i, e.key.length(), e.key)) {
                    output += e.value;
                    i += e.key.length() - 1; // skip ahead.
                    found = true;
                    break;
                }
            }

            if (!found) {
                output += '&';
            }
        } else {
            output += str[i];
        }
    }

    return output;
}

// Note: This url_decode uses curl_unescape and html_decode since it runs in the curl devoptab context.
// It differs from the manual UrlDecode implemented in web_http.cpp for the embedded web server.
std::string MountCurlDevice::url_decode(const std::string& str) {
    auto unescaped = curl_unescape(str.c_str(), str.length());
    if (!unescaped) {
        return str;
    }
    ON_SCOPE_EXIT(curl_free(unescaped));

    return html_decode(unescaped);
}

std::string MountCurlDevice::build_url(const std::string& _path, bool is_dir) {
    log_write("[CURL] building url for path: %s\n", _path.c_str());

    if (m_sphaira_state == SphairaShareState::Detected) {
        std::string logical_path = _path;
        if (!m_url_path.empty()) {
            auto base = m_url_path;
            if (base.ends_with('/')) {
                base.pop_back();
            }
            if (logical_path.starts_with('/')) {
                logical_path = base + logical_path;
            } else {
                logical_path = base + '/' + logical_path;
            }
        }
        if (logical_path.empty() || !logical_path.starts_with('/')) {
            logical_path = '/' + logical_path;
        }

        CURL* handle = curl ? curl : transfer_curl;
        char* escaped = handle ? curl_easy_escape(handle, logical_path.c_str(), logical_path.length()) : nullptr;
        std::string query = "path=" + std::string(escaped ? escaped : "");
        if (escaped) {
            curl_free(escaped);
        }

        const char* route = is_dir ? "/list" : "/download";
        curl_url_set(curlu, CURLUPART_PATH, route, 0);
        curl_url_set(curlu, CURLUPART_QUERY, query.c_str(), 0);

        char* encoded_url{};
        const auto rc = curl_url_get(curlu, CURLUPART_URL, &encoded_url, 0);
        curl_url_set(curlu, CURLUPART_QUERY, nullptr, 0);

        if (rc != CURLUE_OK || !encoded_url) {
            log_write("[CURL] failed to get encoded Sphaira url: %s\n", curl_url_strerror_wrap(rc));
            return {};
        }
        ON_SCOPE_EXIT(curl_free(encoded_url));

        log_write("[CURL] encoded Sphaira url: %s\n", encoded_url);
        return encoded_url;
    }

    curl_url_set(curlu, CURLUPART_QUERY, nullptr, 0);
    auto path = _path;
    if (is_dir && !path.ends_with('/')) {
        path += '/'; // append trailing slash for folder.
    }

    if (!m_url_path.empty()) {
        // join base and path with exactly one slash. A host-only source url
        // (eg "http://host:8080") parses to a base path of "/", so a browse
        // path that already starts with "/" would otherwise produce a double
        // slash ("//games/..."). Some http servers list and HEAD such urls
        // fine but stall the streaming GET, hanging installs.
        auto base = m_url_path;
        if (base.ends_with('/')) {
            base.pop_back();
        }
        if (path.starts_with('/')) {
            path = base + path;
        } else {
            path = base + '/' + path;
        }
    }

    if (!path.empty()) {
        const auto rc = curl_url_set(curlu, CURLUPART_PATH, path.c_str(), CURLU_URLENCODE);
        if (rc != CURLUE_OK) {
            log_write("[CURL] failed to set path: %s\n", curl_url_strerror_wrap(rc));
            return {};
        }
    }

    char* encoded_url;
    const auto rc = curl_url_get(curlu, CURLUPART_URL, &encoded_url, 0);
    if (rc != CURLUE_OK) {
        log_write("[CURL] failed to get encoded url: %s\n", curl_url_strerror_wrap(rc));
        return {};
    }
    ON_SCOPE_EXIT(curl_free(encoded_url));

    log_write("[CURL] encoded url: %s\n", encoded_url);
    return encoded_url;
}


int MountCurlDevice::devoptab_open(void *fileStruct, const char *path, int flags, int mode) {
    auto* state = static_cast<CurlFileState*>(fileStruct);
    // the memory devoptab hands us is raw: the state (and its std::string)
    // must be constructed in-place, assignment would crash.
    new (state) CurlFileState();

    state->curl = curl_easy_init();
    if (!state->curl) {
        state->~CurlFileState();
        return -ENOMEM;
    }

    state->url = build_url(path, false);
    if (state->url.empty()) {
        curl_easy_cleanup(state->curl);
        state->~CurlFileState();
        return -EINVAL;
    }

    state->write_mode = (flags & O_ACCMODE) != O_RDONLY;

    if (state->write_mode) {
        state->pull_data = CreatePullData(state->curl, state->url, (flags & O_APPEND) != 0);
        if (!state->pull_data) {
            curl_easy_cleanup(state->curl);
            state->~CurlFileState();
            return -EIO;
        }
    } else {
        log_write("[CURL] devoptab_open: performing HEAD request for %s\n", state->url.c_str());
        curl_set_common_options(state->curl, state->url);
        curl_easy_setopt(state->curl, CURLOPT_NOBODY, 1L);
        const auto res = curl_perform_cancellable(state->curl);
        log_write("[CURL] devoptab_open: HEAD returned %d\n", res);
        if (res == CURLE_OK) {
            double cl{};
            curl_easy_getinfo(state->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
            state->size = (cl > 0) ? (size_t)cl : 0;
            log_write("[CURL] devoptab_open: HEAD success, size: %zu\n", state->size);
        } else {
            long code{};
            curl_easy_getinfo(state->curl, CURLINFO_RESPONSE_CODE, &code);
            log_write("[CURL] devoptab_open: HEAD failed, response code: %ld\n", code);
            if (code == 404 || res == CURLE_REMOTE_FILE_NOT_FOUND) {
                curl_easy_cleanup(state->curl);
                state->~CurlFileState();
                return -ENOENT;
            }
            // the server rejects HEAD (eg dbibackend replies 501): probe the
            // size with a 1-byte ranged GET so seeks and fstat keep working.
            log_write("[CURL] devoptab_open: probing size via range\n");
            const auto total = probe_size_via_range(state->curl, state->url);
            log_write("[CURL] devoptab_open: range probe returned: %lld\n", (long long)total);
            if (total >= 0) {
                state->size = total;
            }
        }

        // the download transfer is created lazily on the first read: callers
        // often seek right after opening (container parsing), and starting a
        // full-file stream here would only be torn down again immediately.
    }

    return 0;
}

int MountCurlDevice::devoptab_close(void *fd) {
    auto* state = static_cast<CurlFileState*>(fd);
    bool curl_leaked{};
    if (state->push_data) {
        curl_leaked |= !DestroyTransfer(state->push_data);
        state->push_data = nullptr;
    }
    if (state->pull_data) {
        curl_leaked |= !DestroyTransfer(state->pull_data);
        state->pull_data = nullptr;
    }
    if (state->curl) {
        // a leaked zombie thread still uses the handle: it must outlive us.
        if (!curl_leaked) {
            curl_easy_cleanup(state->curl);
        }
        state->curl = nullptr;
    }
    state->~CurlFileState();
    return 0;
}

ssize_t MountCurlDevice::devoptab_read(void *fd, char *ptr, size_t len) {
    auto* state = static_cast<CurlFileState*>(fd);
    if (state->write_mode) {
        return -EBADF;
    }

    if (!state->push_data) {
        log_write("[CURL] devoptab_read: creating push data for %s at offset %zu\n", state->url.c_str(), state->offset);
        state->push_data = CreatePushData(state->curl, state->url, state->offset);
        if (!state->push_data) {
            log_write("[CURL] devoptab_read: failed to create push data\n");
            return -EIO;
        }
    }

    log_write("[CURL] devoptab_read: calling PullData for %zu bytes\n", len);
    size_t read = state->push_data->PullData(ptr, len, false);
    log_write("[CURL] devoptab_read: PullData returned %zu bytes\n", read);
    // a short/empty read is either a genuine eof or a dropped transfer.
    // without this check a failed download would look like a smaller file,
    // silently truncating copies.
    if (read < len && state->push_data->HasError()) {
        log_write("[CURL] devoptab_read: short read detected, error: %d\n", state->push_data->HasError());
        return -EIO;
    }

    state->offset += read;
    return read;
}

ssize_t MountCurlDevice::devoptab_write(void *fd, const char *ptr, size_t len) {
    auto* state = static_cast<CurlFileState*>(fd);
    if (!state->pull_data) {
        return -EBADF;
    }

    size_t written = state->pull_data->PushData(ptr, len, false);
    // PushData only returns 0 once the upload thread has died (error or
    // finished) - report an error instead of letting callers spin on 0.
    if (!written) {
        return -EIO;
    }

    state->offset += written;
    return written;
}

ssize_t MountCurlDevice::devoptab_seek(void *fd, off_t pos, int dir) {
    auto* state = static_cast<CurlFileState*>(fd);
    if (state->write_mode) {
        return -ENOSYS;
    }

    off_t target_offset = state->offset;
    if (dir == SEEK_SET) {
        target_offset = pos;
    } else if (dir == SEEK_CUR) {
        target_offset += pos;
    } else if (dir == SEEK_END) {
        target_offset = state->size + pos;
    }

    if (target_offset < 0) {
        return -EINVAL;
    }

    if ((size_t)target_offset != state->offset) {
        // only tear down here: the transfer at the new offset is created
        // lazily by the next read, so back-to-back seeks cost nothing.
        if (state->push_data) {
            if (!DestroyTransfer(state->push_data)) {
                // the leaked zombie thread still owns the old curl handle:
                // continue with a fresh one.
                state->curl = curl_easy_init();
            }
            state->push_data = nullptr;
            if (!state->curl) {
                return -EIO;
            }
        }
        state->offset = target_offset;
    }

    return state->offset;
}

int MountCurlDevice::devoptab_fstat(void *fd, struct stat *st) {
    auto* state = static_cast<CurlFileState*>(fd);
    std::memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
    if (state->write_mode) {
        st->st_mode |= S_IWUSR;
    }
    st->st_nlink = 1;
    st->st_size = state->size;
    return 0;
}


} // namespace sphaira::devoptab::common
