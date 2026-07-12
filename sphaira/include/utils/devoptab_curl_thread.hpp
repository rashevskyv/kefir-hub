#pragma once

#include "defines.hpp"
#include <curl/curl.h>
#include <switch.h>
#include <vector>

namespace sphaira::devoptab::common {

struct PushPullThreadData {
    static constexpr size_t MAX_BUFFER_SIZE = 1024 * 64; // 64KB max buffer

    explicit PushPullThreadData(CURL* _curl);
    virtual ~PushPullThreadData();

    Result CreateAndStart();
    void Cancel();
    bool IsRunning();

    // only set curl=true if called from a curl callback.
    size_t PullData(char* data, size_t total_size, bool curl = false);
    size_t PushData(const char* data, size_t total_size, bool curl = false);

    static size_t progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

private:
    static void thread_func(void* arg);

public:
    CURL* const curl{};
    std::vector<char> buffer{};
    Mutex mutex{};
    CondVar can_push{};
    CondVar can_pull{};

    long code{};
    bool error{};
    bool finished{};
    bool started{};

private:
    Thread thread{};
};

struct PullThreadData final : PushPullThreadData {
    using PushPullThreadData::PushPullThreadData;
    static size_t pull_thread_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
};

struct PushThreadData final : PushPullThreadData {
    using PushPullThreadData::PushPullThreadData;
    static size_t push_thread_callback(const char *ptr, size_t size, size_t nmemb, void *userdata);
};

} // namespace sphaira::devoptab::common
