// Host test for sphaira/include/web_part_buffer.hpp (in-order window of the multi-connection web install)
//
// g++ -std=c++20 -pthread -Wall -Wextra -Werror -I sphaira/include tests/test_web_part_buffer.cpp -o /tmp/t && /tmp/t

#include "web_part_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static uint8_t Pattern(int64_t i) {
    return static_cast<uint8_t>((i * 31 + (i >> 8)) & 0xFF);
}

static std::vector<uint8_t> Part(int64_t off, int64_t len) {
    std::vector<uint8_t> v(len);
    for (int64_t i = 0; i < len; i++) {
        v[i] = Pattern(off + i);
    }
    return v;
}

int main() {
    using sphaira::PartBuffer;
    const PartBuffer::Abort never = [] { return false; };

    // three senders put 1 MiB parts out of order (a 4 MiB window), one reader checks every byte in order
    {
        constexpr int64_t CH = 1 << 20;
        constexpr int64_t TOTAL = 10 * CH + 12345; // the last part is short
        PartBuffer buf{TOTAL, 4 * CH};
        std::atomic<int64_t> next{0};
        std::vector<std::thread> senders;
        for (int t = 0; t < 3; t++) {
            senders.emplace_back([&, t] {
                for (;;) {
                    const int64_t off = next.fetch_add(CH);
                    if (off >= TOTAL) {
                        return;
                    }
                    // thread 1 is slow, so its parts arrive after later ones
                    if (t == 1) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(15));
                    }
                    const int64_t len = std::min(CH, TOTAL - off);
                    buf.Put(off, Part(off, len), never);
                    buf.Put(off, Part(off, len), never); // a retry is accepted and dropped
                }
            });
        }

        int64_t got = 0;
        bool ok = true;
        std::vector<uint8_t> chunk(777777);
        for (;;) {
            const auto n = buf.Read(chunk.data(), chunk.size(), never);
            if (n <= 0) {
                ok = ok && n == 0;
                break;
            }
            for (int64_t i = 0; i < n; i++) {
                ok = ok && chunk[i] == Pattern(got + i);
            }
            got += n;
        }
        for (auto& th : senders) {
            th.join();
        }
        CHECK(ok);
        CHECK(got == TOTAL);
        CHECK(buf.Pos() == TOTAL);
    }

    // the reader waits for the hole, and a stalled sender times out
    {
        PartBuffer buf{100, 100, 40};
        uint8_t out[10];
        CHECK(buf.Read(out, 10, never) == -1);          // nothing arrives
        CHECK(!buf.Put(0, Part(0, 101), never));        // longer than the package
        CHECK(!buf.Put(-1, Part(0, 5), never));
        CHECK(!buf.Put(5, {}, never));                  // empty
    }

    // a part beyond the window waits, then the abort ends the wait
    {
        PartBuffer buf{1000, 100, 60000};
        std::atomic<bool> stop{false};
        std::atomic<bool> result{true};
        std::thread t([&] { result = buf.Put(500, Part(500, 10), [&] { return stop.load(); }); });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        stop = true;
        t.join();
        CHECK(!result);
        CHECK(buf.Put(50, Part(50, 10), never));        // inside the window
        uint8_t out[60];
        CHECK(buf.Read(out, 1, [] { return true; }) == -1); // byte 0 is missing: abort answers
    }

    // a part overlapping the position is read from the middle
    {
        PartBuffer buf{100, 100};
        CHECK(buf.Put(0, Part(0, 10), never));
        uint8_t out[4];
        CHECK(buf.Read(out, 4, never) == 4 && out[0] == Pattern(0) && out[3] == Pattern(3));
        CHECK(buf.Read(out, 4, never) == 4 && out[0] == Pattern(4));
        CHECK(buf.Put(5, Part(5, 10), never));          // partly read already: kept, read from byte 8
        CHECK(buf.Read(out, 4, never) == 4 && out[0] == Pattern(8));
        CHECK(buf.Read(out, 4, never) == 3 && out[0] == Pattern(12)); // the part ends at 15
        CHECK(buf.Put(15, Part(15, 5), never));
        CHECK(buf.Read(out, 4, never) == 4 && out[0] == Pattern(15));
    }

    std::printf("ok %d checks\n", g_checks);
    return 0;
}
