#include "threaded_file_transfer.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "app.hpp"
#include "minizip_helper.hpp"
#include "path_util.hpp"
#include "ui/menus/filebrowser.hpp"

#include <vector>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <limits>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::thread {
namespace {

// used for file based emummc and zip/unzip.
constexpr u64 SMALL_BUFFER_SIZE = 1024 * 512;
// used for everything else.
constexpr u64 NORMAL_BUFFER_SIZE = 1024*1024*4;

using TransferProgressCallback = std::function<void(s64, s64)>;
using UnzipProgressCallback = std::function<void(s64)>;

void UpdateTransferProgress(ui::ProgressBox* pbox, const TransferProgressCallback& progress, s64 offset, s64 size) {
    if (progress) {
        progress(offset, size);
    } else {
        pbox->UpdateTransfer(offset, size);
    }
}

struct ThreadBuffer {
    ThreadBuffer() {
        buf.reserve(NORMAL_BUFFER_SIZE);
    }

    std::vector<u8> buf;
    s64 off;
};

template<std::size_t Size>
struct RingBuf {
private:
    ThreadBuffer buf[Size]{};
    unsigned r_index{};
    unsigned w_index{};

    static_assert((sizeof(RingBuf::buf) & (sizeof(RingBuf::buf) - 1)) == 0, "Must be power of 2!");

public:
    void ringbuf_reset() {
        this->r_index = this->w_index;
    }

    unsigned ringbuf_capacity() const {
        return sizeof(this->buf) / sizeof(this->buf[0]);
    }

    unsigned ringbuf_size() const {
        return (this->w_index - this->r_index) % (ringbuf_capacity() * 2U);
    }

    unsigned ringbuf_free() const {
        return ringbuf_capacity() - ringbuf_size();
    }

    void ringbuf_push(std::vector<u8>& buf_in, s64 off_in) {
        auto& value = this->buf[this->w_index % ringbuf_capacity()];
        value.off = off_in;
        std::swap(value.buf, buf_in);

        this->w_index = (this->w_index + 1U) % (ringbuf_capacity() * 2U);
    }

    void ringbuf_pop(std::vector<u8>& buf_out, s64& off_out) {
        auto& value = this->buf[this->r_index % ringbuf_capacity()];
        off_out = value.off;
        std::swap(value.buf, buf_out);

        this->r_index = (this->r_index + 1U) % (ringbuf_capacity() * 2U);
    }
};

struct ThreadData {
    ThreadData(ui::ProgressBox* _pbox, s64 size, ReadCallback _rfunc, WriteCallback _wfunc, u64 buffer_size);

    auto GetResults() volatile -> Result;
    void WakeAllThreads();

    auto IsAnyRunning() volatile const -> bool {
        return read_running || write_running;
    }

    auto GetWriteOffset() volatile const -> s64 {
        return write_offset;
    }

    auto GetWriteSize() const {
        return write_size;
    }

    auto GetDoneEvent() {
        return &m_uevent_done;
    }

    auto GetProgressEvent() {
        return &m_uevent_progres;
    }

    void SetReadResult(Result result) {
        read_result = result;
        if (R_FAILED(result)) {
            ueventSignal(GetDoneEvent());
        }
    }

    void SetWriteResult(Result result) {
        write_result = result;
        ueventSignal(GetDoneEvent());
    }

    void SetPullResult(Result result) {
        pull_result = result;
        if (R_FAILED(result)) {
            ueventSignal(GetDoneEvent());
        }
    }

    Result Pull(void* data, s64 size, u64* bytes_read);
    Result readFuncInternal();
    Result writeFuncInternal();

private:
    Result SetWriteBuf(std::vector<u8>& buf, s64 size);
    Result GetWriteBuf(std::vector<u8>& buf_out, s64& off_out);
    Result SetPullBuf(std::vector<u8>& buf, s64 size);
    Result GetPullBuf(void* data, s64 size, u64* bytes_read);

    Result Read(void* buf, s64 size, u64* bytes_read);

private:
    // these need to be copied
    ui::ProgressBox* const pbox;
    const ReadCallback rfunc;
    const WriteCallback wfunc;

    // these need to be created
    Mutex mutex{};
    Mutex pull_mutex{};

    CondVar can_read{};
    CondVar can_write{};
    CondVar can_pull{};
    CondVar can_pull_write{};

    UEvent m_uevent_done{};
    UEvent m_uevent_progres{};

    RingBuf<2> write_buffers{};
    std::vector<u8> pull_buffer{};
    s64 pull_buffer_offset{};

    const u64 read_buffer_size;
    const s64 write_size;

    // these are shared between threads
    std::atomic<s64> read_offset{};
    std::atomic<s64> write_offset{};

    std::atomic<Result> read_result{};
    std::atomic<Result> write_result{};
    std::atomic<Result> pull_result{};

