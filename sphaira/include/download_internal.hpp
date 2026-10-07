#pragma once

#include "download.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "defines.hpp"
#include <curl/curl.h>
#include <switch.h>
#include <atomic>
#include <string>
#include <vector>
#include <span>
#include <unordered_map>

struct yyjson_mut_doc;
struct yyjson_mut_val;

namespace sphaira::curl {

#define CURL_EASY_SETOPT_LOG(handle, opt, v) \
    if (auto r = curl_easy_setopt(handle, opt, v); r != CURLE_OK) { \
        log_write("curl_easy_setopt(%s, %s) msg: %s\n", #opt, #v, curl_easy_strerror(r)); \
    }

#define CURL_SHARE_SETOPT_LOG(handle, opt, v) \
    if (auto r = curl_share_setopt(handle, opt, v); r != CURLSHE_OK) { \
        log_write("curl_share_setopt(%s, %s) msg: %s\n", #opt, #v, curl_share_strerror(r)); \
    }

inline constexpr u64 CHUNK_SIZE = 1024*1024;
inline constexpr auto MAX_THREADS = 4;
inline constexpr auto DOWNLOAD_MAX_ATTEMPTS = 3;
inline constexpr u64 DOWNLOAD_RETRY_DELAY_NS = 250'000'000;
inline constexpr int THREAD_PRIO = PRIO_PREEMPTIVE;
inline constexpr int THREAD_CORE = 1;

extern std::atomic_bool g_running;
extern CURLSH* g_curl_share;
extern CURL* g_curl_single;
extern Mutex g_mutex_single;
extern Mutex g_mutex_share[CURL_LOCK_DATA_LAST];

struct UploadStruct {
    std::span<const u8> data;
    s64 offset{};
    s64 size{};
    fs::File f{};
};

struct DataStruct {
    std::vector<u8> data;
    s64 offset{};
    fs::File f{};
    s64 file_offset{};
    s64 resume_offset{};
    bool resume_rejected{};
};

struct DownloadHeaderContext {
    Header* header{};
    DataStruct* chunk{};
};

struct SeekCustomData {
    OnUploadSeek cb{};
    s64 size{};
};

struct Cache {
    using Value = std::pair<std::string, std::string>;
    bool init();
    void exit();
    void get(const fs::FsPath& path, curl::Header& header);
    void set(const fs::FsPath& path, const curl::Header& header);

    static constexpr inline fs::FsPath JSON_PATH{"/switch/sphaira/cache/cache.json"};
    static constexpr inline const char* ETAG_STR{"etag"};
    static constexpr inline const char* LAST_MODIFIED_STR{"last-modified"};

    Mutex m_mutex{};
    yyjson_mut_doc* m_json{};
    yyjson_mut_val* m_root{};
    std::unordered_map<std::string, Value> m_cache{};

private:
    auto get_internal(const fs::FsPath& path) -> Value;
    void set_internal(const fs::FsPath& path, const Value& value);
};

extern Cache g_cache;

inline void Yield() {
    svcSleepThread(YieldType_WithoutCoreMigration);
}

void GetDownloadTempPath(fs::FsPath& buf);
auto EscapeString(CURL* curl, const std::string& str) -> std::string;
auto UnescapeString(CURL* curl, const std::string& str) -> std::string;
auto EncodeUrl(std::string url) -> std::string;

auto ProgressCallbackFunc2(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) -> int;
auto SeekCallback(void *clientp, curl_off_t offset, int origin) -> int;
auto SeekCustomCallback(void *clientp, curl_off_t offset, int origin) -> int;
auto ReadFileCallback(char *ptr, size_t size, size_t nmemb, void *userp) -> size_t;
auto ReadMemoryCallback(char *ptr, size_t size, size_t nmemb, void *userp) -> size_t;
auto ReadCustomCallback(char *ptr, size_t size, size_t nmemb, void *userp) -> size_t;
auto WriteMemoryCallback(void *contents, size_t size, size_t num_files, void *userp) -> size_t;
auto WriteFileCallback(void *contents, size_t size, size_t num_files, void *userp) -> size_t;
auto header_callback(char* b, size_t size, size_t nitems, void* userdata) -> size_t;
auto download_header_callback(char* b, size_t size, size_t nitems, void* userdata) -> size_t;
auto IsRetryableDownloadError(CURLcode result) -> bool;

void SetCommonCurlOptions(CURL* curl, const Api& e);
auto DownloadInternal(CURL* curl, const Api& e) -> ApiResult;
auto UploadInternal(CURL* curl, const Api& e) -> ApiResult;
auto WebdavCreateFolder(CURL* curl, const Api& e) -> bool;

} // namespace sphaira::curl
