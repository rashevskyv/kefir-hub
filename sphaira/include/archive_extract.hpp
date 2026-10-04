#pragma once

#include "fs.hpp"
#include "ui/progress_box.hpp"

namespace sphaira::archive {

// Extracts a rar / 7z / tar(.gz/.xz/.bz2) archive, or a bare .gz/.xz/.bz2 file, from `fs`
// into `out_dir` on the same fs. Entries that would leave `out_dir` are skipped.
Result Extract(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& archive_path, const fs::FsPath& out_dir);

} // namespace sphaira::archive
