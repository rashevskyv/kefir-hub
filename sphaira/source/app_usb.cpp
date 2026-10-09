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
#include "ui/option_box.hpp"
#include "usb_install_probe.hpp"

#include <usbhsfs.h>
#include <switch.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace sphaira {

extern App* g_app;

void App::PollUsbStorage() {
    bool host_changed = false;
    if (auto* ev = usbHsFsGetStatusChangeUserEvent(); ev) {
        host_changed = R_SUCCEEDED(waitSingle(waiterForUEvent(ev), 0));
    }
    const bool timed = !m_usb_poll_primed || m_usb_poll_ts.GetSeconds() >= 1;
    if (!timed && !host_changed) {
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
                log_write("[USB] install host answered; opening PC Install (USB)\n");
                App::Push<ui::menu::dbi::Menu>(ui::menu::MenuFlag_None, std::move(source), std::move(names));
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
    if (!haze::IsRunning() || usb_probe::GetState() != usb_probe::State::Idle) {
        return;
    }
    if (!g_app->m_widgets.empty() && g_app->m_widgets.back()->IsModal()) {
        return;
    }

    App::Push<ui::OptionBox>("A PC install app is waiting.\nStop MTP and open PC Install (USB)?"_i18n,
        "No"_i18n, "PC Install (USB)"_i18n, 1, [](auto op_index) {
            // the marker is a real file when it was dropped on the memory card.
            fs::FsPath marker;
            std::snprintf(marker, sizeof(marker), "/%s", PC_INSTALL_MARKER);
            fs::FsNativeSd{}.DeleteFile(marker);
            if (!op_index || !*op_index || !haze::IsRunning()) {
                return;
            }
            log_write("[USB] PC install marker: stopping MTP, probing for the install host\n");
            haze::Exit();
            // PollUsbStorage opens PC Install (USB) when the host answers and
            // restarts MTP when nobody does.
            usb_probe::Start();
            if (usb_probe::GetState() == usb_probe::State::Idle) {
                g_app->TryStartAutoMtp();
            }
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

    log_write("[USB] PC host detected; starting MTP\n");
    if (App::GetMtpEnable()) {
        // setting already on; just take the port. haze::Init drops usbhsfs.
        if (haze::Init()) {
            ui::menu::stream::BackgroundInstaller::RegisterMtpCallbacks();
            App::Notify("Computer connected — MTP started"_i18n);
        }
        return;
    }

    const bool restore_hdd = App::GetHddEnable();
    ApplyMtpEnable(true, false);
    if (App::GetMtpEnable()) {
        m_usb_auto_mtp = true;
        m_usb_auto_mtp_restore_hdd = restore_hdd;
        App::Notify("Computer connected — MTP started"_i18n);
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
