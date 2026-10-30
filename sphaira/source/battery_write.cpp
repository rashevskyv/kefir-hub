// Tools → Battery → Calibrate (F.18 stage A): the only code that writes the fuel gauge chip.
// - Reset from factory data: BDF's X sequence (battery_init.hpp), then MiscCFG bit 15 is cleared so no MMC
//   writes its old copy back at the next boot (psm restores set:sys into the chip only while that bit is set,
//   psm-fw NOTES §1), and this MMC's copy gets the new model.
// - Restore from the last backup: BDF's Y (the nine learned parameters), cycles = max(copy, backup).
// - Match the other MMC: this MMC's copy = the chip's model, cycles = the larger of the two MMCs; no chip write.
// Every write is preceded by a backup (.cal + .txt).
#include "battery_page.hpp"
#include "battery_internal.hpp"
#include "battery_init.hpp"
#include "battery_zero.hpp"
#include "battery_calib_internal.hpp"
#include "battery_wizard.hpp"
#include "system_info.hpp"
#include "ui/menus/system_info_menu.hpp"
#include "fuel_gauge.hpp"
#include "account/account_restore.hpp"
#include "ui/option_box.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::battery {
namespace {

using namespace detail;

constexpr const char* STATE_PATH = "/config/kefir/battery/calibration.txt";

} // namespace

namespace detail {

auto LoadState() -> wizard::State {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd{}.read_entire_file(STATE_PATH, data))) {
        return {};
    }
    return wizard::Parse({reinterpret_cast<const char*>(data.data()), data.size()});
}

void SaveState(const wizard::State& st) {
    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursively(BACKUP_DIR);
    const auto text = wizard::Format(st);
    fs.write_entire_file(STATE_PATH, std::vector<u8>(text.begin(), text.end()));
}

auto CycleTotalNow() -> std::optional<u32> {
    if (R_FAILED(setsysInitialize())) {
        return std::nullopt;
    }
    ON_SCOPE_EXIT(setsysExit());
    SetSysPtmFuelGaugeParameter p{};
    if (R_FAILED(setsysGetPtmFuelGaugeParameter(&p))) {
        return std::nullopt;
    }
    FuelGauge g;
    const auto reg = g.Read(MAX17050_Cycles);
    if (!reg) {
        return std::nullopt;
    }
    return p.cycles_actual + (*reg > 0x60 ? *reg - 0x60u : 0u);
}

} // namespace detail

namespace {

// this MMC's copy := the chip's nine parameters; `cycles` replaces the count when given.
auto SaveCopy(FuelGauge& g, std::optional<u32> cycles) -> Result {
    // the Hub keeps no set:sys session open; every module opens its own
    R_TRY(setsysInitialize());
    ON_SCOPE_EXIT(setsysExit());
    SetSysPtmFuelGaugeParameter p{};
    R_TRY(setsysGetPtmFuelGaugeParameter(&p));
    Params chip;
    for (size_t i = 0; i < PARAM_REGS.size(); i++) {
        const auto v = g.Read(PARAM_REGS[i]);
        R_UNLESS(v.has_value(), Result_ArchiveRead);
        chip.v[i] = *v;
    }
    const auto& c = chip.v;
    p.rcomp0 = c[0]; p.tempc0 = c[1]; p.fullcap = c[2]; p.fullcapnom = c[3]; p.lavgempty = c[4];
    p.qresidual00 = c[5]; p.qresidual10 = c[6]; p.qresidual20 = c[7]; p.qresidual30 = c[8];
    if (cycles) {
        p.cycles_actual = *cycles;
    }
    return setsysSetPtmFuelGaugeParameter(&p);
}

// BDF X with the step list from battery_init.hpp. `failed` names the step that did not stick.
// `v_empty`: what 0x3A gets (the console's own 0xA05F, or the calibration's zero in VE)
auto FactoryInit(FuelGauge& g, const Table& t, u16 v_empty, std::string& failed) -> Result {
    std::array<u16, 4> slots{};
    slots[SLOT_VEMPTY] = v_empty;
    const auto steps = FactoryInitSteps();
    for (size_t i = 0; i < steps.size(); i++) {
        const auto& st = steps[i];
        bool ok = true;
        switch (st.op) {
            case Op::Read:
                if (const auto v = g.Read(st.reg)) {
                    slots[st.value] = *v;
                } else {
                    ok = false;
                }
                break;
            case Op::Sleep:
                svcSleepThread(st.value * 1'000'000ULL);
                break;
            case Op::Write:
                ok = g.Write(st.reg, StepValue(st, t, slots));
                break;
            case Op::Verify:
                ok = g.WriteVerify(st.reg, StepValue(st, t, slots));
                break;
        }
        if (!ok) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "step %zu, register 0x%02X", i + 1, st.reg);
            failed = buf;
            log_write("[Battery] factory init failed at %s\n", buf);
            // never leave the model table unlocked
            g.Write(0x62, 0x0000);
            g.Write(0x63, 0x0000);
            R_THROW(Result_ArchiveRead);
        }
    }
    // BDF leaves NeedToRestoreParameters set; then the first MMC booted writes its old copy over this.
    const auto misc = g.Read(MAX17050_MiscCFG);
    R_UNLESS(misc && g.WriteVerify(MAX17050_MiscCFG, *misc & 0x7FFF), Result_ArchiveRead);
    R_SUCCEED();
}

