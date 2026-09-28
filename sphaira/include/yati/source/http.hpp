#pragma once

#include "yati/source/base.hpp"
#include <curl/curl.h>
#include <string>
#include <memory>
#include <atomic>

namespace sphaira::devoptab::common {
struct PushThreadData;
}

namespace sphaira::yati::source {

// a remote file, read over http or https. opens a transfer at the first read,
// for as long as reads follow on; a jump, or an early end, starts another.
struct Http final : Base {
    // `user` empty sends no credentials; otherwise they go as basic auth on the
    // first request, rather than after the server has challenged for them.
    Http(const std::string& url, const std::string& user, const std::string& pass);
    ~Http();

    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;
    void SignalCancel() override { m_cancelled = true; }

private:
    Result Start(s64 off);

    const std::string m_url;
    const std::string m_user;
    const std::string m_pass;
    CURL* m_curl{};
    std::unique_ptr<devoptab::common::PushThreadData> m_transfer{};
    s64 m_request_offset{};
    std::atomic_bool m_cancelled{};
    // where the running transfer has read up to.
    s64 m_offset{};
};

} // namespace sphaira::yati::source
