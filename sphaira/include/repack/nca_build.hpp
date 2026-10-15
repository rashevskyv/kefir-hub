#pragma once

#include "repack/program_romfs.hpp"
#include <array>
#include <memory>

namespace sphaira::ui { struct ProgressBox; }

namespace sphaira::repack {

// A content NCA rebuilt for a repack. Every section is stored plaintext and the header keeps the
// original (now invalid) signature, so the console needs sigpatches to install it. Read() builds
// any range on the fly; nothing is written to the SD card.
struct BuiltNca {
    std::shared_ptr<yati::source::Base> data{};
    u64 size{};
    std::array<u8, 0x20> hash{};
    NcmContentId id{}; // first 16 bytes of the hash, as Nintendo names contents.
};

// `orig` (a program NCA) with its romfs section replaced by `romfs` (an image of romfs_size bytes).
// Reads the romfs twice: once for the IVFC hash levels, once for the NCA hash.
Result BuildProgramNca(ui::ProgressBox* pbox, const keys::Keys& keys, const NcaFile& orig, const std::shared_ptr<yati::source::Base>& romfs, u64 romfs_size, BuiltNca& out);

// `orig` (a meta NCA) with its program content entry pointed at `program`.
Result BuildMetaNca(const keys::Keys& keys, const NcaFile& orig, const BuiltNca& program, BuiltNca& out);

} // namespace sphaira::repack
