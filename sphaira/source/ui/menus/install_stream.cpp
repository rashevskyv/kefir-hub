#include "ui/menus/install_stream_menu_base.hpp"

#if ENABLE_NETWORK_INSTALL
#include "yati/yati.hpp"
#include "log.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/progress_box.hpp"
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::stream {
namespace {

// the level at which Push() pauses once to let the installer catch up. this is
// a soft threshold, not a hard cap -- see the note in Push(). a deeper buffer
// smooths over the installer's throughput dips (an nsz decompresses to far more
// than it reads, so the write thread stalls in bursts); the more slack there is
// here, the shorter each usb pause, which keeps windows from deciding the
// device has stopped responding and killing the transfer. proportional
// compaction in ReadChunk keeps this cheap regardless of size.
constexpr u64 MAX_BUFFER_SIZE = 1024ULL*1024ULL*16ULL;
constexpr u64 MAX_BUFFER_RESERVE_SIZE = 1024ULL*1024ULL*48ULL;

// Push() blocking is normal and frequent, and a burst of short pauses is
// expected while the pipeline paces itself; only a genuinely long stall risks
// the windows timeout, so log only those. must stay well off the hot path --
// each log line is an open/write/close on the same sd card the installer is
// writing to, which is exactly what was starving the nsz write thread.
constexpr u64 STALL_LOG_THRESHOLD_NS = 2000ULL*1000ULL*1000ULL;

// Push() never waits on can_write longer than this before accepting the data
// anyway. once the installer's read thread has finished (install done, or
// briefly between ncas while ncm registers) nothing ever signals can_write, so
// an unbounded wait here would hang the mtp responder and windows would report
// "device stopped responding" at the very end of an otherwise-successful
// install. after the timeout the buffer is allowed to overshoot (reserve
// covers it) so the responder keeps answering.
constexpr u64 PUSH_STALL_TIMEOUT_NS = 500ULL*1000ULL*1000ULL;

// if ReadChunk() waits this long for data that never comes, treat the transfer
// as interrupted (host cancelled the copy, cable pulled, or the host itself
// timed out) rather than hanging the install forever. under backpressure the
// buffer is normally full, so a long-empty buffer means the host has genuinely
// stopped sending -- 15s is well beyond any legitimate pause.
constexpr u64 READ_STALL_TIMEOUT_NS = 15ULL*1000ULL*1000ULL*1000ULL;

// .nro is homebrew, not a title: it isn't installed through yati but copied to
// /switch/<name>/<name>.nro so the homebrew menu picks it up. it still runs
// through the whole stream pipeline (queue, progress box, cancellation) so a
// dropped .nro looks and behaves like any other install.
bool IsNroPath(const char* path) {
    const auto ext = std::strrchr(path, '.');
    return ext && !strcasecmp(ext, ".nro");
}

// grow the destination in chunks: the stream length isn't known up front.
constexpr s64 NRO_GROW_CHUNK = 1024 * 1024 * 4;
constexpr s64 NRO_COPY_CHUNK = 1024 * 512;

// streams the whole source into /switch/<stem>/<name>.nro.
Result InstallNroFromStream(ui::InstallProgress* pbox, Stream* source) {
    const auto path = source->GetPath();
    const char* slash = std::strrchr(path.s, '/');
    const char* name = slash ? slash + 1 : path.s;
    R_UNLESS(name[0], Result_TransferCancelled);

    const auto ext = std::strrchr(name, '.');
    const auto stem_len = ext ? (int)(ext - name) : (int)std::strlen(name);

    fs::FsPath dir;
    std::snprintf(dir, sizeof(dir), "/switch/%.*s", stem_len, name);

    fs::FsPath full;
    std::snprintf(full, sizeof(full), "%s/%s", dir.s, name);

    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(dir));
    sd.DeleteFile(full); // replace any previous copy of this homebrew.

    if (const auto rc = sd.CreateFile(full, 0, 0); R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
        R_THROW(rc);
    }

    fs::File file;
    R_TRY(sd.OpenFile(full, FsOpenMode_Write, &file));
    R_TRY(file.SetSize(0));

    // a partial .nro would show up in the homebrew menu as a broken entry.
    struct Guard {
        fs::FsNativeSd& sd;
        const fs::FsPath& path;
        bool success{};
        ~Guard() { if (!success) { sd.DeleteFile(path); } }
    } guard{sd, full};

    pbox->SetInstallTitle(name);
    pbox->SetInstallTarget(true);

    std::vector<u8> buf(NRO_COPY_CHUNK);
    s64 offset{};
    s64 allocated{};

    for (;;) {
        R_TRY(pbox->CheckCancelled());

        u64 bytes_read{};
        R_TRY(source->ReadChunk(buf.data(), buf.size(), &bytes_read));
        if (!bytes_read) {
            break; // end of stream.
        }

        if (allocated < offset + (s64)bytes_read) {
            allocated = offset + (s64)bytes_read + NRO_GROW_CHUNK;
            R_TRY(file.SetSize(allocated));
        }

        R_TRY(file.Write(offset, buf.data(), bytes_read, FsWriteOption_None));
        offset += bytes_read;
        // the transport never tells us how big the file is, so report an unknown
        // total (size 0): the progress box then shows a sliding bar with the
        // bytes received and the speed. reporting offset against itself used to
        // peg it at "100% - 0 seconds remaining" from the first chunk, which
        // read as "done" while the host was still only halfway through sending.
        pbox->UpdateInstallTransfer(offset, 0);
        pbox->UpdateInstallReadWrite(offset, offset);
    }

    R_UNLESS(offset > 0, Result_TransferInterrupted);

    R_TRY(file.SetSize(offset)); // trim the padding from the last grow.
    file.Close();
    R_TRY(sd.Commit());

    guard.success = true;
    log_write("[NRO] installed homebrew to %s (%ld bytes)\n", full.s, offset);

    // the homebrew menu rescans /switch on the next frame.
    homebrew::SignalChange();
    R_SUCCEED();
}

#define USE_CONDI_VAR 1

} // namespace