// BDF Y: the nine learned parameters into the chip.
auto RestoreParams(FuelGauge& g, const Params& p) -> Result {
    const u8 verify_regs[] = {0x38, 0x39, 0x10, 0x23, 0x12, 0x22, 0x32, 0x42};
    const size_t verify_idx[] = {0, 1, 2, 3, 5, 6, 7, 8};
    for (size_t i = 0; i < std::size(verify_regs); i++) {
        if (!g.WriteVerify(verify_regs[i], p.v[verify_idx[i]])) {
            const auto now = g.Read(verify_regs[i]);
            log_write("[Battery] restore: reg 0x%02X want %04X has %04X write rc 0x%X\n", verify_regs[i],
                p.v[verify_idx[i]], now ? *now : 0xFFFF, g.last_rc);
            R_THROW(Result_ArchiveRead);
        }
    }
    R_UNLESS(g.Write(0x36, p.v[4]), Result_ArchiveRead);
    svcSleepThread(350'000'000ULL);
    R_SUCCEED();
}

auto LatestBackup(bool emummc) -> std::optional<BattCal> {
    fs::FsNativeSd fs;
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(BACKUP_DIR, FsDirOpenMode_ReadFiles, &dir))) {
        return std::nullopt;
    }
    std::vector<FsDirectoryEntry> entries;
    dir.ReadAll(entries);
    std::string best;
    const std::string suffix = emummc ? "-emummc.cal" : "-sysmmc.cal";
    for (const auto& e : entries) {
        const std::string name = e.name;
        if (name.size() > suffix.size() && name.ends_with(suffix) && name > best) {
            best = name; // <stamp> sorts by time
        }
    }
    std::vector<u8> data;
    if (best.empty() || R_FAILED(fs.read_entire_file(fs::FsPath{std::string{BACKUP_DIR} + "/" + best}, data)) || data.size() != sizeof(BattCal)) {
        return std::nullopt;
    }
    BattCal cal{};
    std::memcpy(&cal, data.data(), sizeof(cal));
    return cal;
}

void Done(Result rc, const std::string& ok_text, const std::shared_ptr<std::string>& failed) {
    if (rc == Result_TransferCancelled) {
        App::Notify("Cancelled: the chip was not changed"_i18n);
        return;
    }
    log_write("[Battery] write result 0x%X %s\n", rc, failed->c_str());
    if (R_SUCCEEDED(rc)) {
        App::Push<ui::OptionBox>(ok_text, "OK"_i18n);
    } else {
        App::Push<ui::OptionBox>("The chip was not changed as planned. A backup was saved first; Restore from the last backup puts it back."_i18n
                                 + (failed->empty() ? std::string{} : "\n\n" + *failed), "OK"_i18n);
    }
}

// back up, write the factory model, save this MMC's copy and the wizard's state. Any charge level: the init seeds the
// counters from VFSOC (the voltage), so the gauge does not need a full battery (the old wizard's 100 % rule is gone)
auto ResetNow(ui::ProgressBox* pbox, std::string& failed) -> Result {
    auto s = Read();
    R_UNLESS(s.gauge, Result_ArchiveRead);
    std::string name;
    pbox->NewTransfer("Back up"_i18n);
    R_TRY(Backup(s, name));
    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    pbox->NewTransfer("Writing the factory model"_i18n);
    // the calibration's zero (the cut-off voltage) survives the reset: the threshold is not a learned value
    u16 v_empty = zero::VE_DEFAULT;
    if (const auto m = calib_flow::LoadMarker(); m && m->zero_at && zero::InRange(m->zero_mv)) {
        v_empty = zero::VEmptyWith(v_empty, m->zero_mv);
    }
    R_TRY(FactoryInit(g, ChooseTable(s.cal0_version, s.cal0_vendor), v_empty, failed));
    pbox->NewTransfer("Saving this MMC's copy"_i18n);
    // a cycle count lost to a reset never comes back: keep the larger of the two MMCs
    u32 cycles = s.hos.cycles_actual;
    if (s.other) {
        cycles = std::max(cycles, s.other->cal.cycles);
    }
    R_TRY(SaveCopy(g, cycles));
    wizard::State st;
    st.reset_at = static_cast<u64>(std::time(nullptr));
    st.reset_mmc = s.emummc ? wizard::EMUMMC : wizard::SYSMMC;
    st.total_at_reset = cycles; // the reset set the chip's Cycles register to 0x60: no delta
    SaveState(st);
    log_write("[Battery] factory init done (table v%d '%c'), cycles %u, V_empty %04X\n", s.cal0_version, s.cal0_vendor ? s.cal0_vendor : '?', cycles, v_empty);
    R_SUCCEED();
}

