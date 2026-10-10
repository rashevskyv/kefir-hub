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

        // NOTE: credentials are intentionally NOT embedded in the url. They
        // are applied per-request via CURLOPT_USERNAME/CURLOPT_PASSWORD in
        // curl_set_common_options() instead. This mirrors download.cpp, which
        // is what makes its source probe succeed where browsing used to fail:
        // when a http:// source 301-redirects to https:// (as the redirected
        // "Sphaira WebDAV" server does), the device's curl re-sends explicit
        // credentials across the redirect but drops ones parsed from the url,
        // replying 401 to every browse. Keeping them out of the url also stops
        // the password from being written to log.txt.

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


namespace {

struct RangeProbe {
    s64 total{-1};
};

size_t range_probe_header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* probe = static_cast<RangeProbe*>(userdata);
    std::string line{buffer, size * nitems};
    std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    // "content-range: bytes 0-0/354008"
    if (line.starts_with("content-range:")) {
        if (const auto slash = line.rfind('/'); slash != std::string::npos) {
            probe->total = std::strtoll(line.c_str() + slash + 1, nullptr, 10);
        }
    }
    return size * nitems;
}

size_t range_probe_write_callback(char*, size_t, size_t, void*) {
    // headers are all we need, abort before streaming the body.
    return 0;
}

} // namespace

s64 MountCurlDevice::probe_size_via_range(CURL* handle, const std::string& url, bool* out_is_dir) {
    if (out_is_dir) {
        *out_is_dir = false;
    }

    curl_easy_reset(handle);
    curl_set_common_options(handle, url);
    curl_easy_setopt(handle, CURLOPT_RANGE, "0-0");

    RangeProbe probe{};
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, range_probe_header_callback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &probe);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, range_probe_write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, nullptr);

    const auto res = curl_perform_cancellable(handle);
    long code{};
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code);

    if (out_is_dir) {
        char* effective_url{};
        curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &effective_url);
        if (effective_url && std::string_view{effective_url}.ends_with('/')) {
            *out_is_dir = true;
        }
    }

    if (code == 206 && probe.total >= 0) {
        return probe.total;
    }
    if (code == 416) {
        // range not satisfiable on offset 0: an empty file.
        return 0;
    }
    if (code >= 200 && code < 300) {
        // server ignored the range, content-length is the full size.
        curl_off_t cl{-1};
        curl_easy_getinfo(handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
        if (cl >= 0) {
            return cl;
        }
    }

    log_write("[CURL] range size probe failed (res=%d, code=%ld): %s\n", res, code, url.c_str());
    return -1;
}

