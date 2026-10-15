#pragma once

// Layout of a RomFS image as Atmosphere reads it from atmosphere/contents/<id>/romfs.bin.
// Pure logic (no libnx): the caller writes `header` at 0, copies each file to its
// data offset and writes `tables` at `tables_offset`.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sphaira::romfs_pack {

// fs.mitm reads file data at this fixed offset, whatever the header says.
inline constexpr std::uint64_t DATA_OFFSET = 0x200;

struct Entry {
    std::string path; // relative to the romfs root, '/' separated, no leading or trailing '/'
    std::uint64_t size{}; // files only
    bool is_dir{};
};

struct Layout {
    std::vector<std::uint8_t> header; // DATA_OFFSET bytes
    std::vector<std::uint8_t> tables;
    std::uint64_t tables_offset{};
    std::uint64_t total_size{};
    // absolute offset in the image for each input entry (0 for directories).
    std::vector<std::uint64_t> data_offsets;
};

// Entries come in any order; parent directories of files are implied.
// File data is ordered by full path, as fs.mitm sorts files: it rejects an image
// whose data offsets go down in that order.
auto BuildLayout(const std::vector<Entry>& entries) -> Layout;

auto PathHash(std::uint32_t parent, const char* name, std::size_t len) -> std::uint32_t;
auto HashTableCount(std::uint32_t entries) -> std::uint32_t;

} // namespace sphaira::romfs_pack
