#include "utils/devoptab_curl_thread.hpp"
#include "utils/thread.hpp"
#include "log.hpp"
#include <cstring>
#include <algorithm>

namespace sphaira::devoptab::common {

PushPullThreadData::PushPullThreadData(CURL* _curl) : curl{_curl} {
    mutexInit(&mutex);
    condvarInit(&can_push);
    condvarInit(&can_pull);
}

PushPullThreadData::~PushPullThreadData() {
    log_write("[PUSH:PULL] Destructor\n");
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
    SCOPED_MUTEX(&mutex);
    finished = true;
    condvarWakeOne(&can_pull);
    condvarWakeOne(&can_push);
}

bool PushPullThreadData::IsRunning() {
    SCOPED_MUTEX(&mutex);
    return !finished && !error;
}

size_t PushPullThreadData::PullData(char* data, size_t total_size, bool curl_mode) {
    if (!data || !total_size) {
        return 0;
    }

    SCOPED_MUTEX(&mutex);
    ON_SCOPE_EXIT(condvarWakeOne(&can_push));

    if (curl_mode) {
        // this should be handled in the progress function.
        // however i handle it here as well just in case.
        if (buffer.empty()) {
            if (finished) {
                log_write("[PUSH:PULL] PullData: finished and no data\n");
                return 0;
            }

            return CURL_READFUNC_PAUSE;
        }

        // read what we can.
        const auto rsize = std::min(total_size, buffer.size());
        std::memcpy(data, buffer.data(), rsize);
        buffer.erase(buffer.begin(), buffer.begin() + rsize);
        return rsize;
    } else {
        // if we are not in a curl callback, then we can block until we have data.
        size_t bytes_read = 0;
        while (bytes_read < total_size && !error) {
            if (buffer.empty()) {
                if (finished) {
                    break;
                }

                condvarWakeOne(&can_push);
                condvarWait(&can_pull, &mutex);
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
        // this should be handled in the progress function.
        // however i handle it here as well just in case.
        if (buffer.size() + total_size > MAX_BUFFER_SIZE) {
            return CURL_WRITEFUNC_PAUSE;
        }

        // blocking / pausing is handled in the progress function.
        // do NOT block here as curl does not like it and it will deadlock.
        // the mutex block above is fine as it only blocks to perform a memcpy.
        buffer.insert(buffer.end(), data, data + total_size);
        return total_size;
    } else {
        // if we are not in a curl callback, then we can block until we have space.
        size_t bytes_written = 0;
        while (bytes_written < total_size && !error && !finished) {
            const size_t space_left = MAX_BUFFER_SIZE - buffer.size();
            if (space_left == 0) {
                condvarWakeOne(&can_pull);
                condvarWait(&can_push, &mutex);
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
    bool should_pause;

    {
        SCOPED_MUTEX(&data->mutex);

        // abort early if there was an error.
        if (data->error) {
            log_write("[PUSH:PULL] progress_callback: aborting transfer, error set\n");
            return 1;
        }

        // nothing yet.
        if (!dlnow && !ulnow) {
            return 0;
        }

        // workout if this is a download or upload.
        const auto is_download = dlnow > 0;

        if (is_download) {
            // no more data wanted, usually this is handled by curl using ranges.
            // however, if we did a seek, then we want to cancel early.
            if (data->finished) {
                log_write("[PUSH:PULL] progress_callback: cancelling download, finished set\n");
                return 1;
            }

            // pause if the buffer is full, otherwise continue.
            should_pause = data->buffer.size() >= MAX_BUFFER_SIZE;
        } else {
            // pause if we have no data to send, otherwise continue.
            // do not pause if finished as curl may have internal data pending to send.
            should_pause = !data->finished && data->buffer.empty();
        }
    }

    // curl_easy_pause(CONT) actually calls the read/write callback again immediately.
    // so we need to make sure we are not holding the mutex when calling it.
    // the curl handle is owned by this thread so no need to lock it.
    const auto res = curl_easy_pause(data->curl, should_pause ? CURLPAUSE_ALL : CURLPAUSE_CONT);
    if (res != CURLE_OK) {
        log_write("[PUSH:PULL] progress_callback: curl_easy_pause(%d) failed: %s\n", should_pause, curl_easy_strerror(res));
    }

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

    log_write("[PUSH:PULL] Read thread finished, code: %ld, error: %d\n", data->code, data->error);
}

} // namespace sphaira::devoptab::common
