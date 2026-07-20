#include "utils/devoptab_curl_thread.hpp"
#include "utils/thread.hpp"
#include "log.hpp"
#include <cstring>
#include <algorithm>
#include <atomic>

namespace sphaira::devoptab::common {
namespace {

struct TransferRegistry {
    TransferRegistry() {
        mutexInit(&mutex);
    }

    Mutex mutex{};
    std::vector<PushPullThreadData*> transfers{};
};

auto GetTransferRegistry() -> TransferRegistry& {
    static TransferRegistry registry;
    return registry;
}

std::atomic_bool g_curl_shutdown{};
std::atomic<u64> g_cancel_generation{};

void RegisterTransfer(PushPullThreadData* transfer) {
    auto& registry = GetTransferRegistry();
    SCOPED_MUTEX(&registry.mutex);
    registry.transfers.push_back(transfer);
}

void UnregisterTransfer(PushPullThreadData* transfer) {
    auto& registry = GetTransferRegistry();
    SCOPED_MUTEX(&registry.mutex);
    std::erase(registry.transfers, transfer);
}

} // namespace

void CancelActiveCurlTransfers() {
    // interrupt synchronous requests (HEAD, range probe, PROPFIND, ...)
    // blocked in curl_easy_perform without a timeout.
    g_cancel_generation++;

    auto& registry = GetTransferRegistry();
    SCOPED_MUTEX(&registry.mutex);
    for (auto* transfer : registry.transfers) {
        transfer->Cancel();
    }
}

u64 GetCurlCancelGeneration() {
    return g_cancel_generation;
}

int CurlOpCancelProgressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    if (g_curl_shutdown) {
        return 1;
    }

    const auto* snapshot = static_cast<const u64*>(clientp);
    if (snapshot && *snapshot != g_cancel_generation) {
        log_write("[CURL] aborting synchronous request, transfers were cancelled\n");
        return 1;
    }

    return 0;
}

void RequestCurlShutdown() {
    g_curl_shutdown = true;
    CancelActiveCurlTransfers();
}

int CurlShutdownProgressCallback(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return g_curl_shutdown ? 1 : 0;
}

PushPullThreadData::PushPullThreadData(CURL* _curl) : curl{_curl} {
    mutexInit(&mutex);
    condvarInit(&can_push);
    condvarInit(&can_pull);
    // reserve upfront: the data callbacks insert into the buffer while
    // holding the mutex, and a reallocation there would take the global
    // heap lock under our mutex.
    buffer.reserve(MAX_BUFFER_SIZE);
    RegisterTransfer(this);
}

PushPullThreadData::~PushPullThreadData() {
    log_write("[PUSH:PULL] Destructor\n");
    UnregisterTransfer(this);
    Cancel();

    if (started) {
        log_write("[PUSH:PULL] Waiting for thread to exit\n");
        threadWaitForExit(&thread);
        log_write("[PUSH:PULL] Thread exited\n");
    }

    threadClose(&thread);
}

Result PushPullThreadData::CreateAndStart() {
    SCOPED_MUTEX(&mutex);

    if (started) {
        R_SUCCEED();
    }

    R_TRY(utils::CreateThread(&thread, thread_func, this));
    R_TRY(threadStart(&thread));

    started = true;
    R_SUCCEED();
}

void PushPullThreadData::Cancel() {
    // deliberately lock-free: this is called from the ui thread and from
    // teardown paths, and must never block behind a wedged transfer thread.
    // The flags are atomic; the waiters use timed waits, so even a wake that
    // races a waiter registering is re-checked within 100ms.
    cancelled = true;
    finished = true;
    condvarWakeOne(&can_pull);
    condvarWakeOne(&can_push);
    log_write("[PUSH:PULL] Cancel: flags set and waiters woken\n");
}

bool PushPullThreadData::WaitForExitTimeout(u64 timeout_ns) {
    if (!started) {
        return true;
    }

    s32 idx{};
    return R_SUCCEEDED(svcWaitSynchronization(&idx, &thread.handle, 1, timeout_ns));
}

