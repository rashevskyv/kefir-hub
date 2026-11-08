#pragma once

#include "fs.hpp"
#include "repack/program_romfs.hpp"
#include "repack/romfs_walk.hpp"
#include <functional>
#include <memory>

namespace sphaira::ui { struct ProgressBox; }

namespace sphaira::repack {

// Lists the files of a romfs image read through read(buf, offset, size).
using ReadFn = std::function<Result(void*, u64, u64)>;
Result ListImage(const ReadFn& read, std::vector<RomfsFile>& out);

// A RomFS image built on the fly: the game's files with the title's LayeredFS mod on top, in the
// order Atmosphère applies them: loose files in <contents>/romfs, then <contents>/romfs.bin, then
// the game. Nothing is written to the SD card; Read() assembles any range of the image.
struct ModCount {
    u32 replaced{}; // game files the mod replaces.
    u32 added{};    // files only the mod has.
};

// contents_dir: atmosphere/contents/<program id>. out_count may be null.
Result OpenModdedRomfs(ui::ProgressBox* pbox, const ProgramRomfs& game, const fs::FsPath& contents_dir, std::shared_ptr<yati::source::Base>& out, ModCount* out_count);

} // namespace sphaira::repack
