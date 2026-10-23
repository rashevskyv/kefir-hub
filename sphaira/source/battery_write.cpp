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
#include "battery_wizard.hpp"
#include "system_info.hpp"
#include "ui/menus/system_info_menu.hpp"
#include "fuel_gauge.hpp"
#include "account/account_restore.hpp"
#include "ui/option_box.hpp"
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

constexpr u16 MIN_FULL_MV = 4100;
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

// HOS's cycle total x100 for this MMC: its copy plus what the chip counted since the last fold-in
auto CycleTotal(const Snapshot& s) -> u32 {
    const u16 reg = s.regs[MAX17050_Cycles];
    return s.hos.cycles_actual + (reg > 0x60 ? reg - 0x60u : 0u);
}

void OpenWizard();

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
auto FactoryInit(FuelGauge& g, const Table& t, std::string& failed) -> Result {
    std::array<u16, 3> slots{};
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
        App::Push<ui::OptionBox>(ok_text, "OK"_i18n, [](auto) { OpenWizard(); });
    } else {
        App::Push<ui::OptionBox>("The chip was not changed as planned. A backup was saved first; Restore from the last backup puts it back."_i18n
                                 + (failed->empty() ? std::string{} : "\n\n" + *failed), "OK"_i18n, [](auto) { OpenWizard(); });
    }
}

struct ChargeState {
    bool charger{};
    u32 pct{};
    u32 mv{};
};

auto ReadCharge() -> ChargeState {
    ChargeState c;
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    c.charger = charger != PsmChargerType_Unconnected;
    psmGetBatteryChargePercentage(&c.pct);
    FuelGauge g;
    if (const auto v = g.Read(MAX17050_VCell)) {
        c.mv = static_cast<u32>((*v >> 3) * 0.625);
    }
    return c;
}

// the reset needs a full battery: the model starts from "full" and the chip learns from there. The box
// waits on the charger (no auto sleep) until then; B cancels.
auto WaitForFull(ui::ProgressBox* pbox) -> Result {
    App::SetAutoSleepDisabled(true);
    ON_SCOPE_EXIT(App::SetAutoSleepDisabled(false));
    pbox->SetHideSpeed(true); // a charge has no transfer speed or ETA
    std::string last;
    while (true) {
        R_TRY(pbox->ShouldExitResult());
        const auto c = ReadCharge();
        if (c.charger && c.pct >= 100 && c.mv >= MIN_FULL_MV) {
            R_SUCCEED();
        }
        // the text changes only with the state: a new text restarts the box's clock, and the time left
        // it shows is then the real time to a full battery. The % is beside the bar.
        std::string text = !c.charger ? "Connect the charger"_i18n
            : c.pct < 100 ? "Charging the battery to 100%"_i18n
            : "Topping up the cell to 4.1 V"_i18n;
        if (text != last) {
            pbox->NewTransfer(text);
            last = text;
        }
        pbox->UpdateTransfer(c.pct, 100);
        for (int i = 0; i < 20 && !pbox->ShouldExit(); i++) {
            svcSleepThread(100'000'000ULL);
        }
    }
}

// back up, write the factory model, save this MMC's copy and the wizard's state: what the reset does once the battery
// is where it should be (full for the old wizard, near empty at the cut-off for the calibration)
auto ResetNow(ui::ProgressBox* pbox, std::string& failed) -> Result {
    auto s = Read();
    R_UNLESS(s.gauge, Result_ArchiveRead);
    std::string name;
    pbox->NewTransfer("Back up"_i18n);
    R_TRY(Backup(s, name));
    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    pbox->NewTransfer("Writing the factory model"_i18n);
    R_TRY(FactoryInit(g, ChooseTable(s.cal0_version, s.cal0_vendor), failed));
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
    log_write("[Battery] factory init done (table v%d '%c'), cycles %u\n", s.cal0_version, s.cal0_vendor ? s.cal0_vendor : '?', cycles);
    R_SUCCEED();
}

