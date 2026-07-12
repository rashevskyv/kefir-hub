#include "web_upload.hpp"
#include "web.hpp"
#include "ui/progress_box.hpp"
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

namespace sphaira {

constexpr u32 IDLE_TIMEOUT_MS = 30000;

UploadState g_upload_state;

SocketStream::SocketStream(Socket sock, const std::string& initial_data, s64 content_length)
    : m_sock(sock), m_initial_data(initial_data), m_content_length(content_length) {
    m_open_result = 0;
}

Result SocketStream::ReadChunk(void* buf, s64 size, u64* bytes_read) {
    *bytes_read = 0;
    if (m_total_read >= m_content_length || size <= 0) {
        return 0;
    }

    s64 want = std::min<s64>(size, m_content_length - m_total_read);
    u8* out_ptr = static_cast<u8*>(buf);
    s64 read_now = 0;

    if (m_initial_offset < static_cast<s64>(m_initial_data.size())) {
        s64 avail = static_cast<s64>(m_initial_data.size()) - m_initial_offset;
        s64 todo = std::min<s64>(want, avail);
        std::memcpy(out_ptr, m_initial_data.data() + m_initial_offset, todo);
        m_initial_offset += todo;
        m_total_read += todo;
        read_now += todo;
        out_ptr += todo;
        want -= todo;
    }

    u32 idle_count = 0;
    while (want > 0) {
        if (!WebShareIsRunning()) {
            return -1;
        }
        if (auto pbox = WebGetProgressBox()) {
            if (pbox->ShouldExit()) {
                return -1;
            }
        }
        int got = recv(m_sock, out_ptr, want, 0);
        if (got > 0) {
            idle_count = 0;
            m_total_read += got;
            read_now += got;
            out_ptr += got;
            want -= got;
        } else if (got == 0) {
            break;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            idle_count++;
            if (idle_count > IDLE_TIMEOUT_MS) {
                return -1;
            }
            svcSleepThread(1'000'000);
        } else {
            return -1;
        }
    }

    *bytes_read = read_now;
    g_upload_state.bytes.store(m_total_read);
    return 0;
}

} // namespace sphaira