Result RunInstall(ui::InstallProgress* pbox, Stream* source) {
    if (IsNroPath(source->GetPath())) {
        return InstallNroFromStream(pbox, source);
    }
    return yati::InstallFromSource(pbox, source, source->GetPath());
}

Stream::Stream(const fs::FsPath& path, std::stop_token token) {
    m_path = path;
    m_token = token;
    m_active = true;
    m_buffer.reserve(MAX_BUFFER_RESERVE_SIZE);
    m_read_offset = 0;

    mutexInit(&m_mutex);
    condvarInit(&m_can_read);
    condvarInit(&m_can_write);
}

// NOTE: this and Push() below run once per USB packet on the MTP data path.
// do not add log_write() here -- every call open()s, write()s and close()s
// log.txt on the sd card while holding a process-wide mutex, which starves the
// very sd writes the installer is doing and stalls the usb endpoint until the
// windows mtp host times the transfer out.
Result Stream::ReadChunk(void* buf, s64 size, u64* bytes_read) {
    u64 wait_started = 0;
    while (!m_token.stop_requested()) {
        SCOPED_MUTEX(&m_mutex);
        if (m_active && m_buffer.size() == m_read_offset) {
            if (!wait_started) {
                wait_started = armTicksToNs(armGetSystemTick());
            }
            // bounded wait so a host that stops sending without closing the file
            // (cancelled copy, pulled cable, host-side timeout) doesn't hang the
            // read thread forever. a timeout is not itself an error -- only give
            // up once the buffer has stayed empty past READ_STALL_TIMEOUT_NS.
            condvarWaitTimeout(std::addressof(m_can_read), std::addressof(m_mutex), PUSH_STALL_TIMEOUT_NS);
            if (m_active && m_buffer.size() == m_read_offset &&
                armTicksToNs(armGetSystemTick()) - wait_started >= READ_STALL_TIMEOUT_NS) {
                log_write("[Stream::ReadChunk] no data for %llu s, treating transfer as interrupted\n", READ_STALL_TIMEOUT_NS / 1000000000ULL);
                R_THROW(Result_TransferInterrupted);
            }
        } else {
            wait_started = 0;
        }

        if (m_token.stop_requested()) {
            break;
        }

        if (!m_active && m_buffer.size() == m_read_offset) {
            *bytes_read = 0;
            return 0;
        }

        // spurious wakeup with no data yet, wait again rather than
        // returning a zero-byte read (treated as eof by the caller).
        if (m_buffer.size() == m_read_offset) {
            continue;
        }

        const s64 available = m_buffer.size() - m_read_offset;
        size = std::min<s64>(size, available);
        std::memcpy(buf, m_buffer.data() + m_read_offset, size);
        m_read_offset += size;
        *bytes_read = size;

        if (m_read_offset == m_buffer.size()) {
            m_buffer.clear();
            m_read_offset = 0;
        } else if (m_read_offset * 2 >= m_buffer.size()) {
            // compact only once at least half the buffer has been consumed, so
            // each memmove shifts at most half of it -- amortised O(1) per byte
            // no matter how large the buffer grows. a fixed trigger (the old
            // 4MiB) keeps the same frequency while the moved amount scales with
            // the buffer, which quietly destroys throughput as it fills.
            std::memmove(m_buffer.data(), m_buffer.data() + m_read_offset, m_buffer.size() - m_read_offset);
            m_buffer.resize(m_buffer.size() - m_read_offset);
            m_read_offset = 0;
        }

        return condvarWakeOne(&m_can_write);
    }

    R_THROW(Result_TransferCancelled);
}

