// Calibration step 5 without the manual boot dance: the Hub reboots into the other MMC through hekate, matches there
// by itself when it is opened, and offers to boot back. Entry choice and the boot request are in battery_hop.hpp.

#include "battery_page.hpp"
#include "battery_internal.hpp"
#include "battery_hop.hpp"
#include "account/account_restore.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "utils/utils.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::battery {
namespace {

constexpr const char* HEKATE_INI = "/bootloader/hekate_ipl.ini";
constexpr const char* HEKATE_BIN = "/bootloader/update.bin";
constexpr const char* HOP_BIN = "/config/kefir/battery/hekate_hop.bin";

auto MmcName(u32 mmc) -> std::string {
    return mmc == 2 ? "emuMMC" : "sysMMC";
}

// hekate starts the other MMC's entry once; `why` says what is missing when it cannot be done.
auto RebootInto(u32 mmc, std::string& why) -> bool {
    fs::FsNativeSd sd;
    std::vector<u8> ini, payload;
    if (R_FAILED(sd.read_entire_file(HEKATE_INI, ini)) || ini.empty()) {
        why = "hekate_ipl.ini was not found on the microSD."_i18n;
        return false;
    }
    const int index = hop::FindEntry({reinterpret_cast<const char*>(ini.data()), ini.size()}, mmc == 2);
    if (!index) {
        why = "No hekate boot entry for"_i18n + " " + MmcName(mmc) + ".";
        return false;
    }
    if (R_FAILED(sd.read_entire_file(HEKATE_BIN, payload)) || !hop::PatchBootCfg(payload, index)) {
        why = "hekate was not found in /bootloader/update.bin."_i18n;
        return false;
    }
    if (!utils::hekatePayloadApi()) {
        why = "This hekate cannot start a payload for one boot."_i18n;
        return false;
    }
    sd.CreateDirectoryRecursively("/config/kefir/battery");
    if (R_FAILED(sd.write_entire_file(HOP_BIN, payload)) || R_FAILED(sd.Commit())) {
        why = "The microSD could not be written."_i18n;
        return false;
    }
    log_write("[Battery] hop: hekate entry %d (%s)\n", index, MmcName(mmc).c_str());
    return utils::rebootToPayload(HOP_BIN);
}

void GoBack(u32 mmc) {
    std::string why;
    if (!RebootInto(mmc, why)) {
        App::Push<ui::OptionBox>("Could not switch:"_i18n + " " + why + "\n\n" + "Restart the console and choose the system yourself."_i18n, "OK"_i18n);
    }
}

} // namespace

// A on step 5 in the MMC where the cycles were done
void OfferHop() {
    if (App::GetProgressActive()) {
        App::Notify("Finish or stop the running transfer first."_i18n);
        return;
    }
    auto st = detail::LoadState();
    const u32 here = App::IsEmummc() ? 2 : 1;
    const u32 there = here == 2 ? 1 : 2;
    const auto other = MmcName(there);
    App::Push<ui::OptionBox>(
        "The last step is the match in"_i18n + " " + other + ".\n\n" +
            "Kefir Hub restarts into it, matches there as soon as you open it, and then offers to come back. Restart into"_i18n + " " + other + "?",
        "Later"_i18n, "Restart"_i18n, 1, [st, here, there](auto op) mutable {
            if (!op || *op != 1) {
                return;
            }
            st.hop_to = there;
            st.hop_from = here;
            detail::SaveState(st);
            account_restore::ArmReopenHubHint("Battery calibration: open Kefir Hub to finish the match."_i18n, false);
            std::string why;
            if (!RebootInto(there, why)) {
                st.hop_to = st.hop_from = 0;
                detail::SaveState(st);
                account_restore::ClearReopenHubHint();
                App::Push<ui::OptionBox>("Could not switch:"_i18n + " " + why + "\n\n" + "Boot the other MMC yourself, open Tools → Tools → Battery → + and press A on step 5."_i18n, "OK"_i18n);
            }
        });
}

// from the app loop, once after the start: a hop is waiting for this MMC, so the Hub matches by itself
void PollHop() {
    static bool finished = false;
    static TimeStamp since_start{};
    if (finished) {
        return;
    }
    if (since_start.GetSeconds() < 10 || App::GetProgressActive()) {
        return;
    }
    finished = true;
    auto st = detail::LoadState();
    if (!st.hop_to) {
        return;
    }
    const u32 here = App::IsEmummc() ? 2 : 1;
    if (here != st.hop_to) {
        // not the MMC the match waits for: back in the first one without it (boot failed, or the user chose), nothing to do here
        if (here == st.hop_from) {
            st.hop_to = st.hop_from = 0;
            detail::SaveState(st);
            account_restore::ClearReopenHubHint();
        }
        return;
    }
    const u32 from = st.hop_from;
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Match the other MMC"_i18n, "Battery"_i18n, [failed](auto) -> Result {
        return detail::MatchNow(*failed);
    }, [failed, from](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<ui::OptionBox>("The match failed:"_i18n + " " + *failed + "\n\n" + "Open Tools → Tools → Battery → + and press A on step 5 to try again."_i18n, "OK"_i18n);
            return;
        }
        auto s = detail::LoadState();
        s.hop_to = s.hop_from = 0;
        detail::SaveState(s);
        account_restore::ClearReopenHubHint();
        const auto text = "Calibration complete: both MMCs hold the same calibrated model and one cycle count."_i18n;
        if (!from) {
            App::Push<ui::OptionBox>(text, "OK"_i18n);
            return;
        }
        App::Push<ui::OptionBox>(text + "\n\n" + "Restart into"_i18n + " " + MmcName(from) + "?",
            "Stay here"_i18n, "Restart"_i18n, 1, [from](auto op) {
                if (op && *op == 1) {
                    GoBack(from);
                }
            });
    });
}

} // namespace sphaira::battery
