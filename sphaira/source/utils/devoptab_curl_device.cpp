#include "utils/devoptab_curl_device.hpp"
#include "log.hpp"
#include "defines.hpp"
#include <cstring>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace sphaira::devoptab::common {

// curl_url_strerror doesn't exist in the switch version of libcurl as its so old.
// todo: update libcurl and send patches to dkp.
static const char* curl_url_strerror_wrap(CURLUcode code) {
    switch (code) {
        case CURLUE_OK: return "No error";
        case CURLUE_BAD_HANDLE: return "Invalid handle";
        case CURLUE_BAD_PARTPOINTER: return "Invalid pointer to a part of the URL";
        case CURLUE_MALFORMED_INPUT: return "Malformed input";
        case CURLUE_BAD_PORT_NUMBER: return "Invalid port number";
        case CURLUE_UNSUPPORTED_SCHEME: return "Unsupported scheme";
        case CURLUE_URLDECODE: return "Failed to decode URL component";
        case CURLUE_OUT_OF_MEMORY: return "Out of memory";
        case CURLUE_USER_NOT_ALLOWED: return "User not allowed in URL";
        case CURLUE_UNKNOWN_PART: return "Unknown URL part";
        case CURLUE_NO_SCHEME: return "No scheme found in URL";
        case CURLUE_NO_USER: return "No user found in URL";
        case CURLUE_NO_PASSWORD: return "No password found in URL";
        case CURLUE_NO_OPTIONS: return "No options found in URL";
        case CURLUE_NO_HOST: return "No host found in URL";
        case CURLUE_NO_PORT: return "No port number found in URL";
        case CURLUE_NO_QUERY: return "No query found in URL";
        case CURLUE_NO_FRAGMENT: return "No fragment found in URL";
        default: return "Unknown error code";
    }
}

MountCurlDevice::~MountCurlDevice() {
    log_write("[CURL] Cleaning up mount device\n");
    if (curlu) {
        curl_url_cleanup(curlu);
    }

    if (curl) {
        curl_easy_cleanup(curl);
    }

    if (transfer_curl) {
        curl_easy_cleanup(transfer_curl);
    }

    if (m_curl_share) {
        curl_share_cleanup(m_curl_share);
    }
    log_write("[CURL] Cleaned up mount device\n");
}