void RunRestore() {
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Restore from the last backup"_i18n, "Battery"_i18n, [failed](auto pbox) -> Result {
        auto s = Read();
        const auto cal = LatestBackup(s.emummc);
        if (!cal) {
            *failed = "No backup of this MMC in /config/kefir/battery."_i18n;
            R_THROW(Result_ArchiveRead);
        }
        std::string name;
        R_TRY(Backup(s, name)); // what is in the chip now, before it is replaced
        FuelGauge g;
        R_UNLESS(g.open, Result_ArchiveRead);
        R_TRY(RestoreParams(g, ParamsOf(*cal)));
        R_TRY(SaveCopy(g, std::max(s.hos.cycles_actual, cal->cycles)));
        log_write("[Battery] restored from the last backup\n");
        R_SUCCEED();
    }, [failed](Result rc) {
        Done(rc, "The chip and this MMC's copy now hold the backup's model."_i18n, failed);
    });
}

// "Align the charge to the voltage": the chip reports far more charge than its own voltage estimate (VFSOC) says, so
// RemCapMIX, RemCapREP and RepSOC are set to what the voltage says. Meant for the moment the console switches itself
// off at 20-30 %. Everything the chip learned stays as it is; a full charge afterwards lets it learn again.
void RunAlign() {
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Align the charge to the voltage"_i18n, "Battery"_i18n, [failed](auto pbox) -> Result {
        FuelGauge g;
        R_UNLESS(g.open, Result_ArchiveRead);
        PsmChargerType charger{};
        psmGetChargerType(&charger);
        const auto vfsoc = g.Read(MAX17050_VFSOC);
        const auto repsoc = g.Read(MAX17050_RepSOC);
        const auto full = g.Read(MAX17050_FullCap);
        const auto nom = g.Read(MAX17050_FullCapNom);
        R_UNLESS(vfsoc && repsoc && full && nom, Result_ArchiveRead);
        const auto plan = PlanAlign({*vfsoc, *repsoc, *full, *nom, charger != PsmChargerType_Unconnected});
        log_write("[Battery] align: voltage says %u/256 %%, chip says %u/256 %%, charger %d -> %s\n", *vfsoc, *repsoc,
            charger != PsmChargerType_Unconnected, plan.ok ? "write" : "refused");
        if (!plan.ok) {
            *failed = "Not now: the charger must be out, the voltage must say 10% or less, and the chip must show at least 15 points more than the voltage."_i18n;
            R_THROW(Result_ArchiveRead);
        }
        std::string name;
        R_TRY(Backup(Read(), name)); // what is in the chip now, before the change
        const bool ok = g.WriteVerify(MAX17050_MixCap, plan.mix);
        svcSleepThread(350'000'000);
        const bool ok_rep = g.Write(MAX17050_RepCap, plan.rep);
        const bool ok_soc = g.Write(MAX17050_RepSOC, plan.soc);
        const auto after = g.Read(MAX17050_RepSOC);
        log_write("[Battery] align: MIX %04X %d, REP %04X %d, SOC %04X %d; chip now %u/256 %%\n", plan.mix, ok, plan.rep, ok_rep, plan.soc, ok_soc, after ? *after : 0);
        R_UNLESS(ok && ok_rep && ok_soc, Result_ArchiveRead);
        R_SUCCEED();
    }, [failed](Result rc) {
        Done(rc, "The chip's charge now follows the voltage. Switch the console off and on, then charge it fully."_i18n, failed);
    });
}

void RunMatch() {
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Match the other MMC"_i18n, "Battery"_i18n, [failed](auto pbox) -> Result {
        return detail::MatchNow(*failed);
    }, [failed](Result rc) {
        const auto st = LoadState();
        const bool finished = st.full2_at && st.match_at[wizard::SYSMMC] + st.match_at[wizard::EMUMMC] > 0;
        Done(rc, finished ? "Calibration complete: both MMCs hold the same calibrated model and one cycle count."_i18n
                          : "This MMC's copy now holds the chip's model and the larger cycle count of the two MMCs."_i18n, failed);
    });
}