    std::atomic_bool read_running{true};
    std::atomic_bool write_running{true};
};

ThreadData::ThreadData(ui::ProgressBox* _pbox, s64 size, ReadCallback _rfunc, WriteCallback _wfunc, u64 buffer_size)
: pbox{_pbox}
, rfunc{_rfunc}
, wfunc{_wfunc}
, read_buffer_size{buffer_size}
, write_size{size} {
    mutexInit(std::addressof(mutex));
    mutexInit(std::addressof(pull_mutex));

    condvarInit(std::addressof(can_read));
    condvarInit(std::addressof(can_write));
    condvarInit(std::addressof(can_pull));
    condvarInit(std::addressof(can_pull_write));

    ueventCreate(&m_uevent_done, false);
    ueventCreate(&m_uevent_progres, true);
}

auto ThreadData::GetResults() volatile -> Result {
    R_TRY(pbox->ShouldExitResult());
    R_TRY(read_result.load());
    R_TRY(write_result.load());
    R_TRY(pull_result.load());
    R_SUCCEED();
}

void ThreadData::WakeAllThreads() {
    condvarWakeAll(std::addressof(can_read));
    condvarWakeAll(std::addressof(can_write));
    condvarWakeAll(std::addressof(can_pull));
    condvarWakeAll(std::addressof(can_pull_write));

    mutexUnlock(std::addressof(mutex));
    mutexUnlock(std::addressof(pull_mutex));
}

Result ThreadData::SetWriteBuf(std::vector<u8>& buf, s64 size) {
    buf.resize(size);

    mutexLock(std::addressof(mutex));
    if (!write_buffers.ringbuf_free()) {
        if (!write_running) {
            R_SUCCEED();
        }
        R_TRY(condvarWait(std::addressof(can_read), std::addressof(mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(mutex)));
    R_TRY(GetResults());
    write_buffers.ringbuf_push(buf, 0);
    return condvarWakeOne(std::addressof(can_write));
}

Result ThreadData::GetWriteBuf(std::vector<u8>& buf_out, s64& off_out) {
    mutexLock(std::addressof(mutex));
    if (!write_buffers.ringbuf_size()) {
        if (!read_running) {
            buf_out.resize(0);
            R_SUCCEED();
        }
        R_TRY(condvarWait(std::addressof(can_write), std::addressof(mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(mutex)));
    R_TRY(GetResults());
    write_buffers.ringbuf_pop(buf_out, off_out);
    return condvarWakeOne(std::addressof(can_read));
}

Result ThreadData::SetPullBuf(std::vector<u8>& buf, s64 size) {
    buf.resize(size);

    mutexLock(std::addressof(pull_mutex));
    if (!pull_buffer.empty()) {
        R_TRY(condvarWait(std::addressof(can_pull_write), std::addressof(pull_mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(pull_mutex)));
    R_TRY(GetResults());

    pull_buffer.swap(buf);
    return condvarWakeOne(std::addressof(can_pull));
}

Result ThreadData::GetPullBuf(void* data, s64 size, u64* bytes_read) {
    mutexLock(std::addressof(pull_mutex));
    if (pull_buffer.empty()) {
        R_TRY(condvarWait(std::addressof(can_pull), std::addressof(pull_mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(pull_mutex)));
    R_TRY(GetResults());

    *bytes_read = size = std::min<s64>(size, pull_buffer.size() - pull_buffer_offset);
    std::memcpy(data, pull_buffer.data() + pull_buffer_offset, size);
    pull_buffer_offset += size;

    if (pull_buffer_offset == pull_buffer.size()) {
        pull_buffer_offset = 0;
        pull_buffer.clear();
        return condvarWakeOne(std::addressof(can_pull_write));
    } else {
        R_SUCCEED();
    }
}

Result ThreadData::Read(void* buf, s64 size, u64* bytes_read) {
    size = std::min<s64>(size, write_size - read_offset);
    const auto rc = rfunc(buf, read_offset, size, bytes_read);
    read_offset += *bytes_read;
    return rc;
}

Result ThreadData::Pull(void* data, s64 size, u64* bytes_read) {
    return GetPullBuf(data, size, bytes_read);
}

// read thread reads all data from the source
Result ThreadData::readFuncInternal() {
    ON_SCOPE_EXIT( read_running = false; );

    // the main buffer which data is read into.
    std::vector<u8> buf;
    buf.reserve(this->read_buffer_size);

    while (this->read_offset < this->write_size && R_SUCCEEDED(this->GetResults())) {
        // read more data
        s64 read_size = this->read_buffer_size;

        u64 bytes_read{};
        buf.resize(read_size);
        R_TRY(this->Read(buf.data(), read_size, std::addressof(bytes_read)));
        if (!bytes_read) {
            break;
        }

        auto buf_size = bytes_read;
        R_TRY(this->SetWriteBuf(buf, buf_size));
    }

    log_write("finished read thread success!\n");
    R_SUCCEED();
}

// write thread writes data to wfunc.
Result ThreadData::writeFuncInternal() {
    ON_SCOPE_EXIT( write_running = false; );

    std::vector<u8> buf;
    buf.reserve(this->read_buffer_size);

    while (this->write_offset < this->write_size && R_SUCCEEDED(this->GetResults())) {
        s64 dummy_off;
        R_TRY(this->GetWriteBuf(buf, dummy_off));
        const auto size = buf.size();
        if (!size) {
            log_write("exiting write func early because no data was received\n");
            break;
        }

        if (!this->wfunc) {
            R_TRY(this->SetPullBuf(buf, buf.size()));
        } else {
            R_TRY(this->wfunc(buf.data(), this->write_offset, buf.size()));
        }

        this->write_offset += size;
        ueventSignal(GetProgressEvent());
    }

    log_write("finished write thread success!\n");
    R_SUCCEED();
}

void readFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetReadResult(t->readFuncInternal());
    log_write("read thread returned now\n");
}

void writeFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetWriteResult(t->writeFuncInternal());
    log_write("write thread returned now\n");
}

auto GetAlternateCore(int id) {
    return id == 1 ? 2 : 1;
}

Result TransferInternal(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, WriteCallback wfunc, StartCallback2 sfunc, Mode mode, u64 buffer_size = NORMAL_BUFFER_SIZE, TransferProgressCallback progress = nullptr) {
    const auto is_file_based_emummc = App::IsFileBaseEmummc();

    if (is_file_based_emummc) {
        buffer_size = SMALL_BUFFER_SIZE;
    }

    if (mode == Mode::SingleThreadedIfSmaller) {
        if (size <= buffer_size) {
            mode = Mode::SingleThreaded;
        } else {
            mode = Mode::MultiThreaded;
        }
    }

    // single threaded pull buffer is not supported.
    log_write("checking invalid transfer mode: %u %u\n", mode == Mode::MultiThreaded, !sfunc);
    R_UNLESS(mode == Mode::MultiThreaded || !sfunc, 0x1);
    log_write("valid transfer mode\n");

    // todo: support single threaded pull buffer.
    if (mode == Mode::SingleThreaded) {
        std::vector<u8> buf(buffer_size);

        s64 offset{};
        while (offset < size) {
            R_TRY(pbox->ShouldExitResult());

            u64 bytes_read;
            const auto rsize = std::min<s64>(buf.size(), size - offset);
            R_TRY(rfunc(buf.data(), offset, rsize, &bytes_read));
            if (!bytes_read) {
                break;
            }

            R_TRY(wfunc(buf.data(), offset, bytes_read));

            offset += bytes_read;
            UpdateTransferProgress(pbox, progress, offset, size);
        }

        R_SUCCEED();
    }
    else {
        const auto WRITE_THREAD_CORE = sfunc ? pbox->GetCpuId() : GetAlternateCore(pbox->GetCpuId());
        const auto READ_THREAD_CORE = GetAlternateCore(WRITE_THREAD_CORE);

        ThreadData t_data{pbox, size, rfunc, wfunc, buffer_size};

        Thread t_read{};
        R_TRY(threadCreate(&t_read, readFunc, std::addressof(t_data), nullptr, 1024*256, 0x3B, READ_THREAD_CORE));
        ON_SCOPE_EXIT(threadClose(&t_read));

        Thread t_write{};
        R_TRY(threadCreate(&t_write, writeFunc, std::addressof(t_data), nullptr, 1024*256, 0x3B, WRITE_THREAD_CORE));
        ON_SCOPE_EXIT(threadClose(&t_write));

        const auto start_threads = [&]() -> Result {
            log_write("starting threads\n");
            R_TRY(threadStart(std::addressof(t_read)));
            R_TRY(threadStart(std::addressof(t_write)));
            R_SUCCEED();
        };

        ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_read)));
        ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_write)));

        if (sfunc) {
            log_write("[THREAD] doing sfuncn\n");
            t_data.SetPullResult(sfunc(start_threads, [&](void* data, s64 size, u64* bytes_read) -> Result {
                R_TRY(t_data.GetResults());
                return t_data.Pull(data, size, bytes_read);
            }));
        } else {
            log_write("[THREAD] doing normal\n");
            R_TRY(start_threads());
            log_write("[THREAD] started threads\n");

            const auto waiter_progress = waiterForUEvent(t_data.GetProgressEvent());
            const auto waiter_cancel = waiterForUEvent(pbox->GetCancelEvent());
            const auto waiter_done = waiterForUEvent(t_data.GetDoneEvent());

            for (;;) {
                s32 idx;
                if (R_FAILED(waitMulti(&idx, UINT64_MAX, waiter_progress, waiter_cancel, waiter_done))) {
                    break;
                }

                if (!idx) {
                    UpdateTransferProgress(pbox, progress, t_data.GetWriteOffset(), t_data.GetWriteSize());
                } else {
                    break;
                }
            }
        }

        // wait for all threads to close.
        log_write("waiting for threads to close\n");
        while (t_data.IsAnyRunning()) {
            t_data.WakeAllThreads();
            pbox->Yield();

            if (R_FAILED(waitSingleHandle(t_read.handle, 1000))) {
                continue;
            } else if (R_FAILED(waitSingleHandle(t_write.handle, 1000))) {
                continue;
            }
            break;
        }
        log_write("threads closed\n");

        // if any of the threads failed, wake up all threads so they can exit.
        if (R_FAILED(t_data.GetResults())) {
            log_write("some reads failed, waking threads\n");
            log_write("returning due to fail\n");
            return t_data.GetResults();
        }

        log_write("returning from thread func\n");
        return t_data.GetResults();
    }
}

// the HOS filesystem rejects certain characters in a path component with
// FsError_InvalidCharacter. some zip packs (e.g. cheat packs organised by
// human-readable game title) carry entries whose names contain them, which
// would otherwise abort the entire extraction on the first bad entry. replace
// the offending characters with '_' per component, leaving '/' separators
// intact. entries whose names are pure hex (atmosphere cheat paths) are
// unaffected.
bool IsInvalidPathChar(char c) {
    const auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20) {
        return true; // control characters
    }
    switch (c) {
        case ':': case '*': case '?': case '"':
        case '<': case '>': case '|': case '\\':
            return true;
        default:
            return false;
    }
}

