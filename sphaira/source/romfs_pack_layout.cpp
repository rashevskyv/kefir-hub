#include "romfs_pack_layout.hpp"

#include <algorithm>
#include <unordered_map>

namespace sphaira::romfs_pack {
namespace {

constexpr std::uint32_t EMPTY = 0xFFFFFFFF;
constexpr std::uint64_t HEADER_SIZE = 0x50;
constexpr std::uint64_t DIR_ENTRY_SIZE = 0x18;
constexpr std::uint64_t FILE_ENTRY_SIZE = 0x20;

struct Dir {
    std::string name;
    std::uint32_t parent{};
    std::vector<std::uint32_t> dirs{};
    std::vector<std::uint32_t> files{};
    std::uint32_t offset{};
    std::uint32_t sibling{EMPTY};
};

struct File {
    std::string name;
    std::uint32_t parent{};
    std::size_t input{};
    std::uint32_t offset{};
    std::uint32_t sibling{EMPTY};
    std::uint64_t data{};
};

auto Align(std::uint64_t v, std::uint64_t a) -> std::uint64_t {
    return (v + a - 1) & ~(a - 1);
}

void Put32(std::vector<std::uint8_t>& b, std::uint64_t at, std::uint32_t v) {
    for (int i = 0; i < 4; i++) {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

void Put64(std::vector<std::uint8_t>& b, std::uint64_t at, std::uint64_t v) {
    Put32(b, at, static_cast<std::uint32_t>(v));
    Put32(b, at + 4, static_cast<std::uint32_t>(v >> 32));
}

struct Tree {
    std::vector<Dir> dirs{Dir{}};
    std::unordered_map<std::string, std::uint32_t> index{{"", 0}};

    // index of the directory at `path`, created with its parents when missing.
    auto Ensure(const std::string& path) -> std::uint32_t {
        if (const auto it = index.find(path); it != index.end()) {
            return it->second;
        }
        const auto slash = path.rfind('/');
        const auto parent = slash == std::string::npos ? 0 : Ensure(path.substr(0, slash));
        const auto id = static_cast<std::uint32_t>(dirs.size());
        dirs.push_back(Dir{.name = slash == std::string::npos ? path : path.substr(slash + 1), .parent = parent});
        dirs[parent].dirs.push_back(id);
        index.emplace(path, id);
        return id;
    }
};

void Chain(std::vector<std::uint8_t>& t, std::uint64_t table_at, std::uint32_t count, std::uint64_t entry_at, std::uint64_t next_field, std::uint32_t parent, const std::string& name, std::uint32_t offset) {
    const auto slot = table_at + 4ULL * (PathHash(parent, name.data(), name.size()) % count);
    const auto head = static_cast<std::uint32_t>(t[slot] | t[slot + 1] << 8 | t[slot + 2] << 16 | static_cast<std::uint32_t>(t[slot + 3]) << 24);
    Put32(t, entry_at + next_field, head);
    Put32(t, slot, offset);
}

} // namespace

auto PathHash(std::uint32_t parent, const char* name, std::size_t len) -> std::uint32_t {
    std::uint32_t hash = parent ^ 123456789;
    for (std::size_t i = 0; i < len; i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= static_cast<std::uint8_t>(name[i]);
    }
    return hash;
}

auto HashTableCount(std::uint32_t entries) -> std::uint32_t {
    if (entries < 3) {
        return 3;
    } else if (entries < 19) {
        return entries | 1;
    }
    std::uint32_t count = entries;
    while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0 || count % 11 == 0 || count % 13 == 0 || count % 17 == 0) {
        count++;
    }
    return count;
}

auto BuildLayout(const std::vector<Entry>& entries) -> Layout {
    Tree tree;
    std::vector<File> files;
    for (std::size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        if (e.is_dir) {
            tree.Ensure(e.path);
            continue;
        }
        const auto slash = e.path.rfind('/');
        const auto parent = slash == std::string::npos ? 0 : tree.Ensure(e.path.substr(0, slash));
        tree.dirs[parent].files.push_back(static_cast<std::uint32_t>(files.size()));
        files.push_back(File{.name = slash == std::string::npos ? e.path : e.path.substr(slash + 1), .parent = parent, .input = i});
    }
    auto& dirs = tree.dirs;

    // children by name, siblings linked in that order.
    for (auto& d : dirs) {
        std::sort(d.dirs.begin(), d.dirs.end(), [&](auto a, auto b) { return dirs[a].name < dirs[b].name; });
        std::sort(d.files.begin(), d.files.end(), [&](auto a, auto b) { return files[a].name < files[b].name; });
        for (std::size_t k = 1; k < d.dirs.size(); k++) {
            dirs[d.dirs[k - 1]].sibling = d.dirs[k];
        }
        for (std::size_t k = 1; k < d.files.size(); k++) {
            files[d.files[k - 1]].sibling = d.files[k];
        }
    }

    // entry offsets, directories breadth first.
    std::vector<std::uint32_t> order{0};
    for (std::size_t k = 0; k < order.size(); k++) {
        const auto& kids = dirs[order[k]].dirs;
        order.insert(order.end(), kids.begin(), kids.end());
    }
    std::uint64_t dir_table_size = 0, file_table_size = 0;
    for (const auto d : order) {
        dirs[d].offset = static_cast<std::uint32_t>(dir_table_size);
        dir_table_size += DIR_ENTRY_SIZE + Align(dirs[d].name.size(), 4);
        for (const auto f : dirs[d].files) {
            files[f].offset = static_cast<std::uint32_t>(file_table_size);
            file_table_size += FILE_ENTRY_SIZE + Align(files[f].name.size(), 4);
        }
    }

    // file data in full-path order (byte compare, as fs.mitm's strcmp).
    std::vector<std::uint32_t> by_path(files.size());
    for (std::uint32_t f = 0; f < by_path.size(); f++) {
        by_path[f] = f;
    }
    std::sort(by_path.begin(), by_path.end(), [&](auto a, auto b) { return entries[files[a].input].path < entries[files[b].input].path; });
    std::uint64_t data_size = 0;
    for (const auto f : by_path) {
        data_size = Align(data_size, 0x10);
        files[f].data = data_size;
        data_size += entries[files[f].input].size;
    }

    const auto dir_hash_count = HashTableCount(static_cast<std::uint32_t>(dirs.size()));
    const auto file_hash_count = HashTableCount(static_cast<std::uint32_t>(files.size()));

    Layout out;
    out.tables_offset = Align(DATA_OFFSET + data_size, 4);
    const std::uint64_t dir_hash_at = 0;
    const std::uint64_t dir_table_at = dir_hash_at + 4ULL * dir_hash_count;
    const std::uint64_t file_hash_at = dir_table_at + dir_table_size;
    const std::uint64_t file_table_at = file_hash_at + 4ULL * file_hash_count;
    const std::uint64_t tables_size = file_table_at + file_table_size;
    out.total_size = out.tables_offset + tables_size;

    out.header.assign(DATA_OFFSET, 0);
    const std::uint64_t fields[] = {
        HEADER_SIZE,
        out.tables_offset + dir_hash_at, 4ULL * dir_hash_count,
        out.tables_offset + dir_table_at, dir_table_size,
        out.tables_offset + file_hash_at, 4ULL * file_hash_count,
        out.tables_offset + file_table_at, file_table_size,
        DATA_OFFSET,
    };
    for (std::size_t i = 0; i < std::size(fields); i++) {
        Put64(out.header, i * 8, fields[i]);
    }

    auto& t = out.tables;
    t.assign(tables_size, 0);
    std::fill(t.begin() + dir_hash_at, t.begin() + dir_table_at, 0xFF);
    std::fill(t.begin() + file_hash_at, t.begin() + file_table_at, 0xFF);

    for (const auto& d : dirs) {
        const auto at = dir_table_at + d.offset;
        const auto parent = dirs[d.parent].offset;
        Put32(t, at + 0x0, parent);
        Put32(t, at + 0x4, d.sibling == EMPTY ? EMPTY : dirs[d.sibling].offset);
        Put32(t, at + 0x8, d.dirs.empty() ? EMPTY : dirs[d.dirs.front()].offset);
        Put32(t, at + 0xC, d.files.empty() ? EMPTY : files[d.files.front()].offset);
        Chain(t, dir_hash_at, dir_hash_count, at, 0x10, parent, d.name, d.offset);
        Put32(t, at + 0x14, static_cast<std::uint32_t>(d.name.size()));
        std::copy(d.name.begin(), d.name.end(), t.begin() + at + DIR_ENTRY_SIZE);
    }

    out.data_offsets.assign(entries.size(), 0);
    for (const auto& f : files) {
        const auto at = file_table_at + f.offset;
        const auto parent = dirs[f.parent].offset;
        Put32(t, at + 0x0, parent);
        Put32(t, at + 0x4, f.sibling == EMPTY ? EMPTY : files[f.sibling].offset);
        Put64(t, at + 0x8, f.data);
        Put64(t, at + 0x10, entries[f.input].size);
        Chain(t, file_hash_at, file_hash_count, at, 0x18, parent, f.name, f.offset);
        Put32(t, at + 0x1C, static_cast<std::uint32_t>(f.name.size()));
        std::copy(f.name.begin(), f.name.end(), t.begin() + at + FILE_ENTRY_SIZE);
        out.data_offsets[f.input] = DATA_OFFSET + f.data;
    }

    return out;
}

} // namespace sphaira::romfs_pack
