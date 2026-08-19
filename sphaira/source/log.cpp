#include "log.hpp"
#include "app_paths.hpp"
#include <cstdio>
#include <cstdarg>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <mutex>
#include <switch.h>

#if sphaira_USE_LOG
namespace {

const auto& logpath = sphaira::paths::LOG;
const auto& errorpath = sphaira::paths::ERROR_LOG;

// the error log is append-only across launches (unlike log.txt, which is
// truncated on every start) so a failure can still be read after a reboot.
// Rolled over once it gets big rather than growing without bound.
constexpr off_t MAX_ERROR_LOG_BYTES = 256u * 1024u;

int nxlink_socket{};
bool g_file_open{};
std::mutex mutex{};

// Asynchronous logging.
//
// log_write() used to open()/write()/close() the log file on the sd card on
// every single call, under a process-wide mutex. in hot paths (per usb packet,
// per pipeline chunk, per ncz block) that serialised the logger's sd access
// with the installer's own placeholder writes and throttled installs to a
// crawl until windows mtp timed the transfer out.
//
// instead, log_write() now only appends the formatted line to an in-memory
// buffer (cheap: a short lock + a memcpy). a dedicated low-priority background
// thread wakes every ~100ms, swaps the buffer out and writes the whole batch
// with a single open/write/close. so the sd is touched rarely and never from
// the thread doing the logging -- logging can no longer starve anything.
std::string g_buffer{};
Thread g_flush_thread{};
bool g_thread_running{};
bool g_thread_stop{};

// cap so a runaway logging burst can't grow the buffer without bound if the
// flush thread is starved of cpu. dropping debug lines is preferable to OOM.
constexpr size_t MAX_BUFFER_BYTES = 2u * 1024u * 1024u;

// flush interval. short enough that a crash loses little, long enough that
// writes are well batched.
constexpr u64 FLUSH_INTERVAL_NS = 100'000'000ULL;

// performs the actual io. must be called WITHOUT the mutex held so the sd/socket
// write never blocks log_write(). the open flags are snapshotted by the caller.
void do_flush(const std::string& data, bool file_open, int sock) {
    if (data.empty()) {
        return;
    }
    if (file_open) {
        int fd = open(logpath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            write(fd, data.data(), data.size());
            close(fd);
            fsdevCommitDevice("sdmc");
        }
    }
    if (sock > 0) {
        send(sock, data.data(), data.size(), 0);
    }
}

void flush_thread_func(void*) {
    for (;;) {
        svcSleepThread(FLUSH_INTERVAL_NS);

        std::string batch;
        bool stop;
        bool file_open;
        int sock;
        {
            std::scoped_lock lock{mutex};
            batch.swap(g_buffer);
            stop = g_thread_stop;
            file_open = g_file_open;
            sock = nxlink_socket;
        }

        // final iteration still runs while g_file_open/nxlink_socket are true,
        // so the tail is flushed before the consumer is torn down.
        do_flush(batch, file_open, sock);

        if (stop) {
            break;
        }
    }
}

// caller must hold `mutex`. starts the flush thread if it isn't already up.
void ensure_thread_started() {
    if (g_thread_running) {
        return;
    }
    g_thread_stop = false;
    // 0x3B (PRIO_PREEMPTIVE) is the lowest priority homebrew is allowed to
    // create -- 0x3F is out of range and makes threadCreate fail, which would
    // leave the buffer never flushed. no core affinity so it runs in the spare
    // time, as requested; its work (a swap plus one batched write) is tiny.
    if (R_SUCCEEDED(threadCreate(&g_flush_thread, flush_thread_func, nullptr, nullptr, 0x4000, 0x3B, -2))) {
        if (R_SUCCEEDED(threadStart(&g_flush_thread))) {
            g_thread_running = true;
        } else {
            threadClose(&g_flush_thread);
        }
    }
}

// stops and joins the flush thread. its final iteration flushes any pending
// bytes while the consumers are still marked open. caller must NOT hold `mutex`.
void stop_thread() {
    {
        std::scoped_lock lock{mutex};
        if (!g_thread_running) {
            return;
        }
        g_thread_stop = true;
    }
    threadWaitForExit(&g_flush_thread);
    threadClose(&g_flush_thread);
    std::scoped_lock lock{mutex};
    g_thread_running = false;
}

void log_write_arg_internal(const char* s, std::va_list* v) {
    const auto t = std::time(nullptr);
    struct tm tm{};
    localtime_r(&t, &tm);

    char buf[512];
    const auto len = std::snprintf(buf, sizeof(buf), "[%02u:%02u:%02u] -> ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    const auto msg_len = std::vsnprintf(buf + len, sizeof(buf) - len, s, *v);
    const auto total_len = len + (msg_len > 0 ? msg_len : 0);

    // append only; the flush thread does the actual sd/socket io. drop the line
    // if the backlog is already huge (flush thread starved) rather than grow
    // memory unbounded.
    if (g_buffer.size() + total_len <= MAX_BUFFER_BYTES) {
        g_buffer.append(buf, total_len);
    }

    // safety net: if the background flusher never started (threadCreate failed),
    // flush inline so the log is never silently lost. this reintroduces the sd
    // write under the mutex, but only on this cold path -- normally the thread
    // is up and this is skipped.
    if (!g_thread_running && !g_buffer.empty()) {
        std::string batch;
        batch.swap(g_buffer);
        do_flush(batch, g_file_open, nxlink_socket);
    }
}

} // namespace

extern "C" {

auto log_file_init() -> bool {
    std::scoped_lock lock{mutex};
    if (g_file_open) {
        return false;
    }

    int fd = open(logpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        g_file_open = true;
        close(fd);
        ensure_thread_started();
        return true;
    }

    return false;
}

auto log_nxlink_init() -> bool {
    std::scoped_lock lock{mutex};
    if (nxlink_socket) {
        return false;
    }

    nxlink_socket = nxlinkConnectToHost(true, false);
    if (nxlink_socket) {
        ensure_thread_started();
    }
    return nxlink_socket != 0;
}

void log_file_exit() {
    // is the flush thread still needed by nxlink after the file closes?
    bool shared;
    {
        std::scoped_lock lock{mutex};
        if (!g_file_open) {
            return;
        }
        shared = nxlink_socket != 0;
    }

    if (!shared) {
        // stop the thread first; its last iteration flushes the tail to the
        // file (still marked open), then we mark it closed.
        stop_thread();
        std::scoped_lock lock{mutex};
        g_file_open = false;
        return;
    }

    // thread stays up for nxlink: flush whatever is pending to the file now,
    // then mark it closed so future batches skip it.
    std::string batch;
    {
        std::scoped_lock lock{mutex};
        batch.swap(g_buffer);
    }
    do_flush(batch, true, 0);
    std::scoped_lock lock{mutex};
    g_file_open = false;
}

void log_nxlink_exit() {
    bool shared;
    {
        std::scoped_lock lock{mutex};
        if (!nxlink_socket) {
            return;
        }
        shared = g_file_open;
    }

    if (!shared) {
        stop_thread();
    }

    std::scoped_lock lock{mutex};
    if (nxlink_socket) {
        close(nxlink_socket);
        nxlink_socket = 0;
    }
}

bool log_is_init() {
    std::scoped_lock lock{mutex};
    return g_file_open || nxlink_socket;
}

void log_write(const char* s, ...) {
    if (!log_is_init()) {
        return;
    }

    std::scoped_lock lock{mutex};
    std::va_list v{};
    va_start(v, s);
    log_write_arg_internal(s, &v);
    va_end(v);
}

void log_write_arg(const char* s, va_list* v) {
    if (!log_is_init()) {
        return;
    }

    std::scoped_lock lock{mutex};
    log_write_arg_internal(s, v);
}

void log_write_error(const char* s, ...) {
    char msg[512];
    std::va_list v{};
    va_start(v, s);
    std::vsnprintf(msg, sizeof(msg), s, v);
    va_end(v);

    // the error log carries the full date, not just the time: it survives
    // reboots, so "12:26:59" alone would be ambiguous.
    const auto t = std::time(nullptr);
    struct tm tm{};
    localtime_r(&t, &tm);
    char line[640];
    const auto len = std::snprintf(line, sizeof(line), "[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
        1900u + tm.tm_year, 1u + tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msg);

    {
        std::scoped_lock lock{mutex};
        int fd = open(errorpath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            if (lseek(fd, 0, SEEK_END) > MAX_ERROR_LOG_BYTES) {
                close(fd);
                fd = open(errorpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            }
        }
        if (fd >= 0) {
            write(fd, line, len > 0 ? (size_t)len : 0);
            close(fd);
            fsdevCommitDevice("sdmc");
        }
    }

    // mirror into the normal log too, so a trace stays in one place when
    // logging is on.
    log_write("[ERROR] %s\n", msg);
}

} // extern "C"

#endif
