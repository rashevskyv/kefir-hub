#include "keys_dump.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "orphan_content.hpp"
#include "ui/error_box.hpp"
#include "ui/install_queue.hpp"
#include "ui/option_box.hpp"
#include "utils/utils.hpp"
#if ENABLE_NETWORK_INSTALL
#include "ui/menus/install_stream_menu_base.hpp"
#endif

#include <atomic>

namespace sphaira::keys_dump {
namespace {

// the paths Kefir's hekate (patch 0001 autokeys) checks before it starts Lockpick.
constexpr const char* LOCKPICK_PATH = "/bootloader/payloads/Lockpick_RCM.bin";
constexpr const char* AUTOKEYS_STATE_PATH = "/config/autokeys.bin";
constexpr const char* AUTOKEYS_DISABLE_PATH = "/config/autokeys.disable";

// MTP and FTP send one file at a time with no "batch done" signal: wait for quiet (as orphan_content does).
constexpr u64 QUIET_NS = 5'000'000'000ULL;

std::atomic<u64> g_last_tick{0}; // 0 = no keys error waiting for the question

bool IsKeysError(Result rc) {
    switch (rc) {
        case Result_KeyMissingNcaKeyArea:
        case Result_KeyMissingTitleKek:
        case Result_KeyMissingMasterKey:
        case Result_KeyFailedDecyptETicketDeviceKey:
            return true;
        default:
            return false;
    }
}

bool Busy(u64 last) {
#if ENABLE_NETWORK_INSTALL
    if (ui::menu::stream::BackgroundInstaller::IsInstalling()) {
        g_last_tick = armGetSystemTick();
        return true;
    }
#endif
    return orphan_content::InstallRunning() || ui::InstallQueue::Get().IsBusy() || App::GetProgressActive() ||
           ui::ErrorBoxShown() || armTicksToNs(armGetSystemTick() - last) < QUIET_NS;
}

void RebootToDump() {
    fs::FsNativeSd sd;
    // no record of the last dump = hekate starts Lockpick on the next autoboot. A missing file is fine.
    sd.DeleteFile(AUTOKEYS_STATE_PATH);
    sd.Commit();
    log_write("[keys] autokeys record removed, rebooting for a key dump\n");
    const Result rc = utils::requestForcedReboot();
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to reboot"_i18n);
    }
}

} // namespace

void NoteInstallResult(std::uint32_t rc) {
    if (IsKeysError(rc)) {
        g_last_tick = armGetSystemTick();
    }
}

void Poll() {
    const auto last = g_last_tick.load();
    if (!last) {
        return;
    }
    if (Busy(last)) {
        return;
    }
    fs::FsNativeSd sd;
    const auto offer = Decide(true, false, sd.FileExists(LOCKPICK_PATH), sd.FileExists(AUTOKEYS_DISABLE_PATH));
    g_last_tick = 0;

    switch (offer) {
        case Offer::NoPayload:
            App::Push<ui::OptionBox>("Lockpick_RCM.bin is not in /bootloader/payloads, so the Hub cannot dump the keys. Copy it there, then install again."_i18n, "OK"_i18n);
            break;
        case Offer::Disabled:
            App::Push<ui::OptionBox>("Automatic key dump is turned off: /config/autokeys.disable is on the microSD. Delete it to dump the keys."_i18n, "OK"_i18n);
            break;
        case Offer::Ask:
            App::Push<ui::OptionBox>(
                "Reboot and dump the keys?\nThe console restarts, Lockpick_RCM saves new keys, then the console starts as usual. After that, install the game again."_i18n,
                "No"_i18n, "Reboot"_i18n, 1, [](auto op_index) {
                    if (op_index && *op_index == 1) {
                        RebootToDump();
                    }
                });
            break;
        default:
            break;
    }
}

} // namespace sphaira::keys_dump