void RunReset() {
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Battery calibration reset"_i18n, "Then the factory values are written"_i18n, [failed](auto pbox) -> Result {
        R_TRY(WaitForFull(pbox));
        return ResetNow(pbox, *failed);
    }, [failed](Result rc) {
        Done(rc, "The battery calibration now starts from the factory values. Next: two full cycles, the steps below show them."_i18n, failed);
    });
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

void AskReset() {
    if (App::GetProgressActive()) {
        App::Notify("Finish or stop the running transfer first."_i18n);
        return;
    }
    App::Push<ui::OptionBox>(
        "Write the factory battery model into the fuel gauge? The chip's learned values are replaced; a backup is saved first. Kefir Hub waits on the charger until the battery is full, then resets. Afterwards the console needs two full cycles (empty, then full) to learn the battery."_i18n,
        "Back"_i18n, "Reset"_i18n, 0, [](auto o) {
            if (o && *o == 1) {
                RunReset();
            }
        });
}

auto Info(const std::string& text) -> std::function<void()> {
    return [text]() { App::Push<ui::OptionBox>(text, "OK"_i18n); };
}

// X on the calibration page: what can be done to the chip and the copies
void ActionsMenu() {
    ui::PopupList::Items items{"Back up the chip's values"_i18n, "Align the charge to the voltage"_i18n,
                               "Restore from the last backup"_i18n, "Match the other MMC"_i18n, "Battery type (PRODINFO)"_i18n};
    App::Push<ui::PopupList>("Actions"_i18n, items, [](std::optional<s64> index) {
        if (!index) {
            return;
        }
        switch (*index) {
            case 0: {
                std::string name;
                const auto rc = Backup(Read(), name);
                App::Notify(R_SUCCEEDED(rc) ? "Saved: "_i18n + name + ".cal" : std::string{"Backup failed"_i18n});
                break;
            }
            case 1: RunAlign(); break;
            case 2: RunRestore(); break;
            case 3: RunMatch(); break;
            case 4: OpenBatteryType(); break;
        }
    });
}

// A on a cycle step: start the drain, do the cycle again, or read about it
auto CycleMenu(int cycle, std::function<void()> help) -> std::function<void()> {
    return [cycle, help]() {
        ui::PopupList::Items items{"Start the drain"_i18n, "Do this cycle again"_i18n, "About this step"_i18n};
        App::Push<ui::PopupList>("Cycle"_i18n + " " + std::to_string(cycle), items, [cycle, help](std::optional<s64> index) {
            if (!index) {
                return;
            }
            if (*index == 0) {
                OpenDrain(cycle);
            } else if (*index == 1) {
                App::Push<ui::OptionBox>("Mark this cycle as not done and measure it again from now on? A following cycle is measured again with it."_i18n,
                    "Back"_i18n, "Do it again"_i18n, 0, [cycle](auto op) {
                        if (!op || *op != 1) {
                            return;
                        }
                        const auto total = CycleTotalNow();
                        if (!total) {
                            App::Notify("Could not read the battery"_i18n);
                            return;
                        }
                        auto st = LoadState();
                        wizard::Redo(st, cycle, *total);
                        SaveState(st);
                        ui::menu::sysinfo::RequestRebuild();
                        App::Notify("The cycle is measured again from now"_i18n);
                    });
            } else {
                help();
            }
        });
    };
}

// replaces the %s of an already translated sentence
auto Fill(std::string fmt, const std::string& arg) -> std::string {
    if (const auto pos = fmt.find("%s"); pos != std::string::npos) {
        fmt.replace(pos, 2, arg);
    }
    return fmt;
}

auto DateOf(u64 t) -> std::string {
    char buf[32];
    const auto tt = static_cast<time_t>(t);
    std::strftime(buf, sizeof(buf), "%d.%m %H:%M", std::localtime(&tt));
    return buf;
}

