// Shared helpers for tests/test_nso_*.cpp. Run from the repo root (tests/run.sh does).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "nso/common.hpp"

inline int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } \
    } while (0)
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        if (!((a) == (b))) { std::printf("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); g_fail++; } \
    } while (0)

inline std::string ReadFile(const std::string& name) {
    std::ifstream f("tests/data/nso/" + name, std::ios::binary);
    if (!f) { std::printf("FAIL cannot open tests/data/nso/%s (run from the repo root)\n", name.c_str()); std::exit(1); }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
inline nso::Bytes ReadBytes(const std::string& name) {
    const std::string s = ReadFile(name);
    return nso::Bytes(s.begin(), s.end());
}

// xorshift32 bytes, same generator as tests/data/nso/make_fixtures.py
inline nso::Bytes Gen(nso::u32 seed, size_t n) {
    nso::Bytes o;
    nso::u32 x = seed;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        o.push_back(nso::u8(x >> 16));
    }
    return o;
}

struct Expect { size_t size; nso::u32 crc; };
inline std::map<std::string, Expect> LoadExpect() {
    std::map<std::string, Expect> m;
    std::istringstream in(ReadFile("expect.txt"));
    std::string name;
    size_t size;
    std::string crc;
    while (in >> name >> size >> crc) m[name] = {size, nso::u32(std::stoul(crc, nullptr, 16))};
    return m;
}
inline bool Matches(const nso::Bytes& b, const Expect& e) { return b.size() == e.size && nso::Crc32(b) == e.crc; }

inline int Finish(const char* name) {
    if (g_fail) { std::printf("%s: %d FAILED\n", name, g_fail); return 1; }
    std::printf("%s: ok\n", name);
    return 0;
}
