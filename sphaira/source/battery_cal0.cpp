// "Battery type (PRODINFO)": set the battery version byte of CAL0 (2 = LiHV 4.32 V, for a larger battery), with a verified backup.
// The layout, CRC16 and hash are in cal0_battery.hpp. Atmosphère blocks CAL0 writes by default: this page turns the two
// switches on (exosphere.ini and system_settings.ini), restarts, writes at the next start and turns them off again.

#include "battery_page.hpp"
#include "battery_internal.hpp"
#include "cal0_battery.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "utils/utils.hpp"

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::battery {
namespace {

namespace st = ui::menu::settings::detail;

constexpr const char* EXO_INI = "/exosphere.ini";
constexpr const char* EXO_SECTION = "exosphere";
constexpr const char* EXO_KEY = "allow_writing_to_cal_sysmmc";
constexpr const char* SYS_INI = "/atmosphere/config/system_settings.ini";
constexpr const char* SYS_SECTION = "atmosphere";
constexpr const char* SYS_KEY = "enable_hbl_bis_write";
constexpr const char* PENDING_PATH = "/config/kefir/battery/cal0_pending.txt";
constexpr const char* ORIGINAL_PATH = "/config/kefir/battery/cal0_original.txt";

void Hash(std::uint8_t* out, const std::uint8_t* data, std::size_t size) {
    sha256CalculateHash(out, data, size);
}

auto ReadImage(std::vector<std::uint8_t>& img) -> Result {
    FsStorage s{};
    R_TRY(fsOpenBisStorage(&s, FsBisPartitionId_CalibrationBinary));
    ON_SCOPE_EXIT(fsStorageClose(&s));
    img.resize(cal0::IMAGE_SIZE);
    return fsStorageRead(&s, 0, img.data(), img.size());
}

// both switches on: Atmosphère takes the write
auto WritesOpen() -> bool {
    return st::IniValueEquals(EXO_INI, EXO_SECTION, EXO_KEY, "1") && st::IniValueEquals(SYS_INI, SYS_SECTION, SYS_KEY, "u8!0x1");
}

void SetSwitches(bool on) {
    st::SetIniValue(EXO_INI, EXO_SECTION, EXO_KEY, on ? "1" : "0");
    st::SetIniValue(SYS_INI, SYS_SECTION, SYS_KEY, on ? "u8!0x1" : "u8!0x0");
}

auto ReadText(const char* path) -> std::string {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(path, data))) {
        return {};
    }
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

void WriteText(const char* path, const std::string& text) {
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively(BACKUP_DIR);
    sd.write_entire_file(path, std::vector<u8>(text.begin(), text.end()));
    sd.Commit();
}

auto ValueOf(const std::string& text, const std::string& key) -> int {
    const auto pos = text.find(key + "=");
    return pos == std::string::npos ? -1 : std::atoi(text.c_str() + pos + key.size() + 1);
}

auto TypeName(int version) -> std::string {
    return version == 2 ? "LiHV (4.32 V)" : "LiPo (4.2 V)";
}

// Runs in a worker. The backup is read back and compared before anything is written; only the block and the hash change;
// the result is read again and checked; on any doubt the two ranges are put back from the backup.
auto ApplyVersion(int version, std::string& why) -> Result {
    std::vector<std::uint8_t> before;
    R_TRY(ReadImage(before));
    if (!cal0::Verify(before, Hash)) {
        why = "PRODINFO fails its own checks (hashes or CRC): it is left untouched."_i18n;
        R_THROW(Result_ArchiveRead);
    }

    // backup, verified by reading it back
    char stamp[32];
    const auto now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    const std::string backup = std::string{BACKUP_DIR} + "/prodinfo-" + stamp + ".bin";
    {
        fs::FsNativeSd sd;
        sd.CreateDirectoryRecursively(BACKUP_DIR);
        R_TRY(sd.write_entire_file(backup.c_str(), before));
        R_TRY(sd.Commit());
        std::vector<std::uint8_t> back;
        if (R_FAILED(sd.read_entire_file(backup.c_str(), back)) || back != before) {
            why = "The backup could not be verified: nothing was written."_i18n;
            R_THROW(Result_ArchiveRead);
        }
    }
    if (!fs::FsNativeSd().FileExists(ORIGINAL_PATH)) {
        WriteText(ORIGINAL_PATH, "version=" + std::to_string(cal0::BatteryVersion(before)) + "\n");
    }

    auto after = before;
    if (!cal0::PatchBatteryVersion(after, static_cast<std::uint8_t>(version), Hash)) {
        why = "The new value could not be prepared."_i18n;
        R_THROW(Result_ArchiveRead);
    }

    FsStorage s{};
    R_TRY(fsOpenBisStorage(&s, FsBisPartitionId_CalibrationBinary));
    ON_SCOPE_EXIT(fsStorageClose(&s));
    const auto put = [&](const std::vector<std::uint8_t>& img) -> Result {
        R_TRY(fsStorageWrite(&s, cal0::BATTERY_OFF, img.data() + cal0::BATTERY_OFF, cal0::BATTERY_SIZE));
        R_TRY(fsStorageWrite(&s, cal0::BODY_HASH_OFF, img.data() + cal0::BODY_HASH_OFF, cal0::HASH_SIZE));
        return fsStorageFlush(&s);
    };
    const auto rc = put(after);
    if (R_FAILED(rc)) {
        why = "Atmosphère refused the write. It allows it only in the sysMMC: restart into it and try again."_i18n;
        log_write("[Battery] PRODINFO write refused: 0x%X\n", rc);
        R_THROW(rc);
    }

    std::vector<std::uint8_t> check;
    const bool ok = R_SUCCEEDED(ReadImage(check)) && check == after && cal0::Verify(check, Hash);
    if (!ok) {
        put(before); // back to what was there
        why = "The check after writing failed: the original values were put back."_i18n;
        log_write("[Battery] PRODINFO check failed, restored\n");
        R_THROW(Result_ArchiveRead);
    }
    log_write("[Battery] PRODINFO battery version %d -> %d, backup %s\n", cal0::BatteryVersion(before), version, backup.c_str());
    R_SUCCEED();
}

void Finished(Result rc, int version, const std::shared_ptr<std::string>& why, bool turn_off) {
    if (turn_off) {
        SetSwitches(false); // the Hub turned them on: close the lock again
    }
    if (R_FAILED(rc)) {
        App::Push<ui::OptionBox>("The battery type was not changed."_i18n + "\n\n" + *why, "OK"_i18n);
        return;
    }
    App::Push<ui::OptionBox>(
        "The battery type is now"_i18n + " " + TypeName(version) + ".\n\n" +
            "Restart the console, open Battery Calibrate and reset the calibration (X), then do 2-3 full cycles. A backup of PRODINFO is in /config/kefir/battery."_i18n,
        "Later"_i18n, "Restart"_i18n, 1, [](auto op) {
            if (op && *op == 1) {
                utils::requestForcedReboot();
            }
        });
}

void Run(int version, bool turn_off) {
    auto why = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Battery type"_i18n, "PRODINFO", [version, why](auto) -> Result {
        return ApplyVersion(version, *why);
    }, [version, why, turn_off](Result rc) { Finished(rc, version, why, turn_off); });
}

