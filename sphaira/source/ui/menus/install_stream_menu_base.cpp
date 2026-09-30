#include "ui/menus/install_stream_menu_base.hpp"

#if ENABLE_NETWORK_INSTALL
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "haze_helper.hpp"
#include "ftpsrv_helper.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/dbi/install_queue_state.hpp"
#include "evman.hpp"
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::stream {

std::atomic<int> INSTALL_STATE{InstallState_None};

std::shared_ptr<Stream> BackgroundInstaller::s_source{nullptr};
std::stop_source BackgroundInstaller::s_stop_source{};
std::atomic<bool> BackgroundInstaller::s_installing{false};
Mutex BackgroundInstaller::s_mutex{};

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

static std::atomic<bool> s_restart_scheduled{false};

void ScheduleMtpRestart() {
    if (App::IsExiting()) {
        return;
    }
    if (s_restart_scheduled.exchange(true)) {
        return;
    }
    evman::push(evman::FunctionalEventData{
        []() {
            s_restart_scheduled.store(false);
            if (App::IsExiting()) {
                return;
            }
            log_write("[MTP] Restarting haze after source interruption\n");
            BackgroundInstaller::TeardownWorker();
            if (haze::IsRunning()) {
                haze::Exit(false);
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

                // A user-cancelled PTP transaction stays on the same USB connection.
                // Restart MTP only when the source failed independently.
                const bool needs_mtp_restart = ui::menu::dbi::ShouldRestartMtp(
                    c->origin, source_interrupted, true);

                {
                    mutexLock(&s_mutex);
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

                if (needs_mtp_restart && !App::IsExiting()) {
                    c->session->AddLog("Restarting MTP service..."_i18n, ui::menu::dbi::LogKind::Event);
                    c->session->RequestExit();
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

    std::shared_ptr<Stream> src;
    {
        mutexLock(&s_mutex);
        src = s_source;
        mutexUnlock(&s_mutex);
    }
    if (!src) return;

    // don't block the transport callback waiting for the install to finish;
    // s_installing already guards OnInstallStart against accepting a new file
    // before the current one is done.
    src->Disable();
}

} // namespace sphaira::ui::menu::stream

#else

namespace sphaira::ui::menu::stream {

void ScheduleMtpRestart() {}
void BackgroundInstaller::RegisterMtpCallbacks() {}
bool BackgroundInstaller::OnInstallStart(const char* path, ui::menu::dbi::TransportOrigin origin) { return false; }
bool BackgroundInstaller::OnInstallStart(const char* path) { return false; }
bool BackgroundInstaller::OnInstallWrite(const void* buf, size_t size) { return false; }
void BackgroundInstaller::OnInstallClose() {}
void BackgroundInstaller::TeardownWorker() {}

} // namespace sphaira::ui::menu::stream

#endif
