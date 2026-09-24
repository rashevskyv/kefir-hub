#include "download.hpp"
#include "download_internal.hpp"
#include "evman.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include <curl/curl.h>
#include <switch.h>
#include <cassert>
#include <vector>
#include <atomic>

namespace sphaira::curl {

std::atomic_bool g_running{};
CURLSH* g_curl_share{};
CURL* g_curl_single{};
Mutex g_mutex_single{};
Mutex g_mutex_share[CURL_LOCK_DATA_LAST]{};

namespace {
struct ThreadEntry {
    auto Create() -> Result {
        m_curl = curl_easy_init();
        R_UNLESS(m_curl != nullptr, Result_CurlFailedEasyInit);

        ueventCreate(&m_uevent, true);
        R_TRY(threadCreate(&m_thread, ThreadFunc, this, nullptr, 1024*32, THREAD_PRIO, THREAD_CORE));
        R_TRY(svcSetThreadCoreMask(m_thread.handle, THREAD_CORE, THREAD_AFFINITY_DEFAULT(THREAD_CORE)));
        R_TRY(threadStart(&m_thread));
        R_SUCCEED();
    }

    void Close() {
        ueventSignal(&m_uevent);
        threadWaitForExit(&m_thread);
        threadClose(&m_thread);
        if (m_curl) {
            curl_easy_cleanup(m_curl);
            m_curl = nullptr;
        }
    }

    auto InProgress() -> bool {
        return m_in_progress == true;
    }

    auto Setup(const Api& api) -> bool {
        assert(m_in_progress == false && "Setting up thread while active");
        mutexLock(&m_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

        if (m_in_progress) {
            return false;
        }
        m_api = api;
        m_in_progress = true;
        // log_write("started download :)\n");
        ueventSignal(&m_uevent);
        return true;
    }

    static void ThreadFunc(void* p);

    CURL* m_curl{};
    Thread m_thread{};
    Api m_api{};
    std::atomic_bool m_in_progress{};
    Mutex m_mutex{};
    UEvent m_uevent{};
};

struct ThreadQueueEntry {
    Api api;
    bool m_delete{};
};

struct ThreadQueue {
    std::deque<ThreadQueueEntry> m_entries;
    Thread m_thread;
    Mutex m_mutex{};
    UEvent m_uevent{};

    auto Create() -> Result {
        ueventCreate(&m_uevent, true);
        R_TRY(threadCreate(&m_thread, ThreadFunc, this, nullptr, 1024*32, THREAD_PRIO, THREAD_CORE));
        R_TRY(threadStart(&m_thread));
        R_SUCCEED();
    }

    void Close() {
        ueventSignal(&m_uevent);
        threadWaitForExit(&m_thread);
        threadClose(&m_thread);
    }