fs::FsPath SanitizeZipEntryName(const fs::FsPath& name) {
    fs::FsPath out = name;
    bool changed = false;
    for (u32 i = 0; out.s[i] != '\0'; i++) {
        if (IsInvalidPathChar(out.s[i])) {
            out.s[i] = '_';
            changed = true;
        }
    }
    if (changed) {
        log_write("[Unzip] sanitized invalid entry name '%s' -> '%s'\n", name.s, out.s);
    }
    return out;
}

Result ResolveArchiveEntryName(const unz_file_info64& info, const char* name_buf, bool save_dbi_compat, fs::FsPath& out_name) {
    if (info.size_filename == 0) {
        log_write("archive entry has empty name\n");
        R_THROW(FsError_InvalidCharacter);
    }

    if (info.size_filename >= sizeof(out_name.s)) {
        log_write("archive entry name too long (%lu bytes)\n", static_cast<unsigned long>(info.size_filename));
        R_THROW(FsError_TooLongPath);
    }

    if (std::strlen(name_buf) != info.size_filename) {
        log_write("archive entry name length mismatch (%zu != %lu)\n", std::strlen(name_buf), static_cast<unsigned long>(info.size_filename));
        R_THROW(FsError_TooLongPath);
    }

    const std::string_view raw{name_buf, info.size_filename};
    if (save_dbi_compat) {
        const auto norm = path::NormalizeSaveArchiveEntry(raw);
        if (!norm.has_value()) {
            log_write("unsafe save archive entry: %s\n", name_buf);
            R_THROW(FsError_InvalidCharacter);
        }
        if (norm->size() >= sizeof(out_name.s)) {
            log_write("normalized save archive entry too long (%zu bytes)\n", norm->size());
            R_THROW(FsError_TooLongPath);
        }
        std::memcpy(out_name.s, norm->data(), norm->size());
        out_name.s[norm->size()] = '\0';
    } else {
        if (!path::IsSafeArchiveEntry(raw)) {
            log_write("unsafe archive entry path: %s\n", name_buf);
            R_THROW(FsError_InvalidCharacter);
        }
        std::memcpy(out_name.s, name_buf, info.size_filename);
        out_name.s[info.size_filename] = '\0';
    }

    R_SUCCEED();
}

struct ResolvedDestinationEntry {
    fs::FsPath path;
    bool is_directory{false};
    bool keep{false};
};

Result ResolveArchiveDestinationEntry(
    const unz_file_info64& info,
    const char* name_buf,
    const fs::FsPath& base_path,
    UnzipAllFilter filter,
    bool save_dbi_compat,
    ResolvedDestinationEntry& out) {

    fs::FsPath name;
    R_TRY(ResolveArchiveEntryName(info, name_buf, save_dbi_compat, name));
    name = SanitizeZipEntryName(name);

    out.path = fs::AppendPath(base_path, name);
    out.keep = filter ? filter(name, out.path) : true;
    if (out.keep) {
        const auto path_len = out.path.length();
        if (path_len == 0) {
            log_write("empty destination path\n");
            R_THROW(FsError_InvalidCharacter);
        }

        if (!path::IsSafeExtractionDestination(out.path, base_path, save_dbi_compat)) {
            log_write("unsafe destination path: %s\n", out.path.s);
            R_THROW(FsError_InvalidCharacter);
        }

        out.is_directory = (out.path[path_len - 1] == '/');
    } else {
        out.is_directory = false;
    }

    R_SUCCEED();
}

std::vector<std::string> GetParentDirectories(const std::string& path) {
    std::vector<std::string> parents;
    size_t last_slash = path.find_last_of('/');
    while (last_slash != std::string::npos && last_slash > 0) {
        std::string parent = path.substr(0, last_slash);
        parents.push_back(parent);
        last_slash = parent.find_last_of('/');
    }
    return parents;
}

} // namespace

Result Transfer(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, WriteCallback wfunc, Mode mode) {
    return TransferInternal(pbox, size, rfunc, wfunc, nullptr, mode);
}

Result TransferPull(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, StartCallback sfunc, Mode mode) {
    return TransferInternal(pbox, size, rfunc, nullptr, [sfunc](StartThreadCallback start, PullCallback pull) -> Result {
        R_TRY(start());
        return sfunc(pull);
    }, mode);
}

Result TransferPull(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, StartCallback2 sfunc, Mode mode) {
    return TransferInternal(pbox, size, rfunc, nullptr, sfunc, mode);
}

