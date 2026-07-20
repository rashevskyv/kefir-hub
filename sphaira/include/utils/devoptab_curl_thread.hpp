#pragma once

#include "defines.hpp"
#include <curl/curl.h>
#include <switch.h>
#include <vector>
#include <atomic>

namespace sphaira::devoptab::common {

// Cancels the currently active streaming transfers. This is safe to call from
// the applet exit hook and wakes readers blocked in PullData immediately.
void CancelActiveCurlTransfers();

// Marks all curl work as shutting down. Common synchronous requests observe
// this through their progress callback, while streaming requests are cancelled
// by CancelActiveCurlTransfers().
void RequestCurlShutdown();
int CurlShutdownProgressCallback(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t);

// Bumped by CancelActiveCurlTransfers(). Synchronous requests snapshot this
// before curl_easy_perform() and abort (via CurlOpCancelProgressCallback)
// once the value changes, so a cancel also interrupts blocking HEAD/PROPFIND
// requests, not just the streaming transfers.
u64 GetCurlCancelGeneration();

// progress callback for synchronous requests: clientp must point to a u64
// holding the generation snapshot taken before the request started.
int CurlOpCancelProgressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t);

struct PushPullThreadData;

// cancels and destroys a transfer. If its thread refuses to die within a few
// seconds (eg stuck in a blocking bsd syscall), the transfer object is
// intentionally leaked so the caller never freezes: returns false, and the
// curl handle the transfer uses must be considered lost by the caller.
bool DestroyTransfer(PushPullThreadData* data);

struct PushPullThreadData {
    // socket read-ahead between the curl thread and the consumer: while the
    // consumer stalls, curl keeps draining the socket into here instead of
    // blocking, keeping the TCP window open. 8 MiB is a balance -- larger
    // (16 MiB) only made compressed (ncz) installs read in longer, sparser
    // bursts because the true bottleneck there is downstream decompress/write.
    static constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024 * 8;

    // upper bound for every condvar wait: the loops re-check their flags on
    // wake-up, so a missed wake costs at most this much, never a deadlock.
    static constexpr u64 WAIT_TIMEOUT_NS = 100'000'000ULL; // 100ms

    explicit PushPullThreadData(CURL* _curl);
    virtual ~PushPullThreadData();

    Result CreateAndStart();
    void Cancel();
    bool IsRunning();
    bool HasError();

    // waits for the transfer thread to exit, false on timeout.
    bool WaitForExitTimeout(u64 timeout_ns);

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
    // diagnostic counters for the heartbeat logs.
    u32 wait_ticks{};
    u32 progress_ticks{};
    // atomic so that Cancel() never has to take the mutex: cancellation must
    // work even if the transfer thread wedges while holding it.
    std::atomic_bool error{};
    std::atomic_bool finished{};
    std::atomic_bool cancelled{};
    std::atomic_bool started{};
    // one-shot: logs the first payload chunk so a stalled server (delivers a
    // little then goes silent) can be told apart from one that never replies.
    std::atomic_bool first_chunk_logged{};

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
