#pragma once

#include "threaded_file_transfer.hpp"
#include <functional>

namespace sphaira::thread {

// used for file based emummc and zip/unzip.
constexpr u64 SMALL_BUFFER_SIZE = 1024 * 512;
// used for everything else.
constexpr u64 NORMAL_BUFFER_SIZE = 1024 * 1024 * 4;

using TransferProgressCallback = std::function<void(s64, s64)>;

Result TransferInternal(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, WriteCallback wfunc, StartCallback2 sfunc, Mode mode, u64 buffer_size = NORMAL_BUFFER_SIZE, TransferProgressCallback progress = nullptr);

} // namespace sphaira::thread
