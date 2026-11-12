// Calibration step 5 without the manual boot dance: the Hub reboots into the other MMC through hekate, matches there
// by itself when it is opened, and offers to boot back. Entry choice and the boot request are in battery_hop.hpp.
// On a Kefir card the way into the sysMMC is Semi-stock: the Hub turns it on, and off again on the way back (hop::PickWay).

#include "battery_page.hpp"
#include "battery_internal.hpp"
#include "battery_hop.hpp"
#include "battery_calib_internal.hpp"
#include "account/account_restore.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "utils/utils.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"

#include <dirent.h>
#include <memory>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace sphaira::battery {
namespace {

using namespace ui::menu::settings::detail; // file and ini helpers

constexpr const char* HEKATE_INI = "/bootloader/hekate_ipl.ini";
constexpr const char* HEKATE_BIN = "/bootloader/update.bin";
constexpr const char* HOP_BIN = "/config/kefir/battery/hekate_hop.bin";

// Semi-stock, exactly as Kefir's "Semi-Stock Status" package leaves the card (switch/.packages/package.ini), so the
// package, TegraExplorer's Return_to_emuMMC script and the Hub can each undo what another one did.
constexpr const char* EMUMMC_INI = "/emummc/emummc.ini";
constexpr const char* EXO_INI = "/exosphere.ini";
constexpr const char* EMUMMC_BKP = "/config/semistock/emummc.ini.bkp";
constexpr const char* EXO_BKP = "/config/semistock/exosphere.ini.bkp";
constexpr const char* RESCUE_SRC = "/config/semistock/restore.te";
constexpr const char* RESCUE_DST = "/tegraexplorer/scripts/Return_to_emuMMC.te";
constexpr const char* LYT = "/atmosphere/contents/0100000000001000/romfs/lyt"; // home menu theme: the sysMMC firmware may not fit it
constexpr const char* LYT_STASH = "/config/ultrahand/romfs/lyt";
constexpr const char* STASH = "/config/semistock/nextendo"; // Nextendo patches switch certificate checks off: never live in the sysMMC
constexpr const char* CERTS = "/atmosphere/contents/0100000000000803";
constexpr const char* PATCH_DIRS[] = {"exefs_patches", "nro_patches"};

auto MmcName(u32 mmc) -> std::string {
    return mmc == 2 ? "emuMMC" : "sysMMC";
}

auto SameSize(const char* a, const char* b) -> bool {
    struct stat sa{}, sb{};
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_size > 0 && sa.st_size == sb.st_size;
}

// every entry of `from` whose name starts with `prefix` goes to `to`. Names first: a rename inside readdir() skips entries.
auto MoveAll(const std::string& from, const std::string& to, std::string_view prefix) -> Result {
    std::vector<std::string> names;
    if (auto* d = opendir(from.c_str())) {
        while (const auto* e = readdir(d)) {
            const std::string_view name = e->d_name;
            if (name != "." && name != ".." && name.starts_with(prefix)) {
                names.emplace_back(name);
            }
        }
        closedir(d);
    }
    for (const auto& name : names) {
        R_TRY(MovePath(from + "/" + name, to + "/" + name));
    }
    R_SUCCEED();
}

auto WayTo(u32 mmc) -> hop::Way {
    return hop::PickWay(mmc == 2, IniValueEquals(EMUMMC_INI, "emummc", "enabled", "1"), fs::FileExists(RESCUE_SRC),
                        fs::FileExists(EMUMMC_BKP) && fs::FileExists(EXO_BKP));
}

// The package's "off". The two ini files first: with them back the emuMMC starts, so the rest never stops the way back.
auto SemiStockOff(std::string& why) -> bool {
    if (!fs::FileExists(EMUMMC_BKP) || !fs::FileExists(EXO_BKP)) {
        why = "The Semi-stock backups were not found in /config/semistock. Nothing was changed."_i18n;
        return false;
    }
    if (R_FAILED(CopyFileSimple(EMUMMC_BKP, EMUMMC_INI)) || R_FAILED(CopyFileSimple(EXO_BKP, EXO_INI)) ||
        !SameSize(EMUMMC_BKP, EMUMMC_INI) || !SameSize(EXO_BKP, EXO_INI)) {
        why = "emummc.ini or exosphere.ini could not be restored from /config/semistock. The backups are kept."_i18n;
        return false;
    }
    if (!IniValueEquals(EMUMMC_INI, "emummc", "enabled", "1")) { // a backup made in the sysMMC would keep the console there
        SetIniValue(EMUMMC_INI, "emummc", "enabled", "1");
    }
    const std::string stash = STASH;
    if (R_SUCCEEDED(MovePath(LYT_STASH, LYT))) {
        DeletePath("/config/ultrahand/romfs");
    }
    bool back = true;
    for (const auto* dir : PATCH_DIRS) {
        back &= R_SUCCEEDED(MoveAll(stash + "/" + dir, std::string{"/atmosphere/"} + dir, ""));
    }
    back &= R_SUCCEEDED(MovePath(stash + "/contents/0100000000000803", CERTS));
    if (back) { // a file that did not move stays in the stash; the Hub writes its own Nextendo patches anyway
        DeletePath(stash);
    }
    DeletePath(RESCUE_DST);
    DeletePath(EMUMMC_BKP);
    DeletePath(EXO_BKP);
    log_write("[Battery] hop: Semi-stock off (stash moved back: %d)\n", back);
    return true;
}

// The package's "on". The way back is saved and checked before anything live changes, and emummc.ini changes last: a
// failure on the way puts everything back, and a power loss leaves a card that still starts the emuMMC.
auto SemiStockOn(std::string& why) -> bool {
    const bool saved = R_SUCCEEDED(CopyFileSimple(EMUMMC_INI, EMUMMC_BKP)) && R_SUCCEEDED(CopyFileSimple(EXO_INI, EXO_BKP)) &&
                       R_SUCCEEDED(CopyFileSimple(RESCUE_SRC, RESCUE_DST)) && SameSize(EMUMMC_INI, EMUMMC_BKP) &&
                       SameSize(EXO_INI, EXO_BKP) && SameSize(RESCUE_SRC, RESCUE_DST);
    if (!saved) {
        DeletePath(EMUMMC_BKP); // a half-written backup must never be restored
        DeletePath(EXO_BKP);
        DeletePath(RESCUE_DST);
        why = "The way back could not be saved in /config/semistock (backups and the TegraExplorer script). Nothing was changed."_i18n;
        return false;
    }
    const std::string stash = STASH;
    const auto apply = [&]() -> Result {
        fs::FsNativeSd sd;
        R_TRY(sd.CreateDirectoryRecursively(stash + "/contents"));
        R_TRY(MovePath(LYT, LYT_STASH));
        for (const auto* dir : PATCH_DIRS) {
            R_TRY(sd.CreateDirectoryRecursively(stash + "/" + dir));
            R_TRY(MoveAll(std::string{"/atmosphere/"} + dir, stash + "/" + dir, "nextendo_"));
        }
        R_TRY(MovePath(CERTS, stash + "/contents/0100000000000803"));
        R_TRY(SetIniValue(EXO_INI, "exosphere", "blank_prodinfo_sysmmc", "0"));
        R_TRY(SetIniValue(EMUMMC_INI, "emummc", "enabled", "0"));
        R_SUCCEED();
    };
    const auto rc = apply();
    if (R_FAILED(rc) || !IniValueEquals(EMUMMC_INI, "emummc", "enabled", "0")) {
        log_write("[Battery] hop: Semi-stock on failed 0x%X, putting everything back\n", rc);
        std::string ignored;
        SemiStockOff(ignored);
        why = "A file on the microSD could not be moved or written. Semi-stock was not turned on, and everything was put back."_i18n;
        return false;
    }
    log_write("[Battery] hop: Semi-stock on\n");
    return true;
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
        why = mmc == 1 ? "hekate has no entry that starts Atmosphere in the sysMMC: the stock entry does not load Kefir Hub."_i18n : "No hekate boot entry for"_i18n + " " + MmcName(mmc) + ".";
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

// Restarts the console into `mmc`. `switched` = Semi-stock was turned on or off, so the card already starts `mmc` and
// only the restart itself failed.
auto SwitchTo(u32 mmc, std::string& why, bool& switched) -> bool {
    switched = false;
    const auto way = WayTo(mmc);
    if (way == hop::Way::Hekate) {
        return RebootInto(mmc, why);
    }
    if (!(way == hop::Way::SemiStockOn ? SemiStockOn(why) : SemiStockOff(why))) {
        return false;
    }
    switched = true;
    fsdevCommitDevice("sdmc");
    return R_SUCCEEDED(utils::requestForcedReboot());
}

// what the user reads when SwitchTo failed
auto SwitchFailText(u32 mmc, const std::string& why, bool switched, const std::string& advice) -> std::string {
    if (switched) {
        return "The microSD is ready, but the console did not restart. Restart it yourself: it starts in"_i18n + " " + MmcName(mmc) + ".";
    }
    return "Could not switch:"_i18n + " " + why + "\n\n" + advice;
}

void GoBack(u32 mmc) {
    std::string why;
    bool switched = false;
    if (!SwitchTo(mmc, why, switched)) {
        App::Push<ui::OptionBox>(SwitchFailText(mmc, why, switched, "Restart the console and choose the system yourself."_i18n), "OK"_i18n);
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
    const auto way = WayTo(there);
    auto note = way == hop::Way::SemiStockOn ? "\n\n" + "Kefir Hub turns Semi-stock on for this and turns it off on the way back."_i18n
              : way == hop::Way::SemiStockOff ? "\n\n" + "Kefir Hub turns Semi-stock off for this and turns it on again on the way back."_i18n
              : std::string{};
    if (there == 1) {
        note += "\n\n" + "In the sysMMC, hold R and open the Album. Keep R held until Kefir Hub starts."_i18n;
    }
    App::Push<ui::OptionBox>(
        "The last step is the match in"_i18n + " " + other + ".\n\n" +
            "Kefir Hub restarts into it, matches there as soon as you open it, and then offers to come back. Restart into"_i18n + " " + other + "?" + note,
        "Later"_i18n, "Restart"_i18n, 1, [st, here, there](auto op) mutable {
            if (!op || *op != 1) {
                return;
            }
            st.hop_to = there;
            st.hop_from = here;
            detail::SaveState(st);
            // the sysMMC has no Kefir Hub icon on the HOME Menu: the Hub starts there only with R held while the Album opens, and the notice must say so (owner, 2026-11-12)
            account_restore::ArmReopenHubHint(there == 1 ? "Battery calibration: hold R and open the Album. Keep R held until Kefir Hub starts. It finishes the match by itself."_i18n
                                                         : "Battery calibration: open Kefir Hub to finish the match. No icon? Hold R, open the Album, keep R held until Kefir Hub starts."_i18n, false);
            std::string why;
            bool switched = false;
            if (!SwitchTo(there, why, switched)) {
                if (!switched) { // the card still starts this system: the hop is off. (Switched = it goes on after a restart by hand.)
                    st.hop_to = st.hop_from = 0;
                    detail::SaveState(st);
                    account_restore::ClearReopenHubHint();
                }
                App::Push<ui::OptionBox>(SwitchFailText(there, why, switched, "Start the other system yourself and open Kefir Hub there: the calibration moves by itself. For the sysMMC, turn Semi-stock on with the Semi-Stock Status package."_i18n), "OK"_i18n);
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
        std::string text;
        if (R_FAILED(rc)) {
            // The hop state stays: step 5 can try again here, and the first system clears it when the user goes back.
            // The way back is offered all the same, or a failed match would leave the user in Semi-stock with no word about it.
            text = "The match failed:"_i18n + " " + *failed + "\n\n" + "Open Tools → Tools → Battery → + and press A on step 5 to try again."_i18n;
        } else {
            auto s = detail::LoadState();
            s.hop_to = s.hop_from = 0;
            detail::SaveState(s);
            account_restore::ClearReopenHubHint();
            // the wizard ends here: the marker is on the shared microSD, so the first system sees step 5 as done
            if (auto m = calib_flow::LoadMarker()) {
                calib::TransferDone(*m);
                calib_flow::SaveMarker(*m);
            }
            text = "Calibration complete: both MMCs hold the same calibrated model and one cycle count."_i18n;
        }
        if (!from) {
            App::Push<ui::OptionBox>(text, "OK"_i18n);
            return;
        }
        const auto way = WayTo(from);
        const auto note = way == hop::Way::SemiStockOff ? "\n\n" + "Kefir Hub turns Semi-stock off. If you stay here, Semi-stock stays on until you turn it off with the Semi-Stock Status package."_i18n
                        : way == hop::Way::SemiStockOn ? "\n\n" + "Kefir Hub turns Semi-stock on again."_i18n
                        : std::string{};
        App::Push<ui::OptionBox>(text + "\n\n" + "Restart into"_i18n + " " + MmcName(from) + "?" + note,
            "Stay here"_i18n, "Restart"_i18n, 1, [from](auto op) {
                if (op && *op == 1) {
                    GoBack(from);
                }
            });
    });
}

} // namespace sphaira::battery
