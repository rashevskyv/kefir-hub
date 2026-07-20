#include "ui/menus/install_stream_menu_base.hpp"

#if ENABLE_NETWORK_INSTALL
#include "yati/yati.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "haze_helper.hpp"
#include "evman.hpp"
#include <cstring>
 
namespace sphaira::ui::menu::stream {
 
std::atomic<int> INSTALL_STATE{InstallState_None};
 
Menu* BackgroundInstaller::s_active_menu{nullptr};
std::shared_ptr<Stream> BackgroundInstaller::s_source{nullptr};
std::stop_source BackgroundInstaller::s_stop_source{};
std::atomic<bool> BackgroundInstaller::s_installing{false};
Mutex BackgroundInstaller::s_mutex{};
CondVar BackgroundInstaller::s_callback_cond{};
std::atomic<int> BackgroundInstaller::s_callback_count{0};
 
namespace {
 
constexpr u64 MAX_BUFFER_SIZE = 1024ULL*1024ULL*8ULL;
constexpr u64 MAX_BUFFER_RESERVE_SIZE = 1024ULL*1024ULL*32ULL;
 
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

    mutexInit(&m_mutex);
    condvarInit(&m_can_read);
    condvarInit(&m_can_write);
}

Result Stream::ReadChunk(void* buf, s64 size, u64* bytes_read) {
    log_write("[Stream::ReadChunk] inside\n");
    ON_SCOPE_EXIT(
        log_write("[Stream::ReadChunk] exiting\n");
    );

    while (!m_token.stop_requested()) {
        SCOPED_MUTEX(&m_mutex);
        if (m_active && m_buffer.empty()) {
            log_write("[Stream::ReadChunk] buffer empty, waiting on m_can_read\n");
            R_TRY(condvarWait(std::addressof(m_can_read), std::addressof(m_mutex)));
            log_write("[Stream::ReadChunk] woke up from m_can_read, size=%zu, active=%d\n", m_buffer.size(), m_active.load());
        }

        if (m_token.stop_requested()) {
            log_write("[Stream::ReadChunk] stop requested, breaking\n");
            break;
        }

        if (!m_active && m_buffer.empty()) {
            log_write("[Stream::ReadChunk] inactive and empty buffer, returning EOF\n");
            *bytes_read = 0;
            return 0;
        }

        // spurious wakeup with no data yet, wait again rather than
        // returning a zero-byte read (treated as eof by the caller).
        if (m_buffer.empty()) {
            continue;
        }

        size = std::min<s64>(size, m_buffer.size());
        std::memcpy(buf, m_buffer.data(), size);
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + size);
        *bytes_read = size;
        return condvarWakeOne(&m_can_write);
    }

    log_write("[Stream::ReadChunk] failed to read/cancelled\n");
    R_THROW(Result_TransferCancelled);
}