// the steps with their state; A on a step does it or says what to do.
auto WizardGroups(const Snapshot& s, wizard::State& st) -> std::vector<system_info::Group> {
    PsmChargerType charger{};
    u32 pct{};
    psmGetChargerType(&charger);
    psmGetBatteryChargePercentage(&pct);
    const int mmc = s.emummc ? wizard::EMUMMC : wizard::SYSMMC;
    const wizard::Live live{CycleTotal(s), charger != PsmChargerType_Unconnected, pct, mmc, s.other.has_value(),
                            static_cast<u64>(std::time(nullptr))};
    if (wizard::Advance(st, live)) {
        SaveState(st);
        account_restore::ClearReopenHubHint(); // the drain notice did its job
    }
    const auto other_name = mmc == wizard::EMUMMC ? std::string{"sysMMC"} : std::string{"emuMMC"};
    const auto reset_name = st.reset_mmc == wizard::EMUMMC ? std::string{"emuMMC"} : std::string{"sysMMC"};
    const auto match_in = st.reset_mmc == wizard::EMUMMC ? std::string{"sysMMC"} : std::string{"emuMMC"};

    system_info::Group steps{"Calibration steps"_i18n, {}};
    for (const auto& v : wizard::Steps(st, live)) {
        system_info::Row r;
        const bool done = v.mark == wizard::Mark::Done;
        const bool next = v.mark == wizard::Mark::Next;
        r.done = done;
        r.warn = next;
        const auto pct_text = [](u32 x) { return std::to_string(std::min<u32>(x, 999)) + "%"; };
        switch (v.step) {
            case wizard::Step::CheckOther:
                r.label = "1. Other MMC checked"_i18n;
                r.value = done ? "Done"_i18n : Fill("Next: boot %s, Tools → Tools → Battery"_i18n, other_name);
                r.action = Info("Both MMCs keep their own copy of the battery model and their own cycle count. Boot the other MMC once and open Tools → Tools → Battery there; the page saves its copy to the microSD, so this one can compare them."_i18n);
                break;
            case wizard::Step::Reset:
                r.label = "2. Calibration reset"_i18n;
                r.value = done ? "Done "_i18n + DateOf(st.reset_at) + ", " + reset_name : next ? "Next: press A (waits on the charger for 100%)"_i18n : "";
                r.action = AskReset;
                break;
            case wizard::Step::Cycle1:
            case wizard::Step::Cycle2: {
                const bool first = v.step == wizard::Step::Cycle1;
                const u32 target = wizard::CYCLE_DONE;
                const u32 got = v.discharged;
                r.label = first ? "3. First cycle"_i18n
                                : "4. Second cycle"_i18n;
                if (done) {
                    r.value = "Done "_i18n + DateOf(first ? st.full1_at : st.full2_at);
                    if (const auto after = wizard::AfterZero(st, first ? 1 : 2)) {
                        r.value += " · " + "after 0%: "_i18n + std::to_string(after / 60) + " " + "min"_i18n;
                    }
                } else if (next) {
                    r.value = v.discharged >= target ? "Discharged: now charge to 100% and open this page"_i18n
                                                     : "Next: unplug the charger and the Hub starts the drain, or press A for the menu. Done: "_i18n + pct_text(got) + " / 90%";
                }
                const auto help = Info("Two ways to run the battery down; unplug the charger first.\n\n1. Use the console as usual (a game, the screen bright, no sleep) until it switches off by itself.\n2. Let Kefir Hub do it: press A on this step (when it is the next one) and the Hub runs the battery down with the screen at full brightness and the CPU and GPU under load.\n\nThen charge to 100% and open this page: the step counts once 90% of the battery was used since the last step. The chip learns the battery's real capacity on these cycles.\n\nUntil both cycles are done, do not let the battery run flat in the other MMC."_i18n);
                r.action = st.reset_at ? CycleMenu(first ? 1 : 2, help) : help;
                break;
            }
            case wizard::Step::MatchOther:
                r.label = "5. Match in the other MMC"_i18n;
                if (done) {
                    r.value = "Done"_i18n;
                } else if (next) {
                    r.value = mmc == st.reset_mmc ? Fill("Next: boot %s, open this page, A on this step"_i18n, match_in)
                                                  : "Next: press A here"_i18n;
                }
                r.action = mmc == st.reset_mmc && st.reset_at
                    ? (st.full2_at && !done ? std::function<void()>{OfferHop}
                                            : Info("Boot the other MMC, open Tools → Tools → Battery → + and press A on this step there. Its copy then takes the calibrated model and one cycle count for both MMCs; the chip is not written."_i18n))
                    : std::function<void()>{RunMatch};
                break;
            case wizard::Step::Finished:
                r.label = "Calibration finished"_i18n;
                r.value = done ? "Yes"_i18n : "";
                r.action = Info("When all steps are done, both MMCs hold the same calibrated model and one cycle count. Check the first row of Tools → Tools → Battery: the full capacity should now be close to the design capacity, or show the battery's real wear."_i18n);
                break;
        }
        steps.rows.push_back(std::move(r));
    }

    const auto full = s.regs[MAX17050_FullCap];
    const auto design = s.regs[MAX17050_DesignCap];
    system_info::Group now{"Now"_i18n, {
        {"Battery charge"_i18n, std::to_string(pct) + "%" + (charger != PsmChargerType_Unconnected ? " · " + "on the charger"_i18n : std::string{})},
        {"Full capacity / design"_i18n, std::to_string(full / 2) + " / " + std::to_string(design / 2) + " mAh", CapacityLow(full, design)},
    }};
    now.metrics = true; // drawn as the strip of figures above the steps
    if (st.reset_at && !st.full2_at) {
        now.rows.push_back({"Careful"_i18n, "Until step 4 is done, do not let the battery run flat in "_i18n + match_in, true});
    }

    return {std::move(steps), std::move(now)};
}