static Result CreateDirectoryChecked(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& dir_path) {
    if (!fs || !fs->IsNative()) {
        return FsError_NotImplemented;
    }
    auto* native_fs = static_cast<fs::FsNative*>(fs);

    std::string_view path_view{dir_path.s};
    while (path_view.length() > 1 && path_view.back() == '/') {
        path_view.remove_suffix(1);
    }
    if (path_view.empty() || path_view == "/") {
        return 0;
    }

    if (path_view.front() == '/') {
        path_view.remove_prefix(1);
    }

    fs::FsPath current_path{"/"};
    std::string_view sv{path_view};
    while (!sv.empty()) {
        const auto slash_pos = sv.find('/');
        const auto part = (slash_pos == std::string_view::npos) ? sv : sv.substr(0, slash_pos);
        if (slash_pos == std::string_view::npos) {
            sv = {};
        } else {
            sv.remove_prefix(slash_pos + 1);
        }

        if (part.empty()) {
            continue;
        }

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        const auto cur_len = current_path.length();
        const bool need_slash = (cur_len > 0 && current_path[cur_len - 1] != '/');
        const auto part_len = part.size();
        if (cur_len + (need_slash ? 1 : 0) + part_len + 1 > sizeof(fs::FsPath)) {
            return FsError_TooLongPath;
        }
        if (need_slash) {
            current_path += '/';
        }
        current_path += part;

        const auto create_rc = fsFsCreateDirectory(&native_fs->m_fs, current_path.s);
        if (R_SUCCEEDED(create_rc)) {
            R_TRY(fs->Commit());
        } else if (create_rc == FsError_PathAlreadyExists) {
            FsDirEntryType type{};
            R_TRY(fs->GetEntryType(current_path, &type));
            R_UNLESS(type == FsDirEntryType_Dir, FsError_PathAlreadyExists);
        } else {
            return create_rc;
        }
    }

    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    return 0;
}

static Result TransferUnzipInternal(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, s64 size, u32 crc32, Mode mode, UnzipProgressCallback progress, bool update_progress, bool checked_native_save = false, s64 checked_save_journal_size = 0) {
    if (checked_native_save) {
        if (!fs || !fs->IsNative()) {
            return FsError_NotImplemented;
        }
        if (size < 0) {
            return FsError_InvalidSize;
        }
        if (checked_save_journal_size < 0) {
            return FsError_InvalidSize;
        }

        // ponytail: declared-size payload cap/per-read commits do not measure metadata/allocation/block overhead or actual free journal; even one operation may exhaust journal; proven budgeting remains queued.
        s64 request_cap = static_cast<s64>(SMALL_BUFFER_SIZE);
        if (checked_save_journal_size > 0 && checked_save_journal_size < request_cap) {
            request_cap = checked_save_journal_size;
        }

        // Implicit parent directories component by component
        const char* last_slash = std::strrchr(path.s, '/');
        if (last_slash && last_slash > path.s) {
            fs::FsPath parent_dir{};
            std::snprintf(parent_dir, sizeof(parent_dir), "%.*s", static_cast<int>(last_slash - path.s), path.s);
            R_TRY(CreateDirectoryChecked(pbox, fs, parent_dir));
        }

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        auto* native_fs = static_cast<fs::FsNative*>(fs);

        // New checked file: native fsFsCreateFile at validated full s64 size, option 0,
        // then separately CHECK native filesystem Commit before opening payload handle.
        // Unexpected existing destination FILE is an error (do not accept PathAlreadyExists).
        const auto create_rc = fsFsCreateFile(&native_fs->m_fs, path.s, size, 0);
        R_TRY(create_rc);

        const auto commit_create_rc = fs->Commit();
        R_TRY(commit_create_rc);

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        fs::File f;
        auto close_and_invalidate = [&]() {
            if (serviceIsActive(&f.m_native.s)) {
                fsFileClose(&f.m_native);
            }
            f.m_native = {};
            f.m_fs = nullptr;
        };
        ON_SCOPE_EXIT {
            close_and_invalidate();
        };

        if (size == 0) {
            const auto open_rc = fs->OpenFile(path, FsOpenMode_Write, &f);
            if (R_FAILED(open_rc)) {
                return open_rc;
            }

            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    close_and_invalidate();
                    return exit_rc;
                }
            }

            const auto flush_rc = fsFileFlush(&f.m_native);
            close_and_invalidate();
            R_TRY(flush_rc);

            R_TRY(fs->Commit());

            if (pbox) {
                R_TRY(pbox->ShouldExitResult());
            }

            R_UNLESS(!crc32 || crc32 == 0, 0x8);
            return 0;
        }

        const auto open_rc = fs->OpenFile(path, FsOpenMode_Write, &f);
        if (R_FAILED(open_rc)) {
            return open_rc;
        }

        std::vector<u8> buffer(request_cap);
        s64 remaining = size;
        s64 current_offset = 0;
        u32 crc32_out = 0;

        while (remaining > 0) {
            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    close_and_invalidate();
                    return exit_rc;
                }
            }

            const s64 to_read_s64 = std::min(request_cap, remaining);
            const int to_read = static_cast<int>(to_read_s64);

            const int read_res = unzReadCurrentFile(zfile, buffer.data(), to_read);
            if (read_res <= 0) {
                close_and_invalidate();
                log_write("failed to read zip file: %s %d\n", path.s, read_res);
                return Result_UnzReadCurrentFile;
            }
            if (read_res > to_read || static_cast<s64>(read_res) > remaining) {
                close_and_invalidate();
                return FsError_InvalidSize;
            }

            if (crc32) {
                crc32_out = crc32CalculateWithSeed(crc32_out, buffer.data(), read_res);
            }

            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    close_and_invalidate();
                    return exit_rc;
                }
            }

            const auto write_rc = fsFileWrite(&f.m_native, current_offset, buffer.data(), read_res, FsWriteOption_None);
            if (R_FAILED(write_rc)) {
                close_and_invalidate();
                return write_rc;
            }

            const auto flush_rc = fsFileFlush(&f.m_native);
            close_and_invalidate();
            R_TRY(flush_rc);

            const auto commit_rc = fs->Commit();
            R_TRY(commit_rc);

            current_offset += read_res;
            remaining -= read_res;
            if (progress) {
                progress(read_res);
            }

            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    return exit_rc;
                }
            }

            if (remaining > 0) {
                const auto reopen_rc = fs->OpenFile(path, FsOpenMode_Write, &f);
                if (R_FAILED(reopen_rc)) {
                    return reopen_rc;
                }
            }
        }

        if (current_offset != size) {
            return FsError_InvalidSize;
        }

        R_UNLESS(!crc32 || crc32 == crc32_out, 0x8);

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        return 0;
    }

    Result rc;
    if (R_FAILED(rc = fs->CreateDirectoryRecursivelyWithPath(path)) && rc != FsError_PathAlreadyExists) {
        log_write("failed to create folder: %s 0x%04X\n", path.s, rc);
        R_THROW(rc);
    }

    if (R_FAILED(rc = fs->CreateFile(path, size, 0)) && rc != FsError_PathAlreadyExists) {
        log_write("failed to create file: %s 0x%04X\n", path.s, rc);
        R_THROW(rc);
    }

    fs::File f;
    R_TRY(fs->OpenFile(path, FsOpenMode_Write, &f));

    // only update the size if this is an existing file.
    if (rc == FsError_PathAlreadyExists) {
        R_TRY(f.SetSize(size));
    }

    // NOTES: do not use temp file with rename / delete after as it massively slows
    // down small file transfers (RA 21s -> 50s).
    u32 crc32_out{};
    const auto transfer_rc = thread::TransferInternal(pbox, size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto result = unzReadCurrentFile(zfile, data, size);
            if (result <= 0) {
                log_write("failed to read zip file: %s %d\n", path.s, result);
                R_THROW(Result_UnzReadCurrentFile);
            }

            if (crc32) {
                crc32_out = crc32CalculateWithSeed(crc32_out, data, result);
            }

            *bytes_read = result;
            R_SUCCEED();
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            R_TRY(f.Write(off, data, size, FsWriteOption_None));
            if (progress) {
                progress(size);
            }
            R_SUCCEED();
        },
        nullptr, mode, SMALL_BUFFER_SIZE, update_progress ? TransferProgressCallback{} : TransferProgressCallback{[](s64, s64){}}
    );

    R_TRY(transfer_rc);
    // validate crc32 (if set in the info).
    R_UNLESS(!crc32 || crc32 == crc32_out, 0x8);
    R_SUCCEED();
}

