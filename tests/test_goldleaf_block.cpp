// Host test for sphaira/include/usb/goldleaf.hpp -- the fixed 0x1000 byte
// block codec the Goldleaf usb protocol is built out of. Every field a
// BlockReader hands back came off the wire from the PC, so the bounds checks
// are the whole point of this file: a hostile length prefix must be rejected,
// not wrapped around into a read inside the block.
//
//     g++ -std=c++20 -I sphaira/include tests/test_goldleaf_block.cpp -o /tmp/t && /tmp/t
//
// or just: tests/run.sh

#include "usb/goldleaf.hpp"

#include <array>
#include <cstdio>
#include <cstdint>
#include <string>

using namespace sphaira::usb;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

using Block = std::array<std::uint8_t, goldleaf::BLOCK_SIZE>;

// what GoldleafRead() puts on the wire: header, path, offset, size.
static int test_round_trip() {
    Block buf{};
    goldleaf::BlockWriter w{buf.data()};

    w.U32(goldleaf::Magic_In);
    w.U32(static_cast<std::uint32_t>(goldleaf::CmdId::ReadFile));
    w.Str("VIRT:/game.nsp");
    w.U64(0x4000);
    w.U64(0x400000);
    CHECK(w.Ok());
    // 4 + 4 + (4 + 14) + 8 + 8
    CHECK(w.Pos() == 42);

    goldleaf::BlockReader r{buf.data()};
    std::uint32_t magic, cmd;
    std::string path;
    std::uint64_t off, size;

    CHECK(r.U32(&magic));
    CHECK(r.U32(&cmd));
    CHECK(r.Str(&path));
    CHECK(r.U64(&off));
    CHECK(r.U64(&size));

    CHECK(magic == goldleaf::Magic_In);
    CHECK(cmd == 9);
    CHECK(path == "VIRT:/game.nsp");
    CHECK(off == 0x4000);
    CHECK(size == 0x400000);
    CHECK(r.Pos() == w.Pos());

    return 0;
}

// the magic is little endian on the wire, so the bytes must read as ascii.
static int test_magic_bytes() {
    Block buf{};
    goldleaf::BlockWriter w{buf.data()};
    w.U32(goldleaf::Magic_In);
    CHECK(w.Ok());
    CHECK(buf[0] == 'G' && buf[1] == 'L' && buf[2] == 'C' && buf[3] == 'I');

    Block out{};
    goldleaf::BlockWriter w2{out.data()};
    w2.U32(goldleaf::Magic_Out);
    CHECK(out[0] == 'G' && out[1] == 'L' && out[2] == 'C' && out[3] == 'O');

    return 0;
}

static int test_writer_overflow() {
    Block buf{};
    goldleaf::BlockWriter w{buf.data()};

    // exactly fills the block.
    const std::string fill(goldleaf::BLOCK_SIZE - 4, 'x');
    w.Str(fill);
    CHECK(w.Ok());
    CHECK(w.Pos() == goldleaf::BLOCK_SIZE);

    // one byte past is refused, and the writer stays poisoned afterwards so a
    // caller that only looks at Ok() at the end still catches it.
    w.U32(1);
    CHECK(!w.Ok());
    goldleaf::BlockWriter w2{buf.data()};
    w2.Str(std::string(goldleaf::BLOCK_SIZE, 'x'));
    CHECK(!w2.Ok());
    w2.U32(1);
    CHECK(!w2.Ok());

    // a string longer than the whole block must not wrap its length prefix.
    goldleaf::BlockWriter w3{buf.data()};
    w3.Str(std::string(goldleaf::BLOCK_SIZE + 8, 'x'));
    CHECK(!w3.Ok());

    // a default constructed writer has nowhere to put anything.
    goldleaf::BlockWriter w4{};
    w4.U32(1);
    CHECK(!w4.Ok());

    return 0;
}

static int test_reader_bounds() {
    Block buf{};
    goldleaf::BlockReader r{buf.data()};

    // walk to four bytes from the end, then ask for eight.
    for (std::uint32_t i = 0; i < (goldleaf::BLOCK_SIZE / 4) - 1; i++) {
        std::uint32_t v;
        CHECK(r.U32(&v));
    }
    std::uint64_t big;
    CHECK(!r.U64(&big));
    CHECK(r.Pos() == goldleaf::BLOCK_SIZE - 4);

    // the last four bytes are still readable, nothing after them is.
    std::uint32_t last;
    CHECK(r.U32(&last));
    CHECK(!r.U32(&last));

    return 0;
}

// the case that makes this file worth having: a length prefix chosen so that
// pos + len wraps back inside the block if the check is written as an addition.
static int test_hostile_string_length() {
    Block buf{};
    goldleaf::BlockWriter w{buf.data()};
    w.U32(0xFFFFFFFF);
    CHECK(w.Ok());

    goldleaf::BlockReader r{buf.data()};
    std::string s = "untouched";
    CHECK(!r.Str(&s));
    CHECK(s == "untouched");

    // and the merely-too-long case.
    Block buf2{};
    goldleaf::BlockWriter w2{buf2.data()};
    w2.U32(goldleaf::BLOCK_SIZE);
    goldleaf::BlockReader r2{buf2.data()};
    CHECK(!r2.Str(&s));

    // a length that reaches exactly the end of the block is fine.
    Block buf3{};
    goldleaf::BlockWriter w3{buf3.data()};
    w3.U32(goldleaf::BLOCK_SIZE - 4);
    goldleaf::BlockReader r3{buf3.data()};
    CHECK(r3.Str(&s));
    CHECK(s.size() == goldleaf::BLOCK_SIZE - 4);

    return 0;
}

// an empty name is what a host sends for an index it does not have; the codec
// accepts it and GoldleafWaitForConnection() is what rejects it.
static int test_empty_string() {
    Block buf{};
    goldleaf::BlockWriter w{buf.data()};
    w.Str("");
    CHECK(w.Ok());
    CHECK(w.Pos() == 4);

    goldleaf::BlockReader r{buf.data()};
    std::string s = "untouched";
    CHECK(r.Str(&s));
    CHECK(s.empty());

    return 0;
}

int main() {
    if (test_round_trip()) return 1;
    if (test_magic_bytes()) return 1;
    if (test_writer_overflow()) return 1;
    if (test_reader_bounds()) return 1;
    if (test_hostile_string_length()) return 1;
    if (test_empty_string()) return 1;

    std::printf("test_goldleaf_block: %d checks passed\n", g_checks);
    return 0;
}
