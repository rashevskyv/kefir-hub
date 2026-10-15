// Host test for sphaira/include/repack/romfs_walk.hpp: walks images built by romfs_pack::BuildLayout.
// LINK: sphaira/source/repack/romfs_walk.cpp
// LINK: sphaira/source/romfs_pack_layout.cpp
#include "repack/romfs_walk.hpp"
#include "romfs_pack_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <span>

using namespace sphaira;

namespace {

std::uint64_t Field(const romfs_pack::Layout& l, int i) {
    std::uint64_t v;
    std::memcpy(&v, l.header.data() + i * 8, sizeof(v));
    return v;
}

bool Walk(const romfs_pack::Layout& l, std::vector<std::uint8_t> tables, std::vector<repack::RomfsFile>& out) {
    // header fields: 3 dir table offset, 4 size, 7 file table offset, 8 size, 9 file data offset.
    const auto t = std::span<const std::uint8_t>(tables);
    return repack::WalkRomfs(t.subspan(Field(l, 3) - l.tables_offset, Field(l, 4)), t.subspan(Field(l, 7) - l.tables_offset, Field(l, 8)), Field(l, 9), out);
}

void TestRoundTrip() {
    const std::vector<romfs_pack::Entry> in{
        {"Language/ru.csv.z", 41560, false},
        {"Language/en.csv.z", 40000, false},
        {"a.bin", 1, false},
        {"deep/x/y/z.dat", 0, false},
        {"empty", 0, true},
    };
    const auto l = romfs_pack::BuildLayout(in);
    std::vector<repack::RomfsFile> out;
    assert(Walk(l, l.tables, out));
    assert(out.size() == 4);
    for (size_t i = 0; i < 4; i++) {
        const auto it = std::ranges::find(out, in[i].path, &repack::RomfsFile::path);
        assert(it != out.end());
        assert(it->size == in[i].size);
        assert(it->offset == l.data_offsets[i]);
    }
}

void TestEmpty() {
    const auto l = romfs_pack::BuildLayout({});
    std::vector<repack::RomfsFile> out;
    assert(Walk(l, l.tables, out) && out.empty());
}

void TestBroken() {
    const auto l = romfs_pack::BuildLayout({{"a/b", 1, false}, {"a/c", 2, false}});
    std::vector<repack::RomfsFile> out;

    // a file name length past the end of the table.
    auto t = l.tables;
    const auto file_table = Field(l, 7) - l.tables_offset;
    std::memset(t.data() + file_table + 0x1C, 0xFF, 4);
    assert(!Walk(l, t, out));

    // the first file points back to itself as its sibling.
    t = l.tables;
    std::memset(t.data() + file_table + 4, 0, 4);
    assert(!Walk(l, t, out));

    // the child dir of root points back to root.
    t = l.tables;
    std::memset(t.data() + Field(l, 3) - l.tables_offset + 8, 0, 4);
    assert(!Walk(l, t, out));
}

} // namespace

int main() {
    TestRoundTrip();
    TestEmpty();
    TestBroken();
    std::puts("test_romfs_walk: ok");
}
