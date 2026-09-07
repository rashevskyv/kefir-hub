#include "ui/menus/install_stream_menu_base.hpp"

#if ENABLE_NETWORK_INSTALL
#include "yati/yati.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "haze_helper.hpp"
#include "ftpsrv_helper.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/dbi/install_queue_state.hpp"
#include "evman.hpp"
#include <cstring>
#include <vector>
 
namespace sphaira::ui::menu::stream {
 
std::atomic<int> INSTALL_STATE{InstallState_None};
 
Menu* BackgroundInstaller::s_active_menu{nullptr};
std::shared_ptr<Stream> BackgroundInstaller::s_source{nullptr};
std::stop_source BackgroundInstaller::s_stop_source{};
std::atomic<bool> BackgroundInstaller::s_installing{false};
Mutex BackgroundInstaller::s_mutex{};
CondVar BackgroundInstaller::s_callback_cond{};
std::atomic<int> BackgroundInstaller::s_callback_count{0};

static Thread s_install_thread{};
static bool s_install_thread_created{false};

static void JoinInstallThread() {
    if (s_install_thread_created) {
        threadWaitForExit(&s_install_thread);
        threadClose(&s_install_thread);
        s_install_thread_created = false;
    }
}

void BackgroundInstaller::TeardownWorker() {
    JoinInstallThread();
}

 
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

// runs either the yati installer or the homebrew copy, depending on the file.
Result RunInstall(ui::InstallProgress* pbox, Stream* source) {
    if (IsNroPath(source->GetPath())) {
        return InstallNroFromStream(pbox, source);
    }
    return yati::InstallFromSource(pbox, source, source->GetPath());
}

// don't use condivar here as windows mtp is very broken.
// stalling for too longer (3s+) and having too varied transfer speeds
// results in windows stalling the transfer for 1m until it kills it via timeout.
// the workaround is to always accept new data, but stall for 1s.
// UPDATE: it seems possible to trigger this bug during normal file transfer
// including using stock haze.
// it seems random, and ive been unable to trigger it personally.
// for this reason, use condivar rather than trying to work around the issue.
#define USE_CONDI_VAR 1

} // namespace

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

Menu::Menu(const std::string& title, u32 flags) : MenuBase{title, flags} {
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});

    SetAction(Button::START, Action{"Options"_i18n, [this](){
        App::DisplayInstallOptions(false);
    }});

    App::SetAutoSleepDisabled(true);
    mutexInit(&m_mutex);

    INSTALL_STATE = InstallState_None;
}

