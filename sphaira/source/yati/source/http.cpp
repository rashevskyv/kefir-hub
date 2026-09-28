#include "yati/source/http.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <cstdio>

namespace sphaira::yati::source {
namespace {

using devoptab::common::PushThreadData;

constexpr long CONNECT_TIMEOUT_MS = 10000;
// a read blocked on a transfer that has stopped sending can't be woken by
// anything else, so a transfer silent for this long is dropped and started
// again, which costs one request.
constexpr long LOW_SPEED_TIME_S = 15;
// a transfer that ends early is started again where it stopped, for as long as
// this since anything last arrived: a console that drops off its network needs
// that long to rejoin it, and while it is away each attempt fails at once.
constexpr u64 RESUME_WINDOW_NS = 60'000'000'000ULL;
constexpr u64 RETRY_WAIT_NS = 2'000'000'000ULL;

} // namespace

Http::Http(const std::string& url, const std::string& user, const std::string& pass)
: m_url{url}, m_user{user}, m_pass{pass} {
    m_curl = curl_easy_init();
    if (!m_curl) {
        m_open_result = Result_CurlFailedEasyInit;
    }
}

Http::~Http() {
    // the transfer's thread runs on the handle, so it is joined first.
    m_transfer.reset();
    if (m_curl) {
        curl_easy_cleanup(m_curl);
    }
}

Result Http::Start(s64 off) {
    R_UNLESS(!m_cancelled, Result_TransferCancelled);
    m_request_offset = off;
    curl_easy_reset(m_curl);
    curl_easy_setopt(m_curl, CURLOPT_URL, m_url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_USERAGENT, APP_USER_AGENT);
    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(m_curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(m_curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(m_curl, CURLOPT_BUFFERSIZE, 1024L * 64L);
    curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT_MS, CONNECT_TIMEOUT_MS);
    curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME_S);

    if (!m_user.empty()) {
        curl_easy_setopt(m_curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
        curl_easy_setopt(m_curl, CURLOPT_USERNAME, m_user.c_str());
        curl_easy_setopt(m_curl, CURLOPT_PASSWORD, m_pass.c_str());
    }

    // open ended: the transfer only stops early when a read goes elsewhere.
    char range[32];
    std::snprintf(range, sizeof(range), "%ld-", off);
    curl_easy_setopt(m_curl, CURLOPT_RANGE, range);

    m_transfer = std::make_unique<PushThreadData>(m_curl);
    m_transfer->external_cancel = &m_cancelled;
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, +[](const char* data, size_t size, size_t count, void* context) -> size_t {
        auto* self = static_cast<Http*>(context);
        long status{};
        curl_easy_getinfo(self->m_curl, CURLINFO_RESPONSE_CODE, &status);
        // An HTTP server may ignore Range and send the whole file. Never pass
        // those bytes to Yati as if they began at the requested offset.
        if (self->m_request_offset && status != 206) {
            return 0;
        }
        return PushThreadData::push_thread_callback(data, size, count, self->m_transfer.get());
    });
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);

    if (const auto rc = m_transfer->CreateAndStart(); R_FAILED(rc)) {
        m_transfer.reset();
        return rc;
    }

    m_offset = off;
    R_SUCCEED();
}

Result Http::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_TRY(GetOpenResult());
    *bytes_read = 0;

    if (m_transfer && off != m_offset) {
        log_write("[HTTP] read moved from %ld to %ld, restarting transfer\n", m_offset, off);
        m_transfer.reset();
    }

    // every read asks for bytes inside the file, so coming up short is the
    // transfer failing rather than the file ending.
    for (u64 stalled_since = 0;;) {
        R_UNLESS(!m_cancelled, Result_TransferCancelled);
        if (!m_transfer) {
            R_TRY(Start(off + *bytes_read));
        }

        // blocks until all of it has arrived, or the transfer has ended.
        const auto read = m_transfer->PullData(static_cast<char*>(buf) + *bytes_read, size - *bytes_read);
        m_offset += read;
        *bytes_read += read;

        R_UNLESS(!m_cancelled, Result_TransferCancelled);

        if (static_cast<s64>(*bytes_read) == size) {
            R_SUCCEED();
        }

        const auto code = m_transfer->code;
        log_write("[HTTP] read %lu of %ld at %ld, http code: %ld error: %d\n", *bytes_read, size, off, code, (int)m_transfer->error.load());
        m_transfer.reset();

        R_UNLESS(!code || !m_request_offset || code == 206, Result_YatiHttpReadFailed);

        // asking again won't change an error status (credentials, a token gone).
        R_UNLESS(code < 400, Result_YatiHttpReadFailed);
        const auto now = armTicksToNs(armGetSystemTick());
        if (read || !stalled_since) {
            stalled_since = now;
        }
        R_UNLESS(now - stalled_since < RESUME_WINDOW_NS, Result_YatiHttpReadFailed);

        log_write("[HTTP] resuming at %ld\n", m_offset);
        svcSleepThread(RETRY_WAIT_NS);
    }
}

} // namespace sphaira::yati::source