bool DestroyTransfer(PushPullThreadData* data) {
    if (!data) {
        return true;
    }

    log_write("[PUSH:PULL] DestroyTransfer: cancelling transfer\n");
    data->Cancel();
    if (data->WaitForExitTimeout(200'000'000ULL)) {
        delete data;
        return true;
    }

    // the thread is stuck in a syscall that ignores cancellation: leak the
    // transfer (thread, buffer and curl handle) instead of freezing the app.
    log_write("[PUSH:PULL] transfer thread did not exit in time, leaking the transfer!\n");
    return false;
}

bool PushPullThreadData::IsRunning() {
    return !finished && !error;
}

bool PushPullThreadData::HasError() {
    return error;
}

size_t PushPullThreadData::PullData(char* data, size_t total_size, bool curl_mode) {
    if (!data || !total_size) {
        return 0;
    }

    SCOPED_MUTEX(&mutex);
    ON_SCOPE_EXIT(condvarWakeOne(&can_push));

    if (curl_mode) {
        // block until the producer gives us data. Blocking here (rather
        // than pausing the transfer) keeps this thread responsive: Cancel()
        // wakes the condvar and the abort return ends curl_easy_perform().
        // Pausing was broken: a fully paused easy transfer stops receiving
        // progress callbacks, so nothing ever unpaused it.
        while (!error && !cancelled) {
            if (!buffer.empty()) {
                // read what we can.
                const auto rsize = std::min(total_size, buffer.size());
                std::memcpy(data, buffer.data(), rsize);
                buffer.erase(buffer.begin(), buffer.begin() + rsize);
                return rsize;
            }

            if (finished) {
                log_write("[PUSH:PULL] PullData: finished and no data\n");
                return 0;
            }

            condvarWakeOne(&can_push);
            // timed wait: a missed wake (or a cancel racing the wait) must
            // never block this thread forever, the loop re-checks the flags.
            condvarWaitTimeout(&can_pull, &mutex, WAIT_TIMEOUT_NS);

            if (++wait_ticks % 10 == 0) {
                log_write("[PUSH:PULL] PullData: waiting for data (cancelled=%d)\n", cancelled.load());
            }
        }

        return CURL_READFUNC_ABORT;
    } else {
        // if we are not in a curl callback, then we can block until we have data.
        size_t bytes_read = 0;
        while (bytes_read < total_size && !error) {
            if (buffer.empty()) {
                if (finished) {
                    break;
                }

                condvarWakeOne(&can_push);
                condvarWaitTimeout(&can_pull, &mutex, WAIT_TIMEOUT_NS);

                if (++wait_ticks % 10 == 0) {
                    log_write("[PUSH:PULL] consumer: waiting for data (finished=%d, error=%d)\n", finished.load(), error.load());
                }
                continue;
            }

            const auto rsize = std::min(total_size - bytes_read, buffer.size());
            std::memcpy(data + bytes_read, buffer.data(), rsize);
            buffer.erase(buffer.begin(), buffer.begin() + rsize);
            bytes_read += rsize;
        }

        return bytes_read;
    }
}

