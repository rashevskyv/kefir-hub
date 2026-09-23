#pragma once

#include "threaded_file_transfer.hpp"
#include <functional>

namespace sphaira::thread {

using UnzipProgressCallback = std::function<void(s64)>;

Result CreateDirectoryChecked(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& dir_path);

Result TransferUnzipInternal(
    ui::ProgressBox* pbox,
    void* zfile,
    fs::Fs* fs,
    const fs::FsPath& path,
    s64 size,
    u32 crc32,
    Mode mode,
    UnzipProgressCallback progress,
    bool update_progress,
    bool checked_native_save = false,
    s64 checked_save_journal_size = 0
);

} // namespace sphaira::thread
