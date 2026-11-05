#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace sphaira {

// A bounded byte queue from one producer thread to one consumer thread.
// Turns a push-style decoder (UnRAR's data callback) into pull-style reads.
//   producer: Write() blocks while the queue is full, Finish() at the end.
//   consumer: Read() blocks until there is data or the producer finished,
//             Close() when it stops reading: a blocked Write() wakes and returns false.
class BytePipe {
public:
    explicit BytePipe(size_t capacity) : m_buf(capacity ? capacity : 1) {}

    // false once the consumer closed the pipe: the producer should stop.
    bool Write(const void* data, size_t size) {
        auto src = static_cast<const std::uint8_t*>(data);
        std::unique_lock lock{m_mutex};
        while (size) {
            m_space.wait(lock, [this] { return m_closed || m_used < m_buf.size(); });
            if (m_closed) {
                return false;
            }
            const auto tail = (m_head + m_used) % m_buf.size();
            const auto n = std::min({size, m_buf.size() - m_used, m_buf.size() - tail});
            std::memcpy(m_buf.data() + tail, src, n);
            m_used += n;
            src += n;
            size -= n;
            m_data.notify_one();
        }
        return true;
    }

    // the producer has no more data.
    void Finish() {
        std::lock_guard lock{m_mutex};
        m_finished = true;
        m_data.notify_all();
    }

    // up to `size` bytes; 0 = the producer finished and everything was read (or closed).
    size_t Read(void* out, size_t size) {
        auto dst = static_cast<std::uint8_t*>(out);
        std::unique_lock lock{m_mutex};
        m_data.wait(lock, [this] { return m_used || m_finished || m_closed; });
        size_t done = 0;
        while (done < size && m_used) {
            const auto n = std::min({size - done, m_used, m_buf.size() - m_head});
            std::memcpy(dst + done, m_buf.data() + m_head, n);
            m_head = (m_head + n) % m_buf.size();
            m_used -= n;
            done += n;
        }
        m_space.notify_one();
        return done;
    }

    // the consumer stops reading; wakes a blocked producer.
    void Close() {
        std::lock_guard lock{m_mutex};
        m_closed = true;
        m_space.notify_all();
        m_data.notify_all();
    }

    bool Closed() {
        std::lock_guard lock{m_mutex};
        return m_closed;
    }

private:
    std::mutex m_mutex{};
    std::condition_variable m_space{};
    std::condition_variable m_data{};
    std::vector<std::uint8_t> m_buf;
    size_t m_head{};
    size_t m_used{};
    bool m_finished{};
    bool m_closed{};
};

} // namespace sphaira
