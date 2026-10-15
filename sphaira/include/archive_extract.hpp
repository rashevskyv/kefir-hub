#pragma once

#include "fs.hpp"
#include "ui/progress_box.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sphaira::archive {

// Extracts a rar / 7z / tar(.gz/.xz/.bz2) archive, or a bare .gz/.xz/.bz2 file, from `fs`
// into `out_dir` on the same fs. Entries that would leave `out_dir` are skipped.
Result Extract(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& archive_path, const fs::FsPath& out_dir);

// full path on `fs` for an archive entry; nullopt = skip it.
using EntryMap = std::function<std::optional<std::string>(std::string_view entry)>;

// Extracts the files `map` places (folders are made for them); progress through the install
// interface, so an install session (DBI USB) can run it on its own thread.
Result ExtractMapped(ui::InstallProgress* progress, fs::Fs* fs, const fs::FsPath& archive_path, const EntryMap& map);

} // namespace sphaira::archive