// "Actions" on the calibration page: what can be done to the chip and the copies, one row each with what it does
void ActionsMenu() {
    using ui::menu::settings::SettingsItem;
    using ui::menu::settings::SettingsItemKind;
    const auto row = [](std::string label, std::string what, std::function<void()> act) {
        return SettingsItem{std::move(label), std::move(what), {}, std::move(act), SettingsItemKind::Normal};
    };
    std::vector<SettingsItem> items;
    items.push_back(row("Back up the chip's values"_i18n, "Saves a copy of the gauge's values on the SD card. Nothing is changed."_i18n, []() {
        std::string name;
        const auto rc = Backup(Read(), name);
        App::Notify(R_SUCCEEDED(rc) ? "Saved: "_i18n + name + ".cal" : std::string{"Backup failed"_i18n});
    }));
    items.push_back(row("Align the charge to the voltage"_i18n, "Sets the gauge's charge to what the voltage says. For a console that switched off at a high percent."_i18n, RunAlign));
    items.push_back(row("Restore from the last backup"_i18n, "Writes the values of the last backup back into the gauge."_i18n, RunRestore));
    items.push_back(row("Match the other MMC"_i18n, "Copies the calibration to the other system (emuMMC or sysMMC)."_i18n, RunMatch));
    items.push_back(row("Battery type (PRODINFO)"_i18n, "Shows the battery type in the console's calibration data and changes it."_i18n, OpenBatteryType));
    App::Push<ui::menu::tools::SystemToolsMenu>("Actions"_i18n, std::move(items));
}

} // namespace

namespace detail {

void RunReset(std::function<void(bool)> done) {
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Reset the gauge"_i18n, "Battery"_i18n, [failed](auto pbox) -> Result {
        return ResetNow(pbox, *failed);
    }, [failed, done](Result rc) {
        if (rc == Result_TransferCancelled) {
            App::Notify("Cancelled: the chip was not changed"_i18n);
            done(false);
            return;
        }
        log_write("[Battery] reset result 0x%X %s\n", rc, failed->c_str());
        if (R_FAILED(rc)) {
            App::Push<ui::OptionBox>("The chip was not changed as planned. A backup was saved first; Restore from the last backup puts it back."_i18n
                                     + (failed->empty() ? std::string{} : "\n\n" + *failed), "OK"_i18n, [done](auto) { done(false); });
            return;
        }
        done(true);
    });
}

auto MatchNow(std::string& failed) -> Result {
    auto s = Read();
    if (!s.other) {
        failed = "The other MMC was not checked yet: boot it and open this page once."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    std::string name;
    R_TRY(Backup(s, name));
    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    R_TRY(SaveCopy(g, std::max(s.hos.cycles_actual, s.other->cal.cycles)));
    // only a match after both cycles counts as the calibration's last step
    if (auto st = LoadState(); st.reset_at && st.full2_at) {
        st.match_at[s.emummc ? wizard::EMUMMC : wizard::SYSMMC] = static_cast<u64>(std::time(nullptr));
        SaveState(st);
    }
    log_write("[Battery] matched the other MMC: cycles %u\n", std::max(s.hos.cycles_actual, s.other->cal.cycles));
    R_SUCCEED();
}

} // namespace detail

void OpenCalibrate() {
    OpenCalibration();
}

void OpenActions() {
    ActionsMenu();
}

void PollInterruptedDrain() {
    static bool finished = false;
    static TimeStamp since_start{};
    if (finished) {
        return;
    }
    if (!DrainWasInterrupted()) {
        finished = true;
        return;
    }
    // wait until the UI has settled and nothing else runs
    if (since_start.GetSeconds() < 8 || App::GetProgressActive()) {
        return;
    }
    finished = true;
    FuelGauge g;
    if (!g.open) {
        return;
    }
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    const auto vfsoc = g.Read(MAX17050_VFSOC);
    const auto repsoc = g.Read(MAX17050_RepSOC);
    const auto full = g.Read(MAX17050_FullCap);
    const auto nom = g.Read(MAX17050_FullCapNom);
    if (!vfsoc || !repsoc || !full || !nom) {
        return;
    }
    if (!PlanAlign({*vfsoc, *repsoc, *full, *nom, charger != PsmChargerType_Unconnected}).ok) {
        return;
    }
    log_write("[Battery] the drain was cut short and the chip is off: voltage %u/256 %%, chip %u/256 %%\n", *vfsoc, *repsoc);
    const std::string text = "The console switched itself off during the battery drain, and the chip still shows"_i18n + " " +
        std::to_string(*repsoc >> 8) + "% " + "while its voltage says"_i18n + " " + std::to_string(*vfsoc >> 8) + "%.\n\n" +
        "Set the chip's charge to what the voltage says? Then charge the console fully."_i18n;
    App::Push<ui::OptionBox>(text, "Later"_i18n, "Align now"_i18n, 1, [](auto op) {
        if (op && *op == 1) {
            RunAlign();
        }
    });
}

} // namespace sphaira::battery
