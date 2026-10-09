#include "app_frame_buffer.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "haze_helper.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "nro.hpp"
#include "nxlink.h"
#include "ui/nvg_util.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include "utils/devoptab_curl_thread.hpp"

#include <minIni.h>
#include <usbhsfs.h>
#include <switch.h>

#include <algorithm>
#include <type_traits>
#include <variant>

extern "C" {
    extern u32 __nx_applet_exit_mode;
}

namespace sphaira {

extern App* g_app;

void App::Loop() {
    // adjust these if FPSlocker ever supports different min/max fps.
    constexpr double min_delta    = 1000.0 / 120.0; // 120 fps
    constexpr double max_delta    = 1000.0 / 15.0;  // 15  fps
    constexpr double target_delta = 1000.0 / 60.0;  // 60  fps

    u64 start = armTicksToNs(armGetSystemTick());
    m_delta_time = 1.0;

    bool mtp_initialized = false;

    while (!m_quit && appletMainLoop()) {
        if (!mtp_initialized) {
            mtp_initialized = true;
            // MTP on in settings means "talk to a PC when one is there", not
            // "grab the port as a gadget now". Holding usb:ds with nothing
            // configured is what crashed when a flash drive was plugged in.
            PsmChargerType charger{PsmChargerType_Unconnected};
            psmGetChargerType(&charger);
            // with "PC Install on connect" the port stays free at start-up: the
            // first USB poll asks the PC for an install app (the cable may have
            // been in before Kefir Hub was started) and starts MTP only when no
            // app answers.
            if (App::GetMtpEnable() && charger == PsmChargerType_LowPower && !App::GetUsbInstallOnConnect()) {
                haze::Init();
                ui::menu::stream::BackgroundInstaller::RegisterMtpCallbacks();
            }
            if (!haze::IsRunning() && !usbHsFsGetStatusChangeUserEvent()) {
                if (App::GetWriteProtect()) {
                    usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
                }
                usbHsFsInitialize(1);
                log_write("[USB] host stack up for flash detection\n");
            }
        }
        if (m_widgets.empty()) {
            m_quit = true;
            break;
        }

        ui::gfx::updateHighlightAnimation();

        // fire all events in in a 3ms timeslice
        TimeStamp ts_event;
        const u64 event_timeout = 3;

        // limit events to a max per frame in order to not block for too long.
        while (true) {
            if (ts_event.GetMs() >= event_timeout) {
                log_write("event loop timed-out\n");
                break;
            }

            auto event = evman::pop();
            if (!event.has_value()) {
                break;
            }

            std::visit([this](auto&& arg){
                using T = std::decay_t<decltype(arg)>;
                if constexpr(std::is_same_v<T, evman::LaunchNroEventData>) {
                    log_write("[LaunchNroEventData] got event\n");
                    u64 timestamp = 0;
                    timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp);
                    const auto nro_path = nro_normalise_path(arg.path);

                    // update timestamp
                    ini_putl(nro_path.c_str(), "timestamp", timestamp, App::PLAYLOG_PATH);
                    log_write("updating timestamp for: %s %lu\n", nro_path.c_str(), timestamp);

                    // envSetNextLoad belongs on this thread. nxlink used to call
                    // nro_launch from its worker, which set the next NRO while
                    // Draw/teardown were still running.
                    if (R_FAILED(envSetNextLoad(arg.path.c_str(), arg.argv.c_str()))) {
                        log_write("[LaunchNroEventData] envSetNextLoad failed for %s\n", arg.path.c_str());
                        App::Notify("Failed to launch"_i18n);
                        return;
                    }
                    log_write("set launch with path: %s argv: %s\n", arg.path.c_str(), arg.argv.c_str());

                    // force disable pop-back to main menu.
                    __nx_applet_exit_mode = 0;
                    m_quit = true;
                } else if constexpr(std::is_same_v<T, evman::ExitEventData>) {
                    log_write("[ExitEventData] got event\n");
                    m_quit = true;
                } else if constexpr(std::is_same_v<T, NxlinkCallbackData>) {
                    switch (arg.type) {
                        case NxlinkCallbackType_Connected:
                            log_write("[NxlinkCallbackType_Connected]\n");
                            App::Notify("Nxlink Connected"_i18n);
                            break;
                        case NxlinkCallbackType_WriteBegin:
                            log_write("[NxlinkCallbackType_WriteBegin] %s\n", arg.file.filename);
                            App::Notify("Nxlink Upload"_i18n);
                            break;
                        case NxlinkCallbackType_WriteProgress:
                            // log_write("[NxlinkCallbackType_WriteProgress]\n");
                            break;
                        case NxlinkCallbackType_WriteEnd:
                            log_write("[NxlinkCallbackType_WriteEnd] %s\n", arg.file.filename);
                            App::Notify("Nxlink Finished"_i18n);
                            break;
                    }
                } else if constexpr(std::is_same_v<T, curl::DownloadEventData>) {
                    log_write("[DownloadEventData] got event\n");
                    if (arg.callback && !arg.stoken.stop_requested()) {
                        arg.callback(arg.result);
                    }
                } else if constexpr(std::is_same_v<T, evman::FunctionalEventData>) {
                    log_write("[FunctionalEventData] got event\n");
                    if (arg.callback && !arg.stoken.stop_requested()) {
                        arg.callback();
                    }
                } else {
                    static_assert(false, "non-exhaustive visitor!");
                }
            }, event.value());
        }

        const auto fb = GetFrameBufferSize();
        if (fb.size.x != s_width || fb.size.y != s_height) {
            s_width = fb.size.x;
            s_height = fb.size.y;
            m_scale = fb.scale;
            this->destroyFramebufferResources();
            this->createFramebufferResources();
            renderer->UpdateViewSize(s_width, s_height);
        }

        this->Poll();
        this->Update();
        this->Draw();

        // check how long this frame took.
        const u64 now = armTicksToNs(armGetSystemTick());
        // convert to ns.
        const double delta = (double)(now - start) / 1e+6;
        // clamp and normalise to 1.0 as the target, higher values if we took too long.
        m_delta_time = std::clamp(delta, min_delta, max_delta) / target_delta;
        // save timestamp for next frame.
        start = now;

        LogFrame(delta);
    }
}

