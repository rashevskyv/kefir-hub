#pragma once

// Lists the files of a RomFS image from its directory and file tables (libnx-free).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sphaira::repack {

struct RomfsFile {
    std::string path;    // relative to the romfs root, '/' separated, no leading '/'.
    std::uint64_t offset; // from the start of the romfs image (file data offset + entry offset).
    std::uint64_t size;
};

// dirs / files: the tables as read at the header's dirTableOff / fileTableOff.
// data_offset: the header's fileDataOff. Returns false on a malformed or looping table.
bool WalkRomfs(std::span<const std::uint8_t> dirs, std::span<const std::uint8_t> files, std::uint64_t data_offset, std::vector<RomfsFile>& out);

} // namespace sphaira::repack
