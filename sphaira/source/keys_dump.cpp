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
#include "yati/nx/crypto.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#if ENABLE_NETWORK_INSTALL
#include "ui/menus/install_stream_menu_base.hpp"
#endif

#include <atomic>
#include <cstring>
#include <optional>

namespace sphaira::keys_dump {
namespace {

// the paths Kefir's hekate (patch 0001 autokeys) checks before it starts Lockpick.
constexpr const char* LOCKPICK_PATH = "/bootloader/payloads/Lockpick_RCM.bin";
constexpr const char* AUTOKEYS_STATE_PATH = "/config/autokeys.bin";
constexpr const char* AUTOKEYS_DISABLE_PATH = "/config/autokeys.disable";
constexpr const char* PROD_KEYS_PATH = "/switch/prod.keys";
// written before the reboot for a dump; the next start says whether the dump helped.
constexpr const char* DUMP_MARKER_PATH = "/config/kefir/keys_dump_requested";

// same value as in nca.cpp: the key area key source of applications.
constexpr u8 KEY_AREA_KEY_APPLICATION_SOURCE[0x10] = {
    0x7F, 0x59, 0x97, 0x1E, 0x62, 0x9F, 0x36, 0xA1, 0x30, 0x98, 0x06, 0x6F, 0x21, 0x44, 0xC3, 0x0D
};
// any block: the file key and the console's key engine must decrypt it to the same bytes.
constexpr u8 PROBE_BLOCK[0x10] = {
    0x4B, 0x65, 0x66, 0x69, 0x72, 0x20, 0x48, 0x75, 0x62, 0x20, 0x6B, 0x65, 0x79, 0x73, 0x00, 0x01
};

// MTP and FTP send one file at a time with no "batch done" signal: wait for quiet (as orphan_content does).
constexpr u64 QUIET_NS = 5'000'000'000ULL;

std::atomic<u64> g_last_tick{0}; // 0 = no keys error waiting for the question
std::atomic<bool> g_keys_bad{false};
StartupNotice g_startup{StartupNotice::None}; // main thread only
bool g_asked{false};                           // main thread only: the question was shown in this start

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

bool Busy() {
#if ENABLE_NETWORK_INSTALL
    if (ui::menu::stream::BackgroundInstaller::IsInstalling()) {
        if (g_last_tick.load()) {
            g_last_tick = armGetSystemTick();
        }
        return true;
    }
#endif
    return orphan_content::InstallRunning() || ui::InstallQueue::Get().IsBusy() || App::GetProgressActive() ||
           ui::ErrorBoxShown();
}

// the file's key area key against the console's key engine (as nca::DecryptKeak does with a real NCA).
// nullopt = the key engine failed: no verdict.
std::optional<bool> KeyAreaKeyMatches(const keys::Keys& k, u8 key_gen) {
    keys::KeyEntry file_key;
    if (R_FAILED(k.GetNcaKeyArea(&file_key, key_gen, nca::KeyAreaEncryptionKeyIndex_Application))) {
        return false;
    }
    if (R_FAILED(splCryptoInitialize())) {
        return std::nullopt;
    }
    ON_SCOPE_EXIT(splCryptoExit());

    keys::KeyEntry kek;
    u8 by_console[0x10]{};
    if (R_FAILED(splCryptoGenerateAesKek(KEY_AREA_KEY_APPLICATION_SOURCE, key_gen, 0, &kek)) ||
        R_FAILED(splCryptoGenerateAesKey(&kek, PROBE_BLOCK, by_console))) {
        return std::nullopt;
    }
    u8 by_file[0x10]{};
    crypto::cryptoAes128(PROBE_BLOCK, by_file, &file_key, false);
    return std::memcmp(by_console, by_file, sizeof(by_file)) == 0;
}

KeysState CheckKeys() {
    // no Atmosphere (an emulator) = no key engine to ask and no way to dump: say nothing.
    u64 exo{};
    if (R_FAILED(splInitialize())) {
        return KeysState::Unknown;
    }
    const Result exo_rc = splGetConfig(static_cast<SplConfigItem>(65000), &exo);
    splExit();
    const u8 master = (exo >> 32) & 0xFF;
    if (R_FAILED(exo_rc) || master >= 0x1F) {
        log_write("[keys] check skipped: exosphere 0x%X master %u\n", exo_rc, master);
        return KeysState::Unknown;
    }
    // NCA key generation: 0 and 1 both mean master key 0 (keys::Keys::FixKey, spl).
    const u8 key_gen = master + 1;

    fs::FsNativeSd sd;
    const bool file_found = sd.FileExists(PROD_KEYS_PATH);
    if (!file_found) {
        log_write("[keys] %s is not on the microSD\n", PROD_KEYS_PATH);
        return Classify(true, false, false, false, false, false);
    }

    keys::Keys k;
    const Result rc = keys::parse_keys(k, true);
    const bool other_console = rc == Result_KeyFailedDecyptETicketDeviceKey;
    if (R_FAILED(rc) && !other_console) {
        log_write("[keys] check skipped: parse 0x%X\n", rc);
        return KeysState::Unknown;
    }

    const auto app = nca::KeyAreaEncryptionKeyIndex_Application;
    const bool current_found = k.HasNcaKeyArea(key_gen, app) && k.HasTitleKek(key_gen) && k.HasMasterKey(key_gen);
    bool matches = true;
    if (k.HasNcaKeyArea(key_gen, app)) {
        const auto m = KeyAreaKeyMatches(k, key_gen);
        if (!m) {
            log_write("[keys] check skipped: key engine failed\n");
            return KeysState::Unknown;
        }
        matches = *m;
    }
    const bool eticket_found = k.eticket_rsa_kek.IsValid();

    const auto state = Classify(true, true, other_console, current_found, matches, eticket_found);
    log_write("[keys] master %u: area %d titlekek %d master %d match %d eticket %d other console %d -> state %d\n",
              master, k.HasNcaKeyArea(key_gen, app), k.HasTitleKek(key_gen), k.HasMasterKey(key_gen), matches,
              eticket_found, other_console, static_cast<int>(state));
    return state;
}

void RebootToDump() {
    fs::FsNativeSd sd;
    // no record of the last dump = hekate starts Lockpick on the next autoboot. A missing file is fine.
    sd.DeleteFile(AUTOKEYS_STATE_PATH);
    sd.CreateFile(DUMP_MARKER_PATH);
    sd.Commit();
    log_write("[keys] autokeys record removed, rebooting for a key dump\n");
    const Result rc = utils::requestForcedReboot();
    if (R_FAILED(rc)) {
        sd.DeleteFile(DUMP_MARKER_PATH);
        App::PushErrorBox(rc, "Failed to reboot"_i18n);
    }
}

// one question per start: after "Not now" the Hub does not ask again until the next start.
void AskToDump() {
    if (g_asked) {
        return;
    }
    g_asked = true;

    const std::string problem = "The console keys are missing, outdated or do not fit this console. Without them Kefir Hub cannot dump games (the Games folder over MTP stays empty) and cannot install some games."_i18n;
    fs::FsNativeSd sd;
    switch (Decide(true, false, sd.FileExists(LOCKPICK_PATH), sd.FileExists(AUTOKEYS_DISABLE_PATH))) {
        case Offer::NoPayload:
            App::Push<ui::OptionBox>(problem + "\n\n" + "Lockpick_RCM.bin is not in /bootloader/payloads, so the Hub cannot dump the keys. Copy it there, then install again."_i18n, "OK"_i18n);
            break;
        case Offer::Disabled:
            App::Push<ui::OptionBox>(problem + "\n\n" + "Automatic key dump is turned off: /config/autokeys.disable is on the microSD. Delete it to dump the keys."_i18n, "OK"_i18n);
            break;
        case Offer::Ask:
            App::Push<ui::OptionBox>(
                problem + "\n\n" + "Dump the keys now? The console restarts, saves new keys and starts again by itself. You do not need to press anything."_i18n,
                "Not now"_i18n, "Yes, dump the keys"_i18n, 1, [](auto op_index) {
                    if (op_index && *op_index == 1) {
                        RebootToDump();
                    }
                });
            break;
        default:
            break;
    }
}

void ShowStartupNotice() {
    const auto notice = g_startup;
    g_startup = StartupNotice::None;
    switch (notice) {
        case StartupNotice::Ask:
            AskToDump();
            break;
        case StartupNotice::Dumped:
            App::Push<ui::OptionBox>("The keys are dumped. Game dumps and installs work now."_i18n, "OK"_i18n);
            break;
        case StartupNotice::StillBad:
            g_asked = true;
            App::Push<ui::OptionBox>("The keys are still missing or wrong after the dump. Update Kefir or put the latest Lockpick_RCM.bin in /bootloader/payloads, then start Kefir Hub again."_i18n, "OK"_i18n);
            break;
        default:
            break;
    }
}

} // namespace

void CheckAtStartup() {
    const auto state = CheckKeys();
    g_keys_bad = IsBad(state);

    fs::FsNativeSd sd;
    const bool dump_requested = sd.FileExists(DUMP_MARKER_PATH);
    if (dump_requested) {
        sd.DeleteFile(DUMP_MARKER_PATH);
        sd.Commit();
    }
    g_startup = DecideStartup(dump_requested, state);
    log_write("[keys] start-up: state %d, dump requested %d, notice %d\n", static_cast<int>(state), dump_requested,
              static_cast<int>(g_startup));
}

bool KeysBad() {
    return g_keys_bad.load();
}

void NoteKeysResult(std::uint32_t rc) {
    if (IsKeysError(rc)) {
        g_last_tick = armGetSystemTick();
    }
}

void Poll() {
    if (g_startup != StartupNotice::None) {
        if (!Busy()) {
            ShowStartupNotice();
        }
        return;
    }

    const auto last = g_last_tick.load();
    if (!last || Busy() || armTicksToNs(armGetSystemTick() - last) < QUIET_NS) {
        return;
    }
    g_last_tick = 0;
    AskToDump();
}

} // namespace sphaira::keys_dump
