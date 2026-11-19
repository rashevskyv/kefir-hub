// Host test for sphaira/include/tar_stream_layout.hpp: the tar made on the fly has the layout
// a tar reader expects, whatever the read sizes. With a path as the first argument the tar is
// also written there (to check it with a real tar reader by hand).

#include "tar_stream_layout.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace sphaira::tar_stream;

static auto Octal(const std::string& tar, size_t off, size_t len) -> int64_t {
    return std::strtoll(tar.substr(off, len).c_str(), nullptr, 8);
}

// walks the headers like a tar reader: returns name -> data, checks every checksum.
static auto Parse(const std::string& tar) -> std::map<std::string, std::string> {
    std::map<std::string, std::string> out;
    std::string pax_path;
    size_t off = 0;
    while (off + BLOCK <= tar.size() && tar[off] != '\0') {
        unsigned sum = 0;
        for (size_t i = 0; i < BLOCK; i++) {
            sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(tar[off + i]);
        }
        assert(sum == static_cast<unsigned>(Octal(tar, off + 148, 8)));
        assert(tar.substr(off + 257, 5) == "ustar");
        const auto size = static_cast<size_t>(Octal(tar, off + 124, 12));
        const auto data = tar.substr(off + BLOCK, size);
        if (tar[off + 156] == 'x') {
            // "<len> path=<value>\n" as the first record
            const auto space = data.find(' ');
            const auto len = static_cast<size_t>(std::atoi(data.c_str()));
            assert(data.substr(space + 1, 5) == "path=" && data[len - 1] == '\n');
            pax_path = data.substr(space + 6, len - space - 7);
        } else {
            assert(tar[off + 156] == '0');
            const auto name = pax_path.empty() ? std::string{tar.c_str() + off} : pax_path;
            out[name] = data;
            pax_path.clear();
        }
        off += BLOCK + (size + BLOCK - 1) / BLOCK * BLOCK;
    }
    // two empty blocks end the tar
    assert(off + 2 * BLOCK == tar.size());
    assert(tar.find_first_not_of('\0', off) == std::string::npos);
    return out;
}

int main(int argc, char** argv) {
    assert(PaxRecord("path", "abc") == "12 path=abc\n");
    assert(PaxRecord("path", std::string(90, 'a')).starts_with("99 path=") && PaxRecord("path", std::string(91, 'a')).starts_with("101 path="));
    assert(Header("switch/port/port.nro", 5).size() == 512);                 // plain ustar
    assert(Header("switch/Привіт.bin", 5).size() == 3 * 512);                // pax header, record, ustar
    assert(Header(std::string(101, 'a'), 5).size() == 3 * 512);
    assert(Header("big.bin", USTAR_MAX_SIZE).size() == 512 && Header("big.bin", USTAR_MAX_SIZE + 1).size() == 3 * 512);
    assert(Header("big.bin", USTAR_MAX_SIZE + 1).find("size=8589934592\n") != std::string::npos);

    const std::string long_name = "atmosphere/contents/0100B7C00933A000/romfs/" + std::string(120, 'd') + "/b.bin";
    const std::map<std::string, std::string> files{
        {"a.bin", std::string(1000, 'A')},
        {"b.bin", std::string(512, 'B')},
        {"empty.bin", ""},
        {"big.bin", std::string(100000, 'C')},
    };
    const Layout layout{{
        {".kefir/sizes", "", "0\t49152\n", 8},
        {"atmosphere/contents/0100B7C00933A000/romfs/Привіт.bin", "a.bin", "", 1000},
        {long_name, "b.bin", "", 512},
        {"switch/port/empty", "empty.bin", "", 0},
        {"switch/port/data.pak", "big.bin", "", 100000},
    }};
    assert(layout.size % BLOCK == 0);

    const auto read_file = [&](size_t member, int64_t off, void* buf, int64_t n) -> uint32_t {
        const auto& data = files.at(layout.members[member].src);
        assert(off >= 0 && off + n <= static_cast<int64_t>(data.size()));
        std::memcpy(buf, data.data() + off, n);
        return 0;
    };

    std::string whole(layout.size, '\xFF');
    assert(layout.Read(0, whole.data(), layout.size, read_file) == 0);
    const auto got = Parse(whole);
    assert(got.size() == 5 && got.at(".kefir/sizes") == "0\t49152\n");
    assert(got.at("atmosphere/contents/0100B7C00933A000/romfs/Привіт.bin") == files.at("a.bin"));
    assert(got.at(long_name) == files.at("b.bin") && got.at("switch/port/empty").empty());
    assert(got.at("switch/port/data.pak") == files.at("big.bin"));

    // any chunk sizes give the same bytes (libarchive asks for 1 MiB, the last read is short)
    std::srand(1);
    for (const int64_t fixed : {int64_t{1}, int64_t{7}, int64_t{511}, int64_t{512}, int64_t{513}, int64_t{4096}, int64_t{0}}) {
        std::string pieces(layout.size, '\xFF');
        for (int64_t off = 0; off < layout.size;) {
            const auto n = std::min<int64_t>(layout.size - off, fixed ? fixed : 1 + std::rand() % 70000);
            assert(layout.Read(off, pieces.data() + off, n, read_file) == 0);
            off += n;
        }
        assert(pieces == whole);
    }

    // a failed file read stops the tar with that error: no zero fill
    assert(layout.Read(0, whole.data(), layout.size, [](size_t, int64_t, void*, int64_t) -> uint32_t { return 0x202; }) == 0x202);
    assert(Layout{}.size == 0 && Layout{{}}.size == 2 * BLOCK);

    if (argc > 1) {
        assert(layout.Read(0, whole.data(), layout.size, read_file) == 0);
        const auto f = std::fopen(argv[1], "wb");
        assert(f && std::fwrite(whole.data(), 1, whole.size(), f) == whole.size());
        std::fclose(f);
    }
    std::puts("test_tar_stream_layout: ok");
    return 0;
}