bool Stream::Push(const void* buf, s64 size) {
    log_write("[Stream::Push] inside\n");
    ON_SCOPE_EXIT(
        log_write("[Stream::Push] exiting\n");
    );

    while (!m_token.stop_requested()) {
        if (INSTALL_STATE == InstallState_Finished) {
            log_write("[Stream::Push] install has finished\n");
            return true;
        }

        SCOPED_MUTEX(&m_mutex);
        #if USE_CONDI_VAR
        while (m_active && m_buffer.size() >= MAX_BUFFER_SIZE) {
            log_write("[Stream::Push] buffer full (%zu >= %llu), waiting on m_can_write...\n", m_buffer.size(), MAX_BUFFER_SIZE);
            R_TRY(condvarWait(std::addressof(m_can_write), std::addressof(m_mutex)));
            log_write("[Stream::Push] woke up from m_can_write, size=%zu\n", m_buffer.size());
        }
        #else
        if (m_active && m_buffer.size() >= MAX_BUFFER_SIZE) {
            // unlock the mutex and wait for 1s to bring transfer speed down to 1MiB/s.
            log_write("[Stream::Push] buffer is full, delaying\n");
            mutexUnlock(&m_mutex);
            ON_SCOPE_EXIT(mutexLock(&m_mutex));

            svcSleepThread(1e+9);
        }
        #endif

        if (!m_active) {
            log_write("[Stream::Push] file not active\n");
            break;
        }

        const auto offset = m_buffer.size();
        m_buffer.resize(offset + size);
        std::memcpy(m_buffer.data() + offset, buf, size);
        condvarWakeOne(&m_can_read);
        return true;
    }

    log_write("[Stream::Push] failed to push\n");
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
            const auto rc = yati::InstallFromSource(pbox, m_source.get(), m_source->GetPath());
            INSTALL_STATE = InstallState_Finished;

            if (R_FAILED(rc)) {
                m_source->Disable();
                R_THROW(rc);
            }

            R_SUCCEED();
        }, [this](Result rc){
            App::PushErrorBox(rc, "Install failed!"_i18n);

            SCOPED_MUTEX(&m_mutex);

            if (R_SUCCEEDED(rc)) {
                App::Notify("Install success!"_i18n);
                m_state = State::Done;
            } else {
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
}
 
void BackgroundInstaller::RegisterMtpCallbacks() {
    static bool initialized = false;
    if (!initialized) {
        mutexInit(&s_mutex);
        condvarInit(&s_callback_cond);
        initialized = true;
    }

    haze::InitInstallMode(
        [](const char* path) { return OnInstallStart(path); },
        [](const void* buf, size_t size) { return OnInstallWrite(buf, size); },
        []() { OnInstallClose(); }
    );
}
 
bool BackgroundInstaller::OnInstallStart(const char* path) {
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
 
    if (App::GetProgressActive() || s_installing) {
        log_write("[BackgroundInstaller] Already installing, rejecting start\n");
        evman::push(evman::FunctionalEventData {
            []() {
                App::Notify("MTP Install failed: another installation is in progress."_i18n);
            }
        }, false);
        return false;
    }
 
    const char* ext = std::strrchr(path, '.');
    if (!ext) return false;
    bool valid_ext = false;
    static const char* SUPPORTED_EXT[] = { ".nsp", ".xci", ".nsz", ".xcz" };
    for (const auto& supported : SUPPORTED_EXT) {
        if (strcasecmp(ext, supported) == 0) {
            valid_ext = true;
            break;
        }
    }
    if (!valid_ext) return false;
 
    s_installing = true;
    s_stop_source = std::stop_source();
    {
        mutexLock(&s_mutex);
        s_source = std::make_shared<Stream>(path, s_stop_source.get_token());
        mutexUnlock(&s_mutex);
    }
    INSTALL_STATE = InstallState_None;
 
    evman::push(evman::FunctionalEventData {
        [path_str = std::string(path)]() {
            log_write("[BackgroundInstaller] UI event triggered, creating ProgressBox\n");
            App::SetAutoSleepDisabled(true);
            // detached: doesn't block the widget stack, so the user can keep
            // navigating menus while this install runs. minimise/expand via L3.
            App::PushTransfer(std::make_unique<ui::ProgressBox>(0, "Installing "_i18n, path_str, [](auto pbox) -> Result {
                INSTALL_STATE = InstallState_Progress;
                std::shared_ptr<Stream> src;
                {
                    mutexLock(&s_mutex);
                    src = s_source;
                    mutexUnlock(&s_mutex);
                }
                if (!src) R_THROW(Result_TransferCancelled);
                const auto rc = yati::InstallFromSource(pbox, src.get(), src->GetPath());
                INSTALL_STATE = InstallState_Finished;
                if (R_FAILED(rc)) {
                    src->Disable();
                    R_THROW(rc);
                }
                R_SUCCEED();
            }, [](Result rc) {
                App::SetAutoSleepDisabled(false);
                if (R_SUCCEEDED(rc)) {
                    App::PlaySoundEffect(SoundEffect_Install);
                    App::Notify("Install success!"_i18n);
                } else {
                    App::PlaySoundEffect(SoundEffect_Error);
                    App::PushErrorBox(rc, "Install failed!"_i18n);
                }
                {
                    mutexLock(&s_mutex);
                    s_source.reset();
                    mutexUnlock(&s_mutex);
                }
                s_installing = false;
            }));
        }
    }, false);
 
    return true;
}
 
bool BackgroundInstaller::OnInstallWrite(const void* buf, size_t size) {
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

void BackgroundInstaller::RegisterMtpCallbacks() {}
void BackgroundInstaller::SetActiveMenu(Menu* menu) {}
bool BackgroundInstaller::OnInstallStart(const char* path) { return false; }
bool BackgroundInstaller::OnInstallWrite(const void* buf, size_t size) { return false; }
void BackgroundInstaller::OnInstallClose() {}

} // namespace sphaira::ui::menu::stream

#endif