bool Stream::Push(const void* buf, s64 size) {
    while (!m_token.stop_requested()) {
        if (INSTALL_STATE == InstallState_Finished) {
            return true;
        }

        SCOPED_MUTEX(&m_mutex);
        #if USE_CONDI_VAR
        // deliberately `if`, not `while`: wait for at most one drain and then
        // accept the data regardless of whether the buffer is still over the
        // threshold. this is soft backpressure -- it paces the usb thread to
        // roughly the installer's read rate without ever parking it for long.
        //
        // looping here instead (v0.13.283) turns this into a hard gate: when
        // the write thread is the bottleneck the usb thread parks for 1-2s at a
        // time, and windows mtp responds to that kind of bursty throughput by
        // stalling the transfer for ~a minute and then killing it outright --
        // exactly what the header comment above warns about. the buffer is
        // allowed to overshoot MAX_BUFFER_SIZE; reads (4MiB) outpace pushes
        // (~1MiB), so it drains again on its own.
        u64 stall_start = 0;
        if (m_active && (m_buffer.size() - m_read_offset) >= MAX_BUFFER_SIZE) {
            stall_start = armTicksToNs(armGetSystemTick());
            // bounded wait: once the installer's read thread has finished (install
            // done, or briefly between ncas) nothing signals can_write, so an
            // unbounded wait would hang the mtp responder here and windows would
            // report "device stopped responding" at the tail of the transfer.
            // after the timeout we fall through and accept the data anyway (the
            // buffer overshoots MAX_BUFFER_SIZE; reserve covers it), which keeps
            // the responder answering.
            condvarWaitTimeout(std::addressof(m_can_write), std::addressof(m_mutex), PUSH_STALL_TIMEOUT_NS);
        }
        if (stall_start) {
            const auto stalled_ns = armTicksToNs(armGetSystemTick()) - stall_start;
            if (stalled_ns >= STALL_LOG_THRESHOLD_NS) {
                log_write("[Stream::Push] usb paused %llu ms waiting for the installer to drain the buffer\n", stalled_ns / 1000000ULL);
            }
        }
        #else
        if (m_active && (m_buffer.size() - m_read_offset) >= MAX_BUFFER_SIZE) {
            // unlock the mutex and wait for 1s to bring transfer speed down to 1MiB/s.
            mutexUnlock(&m_mutex);
            ON_SCOPE_EXIT(mutexLock(&m_mutex));

            svcSleepThread(1e+9);
        }
        #endif

        if (!m_active) {
            break;
        }

        const auto offset = m_buffer.size();
        m_buffer.resize(offset + size);
        std::memcpy(m_buffer.data() + offset, buf, size);
        condvarWakeOne(&m_can_read);
        return true;
    }

    return false;
}

void Stream::Disable() {
    log_write("[Stream::Disable] disabling file\n");

    SCOPED_MUTEX(&m_mutex);
    m_active = false;
    condvarWakeOne(&m_can_read);
    condvarWakeOne(&m_can_write);
}

} // namespace sphaira::ui::menu::stream

#else

namespace sphaira::ui::menu::stream {

Result RunInstall(ui::InstallProgress*, Stream*) { return 0; }

Stream::Stream(const fs::FsPath&, std::stop_token) {}
Result Stream::ReadChunk(void*, s64, u64*) { return 0; }
bool Stream::Push(const void*, s64) { return false; }
void Stream::Disable() {}

} // namespace sphaira::ui::menu::stream

#endif