    auto Add(const Api& api, bool is_upload = false) -> bool {
        if (api.GetUrl().empty() || !api.GetOnComplete()) {
            return false;
        }

        mutexLock(&m_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

        switch (api.GetPriority()) {
            case Priority::Normal:
                m_entries.emplace_back(api).api.SetUpload(is_upload);
                break;
            case Priority::High:
                m_entries.emplace_front(api).api.SetUpload(is_upload);
                break;
        }

        ueventSignal(&m_uevent);
        return true;
    }

    static void ThreadFunc(void* p);
};

ThreadEntry g_threads[MAX_THREADS]{};
ThreadQueue g_thread_queue;

void my_lock(CURL *handle, curl_lock_data data, curl_lock_access laccess, void *useptr) {
    mutexLock(&g_mutex_share[data]);
}

void my_unlock(CURL *handle, curl_lock_data data, void *useptr) {
    mutexUnlock(&g_mutex_share[data]);
}

void ThreadEntry::ThreadFunc(void* p) {
    auto data = static_cast<ThreadEntry*>(p);
    while (g_running) {
        auto rc = waitSingle(waiterForUEvent(&data->m_uevent), UINT64_MAX);
        // log_write("woke up\n");
        if (!g_running) {
            break;
        }

        if (R_FAILED(rc)) {
            continue;
        }

        const auto result = data->m_api.IsUpload() ? UploadInternal(data->m_curl, data->m_api) : DownloadInternal(data->m_curl, data->m_api);
        if (g_running && data->m_api.GetOnComplete() && !data->m_api.GetToken().stop_requested()) {
            evman::push(
                DownloadEventData{data->m_api.GetOnComplete(), result, data->m_api.GetToken()},
                false
            );
        }

        data->m_in_progress = false;
        // notify the queue that there's a space free
        ueventSignal(&g_thread_queue.m_uevent);
    }
    log_write("exited download thread\n");
}

void ThreadQueue::ThreadFunc(void* p) {
    auto data = static_cast<ThreadQueue*>(p);
    while (g_running) {
        auto rc = waitSingle(waiterForUEvent(&data->m_uevent), UINT64_MAX);
        log_write("[thread queue] woke up\n");
        if (!g_running) {
            return;
        }
        if (R_FAILED(rc)) {
            continue;
        }

        mutexLock(&data->m_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&data->m_mutex));
        if (data->m_entries.empty()) {
            continue;
        }

        // find the next avaliable thread
        u32 pop_count{};
        for (auto& entry : data->m_entries) {
            if (!g_running) {
                return;
            }

            bool keep_going{};

            for (auto& thread : g_threads) {
                if (!g_running) {
                    return;
                }

                if (!thread.InProgress()) {
                    thread.Setup(entry.api);
                    // log_write("[dl queue] starting download\n");
                    // mark entry for deletion
                    entry.m_delete = true;
                    pop_count++;
                    keep_going = true;
                    break;
                }
            }

            if (!keep_going) {
                break;
            }
        }

        // delete all entries marked for deletion
        for (u32 i = 0; i < pop_count; i++) {
            data->m_entries.pop_front();
        }
    }

    log_write("exited download thread queue\n");
}

} // namespace

auto Init() -> bool {
    if (CURLE_OK != curl_global_init(CURL_GLOBAL_DEFAULT)) {
        return false;
    }

    g_curl_share = curl_share_init();
    if (g_curl_share) {
        CURL_SHARE_SETOPT_LOG(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
        CURL_SHARE_SETOPT_LOG(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        CURL_SHARE_SETOPT_LOG(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        CURL_SHARE_SETOPT_LOG(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
        CURL_SHARE_SETOPT_LOG(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_PSL);
        CURL_SHARE_SETOPT_LOG(g_curl_share, CURLSHOPT_LOCKFUNC, my_lock);
        CURL_SHARE_SETOPT_LOG(g_curl_share, CURLSHOPT_UNLOCKFUNC, my_unlock);
    }

    g_running = true;

    if (R_FAILED(g_thread_queue.Create())) {
        log_write("!failed to create download thread queue\n");
    }

    for (auto& entry : g_threads) {
        if (R_FAILED(entry.Create())) {
            log_write("!failed to create download thread\n");
        }
    }

    {
        SCOPED_MUTEX(&g_mutex_single);
        g_curl_single = curl_easy_init();
        if (!g_curl_single) {
            log_write("failed to create g_curl_single\n");
        }
    }

    log_write("finished creating threads\n");

    if (!g_cache.init()) {
        log_write("failed to init json cache\n");
    }

    return true;
}

void RequestShutdown() {
    g_running = false;
    ueventSignal(&g_thread_queue.m_uevent);
    for (auto& entry : g_threads) {
        ueventSignal(&entry.m_uevent);
    }
}

void Exit() {
    RequestShutdown();

    g_thread_queue.Close();

    {
        SCOPED_MUTEX(&g_mutex_single);
        if (g_curl_single) {
            curl_easy_cleanup(g_curl_single);
            g_curl_single = nullptr;
        }
    }

    for (auto& entry : g_threads) {
        entry.Close();
    }

    if (g_curl_share) {
        curl_share_cleanup(g_curl_share);
        g_curl_share = {};
    }

    curl_global_cleanup();
    g_cache.exit();
}


auto ToMemoryAsync(const Api& api) -> bool {
    if (!g_running) {
        return false;
    }
    return g_thread_queue.Add(api);
}

auto ToFileAsync(const Api& e) -> bool {
    if (!g_running) {
        return false;
    }
    return g_thread_queue.Add(e);
}

auto EscapeString(const std::string& str) -> std::string {
    return EscapeString(nullptr, str);
}

auto UnescapeString(const std::string& str) -> std::string {
    return UnescapeString(nullptr, str);
}


} // namespace sphaira::curl
