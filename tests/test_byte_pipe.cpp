// BytePipe: the bounded queue between UnRAR's worker thread and FsArchive reads.
#include "byte_pipe.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using sphaira::BytePipe;

static std::vector<unsigned char> Pattern(size_t n) {
    std::vector<unsigned char> v(n);
    for (size_t i = 0; i < n; i++) {
        v[i] = static_cast<unsigned char>(i * 7 + i / 251);
    }
    return v;
}

int main() {
    // single thread: data, then end.
    {
        BytePipe p{8};
        assert(p.Write("abc", 3));
        p.Finish();
        char out[16]{};
        assert(p.Read(out, sizeof(out)) == 3 && std::string(out, 3) == "abc");
        assert(p.Read(out, sizeof(out)) == 0); // finished and drained
    }

    // a producer much larger than the buffer, odd chunk sizes both sides, wrap-around.
    for (const size_t cap : {1, 3, 64, 4096}) {
        const auto src = Pattern(100000);
        BytePipe p{cap};
        std::thread producer{[&] {
            size_t off = 0, step = 1;
            while (off < src.size()) {
                const auto n = std::min(step, src.size() - off);
                assert(p.Write(src.data() + off, n));
                off += n;
                step = step % 997 + 13;
            }
            p.Finish();
        }};
        std::vector<unsigned char> got;
        unsigned char buf[333];
        size_t want = 1;
        while (const auto n = p.Read(buf, want)) {
            got.insert(got.end(), buf, buf + n);
            want = want % sizeof(buf) + 1;
        }
        producer.join();
        assert(got == src);
    }

    // the reader closes while the producer is blocked on a full pipe: Write returns false.
    {
        BytePipe p{16};
        bool write_result = true;
        std::thread producer{[&] {
            const auto big = Pattern(1000);
            write_result = p.Write(big.data(), big.size());
        }};
        char buf[4];
        assert(p.Read(buf, sizeof(buf)) == 4);
        p.Close();
        producer.join();
        assert(!write_result);
        assert(p.Closed());
        assert(!p.Write("x", 1));
    }

    // a reader blocked on an empty pipe wakes on Finish with 0.
    {
        BytePipe p{16};
        size_t got = 99;
        std::thread reader{[&] {
            char b[4];
            got = p.Read(b, sizeof(b));
        }};
        p.Finish();
        reader.join();
        assert(got == 0);
    }

    std::puts("test_byte_pipe: ok");
    return 0;
}
