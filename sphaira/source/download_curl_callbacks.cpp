#include "download_internal.hpp"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace sphaira::curl {
void GetDownloadTempPath(fs::FsPath& buf) {
    static Mutex mutex{};
    static u64 count{};

    mutexLock(&mutex);
    const auto count_copy = count;
    count++;
    mutexUnlock(&mutex);

    std::snprintf(buf, sizeof(buf), "/switch/sphaira/cache/download_temp%lu", count_copy);
}

auto ProgressCallbackFunc2(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) -> int {
    auto api = static_cast<Api*>(clientp);
    if (!g_running || (api && api->GetToken().stop_requested())) {
        return 1;
    }

    // log_write("pcall called %u %u %u %u\n", dltotal, dlnow, ultotal, ulnow);
    if (api && api->GetOnProgress() && !api->GetOnProgress()(dltotal, dlnow, ultotal, ulnow)) {
        return 1;
    }

    Yield();
    return 0;
}

auto SeekCallback(void *clientp, curl_off_t offset, int origin) -> int {
    if (!g_running) {
        return 0;
    }

    auto data_struct = static_cast<UploadStruct*>(clientp);

    if (origin == SEEK_SET) {
        offset = offset;
    } else if (origin == SEEK_CUR) {
        offset = data_struct->offset + offset;
    } else if (origin == SEEK_END) {
        offset = data_struct->size;
    }

    if (offset < 0 || offset > data_struct->size) {
        return CURL_SEEKFUNC_CANTSEEK;
    }

    data_struct->offset = offset;
    return CURL_SEEKFUNC_OK;
}

auto SeekCustomCallback(void *clientp, curl_off_t offset, int origin) -> int {
    if (!g_running) {
        return 0;
    }

    auto data_struct = static_cast<SeekCustomData*>(clientp);
    if (origin != SEEK_SET || offset < 0 || offset > data_struct->size) {
        return CURL_SEEKFUNC_CANTSEEK;
    }

    if (!data_struct->cb(offset)) {
        return CURL_SEEKFUNC_CANTSEEK;
    }

    return CURL_SEEKFUNC_OK;
}

auto ReadFileCallback(char *ptr, size_t size, size_t nmemb, void *userp) -> size_t {
    if (!g_running) {
        return 0;
    }

    auto data_struct = static_cast<UploadStruct*>(userp);
    const auto realsize = size * nmemb;

    u64 bytes_read;
    if (R_FAILED(data_struct->f.Read(data_struct->offset, ptr, realsize, FsReadOption_None, &bytes_read))) {
        log_write("reading file error\n");
        return 0;
    }

    data_struct->offset += bytes_read;
    Yield();
    return bytes_read;
}

auto ReadMemoryCallback(char *ptr, size_t size, size_t nmemb, void *userp) -> size_t {
    if (!g_running) {
        return 0;
    }

    auto data_struct = static_cast<UploadStruct*>(userp);
    auto realsize = size * nmemb;
    realsize = std::min(realsize, data_struct->data.size() - data_struct->offset);

    std::memcpy(ptr, data_struct->data.data(), realsize);
    data_struct->offset += realsize;

    Yield();
    return realsize;
}

auto ReadCustomCallback(char *ptr, size_t size, size_t nmemb, void *userp) -> size_t {
    if (!g_running) {
        return 0;
    }

    auto data_struct = static_cast<UploadInfo*>(userp);
    auto realsize = size * nmemb;
    const auto result = data_struct->m_callback(ptr, realsize);

    Yield();
    return result;
}

auto WriteMemoryCallback(void *contents, size_t size, size_t num_files, void *userp) -> size_t {
    if (!g_running) {
        return 0;
    }

    auto data_struct = static_cast<DataStruct*>(userp);
    const auto realsize = size * num_files;

    // give it more memory
    if (data_struct->data.capacity() < data_struct->offset + realsize) {
        data_struct->data.reserve(data_struct->data.capacity() + CHUNK_SIZE);
    }

    data_struct->data.resize(data_struct->offset + realsize);
    std::memcpy(data_struct->data.data() + data_struct->offset, contents, realsize);
    data_struct->offset += realsize;

    Yield();
    return realsize;
}