int MountCurlDevice::devoptab_lstat(const char *path, struct stat *st) {
    std::memset(st, 0, sizeof(*st));
    std::string url = build_url(path, false);
    if (url.empty()) {
        return -EINVAL;
    }

    bool is_ftp = url.starts_with("ftp://") || url.starts_with("ftps://");
    if (is_ftp) {
        const auto key = ftp_key(path);
        if (key == "/") {
            st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
            st->st_nlink = 1;
            return 0;
        }

        // answer from the listing of the parent folder (one LIST, then every
        // name in it is known); a name the listing lacks does not exist.
        const auto parent = ftp_key(key.substr(0, key.find_last_of('/')));
        for (int attempt = 0; attempt < 2; attempt++) {
            {
                SCOPED_MUTEX(&m_handle_mutex);
                if (const auto it = m_ftp_stat.find(key); it != m_ftp_stat.end()) {
                    *st = it->second;
                    return 0;
                }
                if (m_ftp_listed.count(parent)) {
                    return -ENOENT;
                }
            }

            alignas(CurlDirState) unsigned char dir_buf[sizeof(CurlDirState)];
            const auto rc = devoptab_diropen(dir_buf, parent.c_str());
            devoptab_dirclose(dir_buf);
            if (rc != 0) {
                break;
            }
        }

        // the folder could not be listed: guess from the name, as before.
        std::string p_str = path;
        if (p_str.ends_with('/') || p_str.find('.') == std::string::npos) {
            st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
        } else {
            st->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        }
        st->st_nlink = 1;
        return 0;
    }

    SCOPED_MUTEX(&m_handle_mutex);
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    curl_easy_setopt(transfer_curl, CURLOPT_NOBODY, 1L);

    CURLcode res = curl_perform_cancellable(transfer_curl);
    if (res != CURLE_OK) {
        // some servers (eg dbibackend) do not implement HEAD at all:
        // probe with a 1-byte ranged GET instead.
        bool probe_is_dir{};
        const auto total = probe_size_via_range(transfer_curl, url, &probe_is_dir);
        if (probe_is_dir) {
            st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
            st->st_nlink = 1;
            return 0;
        }
        if (total >= 0) {
            st->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            st->st_nlink = 1;
            st->st_size = total;
            return 0;
        }

        std::string dir_url = build_url(path, true);
        curl_easy_reset(transfer_curl);
        curl_set_common_options(transfer_curl, dir_url);
        struct curl_slist* list = nullptr;
        if (m_sphaira_state != SphairaShareState::Detected) {
            curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
            list = curl_slist_append(nullptr, "Depth: 0");
            curl_easy_setopt(transfer_curl, CURLOPT_HTTPHEADER, list);
        }
        ON_SCOPE_EXIT(curl_slist_free_all(list));

        std::vector<char> response_data;
        curl_easy_setopt(transfer_curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(transfer_curl, CURLOPT_WRITEDATA, &response_data);

        res = curl_perform_cancellable(transfer_curl);
        long code{};
        if (res == CURLE_OK) {
            curl_easy_getinfo(transfer_curl, CURLINFO_RESPONSE_CODE, &code);
        }
        if (res == CURLE_OK && (m_sphaira_state != SphairaShareState::Detected || code == 200)) {
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
    SCOPED_MUTEX(&m_handle_mutex);
    std::string url = build_url(path, false);
    if (url.starts_with("ftp://") || url.starts_with("ftps://")) {
        // ftp has no DELETE verb: DELE removes a file (rmdir sends RMD).
        const auto rc = ftp_quote({"DELE " + ftp_rel_path(path)});
        m_ftp_stat.clear();
        m_ftp_listed.clear();
        return rc;
    }
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (curl_perform_cancellable(transfer_curl) == CURLE_OK) {
        long code{};
        curl_easy_getinfo(transfer_curl, CURLINFO_RESPONSE_CODE, &code);
        if (code >= 200 && code < 300) {
            return 0;
        }
    }
    return -EIO;
}

int MountCurlDevice::devoptab_rmdir(const char *path) {
    {
        SCOPED_MUTEX(&m_handle_mutex);
        if (const auto url = build_url(path, true); url.starts_with("ftp://") || url.starts_with("ftps://")) {
            const auto rc = ftp_quote({"RMD " + ftp_rel_path(path)});
            m_ftp_stat.clear();
            m_ftp_listed.clear();
            return rc;
        }
    }
    return devoptab_unlink(path);
}

int MountCurlDevice::devoptab_mkdir(const char *path, int mode) {
    SCOPED_MUTEX(&m_handle_mutex);
    std::string url = build_url(path, true);
    if (url.starts_with("ftp://") || url.starts_with("ftps://")) {
        const auto rc = ftp_quote({"MKD " + ftp_rel_path(path)});
        m_ftp_stat.clear();
        m_ftp_listed.clear();
        return rc;
    }
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "MKCOL");
    if (curl_perform_cancellable(transfer_curl) == CURLE_OK) {
        long code{};
        curl_easy_getinfo(transfer_curl, CURLINFO_RESPONSE_CODE, &code);
        if (code >= 200 && code < 300) {
            return 0;
        }
    }
    return -EIO;
}

int MountCurlDevice::devoptab_rename(const char *oldName, const char *newName) {
    SCOPED_MUTEX(&m_handle_mutex);
    std::string url = build_url(oldName, false);
    std::string dst_url = build_url(newName, false);
    if (url.starts_with("ftp://") || url.starts_with("ftps://")) {
        const auto rc = ftp_quote({"RNFR " + ftp_rel_path(oldName), "RNTO " + ftp_rel_path(newName)});
        m_ftp_stat.clear();
        m_ftp_listed.clear();
        return rc;
    }
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, url);
    curl_easy_setopt(transfer_curl, CURLOPT_CUSTOMREQUEST, "MOVE");
    struct curl_slist* list = curl_slist_append(nullptr, (std::string("Destination: ") + dst_url).c_str());
    curl_easy_setopt(transfer_curl, CURLOPT_HTTPHEADER, list);
    ON_SCOPE_EXIT(curl_slist_free_all(list));
    if (curl_perform_cancellable(transfer_curl) == CURLE_OK) {
        long code{};
        curl_easy_getinfo(transfer_curl, CURLINFO_RESPONSE_CODE, &code);
        if (code >= 200 && code < 300) {
            return 0;
        }
    }
    return -EIO;
}

std::string MountCurlDevice::ftp_rel_path(const std::string& path) const {
    // relative to the login folder, like the path of the url build_url makes.
    std::string out = m_url_path;
    if (out.ends_with('/')) {
        out.pop_back();
    }
    out += ftp_key(path);
    out.erase(0, out.find_first_not_of('/'));
    return out;
}

// runs ftp commands on the connection and nothing else. caller holds m_handle_mutex.
int MountCurlDevice::ftp_quote(const std::vector<std::string>& commands) {
    curl_easy_reset(transfer_curl);
    curl_set_common_options(transfer_curl, build_url("/", true));
    curl_easy_setopt(transfer_curl, CURLOPT_NOBODY, 1L);
    // a reused connection still stands in the last folder listed, and these
    // names are relative to the login folder: log in again, then drop it.
    curl_easy_setopt(transfer_curl, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(transfer_curl, CURLOPT_FORBID_REUSE, 1L);

    struct curl_slist* list = nullptr;
    ON_SCOPE_EXIT(curl_slist_free_all(list));
    for (const auto& c : commands) {
        list = curl_slist_append(list, c.c_str());
    }
    curl_easy_setopt(transfer_curl, CURLOPT_QUOTE, list);

    const auto res = curl_perform_cancellable(transfer_curl);
    if (res != CURLE_OK) {
        log_write("[CURL] ftp command failed: %s\n", curl_easy_strerror(res));
        return -EIO;
    }
    return 0;
}

} // namespace sphaira::devoptab::common