// Runs about once a second while the calibration page is open. When a drain cycle is the next step and the charger is
// out, the Hub starts the drain by itself: "charge to 100 %, unplug the charger, and the Hub drains the console".
void AutoDrainTick() {
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    if (charger != PsmChargerType_Unconnected) {
        SetAutoDrainSuppressed(false); // a new charge: the next unplug may start the drain again
        return;
    }
    if (AutoDrainSuppressed() || App::GetProgressActive()) {
        return;
    }
    const auto total = CycleTotalNow();
    u32 pct{};
    psmGetBatteryChargePercentage(&pct);
    if (!total) {
        return;
    }
    auto st = LoadState();
    const wizard::Live live{*total, false, pct, 0, false, static_cast<u64>(std::time(nullptr))};
    if (wizard::Advance(st, live)) {
        SaveState(st);
        App::Notify("Cycle counted"_i18n);
    }
    for (const auto& v : wizard::Steps(st, live)) {
        if (v.mark != wizard::Mark::Next) {
            continue;
        }
        if ((v.step == wizard::Step::Cycle1 || v.step == wizard::Step::Cycle2) && st.reset_at) {
            const int cycle = v.step == wizard::Step::Cycle1 ? 1 : 2;
            if (v.discharged < wizard::CYCLE_DONE) {
                App::Notify("The charger is out: the drain starts"_i18n);
                StartDrain(cycle);
            }
        }
        return;
    }
}

void OpenWizard() {
    auto groups = std::make_shared<std::vector<system_info::Group>>();
    App::Push<ui::ProgressBox>(0, "Reading"_i18n, "Battery calibration"_i18n, [groups](auto) -> Result {
        auto s = Read();
        R_UNLESS(s.gauge, Result_ArchiveRead);
        auto st = LoadState();
        *groups = WizardGroups(s, st);
        R_SUCCEED();
    }, [groups](Result rc) {
        if (R_SUCCEEDED(rc)) {
            App::Push<ui::menu::sysinfo::Menu>("Battery calibration"_i18n, std::move(*groups), "Actions"_i18n, ActionsMenu, std::string{}, std::function<void()>{}, AutoDrainTick, []() -> std::vector<system_info::Group> {
                auto snap = Read();
                if (!snap.gauge) {
                    return {};
                }
                auto state = LoadState();
                return WizardGroups(snap, state);
            });
        }
    });
}

} // namespace

namespace detail {

auto ResetAtCutoff(ui::ProgressBox* pbox, std::string& failed) -> Result {
    return ResetNow(pbox, failed);
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
    OpenWizard();
}

void PollInterruptedDrain() {
    static bool finished = false;
    static TimeStamp since_start{};
    if (finished) {
        return;
    }
    if (!DrainWasInterrupted() || CalibrationActive()) { // the calibration deals with its own cut-offs
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