bool MountCurlDevice::Mount() {
    if (m_mounted) {
        return true;
    }

    if (!curl) {
        curl = curl_easy_init();
        if (!curl) {
            log_write("[CURL] curl_easy_init() failed\n");
            return false;
        }
    }

    if (!transfer_curl) {
        transfer_curl = curl_easy_init();
        if (!transfer_curl) {
            log_write("[CURL] transfer curl_easy_init() failed\n");
            return false;
        }
    }

    // setup url, only the path is updated at runtime.
    if (!curlu) {
        curlu = curl_url();
        if (!curlu) {
            log_write("[CURL] curl_url() failed\n");
            return false;
        }

        auto url = config.url;
        if (url.starts_with("webdav://") || url.starts_with("webdavs://")) {
            log_write("[CURL] updating host: %s\n", url.c_str());
            url.replace(0, std::strlen("webdav"), "http");
            log_write("[CURL] updated host: %s\n", url.c_str());
        }

        // if (url.starts_with("sftp://")) {
        //     log_write("[CURL] updating host: %s\n", url.c_str());
        //     url.replace(0, std::strlen("sftp"), ""); // what should this be?
        //     log_write("[CURL] updated host: %s\n", url.c_str());
        // }

        const auto flags = CURLU_GUESS_SCHEME|CURLU_URLENCODE;
        CURLUcode rc = curl_url_set(curlu, CURLUPART_URL, url.c_str(), flags);
        if (rc != CURLUE_OK) {
            log_write("[CURL] curl_url_set() failed: %s\n", curl_url_strerror_wrap(rc));
            return false;
        }

        if (config.port > 0) {
            rc = curl_url_set(curlu, CURLUPART_PORT, std::to_string(config.port).c_str(), flags);
            if (rc != CURLUE_OK) {
                log_write("[CURL] curl_url_set() port failed: %s\n", curl_url_strerror_wrap(rc));
            }
        }

        if (!config.user.empty()) {
            rc = curl_url_set(curlu, CURLUPART_USER, config.user.c_str(), flags);
            if (rc != CURLUE_OK) {
                log_write("[CURL] curl_url_set() user failed: %s\n", curl_url_strerror_wrap(rc));
            }
        }

        if (!config.pass.empty()) {
            rc = curl_url_set(curlu, CURLUPART_PASSWORD, config.pass.c_str(), flags);
            if (rc != CURLUE_OK) {
                log_write("[CURL] curl_url_set() pass failed: %s\n", curl_url_strerror_wrap(rc));
            }
        }

        // try and parse the path from the url, if any.
        // eg, https://example.com/some/path/here
        char* path{};
        rc = curl_url_get(curlu, CURLUPART_PATH, &path, 0);
        if (rc == CURLUE_OK && path) {
            log_write("[CURL] base path: %s\n", path);
            m_url_path = path;
            curl_free(path);
        }
    }

    // create share handle, used to share info between curl and transfer_curl.
    if (!m_curl_share) {
        m_curl_share = curl_share_init();
        if (!m_curl_share) {
            log_write("[CURL] curl_share_init() failed\n");
            return false;
        }

        // todo: use a mutex instead.
        for (auto& e : m_rwlocks) {
            rwlockInit(&e);
        }

        static const auto lock_func = [](CURL* handle, curl_lock_data data, curl_lock_access access, void* userptr) {
            auto rwlocks = static_cast<RwLock*>(userptr);
            rwlockWriteLock(&rwlocks[data]);

            #if 0
            if (access == CURL_LOCK_ACCESS_SHARED) {
                rwlockReadLock(&rwlocks[data]);
            } else {
                rwlockWriteLock(&rwlocks[data]);
            }
            #endif
        };

        static const auto unlock_func = [](CURL* handle, curl_lock_data data, void* userptr) {
            auto rwlocks = static_cast<RwLock*>(userptr);
            rwlockWriteUnlock(&rwlocks[data]);
        };

        if (m_curl_share) {
            curl_share_setopt(m_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
            curl_share_setopt(m_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
            curl_share_setopt(m_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
            curl_share_setopt(m_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
            curl_share_setopt(m_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_PSL);
            curl_share_setopt(m_curl_share, CURLSHOPT_USERDATA, m_rwlocks);
            curl_share_setopt(m_curl_share, CURLSHOPT_LOCKFUNC, lock_func);
            curl_share_setopt(m_curl_share, CURLSHOPT_UNLOCKFUNC, unlock_func);
        }
    }

    return m_mounted = true;
}

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

void MountCurlDevice::curl_set_common_options(CURL* curl_handle, const std::string& url) {
    // NOTE: port, user and pass are set in the curl_url.
    curl_easy_reset(curl_handle);
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
    curl_easy_setopt(curl_handle, CURLOPT_BUFFERSIZE, 1024L * 64L);
    curl_easy_setopt(curl_handle, CURLOPT_UPLOAD_BUFFERSIZE, 1024L * 64L);
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    if (config.timeout > 0) {
        // cancel if speed is less than 1 bytes/sec for timeout seconds.
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        // todo: change config to accept seconds rather than ms.
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, config.timeout / 1000L);
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT_MS, config.timeout);
    }

    if (m_curl_share) {
        curl_easy_setopt(curl_handle, CURLOPT_SHARE, m_curl_share);
    }
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

size_t MountCurlDevice::write_data_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto data = static_cast<std::span<char>*>(userdata);
    const auto rsize = std::min(size * nmemb, data->size());

    std::memcpy(data->data(), ptr, rsize);
    *data = data->subspan(rsize);
    return rsize;
}

size_t MountCurlDevice::read_data_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto data = static_cast<std::span<const char>*>(userdata);
    const auto rsize = std::min(size * nmemb, data->size());

    std::memcpy(ptr, data->data(), rsize);
    *data = data->subspan(rsize);
    return rsize;
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
    auto path = _path;
    if (is_dir && !path.ends_with('/')) {
        path += '/'; // append trailing slash for folder.
    }

    if (!m_url_path.empty()) {
        if (path.starts_with('/') || m_url_path.ends_with('/')) {
            path = m_url_path + path;
        } else {
            path = m_url_path + '/' + path;
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
        curl_set_common_options(state->curl, state->url);
        curl_easy_setopt(state->curl, CURLOPT_NOBODY, 1L);
        const auto res = curl_easy_perform(state->curl);
        if (res == CURLE_OK) {
            double cl{};
            curl_easy_getinfo(state->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
            state->size = (cl > 0) ? (size_t)cl : 0;
        } else {
            long code{};
            curl_easy_getinfo(state->curl, CURLINFO_RESPONSE_CODE, &code);
            if (code == 404 || res == CURLE_REMOTE_FILE_NOT_FOUND) {
                curl_easy_cleanup(state->curl);
                state->~CurlFileState();
                return -ENOENT;
            }
            // other failures (eg the server rejects HEAD): size stays
            // unknown, the GET below may still succeed.
        }

        state->push_data = CreatePushData(state->curl, state->url, 0);
        if (!state->push_data) {
            curl_easy_cleanup(state->curl);
            state->~CurlFileState();
            return -EIO;
        }
    }

    return 0;
}

int MountCurlDevice::devoptab_close(void *fd) {
    auto* state = static_cast<CurlFileState*>(fd);
    if (state->push_data) {
        delete state->push_data;
        state->push_data = nullptr;
    }
    if (state->pull_data) {
        delete state->pull_data;
        state->pull_data = nullptr;
    }
    if (state->curl) {
        curl_easy_cleanup(state->curl);
        state->curl = nullptr;
    }
    state->~CurlFileState();
    return 0;
}

ssize_t MountCurlDevice::devoptab_read(void *fd, char *ptr, size_t len) {
    auto* state = static_cast<CurlFileState*>(fd);
    if (!state->push_data) {
        return -EBADF;
    }

    size_t read = state->push_data->PullData(ptr, len, false);
    // a short/empty read is either a genuine eof or a dropped transfer.
    // without this check a failed download would look like a smaller file,
    // silently truncating copies.
    if (read < len && state->push_data->HasError()) {
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
        if (state->push_data) {
            delete state->push_data;
        }
        state->offset = target_offset;
        state->push_data = CreatePushData(state->curl, state->url, state->offset);
        if (!state->push_data) {
            return -EIO;
        }
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

namespace {

std::string to_lower_copy(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

// finds the next xml tag with the given local name, ignoring any namespace
// prefix: "<d:response", "<D:response", "<ns0:response" and "<response" all
// match "response". haystack and name must be lowercase.
size_t find_xml_tag(const std::string& haystack, size_t pos, std::string_view name, bool closing = false) {
    while ((pos = haystack.find('<', pos)) != std::string::npos) {
        auto p = pos + 1;
        if (closing) {
            if (p >= haystack.size() || haystack[p] != '/') {
                pos = p;
                continue;
            }
            p++;
        }

        const auto end = haystack.find_first_of(" \t\r\n/>", p);
        if (end == std::string::npos) {
            break;
        }

        std::string_view tag{haystack.data() + p, end - p};
        if (const auto colon = tag.rfind(':'); colon != std::string_view::npos) {
            tag = tag.substr(colon + 1);
        }

        if (tag == name) {
            return pos;
        }

        pos = end;
    }

    return std::string::npos;
}

} // namespace

int MountCurlDevice::devoptab_diropen(void* fd, const char *path) {
    auto* state = static_cast<CurlDirState*>(fd);
    new (state) CurlDirState();

    std::string full_url = build_url(path, true);
    log_write("[CURL] diropen url: %s\n", full_url.c_str());

    std::vector<char> response_data;

    curl_easy_reset(curl);
    curl_set_common_options(curl, full_url);

    bool is_ftp = full_url.starts_with("ftp://") || full_url.starts_with("ftps://");

    // the header list must outlive curl_easy_perform() below.
    struct curl_slist* list = nullptr;
    ON_SCOPE_EXIT(curl_slist_free_all(list));

    if (!is_ftp) {
        // for ftp, curl issues a LIST for urls with a trailing slash,
        // which gives us entry types and sizes (unlike NLST).
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
        list = curl_slist_append(nullptr, "Depth: 1");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    long code{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    if (is_ftp && res != CURLE_OK) {
        log_write("[CURL] diropen perform failed: %s\n", curl_easy_strerror(res));
        return -EIO;
    }

    std::string data_str(response_data.begin(), response_data.end());
    std::string lc = to_lower_copy(data_str);

    bool webdav = false;
    if (!is_ftp) {
        webdav = res == CURLE_OK && (code == 207 || (code >= 200 && code < 300 && lc.find("multistatus") != std::string::npos));
        if (!webdav) {
            // not a webdav server (or PROPFIND was rejected): fall back to
            // fetching the plain html directory index.
            log_write("[CURL] diropen PROPFIND unavailable (res=%d, code=%ld), trying html index\n", res, code);

            response_data.clear();
            curl_easy_reset(curl);
            curl_set_common_options(curl, full_url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

            res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            }
            if (res != CURLE_OK || code < 200 || code >= 300) {
                log_write("[CURL] diropen html index failed (res=%d, code=%ld)\n", res, code);
                return -EIO;
            }

            data_str.assign(response_data.begin(), response_data.end());
            lc = to_lower_copy(data_str);
        }
    }

    if (is_ftp) {
        std::string line;
        std::istringstream stream(data_str);
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty() || line.starts_with("total ")) {
                continue;
            }

            std::string name;
            bool is_dir = false;
            s64 size = 0;

            if (line[0] == 'd' || line[0] == '-' || line[0] == 'l') {
                // unix LIST: "drwxr-xr-x 2 owner group 4096 Jan 16 19:00 name with spaces"
                is_dir = line[0] == 'd';

                std::istringstream ls(line);
                std::string tok, size_tok;
                int field = 0;
                while (field < 8 && (ls >> tok)) {
                    if (field == 4) {
                        size_tok = tok;
                    }
                    field++;
                }
                if (field != 8) {
                    continue;
                }

                std::getline(ls, name);
                name.erase(0, name.find_first_not_of(' '));
                size = std::strtoll(size_tok.c_str(), nullptr, 10);

                // symlink: strip the " -> target" part, guess type by extension.
                if (line[0] == 'l') {
                    if (const auto arrow = name.find(" -> "); arrow != std::string::npos) {
                        name.resize(arrow);
                    }
                    is_dir = name.find('.') == std::string::npos;
                }
            } else if (std::isdigit(static_cast<unsigned char>(line[0]))) {
                // dos LIST: "01-16-26  07:39PM  <DIR>  name" / "01-16-26 07:39PM 123456 name"
                std::istringstream ls(line);
                std::string date, time, third;
                if (!(ls >> date >> time >> third)) {
                    continue;
                }

                if (third == "<DIR>") {
                    is_dir = true;
                } else {
                    size = std::strtoll(third.c_str(), nullptr, 10);
                }

                std::getline(ls, name);
                name.erase(0, name.find_first_not_of(' '));
            } else {
                // unknown format (likely an NLST-style plain name).
                name = line;
                is_dir = name.find('.') == std::string::npos;
            }

            if (name.empty() || name == "." || name == "..") {
                continue;
            }

            dircache entry{};
            entry.name = name;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + name;
            entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            entry.st.st_size = size;
            entry.st.st_nlink = 1;
            state->entries.push_back(entry);
        }
    } else if (webdav) {
        size_t pos = 0;
        while ((pos = find_xml_tag(lc, pos, "response")) != std::string::npos) {
            auto resp_end = find_xml_tag(lc, pos + 1, "response", true);
            if (resp_end == std::string::npos) {
                resp_end = data_str.size();
            }
            const auto next_pos = resp_end;

            const auto href_pos = find_xml_tag(lc, pos, "href");
            if (href_pos == std::string::npos || href_pos >= resp_end) {
                pos = next_pos;
                continue;
            }

            const auto href_start = data_str.find('>', href_pos) + 1;
            const auto href_end = data_str.find('<', href_start);
            if (href_end == std::string::npos) {
                break;
            }

            const std::string href = data_str.substr(href_start, href_end - href_start);
            std::string decoded_href = url_decode(href);

            if (decoded_href.ends_with('/')) {
                decoded_href.pop_back();
            }
            const auto last_slash = decoded_href.find_last_of('/');
            const std::string name = (last_slash != std::string::npos) ? decoded_href.substr(last_slash + 1) : decoded_href;

            if (name.empty() || name == "." || name == "..") {
                pos = next_pos;
                continue;
            }

            dircache entry{};
            entry.name = name;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + name;

            bool is_dir = href.ends_with('/');
            const auto rt_pos = find_xml_tag(lc, pos, "resourcetype");
            if (rt_pos != std::string::npos && rt_pos < resp_end) {
                auto rt_end = find_xml_tag(lc, rt_pos, "resourcetype", true);
                if (rt_end == std::string::npos || rt_end > resp_end) {
                    rt_end = resp_end;
                }
                if (lc.find("collection", rt_pos) < rt_end) {
                    is_dir = true;
                }
            }

            entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            entry.st.st_nlink = 1;

            const auto cl_pos = find_xml_tag(lc, pos, "getcontentlength");
            if (cl_pos != std::string::npos && cl_pos < resp_end) {
                const auto cl_start = data_str.find('>', cl_pos) + 1;
                const auto cl_end = data_str.find('<', cl_start);
                if (cl_end != std::string::npos) {
                    entry.st.st_size = std::strtoll(data_str.substr(cl_start, cl_end - cl_start).c_str(), nullptr, 10);
                }
            }

            std::string current_dir_url = full_url;
            if (current_dir_url.ends_with('/')) current_dir_url.pop_back();
            std::string item_url = build_url(entry.fullpathname, is_dir);
            if (item_url.ends_with('/')) item_url.pop_back();

            if (item_url != current_dir_url) {
                state->entries.push_back(entry);
            }

            pos = next_pos;
        }
    } else {
        // plain http server: parse <a href="..."> links from the index page.
        // extract the path component of the directory url so that absolute
        // links ("/Games/") can be matched against it.
        std::string cur_path = "/";
        if (const auto scheme_end = full_url.find("://"); scheme_end != std::string::npos) {
            if (const auto path_start = full_url.find('/', scheme_end + 3); path_start != std::string::npos) {
                cur_path = full_url.substr(path_start);
            }
        }
        if (!cur_path.ends_with('/')) {
            cur_path += '/';
        }

        size_t pos = 0;
        while ((pos = lc.find("<a", pos)) != std::string::npos) {
            if (pos + 2 >= lc.size() || !std::isspace(static_cast<unsigned char>(lc[pos + 2]))) {
                pos += 2;
                continue;
            }

            const auto tag_pos = pos;
            const auto tag_end = data_str.find('>', tag_pos);
            if (tag_end == std::string::npos) {
                break;
            }
            pos = tag_end;

            const auto href_attr = lc.find("href", tag_pos);
            if (href_attr == std::string::npos || href_attr > tag_end) {
                continue;
            }
            const auto eq = data_str.find('=', href_attr);
            if (eq == std::string::npos || eq > tag_end) {
                continue;
            }
            const auto vstart = data_str.find_first_not_of(" \t\r\n", eq + 1);
            if (vstart == std::string::npos || vstart > tag_end) {
                continue;
            }

            std::string href;
            if (data_str[vstart] == '"' || data_str[vstart] == '\'') {
                const auto vend = data_str.find(data_str[vstart], vstart + 1);
                if (vend == std::string::npos || vend > tag_end) {
                    continue;
                }
                href = data_str.substr(vstart + 1, vend - vstart - 1);
            } else {
                const auto vend = data_str.find_first_of(" \t\r\n>", vstart);
                href = data_str.substr(vstart, vend - vstart);
            }

            // drop sort links ("?C=N;O=D") and fragments.
            if (const auto cut = href.find_first_of("?#"); cut != std::string::npos) {
                href.resize(cut);
            }

            if (href.starts_with("http://") || href.starts_with("https://")) {
                // absolute link: only accept it if it points inside this directory.
                if (!href.starts_with(full_url)) {
                    continue;
                }
                href = href.substr(full_url.size());
            } else if (href.starts_with("//")) {
                continue;
            } else if (const auto colon = href.find(':'); colon != std::string::npos && colon < href.find('/')) {
                // skip other schemes (mailto:, javascript:, ...).
                continue;
            }

            if (href.starts_with('/')) {
                // absolute path: must be inside the current directory.
                if (!href.starts_with(cur_path)) {
                    continue;
                }
                href = href.substr(cur_path.size());
            }

            if (href.empty() || href == ".." || href.starts_with("../") || href == "./") {
                continue;
            }

            const bool is_dir = href.ends_with('/');
            if (is_dir) {
                href.pop_back();
            }

            const std::string name = url_decode(href);
            // only direct children of this directory.
            if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) {
                continue;
            }

            const auto exists = std::find_if(state->entries.begin(), state->entries.end(), [&](auto& e) {
                return e.name == name;
            }) != state->entries.end();
            if (exists) {
                continue;
            }

            dircache entry{};
            entry.name = name;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + name;
            entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            entry.st.st_nlink = 1;
            state->entries.push_back(entry);
        }
    }

    return 0;
}

int MountCurlDevice::devoptab_dirnext(void* fd, char *filename, struct stat *filestat) {
    auto* state = static_cast<CurlDirState*>(fd);
    if (state->index >= state->entries.size()) {
        return -1;
    }

    const auto& entry = state->entries[state->index++];
    std::strncpy(filename, entry.name.c_str(), NAME_MAX - 1);
    filename[NAME_MAX - 1] = '\0';
    std::memcpy(filestat, &entry.st, sizeof(struct stat));
    return 0;
}

int MountCurlDevice::devoptab_dirclose(void* fd) {
    auto* state = static_cast<CurlDirState*>(fd);
    state->~CurlDirState();
    return 0;
}

int MountCurlDevice::devoptab_dirreset(void* fd) {
    auto* state = static_cast<CurlDirState*>(fd);
    state->index = 0;
    return 0;
}

int MountCurlDevice::devoptab_lstat(const char *path, struct stat *st) {
    std::memset(st, 0, sizeof(*st));
    std::string url = build_url(path, false);
    if (url.empty()) {
        return -EINVAL;
    }

    bool is_ftp = url.starts_with("ftp://") || url.starts_with("ftps://");
    if (is_ftp) {
        std::string p_str = path;
        if (p_str.ends_with('/') || p_str.find('.') == std::string::npos) {
            st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
        } else {
            st->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        }
        st->st_nlink = 1;
        return 0;
    }

    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    curl_easy_setopt(transfer_curl, CURLOPT_NOBODY, 1L);

    CURLcode res = curl_easy_perform(transfer_curl);
    if (res != CURLE_OK) {
        std::string dir_url = build_url(path, true);
        curl_easy_reset(transfer_curl);
        curl_set_common_options(transfer_curl, dir_url);
        curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
        struct curl_slist* list = curl_slist_append(nullptr, "Depth: 0");
        curl_easy_setopt(transfer_curl, CURLOPT_HTTPHEADER, list);
        ON_SCOPE_EXIT(curl_slist_free_all(list));

        std::vector<char> response_data;
        curl_easy_setopt(transfer_curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(transfer_curl, CURLOPT_WRITEDATA, &response_data);

        res = curl_easy_perform(transfer_curl);
        if (res == CURLE_OK) {
            st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
            st->st_nlink = 1;
            return 0;
        }
        return -ENOENT;
    }

    // plain http servers redirect "/Games" -> "/Games/": if the effective
    // url after redirects has a trailing slash, this is a directory.
    char* effective_url{};
    curl_easy_getinfo(transfer_curl, CURLINFO_EFFECTIVE_URL, &effective_url);
    if (effective_url && std::string_view{effective_url}.ends_with('/')) {
        st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
        st->st_nlink = 1;
        return 0;
    }

    st->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    st->st_nlink = 1;
    double cl{};
    curl_easy_getinfo(transfer_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
    st->st_size = (cl > 0) ? (size_t)cl : 0;
    return 0;
}

int MountCurlDevice::devoptab_unlink(const char *path) {
    std::string url = build_url(path, false);
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (curl_easy_perform(transfer_curl) == CURLE_OK) {
        long code{};
        curl_easy_getinfo(transfer_curl, CURLINFO_RESPONSE_CODE, &code);
        if (code >= 200 && code < 300) {
            return 0;
        }
    }
    return -EIO;
}

int MountCurlDevice::devoptab_rmdir(const char *path) {
    return devoptab_unlink(path);
}

int MountCurlDevice::devoptab_mkdir(const char *path, int mode) {
    std::string url = build_url(path, true);
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    bool is_ftp = url.starts_with("ftp://") || url.starts_with("ftps://");
    // the quote list must outlive curl_easy_perform() below.
    struct curl_slist* list = nullptr;
    ON_SCOPE_EXIT(curl_slist_free_all(list));
    if (is_ftp) {
        list = curl_slist_append(nullptr, (std::string("MKD ") + path).c_str());
        curl_easy_setopt(transfer_curl, CURLOPT_POSTQUOTE, list);
    } else {
        curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "MKCOL");
    }
    if (curl_easy_perform(transfer_curl) == CURLE_OK) {
        long code{};
        curl_easy_getinfo(transfer_curl, CURLINFO_RESPONSE_CODE, &code);
        if (code >= 200 && code < 300) {
            return 0;
        }
    }
    return -EIO;
}

int MountCurlDevice::devoptab_rename(const char *oldName, const char *newName) {
    std::string url = build_url(oldName, false);
    std::string dst_url = build_url(newName, false);
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "MOVE");
    struct curl_slist* list = curl_slist_append(nullptr, (std::string("Destination: ") + dst_url).c_str());
    curl_easy_setopt(transfer_curl, CURLOPT_HTTPHEADER, list);
    ON_SCOPE_EXIT(curl_slist_free_all(list));
    if (curl_easy_perform(transfer_curl) == CURLE_OK) {
        long code{};
        curl_easy_getinfo(transfer_curl, CURLINFO_RESPONSE_CODE, &code);
        if (code >= 200 && code < 300) {
            return 0;
        }
    }
    return -EIO;
}

} // namespace sphaira::devoptab::common
