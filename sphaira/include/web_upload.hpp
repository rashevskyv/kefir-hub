#pragma once

#include "fs.hpp"
#include "yati/source/stream.hpp"
#include <switch.h>
#include <string>
#include <atomic>
#include <mutex>
#include <memory>

namespace sphaira {

using Socket = int;

struct UploadState {
    std::atomic<bool> active{false};
    std::atomic<s64> bytes{0};
    std::atomic<s64> total{0};
    std::mutex name_mutex{};
    std::string name{};
};

extern UploadState g_upload_state;

struct SocketStream final : yati::source::Stream {
    SocketStream(Socket sock, const std::string& initial_data, s64 content_length);

    Result ReadChunk(void* buf, s64 size, u64* bytes_read) override;

private:
    Socket m_sock;
    std::string m_initial_data;
    s64 m_initial_offset{0};
    s64 m_content_length;
    s64 m_total_read{0};
};

} // namespace sphaira