void Confirm(int version) {
    const auto text = "Change the battery type to"_i18n + " " + TypeName(version) + "?\n\n" +
        "This writes the battery version of PRODINFO (the calibration data of this console). A backup is saved first, the result is checked, and the old values are put back if anything is wrong."_i18n +
        (App::IsEmummc() ? "\n\n" + "This changes the PRODINFO that this system (emuMMC) sees. Do the same in the other system if it should follow."_i18n : std::string{});
    App::Push<ui::OptionBox>(text, "Back"_i18n, "Continue"_i18n, 0, [version](auto op) {
        if (!op || *op != 1) {
            return;
        }
        if (WritesOpen()) {
            Run(version, false);
            return;
        }
        // Atmosphère blocks the write: open it, restart, and the next start does the change
        SetSwitches(true);
        WriteText(PENDING_PATH, "version=" + std::to_string(version) + "\nopened=1\n");
        App::Push<ui::OptionBox>(
            "Atmosphère allows writing PRODINFO only after a restart. Kefir Hub turned this on. It restarts the console now; open Kefir Hub afterwards and it finishes the change by itself, then closes the lock again."_i18n,
            "OK"_i18n, [](auto) { utils::requestForcedReboot(); });
    });
}

} // namespace

// Actions → Battery type
void OpenBatteryType() {
    std::vector<std::uint8_t> img;
    if (R_FAILED(ReadImage(img)) || !cal0::IsUsable(img)) {
        App::Push<ui::OptionBox>("PRODINFO could not be read."_i18n, "OK"_i18n);
        return;
    }
    const int now = cal0::BatteryVersion(img);
    const int original = ValueOf(ReadText(ORIGINAL_PATH), "version");
    ui::PopupList::Items items{"LiHV (4.32 V): for a larger battery"_i18n};
    if (original >= 0 && original != 2) {
        items.push_back("Back to the original"_i18n + " (" + TypeName(original) + ")");
    }
    App::Push<ui::PopupList>("Battery type"_i18n + ": " + TypeName(now), items, [original](std::optional<s64> index) {
        if (!index) {
            return;
        }
        Confirm(*index == 0 ? 2 : original);
    });
}

// from the app loop, once after the start: a change waits for the restart, so it is done now
void PollCal0Pending() {
    static bool finished = false;
    static TimeStamp since_start{};
    if (finished || since_start.GetSeconds() < 10 || App::GetProgressActive()) {
        return;
    }
    finished = true;
    const auto text = ReadText(PENDING_PATH);
    if (text.empty()) {
        return;
    }
    const int version = ValueOf(text, "version");
    const bool turn_off = ValueOf(text, "opened") == 1;
    fs::FsNativeSd().DeleteFile(PENDING_PATH);
    if (version < 0) {
        return;
    }
    if (!WritesOpen()) {
        App::Push<ui::OptionBox>("The battery type was not changed: Atmosphère did not take the setting that allows writing PRODINFO."_i18n, "OK"_i18n);
        return;
    }
    Run(version, turn_off);
}

} // namespace sphaira::battery