Result TransferUnzip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, s64 size, u32 crc32, Mode mode, bool update_progress) {
    return TransferUnzipInternal(pbox, zfile, fs, path, size, crc32, mode, nullptr, update_progress);
}

Result TransferZip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, u32* crc32, Mode mode) {
    fs::File f;
    R_TRY(fs->OpenFile(path, FsOpenMode_Read, &f));

    s64 file_size;
    R_TRY(f.GetSize(&file_size));

    if (crc32) {
        *crc32 = 0;
    }

    return thread::TransferInternal(pbox, file_size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto rc = f.Read(off, data, size, FsReadOption_None, bytes_read);
            if (R_SUCCEEDED(rc) && crc32) {
                *crc32 = crc32CalculateWithSeed(*crc32, data, *bytes_read);
            }
            return rc;
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            if (ZIP_OK != zipWriteInFileInZip(zfile, data, size)) {
                log_write("failed to write zip file: %s\n", path.s);
                R_THROW(Result_ZipWriteInFileInZip);
            }
            R_SUCCEED();
        },
        nullptr, mode, SMALL_BUFFER_SIZE
    );
}

Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile, const fs::FsPath& base_path, UnzipAllFilter filter, bool save_dbi_compat, UnzipPayloadSummary* output, UnzipPayloadInventory* inventory_out, bool allow_empty) {
    unz_global_info64 ginfo;
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        R_THROW(Result_UnzGetGlobalInfo64);
    }

    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        R_THROW(FsError_InvalidSize);
    }

    if (ginfo.number_entry == 0) {
        if (!allow_empty) {
            R_THROW(FsError_InvalidSize);
        }
        if (output) {
            *output = UnzipPayloadSummary{};
        }
        if (inventory_out) {
            *inventory_out = UnzipPayloadInventory{};
        }
        R_SUCCEED();
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    UnzipPayloadSummary local_summary{};
    UnzipPayloadInventory local_inventory{};
    std::set<std::string> explicit_dirs;
    std::vector<u8> drain_buf(64 * 1024);

    for (s64 i = 0; i < entry_count; i++) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile during preflight\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            log_write("failed to get current info during preflight\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        if (info.uncompressed_size > static_cast<u64>(std::numeric_limits<s64>::max())) {
            log_write("archive uncompressed size exceeds s64 maximum\n");
            R_THROW(FsError_InvalidSize);
        }

        ResolvedDestinationEntry resolved{};
        R_TRY(ResolveArchiveDestinationEntry(info, name_buf, base_path, filter, save_dbi_compat, resolved));

        if (resolved.keep) {
            if (resolved.is_directory) {
                if (local_summary.directory_count == std::numeric_limits<s64>::max()) {
                    log_write("archive directory count exceeds s64 maximum\n");
                    R_THROW(FsError_InvalidSize);
                }
                local_summary.directory_count++;
            } else {
                if (local_summary.file_count == std::numeric_limits<s64>::max()) {
                    log_write("archive file count exceeds s64 maximum\n");
                    R_THROW(FsError_InvalidSize);
                }
                if (static_cast<u64>(std::numeric_limits<s64>::max()) - static_cast<u64>(local_summary.file_bytes) < info.uncompressed_size) {
                    log_write("archive file_bytes aggregate overflow exceeds s64 maximum\n");
                    R_THROW(FsError_InvalidSize);
                }
                local_summary.file_count++;
                local_summary.file_bytes += static_cast<s64>(info.uncompressed_size);
            }

            if (inventory_out) {
                if (resolved.is_directory) {
                    std::string canonical_dir = resolved.path.s;
                    while (canonical_dir.size() > 1 && canonical_dir.back() == '/') {
                        canonical_dir.pop_back();
                    }
                    if (canonical_dir.empty() || canonical_dir == "/") {
                        R_THROW(FsError_InvalidCharacter);
                    }
                    if (!explicit_dirs.insert(canonical_dir).second) {
                        log_write("duplicate explicit directory: %s\n", canonical_dir.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    if (local_inventory.files.contains(canonical_dir)) {
                        log_write("dir conflicts with existing file: %s\n", canonical_dir.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    for (const auto& parent : GetParentDirectories(canonical_dir)) {
                        if (local_inventory.files.contains(parent)) {
                            log_write("parent of dir conflicts with existing file: %s\n", parent.c_str());
                            R_THROW(FsError_PathAlreadyExists);
                        }
                        local_inventory.directories.insert(parent);
                    }
                    local_inventory.directories.insert(canonical_dir);
                } else {
                    std::string file_key = resolved.path.s;
                    if (local_inventory.files.contains(file_key)) {
                        log_write("duplicate kept file: %s\n", file_key.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    if (local_inventory.directories.contains(file_key)) {
                        log_write("file conflicts with directory or parent: %s\n", file_key.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    for (const auto& parent : GetParentDirectories(file_key)) {
                        if (local_inventory.files.contains(parent)) {
                            log_write("parent of file conflicts with existing file: %s\n", parent.c_str());
                            R_THROW(FsError_PathAlreadyExists);
                        }
                        local_inventory.directories.insert(parent);
                    }
                    local_inventory.files.emplace(std::move(file_key), static_cast<s64>(info.uncompressed_size));
                }
            }
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            log_write("failed to open current file during preflight: %s\n", name_buf);
            R_THROW(Result_UnzOpenCurrentFile);
        }

        u32 crc32_out = 0;
        u64 bytes_drained = 0;
        int read_res = 0;
        do {
            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    unzCloseCurrentFile(zfile);
                    R_THROW(exit_rc);
                }
            }

            read_res = unzReadCurrentFile(zfile, drain_buf.data(), drain_buf.size());
            if (read_res < 0) {
                log_write("failed to read zip file during preflight: %s %d\n", name_buf, read_res);
                unzCloseCurrentFile(zfile);
                R_THROW(Result_UnzReadCurrentFile);
            }
            if (read_res > 0) {
                const auto read_u64 = static_cast<u64>(read_res);
                if (std::numeric_limits<u64>::max() - bytes_drained < read_u64 || bytes_drained + read_u64 > info.uncompressed_size) {
                    log_write("archive entry exceeded declared uncompressed size during read: %s\n", name_buf);
                    unzCloseCurrentFile(zfile);
                    R_THROW(FsError_InvalidSize);
                }
                if (info.crc) {
                    crc32_out = crc32CalculateWithSeed(crc32_out, drain_buf.data(), read_res);
                }
                bytes_drained += read_u64;
            }
        } while (read_res > 0);

        const int close_res = unzCloseCurrentFile(zfile);
        if (close_res == UNZ_CRCERROR) {
            log_write("crc error on unzCloseCurrentFile during preflight: %s\n", name_buf);
            R_THROW(0x8);
        } else if (close_res != UNZ_OK) {
            log_write("failed to close zip file during preflight: %s %d\n", name_buf, close_res);
            R_THROW(Result_UnzReadCurrentFile);
        }

        if (bytes_drained != info.uncompressed_size) {
            log_write("archive entry drained size mismatch (%llu != %llu)\n", static_cast<unsigned long long>(bytes_drained), static_cast<unsigned long long>(info.uncompressed_size));
            R_THROW(FsError_InvalidSize);
        }

        if (info.crc && crc32_out != info.crc) {
            log_write("archive entry crc mismatch (%08x != %08x)\n", crc32_out, static_cast<unsigned int>(info.crc));
            R_THROW(0x8);
        }
    }

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        log_write("failed to unzGoToFirstFile after preflight\n");
        R_THROW(Result_UnzGoToFirstFile);
    }

    if (output) {
        *output = local_summary;
    }
    if (inventory_out) {
        *inventory_out = std::move(local_inventory);
    }

    R_SUCCEED();
}

Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile, const fs::FsPath& base_path, UnzipAllFilter filter, bool save_dbi_compat, UnzipPayloadSummary* output, UnzipPayloadInventory* inventory_out) {
    return TransferUnzipPreflight(pbox, zfile, base_path, filter, save_dbi_compat, output, inventory_out, false);
}

Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out, const fs::FsPath& base_path, UnzipAllFilter filter, bool save_dbi_compat, UnzipPayloadSummary* output, UnzipPayloadInventory* inventory_out) {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_out, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));

    return TransferUnzipPreflight(pbox, zfile, base_path, filter, save_dbi_compat, output, inventory_out);
}