// Frame accounting, for "it starts to lag and then recovers".
//
// A once a second summary is enough to see a sustained drop, but a stall is a
// single long frame and an average buries it - so a frame that misses badly is
// reported on its own, immediately. Whatever caused it logs from the same
// frame, so the culprit sits right next to the hitch line.
//
// Both go through log_write, which only appends to a buffer; a background
// thread batches it to the sd every 100ms. Nothing here touches the card.
void App::LogFrame(double delta_ms) {
    if (!log_is_init()) {
        return;
    }

    // a frame this long is a visible stutter, not jitter.
    constexpr double HITCH_MS = 100.0;
    constexpr double REPORT_INTERVAL_MS = 1000.0;
    // nothing we do takes seconds; a frame that long is the console having been
    // asleep or the applet suspended. Saying so beats logging a "hitch" of four
    // minutes, and leaving it out of the accumulators keeps that second's
    // averages readable.
    constexpr double SUSPEND_MS = 2000.0;

    if (delta_ms >= SUSPEND_MS) {
        log_write("[frame] resumed, suspended for %.0f s\n", delta_ms / 1000.0);
        // Draw() has already charged the whole suspend to the vsync wait, and
        // the partial second we fell asleep in is meaningless, so start over.
        m_frame_count = 0;
        m_frame_accum_ms = 0.0;
        m_frame_worst_ms = 0.0;
        m_frame_wait_accum_ms = 0.0;
        return;
    }

    m_frame_count++;
    m_frame_accum_ms += delta_ms;
    m_frame_worst_ms = std::max(m_frame_worst_ms, delta_ms);

    if (delta_ms >= HITCH_MS) {
        log_write("[frame] hitch %.0f ms\n", delta_ms);
    }

    if (m_frame_accum_ms < REPORT_INTERVAL_MS) {
        return;
    }

    // "wait" is the vsync block; subtract it and what is left is the work we
    // actually control. High wait means headroom, near zero means cpu bound.
    log_write("[frame] %.1f fps  avg %.1f ms  work %.1f ms  wait %.1f ms  worst %.0f ms\n",
        1000.0 * m_frame_count / m_frame_accum_ms,
        m_frame_accum_ms / m_frame_count,
        (m_frame_accum_ms - m_frame_wait_accum_ms) / m_frame_count,
        m_frame_wait_accum_ms / m_frame_count,
        m_frame_worst_ms);

    m_frame_count = 0;
    m_frame_accum_ms = 0.0;
    m_frame_worst_ms = 0.0;
    m_frame_wait_accum_ms = 0.0;
}

} // namespace sphaira
