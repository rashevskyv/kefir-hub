#include "utils/devoptab_curl_device.hpp"
#include "log.hpp"
#include "defines.hpp"
#include <cstring>
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
    *state = {};

    state->curl = curl_easy_init();
    if (!state->curl) {
        return -ENOMEM;
    }

    state->url = build_url(path, false);
    if (state->url.empty()) {
        curl_easy_cleanup(state->curl);
        state->curl = nullptr;
        return -EINVAL;
    }

    state->write_mode = (flags & (O_WRONLY | O_RDWR | O_CREAT));

    if (state->write_mode) {
        state->pull_data = CreatePullData(state->curl, state->url, (flags & O_APPEND) != 0);
        if (!state->pull_data) {
            curl_easy_cleanup(state->curl);
            state->curl = nullptr;
            return -EIO;
        }
    } else {
        curl_set_common_options(state->curl, state->url);
        curl_easy_setopt(state->curl, CURLOPT_NOBODY, 1L);
        if (curl_easy_perform(state->curl) == CURLE_OK) {
            double cl{};
            curl_easy_getinfo(state->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
            state->size = (cl > 0) ? (size_t)cl : 0;
        }

        state->push_data = CreatePushData(state->curl, state->url, 0);
        if (!state->push_data) {
            curl_easy_cleanup(state->curl);
            state->curl = nullptr;
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
    return 0;
}

ssize_t MountCurlDevice::devoptab_read(void *fd, char *ptr, size_t len) {
    auto* state = static_cast<CurlFileState*>(fd);
    if (!state->push_data) {
        return -EBADF;
    }

    size_t read = state->push_data->PullData(ptr, len, false);
    state->offset += read;
    return read;
}

ssize_t MountCurlDevice::devoptab_write(void *fd, const char *ptr, size_t len) {
    auto* state = static_cast<CurlFileState*>(fd);
    if (!state->pull_data) {
        return -EBADF;
    }

    size_t written = state->pull_data->PushData(ptr, len, false);
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

int MountCurlDevice::devoptab_diropen(void* fd, const char *path) {
    auto* state = static_cast<CurlDirState*>(fd);
    new (state) CurlDirState();

    std::string full_url = build_url(path, true);
    log_write("[CURL] diropen url: %s\n", full_url.c_str());

    std::vector<char> response_data;

    curl_easy_reset(curl);
    curl_set_common_options(curl, full_url);

    bool is_ftp = full_url.starts_with("ftp://") || full_url.starts_with("ftps://");

    if (is_ftp) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "NLST");
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
        struct curl_slist* list = curl_slist_append(nullptr, "Depth: 1");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
        ON_SCOPE_EXIT(curl_slist_free_all(list));
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        log_write("[CURL] diropen perform failed: %s\n", curl_easy_strerror(res));
        return -EIO;
    }

    std::string data_str(response_data.begin(), response_data.end());

    if (is_ftp) {
        std::string line;
        std::istringstream stream(data_str);
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty() || line == "." || line == "..") {
                continue;
            }

            dircache entry{};
            entry.name = line;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + line;
            entry.st.st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            if (line.find('.') == std::string::npos) {
                entry.st.st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
            }
            entry.st.st_nlink = 1;
            state->entries.push_back(entry);
        }
    } else {
        size_t pos = 0;
        while (true) {
            pos = data_str.find("<d:response", pos);
            if (pos == std::string::npos) {
                pos = data_str.find("<response", pos);
            }
            if (pos == std::string::npos) {
                break;
            }

            size_t href_pos = data_str.find("<d:href>", pos);
            if (href_pos == std::string::npos) href_pos = data_str.find("<href>", pos);
            if (href_pos == std::string::npos) {
                pos++;
                continue;
            }

            size_t href_start = data_str.find('>', href_pos) + 1;
            size_t href_end = data_str.find("</", href_start);
            if (href_end == std::string::npos) {
                break;
            }

            std::string href = data_str.substr(href_start, href_end - href_start);
            std::string decoded_href = url_decode(href);

            if (decoded_href.ends_with('/')) {
                decoded_href.pop_back();
            }
            size_t last_slash = decoded_href.find_last_of('/');
            std::string name = (last_slash != std::string::npos) ? decoded_href.substr(last_slash + 1) : decoded_href;

            if (name.empty() || name == "." || name == "..") {
                pos = href_end;
                continue;
            }

            dircache entry{};
            entry.name = name;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + name;

            size_t rt_pos = data_str.find("<d:resourcetype>", pos);
            if (rt_pos == std::string::npos) rt_pos = data_str.find("<resourcetype>", pos);
            bool is_dir = false;
            if (rt_pos != std::string::npos && rt_pos < data_str.find("</d:response>", pos)) {
                size_t rt_end = data_str.find("</d:resourcetype>", rt_pos);
                if (rt_end == std::string::npos) rt_end = data_str.find("</resourcetype>", rt_pos);
                std::string rt_xml = data_str.substr(rt_pos, rt_end - rt_pos);
                if (rt_xml.find("collection") != std::string::npos) {
                    is_dir = true;
                }
            }

            entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            entry.st.st_nlink = 1;

            size_t cl_pos = data_str.find("<d:getcontentlength>", pos);
            if (cl_pos == std::string::npos) cl_pos = data_str.find("<getcontentlength>", pos);
            if (cl_pos != std::string::npos && cl_pos < data_str.find("</d:response>", pos)) {
                size_t cl_start = data_str.find('>', cl_pos) + 1;
                size_t cl_end = data_str.find("</", cl_start);
                entry.st.st_size = std::strtoll(data_str.substr(cl_start, cl_end - cl_start).c_str(), nullptr, 10);
            }

            std::string current_dir_url = full_url;
            if (current_dir_url.ends_with('/')) current_dir_url.pop_back();
            std::string item_url = build_url(entry.fullpathname, is_dir);
            if (item_url.ends_with('/')) item_url.pop_back();

            if (item_url != current_dir_url) {
                state->entries.push_back(entry);
            }

            pos = href_end;
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
    if (is_ftp) {
        struct curl_slist* list = curl_slist_append(nullptr, (std::string("MKD ") + path).c_str());
        curl_easy_setopt(transfer_curl, CURLOPT_POSTQUOTE, list);
        ON_SCOPE_EXIT(curl_slist_free_all(list));
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