Result TransferUnzipAll(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode, bool save_dbi_compat, bool checked_native_save, s64 checked_save_journal_size) {
    if (checked_native_save) {
        if (!fs || !fs->IsNative()) {
            R_THROW(FsError_NotImplemented);
        }
        if (checked_save_journal_size < 0) {
            R_THROW(FsError_InvalidSize);
        }
    }

    unz_global_info64 ginfo;
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        R_THROW(Result_UnzGetGlobalInfo64);
    }

    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        R_THROW(FsError_InvalidSize);
    }
    if (ginfo.number_entry == 0) {
        R_THROW(FsError_InvalidSize);
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    const auto base_len = base_path.length();
    const bool base_needs_slash = (base_len > 0 && base_path[base_len - 1] != '/');

    s64 total_size = 0;
    for (s64 i = 0; i < entry_count; i++) {
        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile while sizing archive\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            log_write("failed to get current info while sizing archive\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        fs::FsPath name;
        R_TRY(ResolveArchiveEntryName(info, name_buf, save_dbi_compat, name));

        const auto full_path_len = base_len + (base_needs_slash ? 1 : 0) + std::strlen(name.s);
        if (full_path_len + 1 > sizeof(fs::FsPath)) {
            log_write("archive entry output path exceeds buffer (%zu bytes)\n", full_path_len + 1);
            R_THROW(FsError_TooLongPath);
        }

        if (info.uncompressed_size > static_cast<u64>(std::numeric_limits<s64>::max())) {
            log_write("archive uncompressed size exceeds s64 maximum\n");
            R_THROW(FsError_InvalidSize);
        }

        if (static_cast<u64>(std::numeric_limits<s64>::max()) - static_cast<u64>(total_size) < info.uncompressed_size) {
            log_write("archive total uncompressed size exceeds s64 maximum\n");
            R_THROW(FsError_InvalidSize);
        }

        total_size += static_cast<s64>(info.uncompressed_size);
    }

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    const auto use_entry_progress = total_size == 0;
    const s64 progress_total = use_entry_progress ? entry_count : total_size;
    s64 progress_offset = 0;

    pbox->ResetTransferProgress();
    pbox->UpdateTransfer(0, progress_total);

    for (s64 i = 0; i < entry_count; i++) {
        R_TRY(pbox->ShouldExitResult());

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            log_write("failed to open current file\n");
            R_THROW(Result_UnzOpenCurrentFile);
        }
        bool curr_file_open = true;
        ON_SCOPE_EXIT {
            if (curr_file_open && zfile) {
                unzCloseCurrentFile(zfile);
            }
        };

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), 0, 0, 0, 0)) {
            log_write("failed to get current info\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        ResolvedDestinationEntry resolved{};
        R_TRY(ResolveArchiveDestinationEntry(info, name_buf, base_path, filter, save_dbi_compat, resolved));

        const auto entry_progress_start = progress_offset;
        const s64 entry_progress_size = use_entry_progress ? 1 : static_cast<s64>(info.uncompressed_size);
        const auto update_progress = [&](s64 bytes) {
            progress_offset = std::min(progress_offset + bytes, progress_total);
            pbox->UpdateTransfer(progress_offset, progress_total);
        };
        const auto finish_entry = [&]() {
            progress_offset = std::min(entry_progress_start + entry_progress_size, progress_total);
            pbox->UpdateTransfer(progress_offset, progress_total);
        };

        if (!resolved.keep) {
            curr_file_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            finish_entry();
            continue;
        }

        if (resolved.is_directory) {
            if (checked_native_save) {
                const auto dir_rc = CreateDirectoryChecked(pbox, fs, resolved.path);
                curr_file_open = false;
                const int close_res = unzCloseCurrentFile(zfile);
                R_TRY(dir_rc);
                if (close_res == UNZ_CRCERROR) {
                    R_THROW(0x8);
                }
                R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
                finish_entry();
            } else {
                Result rc;
                if (R_FAILED(rc = fs->CreateDirectoryRecursively(resolved.path)) && rc != FsError_PathAlreadyExists) {
                    log_write("failed to create folder: %s 0x%04X\n", resolved.path.s, rc);
                    curr_file_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(rc);
                }
                curr_file_open = false;
                const int close_res = unzCloseCurrentFile(zfile);
                if (close_res == UNZ_CRCERROR) {
                    R_THROW(0x8);
                }
                R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
                finish_entry();
            }
        } else {
            const auto unzip_rc = TransferUnzipInternal(pbox, zfile, fs, resolved.path, info.uncompressed_size, info.crc, mode,
                [&](s64 bytes_written) {
                    update_progress(bytes_written);
                },
                false,
                checked_native_save,
                checked_save_journal_size
            );
            curr_file_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            R_TRY(unzip_rc);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            finish_entry();
        }
    }

    R_SUCCEED();
}

Result TransferUnzipAll(ui::ProgressBox* pbox, const fs::FsPath& zip_out, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode, bool save_dbi_compat, bool checked_native_save, s64 checked_save_journal_size) {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_out, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));

    return TransferUnzipAll(pbox, zfile, fs, base_path, filter, mode, save_dbi_compat, checked_native_save, checked_save_journal_size);
}

