#include "utils/devoptab_curl_device.hpp"
#include "log.hpp"
#include "defines.hpp"
#include <cstring>
#include <algorithm>

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
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
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

} // namespace sphaira::devoptab::common