auto WriteFileCallback(void *contents, size_t size, size_t num_files, void *userp) -> size_t {
    if (!g_running) {
        return 0;
    }

    auto data_struct = static_cast<DataStruct*>(userp);
    const auto realsize = size * num_files;

    // A server that ignores Range returns the complete file with HTTP 200.
    // Abort before appending that body to the partial temporary file.
    if (data_struct->resume_rejected) {
        return 0;
    }

    // flush data if incomming data would overflow the buffer
    if (data_struct->offset && data_struct->data.size() < data_struct->offset + realsize) {
        if (R_FAILED(data_struct->f.Write(data_struct->file_offset, data_struct->data.data(), data_struct->offset, FsWriteOption_None))) {
            return 0;
        }

        data_struct->file_offset += data_struct->offset;
        data_struct->offset = 0;
    }

    // we have a huge chunk! write it directly to file
    if (data_struct->data.size() < realsize) {
        if (R_FAILED(data_struct->f.Write(data_struct->file_offset, contents, realsize, FsWriteOption_None))) {
            return 0;
        }

        data_struct->file_offset += realsize;
    } else {
        // buffer data until later
        std::memcpy(data_struct->data.data() + data_struct->offset, contents, realsize);
        data_struct->offset += realsize;
    }

    Yield();
    return realsize;
}

auto header_callback(char* b, size_t size, size_t nitems, void* userdata) -> size_t {
    auto header = static_cast<Header*>(userdata);
    const auto numbytes = size * nitems;

    if (b && numbytes) {
        const auto dilem = (const char*)memchr(b, ':', numbytes);
        if (dilem) {
            const int key_len = dilem - b;
            const int value_len = numbytes - key_len - 4; // "\r\n"
            if (key_len > 0 && value_len > 0) {
                const std::string key(b, key_len);
                const std::string value(dilem + 2, value_len);
                header->m_map.insert_or_assign(key, value);
            }
        }
    }

    return numbytes;
}

auto download_header_callback(char* b, size_t size, size_t nitems, void* userdata) -> size_t {
    auto context = static_cast<DownloadHeaderContext*>(userdata);
    const auto numbytes = size * nitems;

    if (context->chunk->resume_offset > 0 && b && numbytes >= 12 && std::memcmp(b, "HTTP/", 5) == 0) {
        const auto status = static_cast<const char*>(std::memchr(b, ' ', numbytes));
        if (status && status + 4 <= b + numbytes && std::memcmp(status + 1, "200", 3) == 0) {
            context->chunk->resume_rejected = true;
        }
    }

    return header_callback(b, size, nitems, context->header);
}

auto IsRetryableDownloadError(CURLcode result) -> bool {
    switch (result) {
        case CURLE_COULDNT_CONNECT:
        case CURLE_PARTIAL_FILE:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_RANGE_ERROR:
            return true;
        default:
            return false;
    }
}

auto EscapeString(CURL* curl, const std::string& str) -> std::string {
    char* s{};
    if (!curl) {
        s = curl_escape(str.data(), str.length());
    } else {
        s = curl_easy_escape(curl, str.data(), str.length());
    }

    if (!s) {
        return str;
    }

    const std::string result = s;
    curl_free(s);
    return result;
}

auto UnescapeString(CURL* curl, const std::string& str) -> std::string {
    int out_len = 0;
    char* s{};
    if (!curl) {
        s = curl_unescape(str.data(), str.length());
    } else {
        s = curl_easy_unescape(curl, str.data(), str.length(), &out_len);
    }

    if (!s) {
        return str;
    }

    const std::string result(s, out_len > 0 ? out_len : std::strlen(s));
    curl_free(s);
    return result;
}


auto EncodeUrl(std::string url) -> std::string {
    log_write("[CURL] encoding url\n");

    if (url.starts_with("sdmc:/")) {
        url = "file://" + url.substr(5);
    } else if (url.find("://") == std::string::npos) {
        url = std::string("file://") + (url.starts_with('/') ? "" : "/") + url;
    }

    if (url.starts_with("webdav://")) {
        log_write("[CURL] updating host\n");
        url.replace(0, std::strlen("webdav"), "http");
        log_write("[CURL] updated host: %s\n", url.c_str());
    } else if (url.starts_with("webdavs://")) {
        log_write("[CURL] updating secure host\n");
        url.replace(0, std::strlen("webdavs"), "https");
        log_write("[CURL] updated host: %s\n", url.c_str());
    }

    auto clu = curl_url();
    R_UNLESS(clu, url);
    ON_SCOPE_EXIT(curl_url_cleanup(clu));

    log_write("[CURL] setting url\n");
    CURLUcode clu_code;
    clu_code = curl_url_set(clu, CURLUPART_URL, url.c_str(), CURLU_URLENCODE);
    R_UNLESS(clu_code == CURLUE_OK, url);
    log_write("[CURL] set url success\n");

    char* encoded_url;
    clu_code = curl_url_get(clu, CURLUPART_URL, &encoded_url, 0);
    R_UNLESS(clu_code == CURLUE_OK, url);

    log_write("[CURL] encoded url: %s [vs]: %s\n", encoded_url, url.c_str());
    const std::string out = encoded_url;
    curl_free(encoded_url);
    return out;
}


} // namespace sphaira::curl
