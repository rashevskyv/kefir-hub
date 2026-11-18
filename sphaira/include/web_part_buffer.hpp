#pragma once

// In-order window for the multi-connection web install.
// Several connections Put() parts of one package in any order; yati Read()s the bytes in order.
// A part further than `window` bytes ahead of the reader waits (so RAM stays bounded and the
// sender is slowed by TCP). Free of libnx, so tests/test_web_part_buffer.cpp runs on the host.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace sphaira {

class PartBuffer {
public:
    using Abort = std::function<bool()>;

    PartBuffer(int64_t total, int64_t window, int64_t idle_ms = 30000)
        : m_total{total}, m_window{window}, m_idle_ms{idle_ms}, m_last{Clock::now()} {}

    // false: aborted, or nothing moved for idle_ms. A part already read or already held is accepted and dropped
    // (a client retry). The part starting at the read position is always accepted.
    bool Put(int64_t off, std::vector<uint8_t>&& data, const Abort& abort) {
        if (off < 0 || data.empty() || off + static_cast<int64_t>(data.size()) > m_total) {
            return false;
        }
        for (;;) {
            {
                std::scoped_lock lock{m_mutex};
                if (off + static_cast<int64_t>(data.size()) <= m_pos || m_parts.count(off)) {
                    return true;
                }
                if (off < m_pos + m_window) {
                    m_parts.emplace(off, std::move(data));
                    m_last = Clock::now();
                    return true;
                }
            }
            if (Wait(abort)) {
                return false;
            }
        }
    }

    // bytes read (at least 1), 0 at the end, -1 when aborted or nothing arrived for idle_ms.
    int64_t Read(void* out, int64_t size, const Abort& abort) {
        for (;;) {
            {
                std::scoped_lock lock{m_mutex};
                if (m_pos >= m_total || size <= 0) {
                    return 0;
                }
                auto it = m_parts.upper_bound(m_pos);
                if (it != m_parts.begin()) {
                    --it;
                    const int64_t end = it->first + static_cast<int64_t>(it->second.size());
                    if (end > m_pos) {
                        const int64_t n = std::min(size, end - m_pos);
                        std::memcpy(out, it->second.data() + (m_pos - it->first), n);
                        m_pos += n;
                        m_last = Clock::now();
                        // drop every part that is read to its end (a retry or an overlap can leave an older one)
                        while (!m_parts.empty() && m_parts.begin()->first + static_cast<int64_t>(m_parts.begin()->second.size()) <= m_pos) {
                            m_parts.erase(m_parts.begin());
                        }
                        return n;
                    }
                }
            }
            if (Wait(abort)) {
                return -1;
            }
        }
    }

    int64_t Pos() {
        std::scoped_lock lock{m_mutex};
        return m_pos;
    }

private:
    using Clock = std::chrono::steady_clock;

    // sleeps 1 ms; true when the caller must give up
    bool Wait(const Abort& abort) {
        if (abort && abort()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::scoped_lock lock{m_mutex};
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_last).count() > m_idle_ms;
    }

    std::mutex m_mutex;
    std::map<int64_t, std::vector<uint8_t>> m_parts;
    const int64_t m_total;
    const int64_t m_window;
    const int64_t m_idle_ms;
    int64_t m_pos{};
    Clock::time_point m_last;
};

} // namespace sphaira