Menu::~Menu() {
    // signal for thread to exit and wait.
    m_stop_source.request_stop();

    if (m_source) {
        m_source->Disable();
    }

    App::SetAutoSleepDisabled(false);
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    SCOPED_MUTEX(&m_mutex);

    if (m_state == State::Connected) {
        m_state = State::Progress;
        App::Push<ui::ProgressBox>(0, "Installing "_i18n, m_source->GetPath(), [this](auto pbox) -> Result {
            INSTALL_STATE = InstallState_Progress;
            const auto rc = RunInstall(pbox, m_source.get());

            if (R_FAILED(rc)) {
                // if the source (PC) closed the transfer early the stream is
                // already disabled and yati failed on truncated data -- surface
                // that as a friendly "interrupted", not a raw Fs error.
                const bool source_ended = !m_source->m_active && rc != Result_TransferCancelled;
                // do NOT enter the Finished "swallow" state; reset to idle and
                // disable so further writes are rejected and the transport aborts.
                INSTALL_STATE = InstallState_None;
                m_source->Disable();
                R_THROW(source_ended ? Result_TransferInterrupted : rc);
            }

            // clean finish: the installer read only the ncas it needed and
            // finished before the host sent the whole file; swallow the tail.
            INSTALL_STATE = InstallState_Finished;
            R_SUCCEED();
        }, [this](Result rc){
            SCOPED_MUTEX(&m_mutex);

            if (R_SUCCEEDED(rc)) {
                App::Notify("Install success!"_i18n);
                m_state = State::Done;
            } else if (rc == Result_TransferCancelled || rc == Result_TransferInterrupted) {
                // cancelled on the console or by the source (PC): friendly, not scary.
                App::PlaySoundEffect(SoundEffect_Focus);
                App::Notify(rc == Result_TransferInterrupted
                    ? "Install cancelled: the source stopped sending data"_i18n
                    : "Install cancelled"_i18n);
                m_state = State::Done;
            } else {
                App::PushErrorBox(rc, "Install failed!"_i18n);
                m_state = State::Failed;
                OnDisableInstallMode();
            }
        });
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    SCOPED_MUTEX(&m_mutex);

    switch (m_state) {
        case State::None:
        case State::Done:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Drag'n'Drop (NSP, XCI, NSZ, XCZ) to the install folder"_i18n.c_str());
            break;

        case State::Connected:
        case State::Progress:
            break;

        case State::Failed:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Failed to install, press B to exit..."_i18n.c_str());
            break;
    }
}

bool Menu::OnInstallStart(const char* path) {
    log_write("[Menu::OnInstallStart] inside\n");

    for (;;) {
        {
            SCOPED_MUTEX(&m_mutex);

            if (m_state != State::Progress) {
                break;
            }

            if (GetToken().stop_requested()) {
                return false;
            }
        }

        svcSleepThread(1e+6);
    }

    log_write("[Menu::OnInstallStart] got state: %u\n", (u8)m_state);

    if (m_source) {
        log_write("[Menu::OnInstallStart] we have source\n");
        for (;;) {
            {
                SCOPED_MUTEX(&m_source->m_mutex);

                if (!m_source->m_active && INSTALL_STATE != InstallState_Progress) {
                    break;
                }

                if (GetToken().stop_requested()) {
                    return false;
                }
            }

            svcSleepThread(1e+6);
        }

        log_write("[Menu::OnInstallStart] stopped polling source\n");
    }

    SCOPED_MUTEX(&m_mutex);

    m_source = std::make_unique<Stream>(path, GetToken());
    INSTALL_STATE = InstallState_None;
    m_state = State::Connected;
    log_write("[Menu::OnInstallStart] exiting\n");

    return true;
}

bool Menu::OnInstallWrite(const void* buf, size_t size) {
    log_write("[Menu::OnInstallWrite] inside\n");
    return m_source->Push(buf, size);
}

void Menu::OnInstallClose() {
    log_write("[Menu::OnInstallClose] inside\n");

    // don't block here waiting for the install to finish: this runs on
    // haze's single MTP responder thread, and stalling it for the seconds
    // an install can take makes Windows declare the device unresponsive
    // and disconnect it (the install itself still completes in the
    // background - OnInstallStart already waits for INSTALL_STATE to
    // clear before accepting the next file, so nothing here needs to).
    m_source->Disable();
}
 
void BackgroundInstaller::SetActiveMenu(Menu* menu) {
    mutexLock(&s_mutex);
    s_active_menu = menu;
    if (menu == nullptr) {
        while (s_callback_count > 0) {
            condvarWait(&s_callback_cond, &s_mutex);
        }
    }
    mutexUnlock(&s_mutex);
    JoinInstallThread();
}

static std::atomic<bool> s_restart_scheduled{false};

void ScheduleMtpRestart() {
    if (s_restart_scheduled.exchange(true)) {
        return;
    }
    evman::push(evman::FunctionalEventData{
        []() {
            s_restart_scheduled.store(false);
            log_write("[MTP] Restarting haze after install cancellation\n");
            BackgroundInstaller::TeardownWorker();
            if (haze::IsRunning()) {
                haze::Exit();
            }
            if (App::GetMtpEnable() && !App::IsExiting()) {
                if (haze::Init()) {
                    BackgroundInstaller::RegisterMtpCallbacks();
                }
            }
        }
    }, false);
}

void BackgroundInstaller::RegisterMtpCallbacks() {
    static bool initialized = false;
    if (!initialized) {
        mutexInit(&s_mutex);
        condvarInit(&s_callback_cond);
        initialized = true;
    }

    haze::InitInstallMode(
        [](const char* path) { return OnInstallStart(path, ui::menu::dbi::TransportOrigin::Mtp); },
        [](const void* buf, size_t size) { return OnInstallWrite(buf, size); },
        []() { OnInstallClose(); }
    );

    // FTP shares the same background installer, so dropping a file into the FTP
    // "install" folder works whether or not the FTP Install menu is open.
    ftpsrv::InitInstallMode(
        [](const char* path) { return OnInstallStart(path, ui::menu::dbi::TransportOrigin::Ftp); },
        [](const void* buf, size_t size) { return OnInstallWrite(buf, size); },
        []() { OnInstallClose(); }
    );
}

bool BackgroundInstaller::OnInstallStart(const char* path) {
    return OnInstallStart(path, ui::menu::dbi::TransportOrigin::Mtp);
}

bool BackgroundInstaller::OnInstallStart(const char* path, ui::menu::dbi::TransportOrigin origin) {
    log_write("[BackgroundInstaller::OnInstallStart] inside for path: %s\n", path);
    Menu* active = nullptr;
    {
        mutexLock(&s_mutex);
        if (s_active_menu) {
            active = s_active_menu;
            s_callback_count++;
        }
        mutexUnlock(&s_mutex);
    }

    if (active) {
        bool res = active->OnInstallStart(path);
        mutexLock(&s_mutex);
        s_callback_count--;
        if (s_callback_count == 0) {
            condvarWakeAll(&s_callback_cond);
        }
        mutexUnlock(&s_mutex);
        return res;
    }

    const char* ext = std::strrchr(path, '.');
    if (!ext) return false;
    bool valid_ext = false;
    // .nro isn't a title: RunInstall() copies it to /switch instead.
    static const char* SUPPORTED_EXT[] = { ".nsp", ".xci", ".nsz", ".xcz", ".nro" };
    for (const auto& supported : SUPPORTED_EXT) {
        if (strcasecmp(ext, supported) == 0) {
            valid_ext = true;
            break;
        }
    }
    if (!valid_ext) return false;

    for (;;) {
        auto existing_session = App::GetActiveInstallSession();
        if (existing_session && existing_session->GetOrigin() == origin) {
            while (s_installing.load()) {
                if (App::IsExiting()) {
                    return false;
                }
                auto session = App::GetActiveInstallSession();
                if (!session || session->GetOrigin() != origin || session->IsCancelRequested() || session->ShouldExit() || session->GetState() == ui::menu::dbi::State::Cancelled) {
                    return false;
                }
                svcSleepThread(1e+6);
            }
        }

        auto session = App::GetActiveInstallSession();
        const bool can_reuse_session = (session
                                        && session->GetOrigin() == origin
                                        && !session->IsCancelRequested()
                                        && !session->ShouldExit()
                                        && session->GetState() != ui::menu::dbi::State::Cancelled);

        if (!can_reuse_session && (App::GetProgressActive() || App::HasActiveTransfer() || s_installing.load())) {
            log_write("[BackgroundInstaller] Already installing, rejecting start\n");
            // the transport may retry the same file while it waits for the installer
            // (see on_thing() in ftpsrv_helper.cpp), so don't toast every refusal.
            static std::atomic<u64> last_notify_ns{0};
            const auto now = armTicksToNs(armGetSystemTick());
            auto last = last_notify_ns.load(std::memory_order_relaxed);
            if (!last || now - last >= 5000ULL*1000ULL*1000ULL) {
                if (last_notify_ns.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
                    evman::push(evman::FunctionalEventData {
                        []() {
                            App::Notify("Install failed: another installation is in progress."_i18n);
                        }
                    }, false);
                }
            }
            return false;
        }

        bool expected = false;
        if (s_installing.compare_exchange_strong(expected, true)) {
            if (can_reuse_session) {
                auto active_after_claim = App::GetActiveInstallSession();
                if (!active_after_claim
                    || active_after_claim->GetOrigin() != origin
                    || active_after_claim->IsCancelRequested()
                    || active_after_claim->ShouldExit()
                    || active_after_claim->GetState() == ui::menu::dbi::State::Cancelled) {
                    s_installing = false;
                    return false;
                }
            } else {
                if (App::GetProgressActive() || App::HasActiveTransfer()) {
                    s_installing = false;
                    return false;
                }
            }
            break;
        }
    }
    s_stop_source = std::stop_source();
    {
        mutexLock(&s_mutex);
        s_source = std::make_shared<Stream>(path, s_stop_source.get_token());
        mutexUnlock(&s_mutex);
    }
    INSTALL_STATE = InstallState_None;

    evman::push(evman::FunctionalEventData {
        [path_str = std::string(path), origin]() {
            log_write("[BackgroundInstaller] UI event triggered, creating InstallSession\n");

            std::shared_ptr<ui::menu::dbi::InstallSession> session = App::GetActiveInstallSession();
            if (session && session->GetOrigin() != origin) {
                log_write("[BackgroundInstaller] transfer slot taken by different transport, aborting install\n");
                std::shared_ptr<Stream> src;
                {
                    mutexLock(&s_mutex);
                    src = s_source;
                    s_source.reset();
                    mutexUnlock(&s_mutex);
                }
                if (src) {
                    src->Disable();
                }
                s_installing = false;
                App::Notify("Install failed: another installation is in progress."_i18n);
                return;
            }

            if (!session) {
                const std::string title = (origin == ui::menu::dbi::TransportOrigin::Mtp)
                    ? "MTP Install"_i18n
                    : (origin == ui::menu::dbi::TransportOrigin::Ftp)
                        ? "FTP Install"_i18n
                        : "Web Install"_i18n;
                session = std::make_shared<ui::menu::dbi::InstallSession>(title, 0, origin);
                if (origin == ui::menu::dbi::TransportOrigin::Ftp) {
                    for (const auto& f : ftpsrv::GetQueuedInstallFiles()) {
                        session->EnqueueFile(f);
                    }
                }
                if (!session->HasQueuedFile(path_str)) {
                    session->EnqueueFile(path_str);
                }
                if (!App::PushInstallSession(session)) {
                    log_write("[BackgroundInstaller] PushInstallSession refused, aborting\n");
                    std::shared_ptr<Stream> src;
                    {
                        mutexLock(&s_mutex);
                        src = s_source;
                        s_source.reset();
                        mutexUnlock(&s_mutex);
                    }
                    if (src) src->Disable();
                    s_installing = false;
                    App::Notify("Install failed: another installation is in progress."_i18n);
                    return;
                }
            } else {
                if (!session->HasQueuedFile(path_str)) {
                    session->EnqueueFile(path_str);
                }
            }

            App::SetAutoSleepDisabled(true);

            std::shared_ptr<Stream> src;
            {
                mutexLock(&s_mutex);
                src = s_source;
                mutexUnlock(&s_mutex);
            }
            if (!src) {
                s_installing = false;
                return;
            }

            session->SetStream(src);
            session->SetCurrentPackageByName(path_str);
            session->SetState(ui::menu::dbi::State::Installing);
            session->AddLog("Starting: "_i18n + path_str, ui::menu::dbi::LogKind::Event);

            JoinInstallThread();

            struct WorkerContext {
                std::shared_ptr<ui::menu::dbi::InstallSession> session;
                std::shared_ptr<Stream> src;
                std::string path;
                ui::menu::dbi::TransportOrigin origin;
            };
            auto ctx = std::make_unique<WorkerContext>(WorkerContext{session, src, path_str, origin});

            Result rc_thread = threadCreate(&s_install_thread, [](void* arg) {
                auto* c = static_cast<WorkerContext*>(arg);
                INSTALL_STATE = InstallState_Progress;
                const auto rc = RunInstall(c->session.get(), c->src.get());

                const auto cur_pkg = c->session->GetCurrentPackageIndex();
                c->session->MarkPackageComplete(cur_pkg, rc);

                const bool user_cancelled = c->session->IsCancelRequested();
                const bool was_cancelled = rc == Result_TransferCancelled || user_cancelled;
                const bool source_interrupted = R_FAILED(rc) && !user_cancelled
                    && (!c->src->m_active || rc == Result_TransferInterrupted || rc == Result_TransferCancelled);

                if (R_FAILED(rc)) {
                    INSTALL_STATE = InstallState_None;
                    c->src->Disable();
                    if (source_interrupted) {
                        c->session->AddLog("Source disconnected: "_i18n + c->path, ui::menu::dbi::LogKind::Warning);
                        App::PlaySoundEffect(SoundEffect_Focus);
                        App::Notify("Install cancelled: the source stopped sending data"_i18n);
                    } else if (rc == Result_TransferCancelled) {
                        c->session->AddLog("Cancelled: "_i18n + c->path, ui::menu::dbi::LogKind::Warning);
                        App::PlaySoundEffect(SoundEffect_Focus);
                        App::Notify("Install cancelled"_i18n);
                    } else {
                        c->session->AddLog("Failed: "_i18n + c->path, ui::menu::dbi::LogKind::Error);
                        App::PlaySoundEffect(SoundEffect_Error);
                        App::PushErrorBox(rc, "Install failed!"_i18n);
                    }
                } else {
                    c->session->AddLog("Installed: "_i18n + c->path, ui::menu::dbi::LogKind::Success);
                    INSTALL_STATE = InstallState_Finished;
                    App::PlaySoundEffect(SoundEffect_Install);
                    switch (c->origin) {
                        case ui::menu::dbi::TransportOrigin::Mtp:
                            App::Notify("MTP install success!"_i18n);
                            break;
                        case ui::menu::dbi::TransportOrigin::Ftp:
                            App::Notify("FTP install success!"_i18n);
                            break;
                        case ui::menu::dbi::TransportOrigin::Web:
                            App::Notify("Web install success!"_i18n);
                            break;
                        default:
                            App::Notify("Install success!"_i18n);
                            break;
                    }
                }

                App::SetAutoSleepDisabled(false);

                const bool needs_mtp_restart = ui::menu::dbi::ShouldRestartMtp(
                    c->origin, was_cancelled || source_interrupted || rc == Result_TransferInterrupted, true);

                {
                    mutexLock(&s_mutex);
                    while (s_callback_count > 0) {
                        condvarWait(&s_callback_cond, &s_mutex);
                    }
                    s_source.reset();
                    s_installing = false;
                    mutexUnlock(&s_mutex);
                }

                if (c->origin == ui::menu::dbi::TransportOrigin::Ftp) {
                    const size_t queued = ftpsrv::HasActiveOrQueuedFiles() ? 1 : 0;
                    if (ui::menu::dbi::CanTransitionToSummary(c->origin, false, false, queued)) {
                        c->session->TransitionToSummary();
                    }
                }

                if (needs_mtp_restart) {
                    c->session->AddLog("Restarting MTP service..."_i18n, ui::menu::dbi::LogKind::Event);
                    if (source_interrupted) {
                        c->session->RequestExit();
                    }
                    ScheduleMtpRestart();
                }

                delete c;
            }, ctx.get(), nullptr, 1024 * 128, PRIO_PREEMPTIVE, 1);

            if (R_SUCCEEDED(rc_thread)) {
                rc_thread = threadStart(&s_install_thread);
                if (R_SUCCEEDED(rc_thread)) {
                    s_install_thread_created = true;
                    ctx.release();
                } else {
                    threadClose(&s_install_thread);
                }
            }

            if (!s_install_thread_created) {
                log_write("[BackgroundInstaller] Failed to create or start worker thread: 0x%x\n", rc_thread);
                src->Disable();
                {
                    mutexLock(&s_mutex);
                    s_source.reset();
                    mutexUnlock(&s_mutex);
                }
                s_installing = false;
                App::SetAutoSleepDisabled(false);
                App::Notify("Install failed: unable to start worker thread."_i18n);
                return;
            }
        }
    }, false);

    return true;
}
 
bool BackgroundInstaller::OnInstallWrite(const void* buf, size_t size) {
    // the installer only reads the ncas it needs, so it finishes before the mtp
    // host has sent the whole file (skipped ncas / trailing padding still come
    // down the wire). once the install is done, silently accept and drop any
    // trailing bytes -- returning false here makes haze fail the WriteFile,
    // which the host reports as "cannot copy" even though the game installed
    // fine. this must be checked before touching s_source, which the done
    // callback has usually already reset by now.
    if (INSTALL_STATE == InstallState_Finished) {
        return true;
    }

    Menu* active = nullptr;
    {
        mutexLock(&s_mutex);
        if (s_active_menu) {
            active = s_active_menu;
            s_callback_count++;
        }
        mutexUnlock(&s_mutex);
    }

    if (active) {
        bool res = active->OnInstallWrite(buf, size);
        mutexLock(&s_mutex);
        s_callback_count--;
        if (s_callback_count == 0) {
            condvarWakeAll(&s_callback_cond);
        }
        mutexUnlock(&s_mutex);
        return res;
    }

    std::shared_ptr<Stream> src;
    {
        mutexLock(&s_mutex);
        src = s_source;
        mutexUnlock(&s_mutex);
    }
    if (!src) return false;
    return src->Push(buf, size);
}
 
void BackgroundInstaller::OnInstallClose() {
    log_write("[BackgroundInstaller::OnInstallClose] inside\n");
    Menu* active = nullptr;
    {
        mutexLock(&s_mutex);
        if (s_active_menu) {
            active = s_active_menu;
            s_callback_count++;
        }
        mutexUnlock(&s_mutex);
    }

    if (active) {
        active->OnInstallClose();
        mutexLock(&s_mutex);
        s_callback_count--;
        if (s_callback_count == 0) {
            condvarWakeAll(&s_callback_cond);
        }
        mutexUnlock(&s_mutex);
        return;
    }

    std::shared_ptr<Stream> src;
    {
        mutexLock(&s_mutex);
        src = s_source;
        mutexUnlock(&s_mutex);
    }
    if (!src) return;

    // don't block the MTP responder thread waiting for the install to
    // finish - see the comment in Menu::OnInstallClose. s_installing
    // already guards OnInstallStart against accepting a new file before
    // this one is done.
    src->Disable();
}
 
} // namespace sphaira::ui::menu::stream

#else

namespace sphaira::ui::menu::stream {

void ScheduleMtpRestart() {}
void BackgroundInstaller::RegisterMtpCallbacks() {}
void BackgroundInstaller::SetActiveMenu(Menu* menu) {}
bool BackgroundInstaller::OnInstallStart(const char* path, ui::menu::dbi::TransportOrigin origin) { return false; }
bool BackgroundInstaller::OnInstallStart(const char* path) { return false; }
bool BackgroundInstaller::OnInstallWrite(const void* buf, size_t size) { return false; }
void BackgroundInstaller::OnInstallClose() {}
void BackgroundInstaller::TeardownWorker() {}

} // namespace sphaira::ui::menu::stream

#endif
