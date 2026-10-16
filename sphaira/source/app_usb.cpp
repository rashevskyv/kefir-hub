#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "haze_helper.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "log.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/first_start.hpp"
#include "ui/option_box.hpp"
#include "usb_install_probe.hpp"

#include <usbhsfs.h>
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace sphaira {

extern App* g_app;

namespace {
// "No" to PC Install for this cable: the backend then sees MTP and drops its marker to ask again.
// Its marker is removed without a question until the cable is unplugged.
std::atomic<bool> g_pc_install_declined{false};

// a plain 5 V charger and a PC both report a LowPower charger, so auto MTP starts for both. Only a PC
// configures the USB device; until then say nothing, and stop MTP when nobody answered.
// 20 s covers a first-time Windows MTP driver install; a host that gave the console an address (Address) and is
// still installing its driver keeps it waiting. A charger can hold the port in Default (seen on .141), so not that.
constexpr u64 PC_ANSWER_SECONDS = 20;
// never wait longer than this, whatever the state.
constexpr u64 PC_ANSWER_MAX_SECONDS = 60;
std::atomic<bool> g_mtp_waiting_for_pc{false};
TimeStamp g_mtp_wait_ts{};

void DeletePcInstallMarker() {
    fs::FsPath marker;
    std::snprintf(marker, sizeof(marker), "/%s", App::PC_INSTALL_MARKER);
    fs::FsNativeSd{}.DeleteFile(marker);
}
} // namespace

void App::PollUsbStorage() {
    // the first set-up (language, HOME icon advice) comes before any PC Install window
    // or MTP offer: a PC with a USB install app plugged in must not take the screen.
    if (ui::FirstStartActive()) {
        return;
    }
    bool host_changed = false;
    if (auto* ev = usbHsFsGetStatusChangeUserEvent(); ev) {
        host_changed = R_SUCCEEDED(waitSingle(waiterForUEvent(ev), 0));
    }
    // a finished install probe is picked up on the next frame, not on the next one-second tick.
    const auto probe = usb_probe::GetState();
    const bool probe_done = probe == usb_probe::State::Found || probe == usb_probe::State::NotFound;
    const bool timed = !m_usb_poll_primed || m_usb_poll_ts.GetSeconds() >= 1;
    if (!timed && !host_changed && !probe_done) {
        return;
    }
    if (timed) {
        m_usb_poll_ts.Update();
    }

    // a PC was plugged in and the install probe is asking it whether a USB
    // install app is waiting. Nothing else touches the port until it answers.
    switch (usb_probe::GetState()) {
        case usb_probe::State::Running:
            return;
        case usb_probe::State::Found:
        case usb_probe::State::NotFound: {
            std::unique_ptr<yati::source::Usb> source;
            std::vector<std::string> names;
            usb_probe::Finish(source, names);
#if ENABLE_NETWORK_INSTALL
            if (source) {
                // the PC answered: ask before taking the screen. "No" frees the port and starts MTP.
                log_write("[USB] install host answered; asking before PC Install (USB)\n");
                auto found = std::make_shared<std::pair<std::unique_ptr<yati::source::Usb>, std::vector<std::string>>>(
                    std::move(source), std::move(names));
                App::Push<ui::OptionBox>("A PC install app is waiting.\nOpen PC Install (USB)?"_i18n,
                    "No"_i18n, "PC Install (USB)"_i18n, 1, [found](auto op_index) {
                        if (op_index && *op_index) {
                            App::ConfirmInstall([found]() {
                                App::Push<ui::menu::dbi::Menu>(ui::menu::MenuFlag_None, std::move(found->first), std::move(found->second));
                            });
                            return;
                        }
                        found->first.reset();
                        g_pc_install_declined = true;
                        g_app->TryStartAutoMtp();
                    });
                return;
            }
#endif
            PsmChargerType still{PsmChargerType_Unconnected};
            psmGetChargerType(&still);
            if (still != PsmChargerType_Unconnected) {
                TryStartAutoMtp();
            }
            return;
        }
        case usb_probe::State::Idle:
            break;
    }

    PsmChargerType charger{PsmChargerType_Unconnected};
    psmGetChargerType(&charger);

    UsbState usb_state{};
    const bool usbds_up = R_SUCCEEDED(usbDsGetState(&usb_state));
    const bool pc_enumerated = usbds_up && usb_state == UsbState_Configured;
    if (pc_enumerated && haze::IsRecovering()) {
        haze::ClearRecovering();
        if (!haze::IsRecovering()) {
            log_write("[USB] MTP re-enumerated in device mode after a local abort\n");
        }
    }
    if (g_mtp_waiting_for_pc && !haze::IsRunning()) {
        g_mtp_waiting_for_pc = false;
    } else if (g_mtp_waiting_for_pc && pc_enumerated) {
        g_mtp_waiting_for_pc = false;
        log_write("[USB] PC answered over MTP\n");
        App::Notify("Computer connected — MTP started"_i18n);
    } else if (g_mtp_waiting_for_pc && g_mtp_wait_ts.GetSeconds() >= PC_ANSWER_SECONDS
               && (usb_state != UsbState_Address
                   || g_mtp_wait_ts.GetSeconds() >= PC_ANSWER_MAX_SECONDS)) {
        g_mtp_waiting_for_pc = false;
        log_write("[USB] no PC answered in %lus (state %u): a charger only; stopping MTP\n",
            g_mtp_wait_ts.GetSeconds(), static_cast<unsigned>(usb_state));
        if (m_usb_auto_mtp) {
            RestoreUsbAfterAutoMtp();
        } else {
            haze::Exit();
        }
    }

    // USB install owns usb:ds without haze while a PC is sending files.
    if (usbds_up && !haze::IsRunning() && pc_enumerated) {
        return;
    }

    // PC unplugged: usb:ds drops off Configured even if VBUS lags. Free the
    // port immediately so a flash drive can enumerate as a host device.
    // Controlled MTP recovery tears down usb:ds briefly to force Windows
    // to discard dead transfer data; do not treat this intentional Detached
    // as a physical disconnect unless the cable is genuinely unplugged.
    const bool is_recovering = haze::IsRecovering();
    const bool genuinely_unplugged = (charger == PsmChargerType_Unconnected);
    if (haze::IsRunning() && (!is_recovering || genuinely_unplugged) && (!usbds_up || usb_state == UsbState_Detached)) {
        log_write("[USB] MTP session gone (state=%u recovering=%d unplugged=%d); releasing port to host\n",
            static_cast<unsigned>(usb_state), is_recovering ? 1 : 0, genuinely_unplugged ? 1 : 0);
        haze::Exit();
    }

    const bool host_up = usbHsFsGetStatusChangeUserEvent();
    const bool handheld = appletGetOperationMode() != AppletOperationMode_Console;

    std::vector<location::StdioEntry> devices;
    if (host_up && !haze::IsRunning()) {
        devices = location::GetStdio(false);
    }
    std::vector<std::string> mounts;
    mounts.reserve(devices.size());
    for (const auto& d : devices) {
        mounts.push_back(d.mount);
    }

    if (!m_usb_poll_primed) {
        m_usb_mounts = std::move(mounts);
        m_usb_charger = charger;
        m_usb_poll_primed = true;
        if (charger == PsmChargerType_LowPower
            && m_usb_mounts.empty()
            && !haze::IsRunning() && handheld) {
            m_usb_pending_mtp = true;
            m_usb_pending_mtp_ts.Update();
        }
        return;
    }

    for (const auto& d : devices) {
        if (std::ranges::find(m_usb_mounts, d.mount) == m_usb_mounts.end()) {
            log_write("[USB] mass-storage %s vid/pid via %s\n", d.name.c_str(), d.mount.c_str());
            m_usb_pending_mtp = false;
            OfferOpenUsbDrive(d.name, d.mount, d.flags);
        }
    }
    for (const auto& old : m_usb_mounts) {
        if (std::ranges::find(mounts, old) == mounts.end()) {
            log_write("[USB] mass-storage removed %s\n", old.c_str());
            App::Notify("USB drive removed"_i18n);
            CloseFileBrowsersOnUsbMount(old);
        }
    }
    m_usb_mounts = std::move(mounts);

    if (charger != m_usb_charger) {
        log_write("[USB] charger %u -> %u (mounted=%zu haze=%d configured=%d)\n",
            static_cast<unsigned>(m_usb_charger), static_cast<unsigned>(charger),
            m_usb_mounts.size(), haze::IsRunning() ? 1 : 0, pc_enumerated ? 1 : 0);
        if (charger == PsmChargerType_Unconnected) {
            m_usb_pending_mtp = false;
            g_pc_install_declined = false;
            if (m_usb_auto_mtp) {
                RestoreUsbAfterAutoMtp();
            } else if (haze::IsRunning() && !pc_enumerated) {
                log_write("[USB] MTP idle with no PC; releasing port to host\n");
                haze::Exit();
            }
        } else if (charger == PsmChargerType_LowPower
            && m_usb_mounts.empty()
            && !haze::IsRunning() && handheld) {
            m_usb_pending_mtp = true;
            m_usb_pending_mtp_ts.Update();
        } else {
            m_usb_pending_mtp = false;
        }
        m_usb_charger = charger;
    }

    if (m_usb_pending_mtp) {
        if (!m_usb_mounts.empty() || haze::IsRunning()) {
            m_usb_pending_mtp = false;
        } else if (m_usb_pending_mtp_ts.GetSeconds() >= 2) {
            m_usb_pending_mtp = false;
            // ask for a USB install app first; MTP only when nobody answers
            // (or in a build without network install, where Start() is a no-op).
            if (App::GetUsbInstallOnConnect()) {
                usb_probe::Start();
            }
            if (usb_probe::GetState() == usb_probe::State::Idle) {
                TryStartAutoMtp();
            }
        }
    }
}

void App::OfferOpenUsbDrive(std::string name, std::string mount, u32 flags) {
    if (!m_widgets.empty()) {
        m_widgets.back()->OnFocusGained();
    }

    const std::string message = name.empty()
        ? "USB drive connected"_i18n
        : "USB drive connected"_i18n + ":\n" + name;

    if (!m_widgets.empty() && m_widgets.back()->IsModal()) {
        App::Notify(message);
        return;
    }

    App::Push<ui::OptionBox>(message, "No"_i18n, "Open in file browser"_i18n, 1,
        [mount, name, flags](auto op_index) {
            if (!op_index || !*op_index || mount.empty()) {
                return;
            }
            const ui::menu::filebrowser::FsEntry entry{
                name, mount, ui::menu::filebrowser::FsType::Stdio, flags};
            App::Push<ui::menu::filebrowser::Menu>(ui::menu::MenuFlag_None, entry, mount);
        });
}

void App::OfferPcInstallSwitch() {
    if (ui::FirstStartActive()) {
        return;
    }
    if (!haze::IsRunning() || usb_probe::GetState() != usb_probe::State::Idle) {
        return;
    }
    if (g_pc_install_declined) {
        log_write("[USB] PC install marker ignored: declined for this cable\n");
        DeletePcInstallMarker();
        return;
    }
    if (!g_app->m_widgets.empty() && g_app->m_widgets.back()->IsModal()) {
        return;
    }

    App::Push<ui::OptionBox>("A PC install app is waiting.\nStop MTP and open PC Install (USB)?"_i18n,
        "No"_i18n, "PC Install (USB)"_i18n, 1, [](auto op_index) {
            // the marker is a real file when it was dropped on the memory card.
            DeletePcInstallMarker();
            if (!op_index || !*op_index) {
                g_pc_install_declined = true;
                return;
            }
            if (!haze::IsRunning()) {
                return;
            }
            // the PC asked for this switch, so its install app is waiting: open PC Install (USB) at once.
            // That screen waits for the PC itself and fills the queue when the list arrives; a probe
            // first only added its own wait plus up to a second of the USB poll.
            App::ConfirmInstall([]() {
                log_write("[USB] PC install marker: stopping MTP, opening PC Install (USB)\n");
                haze::Exit();
                App::Push<ui::menu::dbi::Menu>(ui::menu::MenuFlag_None);
            });
        });
}

void App::CloseFileBrowsersOnUsbMount(const std::string& mount) {
    for (auto& w : m_widgets) {
        w->OnUsbMountRemoved(mount);
    }

    const auto first_pop = std::ranges::find_if(m_widgets, [](const auto& w) {
        return w && w->ShouldPop();
    });
    if (first_pop != m_widgets.end()) {
        const auto count = static_cast<size_t>(std::distance(first_pop, m_widgets.end()));
        for (auto it = first_pop; it != m_widgets.end(); ++it) {
            if (*it) {
                (*it)->SetPop();
            }
        }
        log_write("[USB] closing %zu widget(s) for removed mount %s\n", count, mount.c_str());
    }
}

void App::TryStartAutoMtp() {
    if (haze::IsRunning()) {
        return;
    }
    if (appletGetOperationMode() == AppletOperationMode_Console) {
        return;
    }

    UsbState usb_state{};
    if (R_SUCCEEDED(usbDsGetState(&usb_state))) {
        log_write("[USB] skip auto MTP; usbDs already active (state %u)\n",
            static_cast<unsigned>(usb_state));
        return;
    }

    log_write("[USB] USB power detected; starting MTP and waiting for a PC\n");
    if (App::GetMtpEnable()) {
        // setting already on; just take the port. haze::Init drops usbhsfs.
        if (haze::Init()) {
            ui::menu::stream::BackgroundInstaller::RegisterMtpCallbacks();
            g_mtp_waiting_for_pc = true;
            g_mtp_wait_ts.Update();
        }
        return;
    }

    const bool restore_hdd = App::GetHddEnable();
    ApplyMtpEnable(true, false);
    if (App::GetMtpEnable()) {
        m_usb_auto_mtp = true;
        m_usb_auto_mtp_restore_hdd = restore_hdd;
        g_mtp_waiting_for_pc = true;
        g_mtp_wait_ts.Update();
    }
}

void App::RestoreUsbAfterAutoMtp() {
    m_usb_auto_mtp = false;
    const bool restore_hdd = m_usb_auto_mtp_restore_hdd;
    m_usb_auto_mtp_restore_hdd = false;
    log_write("[USB] PC unplugged; stopping auto MTP (restore hdd=%d)\n", restore_hdd);
    if (App::GetMtpEnable()) {
        ApplyMtpEnable(false, false);
    }
    if (restore_hdd && !App::GetHddEnable()) {
        App::SetHddEnable(true);
    }
}

} // namespace sphaira
