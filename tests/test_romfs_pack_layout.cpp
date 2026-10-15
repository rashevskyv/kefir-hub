// Host test for sphaira/include/romfs_pack_layout.hpp: builds a layout, then reads
// the tables back the way fs.mitm and hactool do.
// LINK: sphaira/source/romfs_pack_layout.cpp
#include "romfs_pack_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace sphaira::romfs_pack;

namespace {

constexpr std::uint32_t EMPTY = 0xFFFFFFFF;

auto Get32(const std::vector<std::uint8_t>& b, std::uint64_t at) -> std::uint32_t {
    return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<std::uint32_t>(b[at + 3]) << 24;
}

auto Get64(const std::vector<std::uint8_t>& b, std::uint64_t at) -> std::uint64_t {
    return Get32(b, at) | static_cast<std::uint64_t>(Get32(b, at + 4)) << 32;
}

struct Image {
    std::vector<std::uint8_t> t; // tables, indexed by absolute offset - base
    std::uint64_t base{};
    std::uint64_t dir_hash{}, dir_hash_size{}, dir_table{}, file_hash{}, file_hash_size{}, file_table{};

    auto U32(std::uint64_t abs) const { return Get32(t, abs - base); }
    auto U64(std::uint64_t abs) const { return Get64(t, abs - base); }
    auto Name(std::uint64_t abs, std::uint32_t len) const {
        return std::string(t.begin() + (abs - base), t.begin() + (abs - base) + len);
    }
};

struct Found {
    std::uint64_t offset{}, size{};
};

// walks child/sibling links from the root, as fs.mitm's VisitDirectory does.
void Walk(const Image& im, std::uint32_t dir, const std::string& prefix, std::map<std::string, Found>& files, std::vector<std::string>& dirs) {
    const auto d = im.dir_table + dir;
    for (auto f = im.U32(d + 0xC); f != EMPTY; f = im.U32(im.file_table + f + 0x4)) {
        const auto e = im.file_table + f;
        assert(im.U32(e + 0x0) == dir);
        const auto name = im.Name(e + 0x20, im.U32(e + 0x1C));
        files[prefix + name] = {im.U64(e + 0x8), im.U64(e + 0x10)};

        // the file is reachable through its hash chain.
        const auto bucket = PathHash(dir, name.data(), name.size()) % (im.file_hash_size / 4);
        bool hit = false;
        for (auto c = im.U32(im.file_hash + 4 * bucket); c != EMPTY; c = im.U32(im.file_table + c + 0x18)) {
            hit |= c == f;
        }
        assert(hit);
    }
    for (auto c = im.U32(d + 0x8); c != EMPTY; c = im.U32(im.dir_table + c + 0x4)) {
        const auto e = im.dir_table + c;
        assert(im.U32(e + 0x0) == dir);
        const auto name = im.Name(e + 0x18, im.U32(e + 0x14));
        dirs.push_back(prefix + name);

        const auto bucket = PathHash(dir, name.data(), name.size()) % (im.dir_hash_size / 4);
        bool hit = false;
        for (auto h = im.U32(im.dir_hash + 4 * bucket); h != EMPTY; h = im.U32(im.dir_table + h + 0x10)) {
            hit |= h == c;
        }
        assert(hit);
        Walk(im, c, prefix + name + "/", files, dirs);
    }
}

} // namespace

int main() {
    const std::vector<Entry> in{
        {"a/c/e", 100, false},
        {"a.txt", 5, false},
        {"a/b.bin", 17, false},
        {"empty", 0, true},
        {"a/c/d", 0, false},
        {"a.b", 3, false},
        {"Z", 1, false},
        {"deep/x/y/z.dat", 33, false},
    };
    const auto l = BuildLayout(in);

    // header
    assert(l.header.size() == DATA_OFFSET);
    assert(Get64(l.header, 0x00) == 0x50);
    assert(Get64(l.header, 0x48) == DATA_OFFSET);
    assert(l.tables_offset % 4 == 0);
    assert(l.total_size == l.tables_offset + l.tables.size());

    Image im{l.tables, l.tables_offset};
    im.dir_hash = Get64(l.header, 0x08);
    im.dir_hash_size = Get64(l.header, 0x10);
    im.dir_table = Get64(l.header, 0x18);
    im.file_hash = Get64(l.header, 0x28);
    im.file_hash_size = Get64(l.header, 0x30);
    im.file_table = Get64(l.header, 0x38);
    assert(im.dir_hash == l.tables_offset);
    assert(im.dir_table == im.dir_hash + im.dir_hash_size);
    assert(im.file_hash == im.dir_table + Get64(l.header, 0x20));
    assert(im.file_table == im.file_hash + im.file_hash_size);
    assert(im.file_table + Get64(l.header, 0x40) == l.total_size);

    // root: offset 0, its own parent, no name.
    assert(im.U32(im.dir_table + 0x0) == 0);
    assert(im.U32(im.dir_table + 0x4) == EMPTY);
    assert(im.U32(im.dir_table + 0x14) == 0);

    std::map<std::string, Found> files;
    std::vector<std::string> dirs;
    Walk(im, 0, "", files, dirs);

    std::size_t n_files = 0;
    for (std::size_t i = 0; i < in.size(); i++) {
        if (in[i].is_dir) {
            assert(l.data_offsets[i] == 0);
            continue;
        }
        n_files++;
        const auto it = files.find(in[i].path);
        assert(it != files.end());
        assert(it->second.size == in[i].size);
        assert(DATA_OFFSET + it->second.offset == l.data_offsets[i]);
        assert(l.data_offsets[i] % 0x10 == 0);
    }
    assert(files.size() == n_files);
    for (const auto* d : {"a", "a/c", "deep", "deep/x", "deep/x/y", "empty"}) {
        assert(std::find(dirs.begin(), dirs.end(), d) != dirs.end());
    }
    assert(dirs.size() == 6);

    // data goes up in full-path order and nothing overlaps; fs.mitm fails otherwise.
    std::uint64_t end = DATA_OFFSET;
    for (const auto& [path, f] : files) {
        assert(DATA_OFFSET + f.offset >= end);
        end = DATA_OFFSET + f.offset + f.size;
    }
    assert(end <= l.tables_offset);
    // "a.b" < "a.txt" < "a/b.bin": '.' sorts before '/', unlike a tree walk.
    assert(files["a.txt"].offset < files["a/b.bin"].offset);

    // nothing to pack: a valid image with only the root.
    const auto e = BuildLayout({});
    assert(e.total_size == e.tables_offset + e.tables.size());
    assert(Get64(e.header, 0x20) == 0x18);
    assert(Get64(e.header, 0x40) == 0);

    std::puts("test_romfs_pack_layout: ok");
}
