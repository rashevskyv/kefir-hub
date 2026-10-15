// LINK: sphaira/source/repack/cnmt_patch.cpp
#include "repack/cnmt_patch.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace sphaira::repack;

namespace {

// header 0x20, extended header `ext` bytes, then one 0x38 content info per type.
std::vector<std::uint8_t> Cnmt(std::uint16_t ext, std::vector<std::uint8_t> types) {
    std::vector<std::uint8_t> c(0x20 + ext + types.size() * 0x38 + 0x20, 0xAA);
    const std::uint16_t count = types.size();
    std::memcpy(c.data() + 0xE, &ext, 2);
    std::memcpy(c.data() + 0x10, &count, 2);
    for (size_t i = 0; i < types.size(); i++) {
        auto* e = c.data() + 0x20 + ext + i * 0x38;
        e[0x36] = types[i];
        e[0x37] = 0;
    }
    return c;
}

void TestPatch() {
    std::array<std::uint8_t, 0x20> hash;
    for (int i = 0; i < 0x20; i++) {
        hash[i] = i + 1;
    }
    auto c = Cnmt(0x18, {3, 1, 2});
    const auto before = c;
    assert(PatchCnmtContent(c, CNMT_CONTENT_PROGRAM, hash, 0x123456789A));

    const auto* e = c.data() + 0x20 + 0x18 + 0x38;
    assert(!std::memcmp(e, hash.data(), 0x20));
    assert(!std::memcmp(e + 0x20, hash.data(), 0x10));
    const std::uint8_t size[]{0x9A, 0x78, 0x56, 0x34, 0x12};
    assert(!std::memcmp(e + 0x30, size, 5));
    // attributes, type, id offset and every other byte stay.
    for (size_t i = 0; i < c.size(); i++) {
        const auto rel = i - (0x20 + 0x18 + 0x38);
        if (rel < 0x35) {
            continue;
        }
        assert(c[i] == before[i]);
    }
}

void TestRefuse() {
    std::array<std::uint8_t, 0x20> hash{};
    auto none = Cnmt(0x10, {3, 2});
    assert(!PatchCnmtContent(none, CNMT_CONTENT_PROGRAM, hash, 1));
    auto two = Cnmt(0x10, {1, 1});
    assert(!PatchCnmtContent(two, CNMT_CONTENT_PROGRAM, hash, 1));
    auto c = Cnmt(0x10, {1});
    assert(!PatchCnmtContent(std::span(c).first(0x20 + 0x10 + 0x37), CNMT_CONTENT_PROGRAM, hash, 1));
    assert(!PatchCnmtContent(c, CNMT_CONTENT_PROGRAM, hash, 1ull << 40));
    // a program with a non-zero id offset (a second program in the same title) is not the main one.
    c[0x20 + 0x10 + 0x37] = 1;
    assert(!PatchCnmtContent(c, CNMT_CONTENT_PROGRAM, hash, 1));
}

} // namespace

int main() {
    TestPatch();
    TestRefuse();
    std::puts("test_cnmt_patch: ok");
}
