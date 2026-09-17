#pragma once

#include "ui/progress_box.hpp"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <switch.h>

namespace sphaira::thread {

enum class Mode {
    // default, always multi-thread.
    MultiThreaded,
    // always single-thread.
    SingleThreaded,
    // check buffer size, if smaller, single thread.
    SingleThreadedIfSmaller,
};

using ReadCallback = std::function<Result(void* data, s64 off, s64 size, u64* bytes_read)>;
using WriteCallback = std::function<Result(const void* data, s64 off, s64 size)>;

// used for pull api
using PullCallback = std::function<Result(void* data, s64 size, u64* bytes_read)>;
using StartThreadCallback = std::function<Result(void)>;

// called when threads are started.
// call pull() to receive data.
using StartCallback = std::function<Result(PullCallback pull)>;

// same as above, but the callee must call start() in order to start threads.
// this is for convenience as there may be race conditions otherwise, such as the read thread
// trying to read from the pull callback before it is set.
using StartCallback2 = std::function<Result(StartThreadCallback start, PullCallback pull)>;

// reads data from rfunc into wfunc.
Result Transfer(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, WriteCallback wfunc, Mode mode = Mode::MultiThreaded);

// reads data from rfunc, pull data from provided pull() callback.
Result TransferPull(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, StartCallback sfunc, Mode mode = Mode::MultiThreaded);
Result TransferPull(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, StartCallback2 sfunc, Mode mode = Mode::MultiThreaded);

// helper for extract zips.
// this will multi-thread unzip if size >= 512KiB, otherwise it'll single pass.
Result TransferUnzip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, s64 size, u32 crc32 = 0, Mode mode = Mode::SingleThreadedIfSmaller, bool update_progress = true);

// same as above but for zipping files.
Result TransferZip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, u32* crc32 = nullptr, Mode mode = Mode::SingleThreadedIfSmaller);

// passes the name inside the zip an final output path.
using UnzipAllFilter = std::function<bool(const fs::FsPath& name, fs::FsPath& path)>;

struct UnzipPayloadSummary {
    s64 file_bytes{0};
    s64 file_count{0};
    s64 directory_count{0};
};

struct UnzipPayloadInventory {
    std::map<std::string, s64> files;
    std::set<std::string> directories;
};

// helper all-in-one unzip function that unzips a zip (either open or path provided).
// the filter function can be used to modify the path and filter out unwanted files.
Result TransferUnzipAll(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, Mode mode = Mode::SingleThreadedIfSmaller, bool save_dbi_compat = false, bool checked_native_save = false);
Result TransferUnzipAll(ui::ProgressBox* pbox, const fs::FsPath& zip_out, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, Mode mode = Mode::SingleThreadedIfSmaller, bool save_dbi_compat = false, bool checked_native_save = false);

// preflights a zip archive before destination mutation:
// validates archive structure, entry names/destinations, decompresses and CRC-checks every entry,
// and rewinds the archive to the beginning.
Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false, UnzipPayloadSummary* output = nullptr, UnzipPayloadInventory* inventory_out = nullptr);
Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out, const fs::FsPath& base_path, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false, UnzipPayloadSummary* output = nullptr, UnzipPayloadInventory* inventory_out = nullptr);

// Verifies archive contents against native filesystem:
// validates exact inventory (files, sizes, implicit parents, empty directories, no leftovers),
// streams and compares file bytes with bounded buffers, checks CRC and iterator completion.
Result VerifyArchiveAgainstNative(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& base_path, const UnzipPayloadInventory& expected_inventory, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false);
Result VerifyArchiveAgainstNative(ui::ProgressBox* pbox, const fs::FsPath& zip_out, fs::Fs* fs, const fs::FsPath& base_path, const UnzipPayloadInventory& expected_inventory, UnzipAllFilter filter = nullptr, bool save_dbi_compat = false);

} // namespace sphaira::thread