size_t PushPullThreadData::PushData(const char* data, size_t total_size, bool curl_mode) {
    if (!data || !total_size) {
        return 0;
    }

    SCOPED_MUTEX(&mutex);
    ON_SCOPE_EXIT(condvarWakeOne(&can_pull));

    if (curl_mode) {
        // block until the consumer makes space (see PullData above for why
        // blocking beats pausing). The short return on cancel/finish makes
        // curl report a write error, which ends the transfer thread.
        while (!error && !cancelled && !finished) {
            if (buffer.size() + total_size <= MAX_BUFFER_SIZE) {
                buffer.insert(buffer.end(), data, data + total_size);
                return total_size;
            }

            condvarWakeOne(&can_pull);
            // see PullData: the timeout makes a lost wake self-healing.
            condvarWaitTimeout(&can_push, &mutex, WAIT_TIMEOUT_NS);

            // diagnostic heartbeat roughly once a second while throttled.
            if (++wait_ticks % 10 == 0) {
                log_write("[PUSH:PULL] PushData: waiting for space (buffer=%zu, cancelled=%d)\n", buffer.size(), cancelled.load());
            }
        }

        return 0;
    } else {
        // if we are not in a curl callback, then we can block until we have space.
        size_t bytes_written = 0;
        while (bytes_written < total_size && !error && !finished) {
            const size_t space_left = MAX_BUFFER_SIZE - buffer.size();
            if (space_left == 0) {
                condvarWakeOne(&can_pull);
                condvarWaitTimeout(&can_push, &mutex, WAIT_TIMEOUT_NS);
                continue;
            }

            const auto wsize = std::min(total_size - bytes_written, space_left);
            buffer.insert(buffer.end(), data + bytes_written, data + bytes_written + wsize);
            bytes_written += wsize;
        }

        return bytes_written;
    }
}

size_t PushThreadData::push_thread_callback(const char *ptr, size_t size, size_t nmemb, void *userdata) {
    if (!ptr || !userdata || !size || !nmemb) {
        return 0;
    }

    auto* data = static_cast<PushThreadData*>(userdata);
    if (!data->first_chunk_logged.exchange(true)) {
        log_write("[PUSH:PULL] first %zu bytes received from server\n", size * nmemb);
    }
    return data->PushData(ptr, size * nmemb, true);
}

size_t PullThreadData::pull_thread_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    if (!ptr || !userdata || !size || !nmemb) {
        return 0;
    }

    auto* data = static_cast<PullThreadData*>(userdata);
    return data->PullData(ptr, size * nmemb, true);
}

size_t PushPullThreadData::progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    auto *data = static_cast<PushPullThreadData*>(clientp);

    if (g_curl_shutdown) {
        return 1;
    }

    SCOPED_MUTEX(&data->mutex);

    // abort early if there was an error.
    if (data->error) {
        log_write("[PUSH:PULL] progress_callback: aborting transfer, error set\n");
        return 1;
    }

    // Cancellation must also interrupt connection setup, before curl has
    // transferred its first byte (the data callbacks are not called then).
    if (data->cancelled) {
        log_write("[PUSH:PULL] progress_callback: aborting cancelled transfer\n");
        return 1;
    }

    // the consumer is done with this download (seek or close): abort early
    // instead of streaming the rest of the file. Uploads keep running so
    // the read callback can flush the remaining buffered data.
    if (data->finished && dlnow > 0) {
        log_write("[PUSH:PULL] progress_callback: cancelling download, finished set\n");
        return 1;
    }

    // diagnostic heartbeat: proves the curl loop is alive at all.
    if (++data->progress_ticks % 16 == 0) {
        log_write("[PUSH:PULL] progress tick %u (dlnow=%lld, buffer=%zu)\n", data->progress_ticks, (long long)dlnow, data->buffer.size());
    }

    // backpressure is handled by blocking inside the data callbacks.
    return 0;
}

void PushPullThreadData::thread_func(void* arg) {
    log_write("[PUSH:PULL] Read thread started\n");
    auto data = static_cast<PushPullThreadData*>(arg);

    curl_easy_setopt(data->curl, CURLOPT_XFERINFODATA, data);
    curl_easy_setopt(data->curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    const auto res = curl_easy_perform(data->curl);

    log_write("[PUSH:PULL] curl_easy_perform() returned: %s\n", curl_easy_strerror(res));

    // when finished, lock mutex and signal for anything waiting.
    SCOPED_MUTEX(&data->mutex);
    condvarWakeOne(&data->can_push);
    condvarWakeOne(&data->can_pull);

    data->finished = true;
    data->error = res != CURLE_OK;
    curl_easy_getinfo(data->curl, CURLINFO_RESPONSE_CODE, &data->code);

    log_write("[PUSH:PULL] Read thread finished, code: %ld, error: %d\n", data->code, data->error.load());
}

} // namespace sphaira::devoptab::common