Result VerifyArchiveAgainstNative(
    ui::ProgressBox* pbox,
    void* zfile,
    fs::Fs* fs,
    const fs::FsPath& base_path,
    const UnzipPayloadInventory& expected_inventory,
    UnzipAllFilter filter,
    bool save_dbi_compat,
    bool allow_empty) {

    if (!fs || !fs->IsNative()) {
        R_THROW(FsError_NotImplemented);
    }
    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    // 1. Enumerate native destination with actual sizes
    ui::menu::filebrowser::FsDirCollections collections;
    R_TRY(ui::menu::filebrowser::FsView::get_collections(fs, base_path, "", collections, true));

    std::map<std::string, s64> native_files;
    std::set<std::string> native_dirs;

    for (const auto& col : collections) {
        if (col.path != base_path) {
            std::string dir_key = col.path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                native_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    native_dirs.insert(parent);
                }
            }
        }
        for (const auto& d : col.dirs) {
            auto dir_path = fs::AppendPath(col.path, d.name);
            std::string dir_key = dir_path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                native_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    native_dirs.insert(parent);
                }
            }
        }
        for (const auto& f : col.files) {
            R_UNLESS(f.file_size >= 0, FsError_InvalidSize);
            auto file_path = fs::AppendPath(col.path, f.name);
            std::string file_key = file_path.s;
            auto [fit, finserted] = native_files.try_emplace(file_key, f.file_size);
            R_UNLESS(finserted, FsError_PathAlreadyExists);
            for (const auto& parent : GetParentDirectories(file_key)) {
                native_dirs.insert(parent);
            }
        }
    }

    // 2. Exact inventory bijection check
    R_UNLESS(native_files.size() == expected_inventory.files.size(), FsError_PathNotFound);
    for (const auto& [path, size] : expected_inventory.files) {
        auto it = native_files.find(path);
        R_UNLESS(it != native_files.end(), FsError_PathNotFound);
        R_UNLESS(it->second == size, FsError_InvalidSize);
    }
    R_UNLESS(native_dirs.size() == expected_inventory.directories.size(), FsError_PathNotFound);
    for (const auto& dir : expected_inventory.directories) {
        R_UNLESS(native_dirs.contains(dir), FsError_PathNotFound);
    }

    // 3. Traversal and streaming byte-for-byte comparison
    unz_global_info64 ginfo;
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        R_THROW(Result_UnzGetGlobalInfo64);
    }
    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        R_THROW(FsError_InvalidSize);
    }
    if (ginfo.number_entry == 0) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }
        R_UNLESS(expected_inventory.files.empty() && expected_inventory.directories.empty(), FsError_InvalidSize);
        R_UNLESS(native_files.empty() && native_dirs.empty(), FsError_PathNotFound);
        if (!allow_empty) {
            R_THROW(FsError_InvalidSize);
        }
        R_SUCCEED();
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    UnzipPayloadInventory observed_inventory{};
    std::set<std::string> observed_explicit_dirs;
    std::set<std::string> verified_files;

    for (s64 i = 0; i < entry_count; i++) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile in VerifyArchiveAgainstNative\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            log_write("failed to get current info in VerifyArchiveAgainstNative\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        if (info.uncompressed_size > static_cast<u64>(std::numeric_limits<s64>::max())) {
            log_write("archive uncompressed size exceeds s64 maximum in verifier\n");
            R_THROW(FsError_InvalidSize);
        }

        ResolvedDestinationEntry resolved{};
        R_TRY(ResolveArchiveDestinationEntry(info, name_buf, base_path, filter, save_dbi_compat, resolved));

        if (resolved.keep) {
            if (resolved.is_directory) {
                std::string canonical_dir = resolved.path.s;
                while (canonical_dir.size() > 1 && canonical_dir.back() == '/') {
                    canonical_dir.pop_back();
                }
                if (canonical_dir.empty() || canonical_dir == "/") {
                    R_THROW(FsError_InvalidCharacter);
                }
                if (!observed_explicit_dirs.insert(canonical_dir).second) {
                    log_write("verifier observed duplicate explicit directory: %s\n", canonical_dir.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                if (observed_inventory.files.contains(canonical_dir)) {
                    log_write("verifier dir conflicts with file: %s\n", canonical_dir.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                for (const auto& parent : GetParentDirectories(canonical_dir)) {
                    if (observed_inventory.files.contains(parent)) {
                        log_write("verifier parent of dir conflicts with file: %s\n", parent.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    observed_inventory.directories.insert(parent);
                }
                observed_inventory.directories.insert(canonical_dir);
            } else {
                std::string file_key = resolved.path.s;
                if (observed_inventory.files.contains(file_key)) {
                    log_write("verifier observed duplicate kept file: %s\n", file_key.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                if (observed_inventory.directories.contains(file_key)) {
                    log_write("verifier file conflicts with directory: %s\n", file_key.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                for (const auto& parent : GetParentDirectories(file_key)) {
                    if (observed_inventory.files.contains(parent)) {
                        log_write("verifier parent of file conflicts with file: %s\n", parent.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    observed_inventory.directories.insert(parent);
                }
                observed_inventory.files.emplace(file_key, static_cast<s64>(info.uncompressed_size));
            }
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            log_write("failed to open current file in VerifyArchiveAgainstNative: %s\n", name_buf);
            R_THROW(Result_UnzOpenCurrentFile);
        }
        bool curr_open = true;
        ON_SCOPE_EXIT {
            if (curr_open && zfile) {
                unzCloseCurrentFile(zfile);
            }
        };

        if (!resolved.keep) {
            // Excluded source metadata: drain completely and check CRC
            std::vector<u8> drain_buf(32768);
            u32 crc_calc = 0;
            u64 bytes_drained = 0;
            int zr = 0;
            do {
                if (pbox) {
                    const auto exit_rc = pbox->ShouldExitResult();
                    if (R_FAILED(exit_rc)) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return exit_rc;
                    }
                }
                zr = unzReadCurrentFile(zfile, drain_buf.data(), drain_buf.size());
                if (zr < 0) {
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(Result_UnzReadCurrentFile);
                }
                if (zr > 0) {
                    const auto r_u64 = static_cast<u64>(zr);
                    if (std::numeric_limits<u64>::max() - bytes_drained < r_u64 || bytes_drained + r_u64 > info.uncompressed_size) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        R_THROW(FsError_InvalidSize);
                    }
                    if (info.crc) {
                        crc_calc = crc32CalculateWithSeed(crc_calc, drain_buf.data(), zr);
                    }
                    bytes_drained += r_u64;
                }
            } while (zr > 0);

            curr_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            R_UNLESS(bytes_drained == info.uncompressed_size, FsError_InvalidSize);
            if (info.crc && crc_calc != info.crc) {
                R_THROW(0x8);
            }
        } else if (resolved.is_directory) {
            std::string canonical_dir = resolved.path.s;
            while (canonical_dir.size() > 1 && canonical_dir.back() == '/') {
                canonical_dir.pop_back();
            }
            R_UNLESS(expected_inventory.directories.contains(canonical_dir), FsError_PathNotFound);
            R_UNLESS(native_dirs.contains(canonical_dir), FsError_PathNotFound);

            // Drain kept directory entries through EOF with checked byte count/CRC/close
            std::vector<u8> drain_buf(32768);
            u32 crc_calc = 0;
            u64 bytes_drained = 0;
            int zr = 0;
            do {
                if (pbox) {
                    const auto exit_rc = pbox->ShouldExitResult();
                    if (R_FAILED(exit_rc)) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return exit_rc;
                    }
                }
                zr = unzReadCurrentFile(zfile, drain_buf.data(), drain_buf.size());
                if (zr < 0) {
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(Result_UnzReadCurrentFile);
                }
                if (zr > 0) {
                    const auto r_u64 = static_cast<u64>(zr);
                    if (std::numeric_limits<u64>::max() - bytes_drained < r_u64 || bytes_drained + r_u64 > info.uncompressed_size) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        R_THROW(FsError_InvalidSize);
                    }
                    if (info.crc) {
                        crc_calc = crc32CalculateWithSeed(crc_calc, drain_buf.data(), zr);
                    }
                    bytes_drained += r_u64;
                }
            } while (zr > 0);

            curr_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            R_UNLESS(bytes_drained == info.uncompressed_size, FsError_InvalidSize);
            if (info.crc && crc_calc != info.crc) {
                R_THROW(0x8);
            }
        } else {
            auto eit = expected_inventory.files.find(resolved.path.s);
            R_UNLESS(eit != expected_inventory.files.end(), FsError_PathNotFound);
            R_UNLESS(static_cast<u64>(eit->second) == info.uncompressed_size, FsError_InvalidSize);
            R_UNLESS(!verified_files.contains(resolved.path.s), FsError_PathAlreadyExists);

            fs::File nf;
            const auto open_rc = fs->OpenFile(resolved.path, FsOpenMode_Read, &nf);
            if (R_FAILED(open_rc)) {
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return open_rc;
            }

            s64 nf_size = 0;
            const auto size_rc = nf.GetSize(&nf_size);
            if (R_FAILED(size_rc)) {
                nf.Close();
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return size_rc;
            }
            if (nf_size != eit->second) {
                nf.Close();
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return FsError_InvalidSize;
            }

            constexpr size_t CMP_BUF_SIZE = 32768;
            std::vector<u8> zbuf(CMP_BUF_SIZE);
            std::vector<u8> fbuf(CMP_BUF_SIZE);
            s64 offset = 0;
            u32 file_crc = 0;

            while (offset < nf_size) {
                if (pbox) {
                    const auto exit_rc = pbox->ShouldExitResult();
                    if (R_FAILED(exit_rc)) {
                        nf.Close();
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return exit_rc;
                    }
                }
                const auto chunk = static_cast<s64>(std::min<size_t>(CMP_BUF_SIZE, nf_size - offset));
                s64 z_accum = 0;
                while (z_accum < chunk) {
                    int zr = unzReadCurrentFile(zfile, zbuf.data() + z_accum, chunk - z_accum);
                    if (zr <= 0) {
                        nf.Close();
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return FsError_InvalidSize;
                    }
                    z_accum += zr;
                }
                if (info.crc) {
                    file_crc = crc32CalculateWithSeed(file_crc, zbuf.data(), chunk);
                }

                u64 fread = 0;
                const auto read_rc = nf.Read(offset, fbuf.data(), chunk, FsReadOption_None, &fread);
                if (R_FAILED(read_rc)) {
                    nf.Close();
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    return read_rc;
                }
                if (static_cast<s64>(fread) != chunk) {
                    nf.Close();
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    return FsError_InvalidSize;
                }
                if (std::memcmp(zbuf.data(), fbuf.data(), chunk) != 0) {
                    nf.Close();
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    return FsError_InvalidSize;
                }
                offset += chunk;
            }

            u8 dummy;
            int extra_zr = unzReadCurrentFile(zfile, &dummy, 1);
            if (extra_zr != 0) {
                nf.Close();
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return FsError_InvalidSize;
            }

            nf.Close();

            curr_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            if (info.crc && file_crc != info.crc) {
                R_THROW(0x8);
            }

            verified_files.insert(resolved.path.s);
        }
    }

    R_UNLESS(observed_inventory.files == expected_inventory.files, FsError_PathNotFound);
    R_UNLESS(observed_inventory.directories == expected_inventory.directories, FsError_PathNotFound);
    R_UNLESS(verified_files.size() == expected_inventory.files.size(), FsError_PathNotFound);
    R_UNLESS(UNZ_END_OF_LIST_OF_FILE == unzGoToNextFile(zfile), Result_UnzGoToNextFile);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        log_write("failed to rewind zip after verification\n");
        R_THROW(Result_UnzGoToFirstFile);
    }

    // 4. Re-enumerate native inventory after byte comparison to catch observable changes
    ui::menu::filebrowser::FsDirCollections post_collections;
    R_TRY(ui::menu::filebrowser::FsView::get_collections(fs, base_path, "", post_collections, true));

    std::map<std::string, s64> post_files;
    std::set<std::string> post_dirs;

    for (const auto& col : post_collections) {
        if (col.path != base_path) {
            std::string dir_key = col.path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                post_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    post_dirs.insert(parent);
                }
            }
        }
        for (const auto& d : col.dirs) {
            auto dir_path = fs::AppendPath(col.path, d.name);
            std::string dir_key = dir_path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                post_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    post_dirs.insert(parent);
                }
            }
        }
        for (const auto& f : col.files) {
            R_UNLESS(f.file_size >= 0, FsError_TargetLocked);
            auto file_path = fs::AppendPath(col.path, f.name);
            std::string file_key = file_path.s;
            auto [fit, finserted] = post_files.try_emplace(file_key, f.file_size);
            R_UNLESS(finserted, FsError_TargetLocked);
            for (const auto& parent : GetParentDirectories(file_key)) {
                post_dirs.insert(parent);
            }
        }
    }

    R_UNLESS(post_files.size() == native_files.size(), FsError_TargetLocked);
    for (const auto& [path, size] : native_files) {
        auto it = post_files.find(path);
        R_UNLESS(it != post_files.end(), FsError_TargetLocked);
        R_UNLESS(it->second == size, FsError_TargetLocked);
    }
    R_UNLESS(post_dirs.size() == native_dirs.size(), FsError_TargetLocked);
    for (const auto& dir : native_dirs) {
        R_UNLESS(post_dirs.contains(dir), FsError_TargetLocked);
    }

    R_SUCCEED();
}

Result VerifyArchiveAgainstNative(
    ui::ProgressBox* pbox,
    const fs::FsPath& zip_out,
    fs::Fs* fs,
    const fs::FsPath& base_path,
    const UnzipPayloadInventory& expected_inventory,
    UnzipAllFilter filter,
    bool save_dbi_compat,
    bool allow_empty) {

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_out, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));

    return VerifyArchiveAgainstNative(pbox, zfile, fs, base_path, expected_inventory, filter, save_dbi_compat, allow_empty);
}

} // namespace::thread
