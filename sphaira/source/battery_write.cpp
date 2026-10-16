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
#include "fuel_gauge.hpp"
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
#include <memory>
#include <string>
#include <vector>

namespace sphaira::battery {
namespace {

using namespace detail;

constexpr u16 MIN_FULL_MV = 4100;

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
    log_write("[Battery] write result 0x%X %s\n", rc, failed->c_str());
    if (R_SUCCEEDED(rc)) {
        App::Push<ui::OptionBox>(ok_text, "OK"_i18n, [](auto) { Open(); });
    } else {
        App::Push<ui::OptionBox>("The chip was not changed as planned. A backup was saved first; Restore from the last backup puts it back."_i18n
                                 + (failed->empty() ? std::string{} : "\n\n" + *failed), "OK"_i18n, [](auto) { Open(); });
    }
}

void RunReset() {
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Reset from factory data"_i18n, "Battery"_i18n, [failed](auto pbox) -> Result {
        auto s = Read();
        R_UNLESS(s.gauge, Result_ArchiveRead);
        std::string name;
        pbox->NewTransfer("Back up"_i18n);
        R_TRY(Backup(s, name));
        FuelGauge g;
        R_UNLESS(g.open, Result_ArchiveRead);
        pbox->NewTransfer("Writing the factory model"_i18n);
        R_TRY(FactoryInit(g, ChooseTable(s.cal0_version, s.cal0_vendor), *failed));
        pbox->NewTransfer("Saving this MMC's copy"_i18n);
        // a cycle count lost to a reset never comes back: keep the larger of the two MMCs
        u32 cycles = s.hos.cycles_actual;
        if (s.other) {
            cycles = std::max(cycles, s.other->cal.cycles);
        }
        R_TRY(SaveCopy(g, cycles));
        log_write("[Battery] factory init done (table v%d '%c'), cycles %u\n", s.cal0_version, s.cal0_vendor ? s.cal0_vendor : '?', cycles);
        R_SUCCEED();
    }, [failed](Result rc) {
        Done(rc, "The fuel gauge now runs the factory model.\n\nNext: charge to 100%, then use the console on battery until it switches off, and charge it again. Twice. The chip learns the battery on these cycles.\n\nUntil then do not let the battery run flat in the other MMC. Open this page there once and choose Match the other MMC."_i18n, failed);
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

void RunMatch() {
    auto failed = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Match the other MMC"_i18n, "Battery"_i18n, [failed](auto pbox) -> Result {
        auto s = Read();
        if (!s.other) {
            *failed = "The other MMC was not checked yet: boot it and open this page once."_i18n;
            R_THROW(Result_ArchiveRead);
        }
        std::string name;
        R_TRY(Backup(s, name));
        FuelGauge g;
        R_UNLESS(g.open, Result_ArchiveRead);
        R_TRY(SaveCopy(g, std::max(s.hos.cycles_actual, s.other->cal.cycles)));
        log_write("[Battery] matched the other MMC: cycles %u\n", std::max(s.hos.cycles_actual, s.other->cal.cycles));
        R_SUCCEED();
    }, [failed](Result rc) {
        Done(rc, "This MMC's copy now holds the chip's model and the larger cycle count of the two MMCs."_i18n, failed);
    });
}

// the reset needs a full battery: the model starts from "full" and the chip learns from there.
auto ResetGate(std::string& why) -> bool {
    PsmChargerType charger{};
    u32 pct{};
    psmGetChargerType(&charger);
    psmGetBatteryChargePercentage(&pct);
    FuelGauge g;
    const auto vcell = g.Read(MAX17050_VCell);
    const u32 mv = vcell ? static_cast<u32>((*vcell >> 3) * 0.625) : 0;
    if (charger == PsmChargerType_Unconnected) {
        why = "Connect the charger first."_i18n;
    } else if (pct < 100) {
        why = "Charge to 100% first. Now: "_i18n + std::to_string(pct) + "%";
    } else if (mv < MIN_FULL_MV) {
        why = "The battery is not full yet: "_i18n + std::to_string(mv) + " mV";
    } else if (App::GetProgressActive()) {
        why = "Finish or stop the running transfer first."_i18n;
    } else {
        return true;
    }
    return false;
}

} // namespace

void OpenCalibrate() {
    ui::PopupList::Items items{"Reset from factory data"_i18n, "Restore from the last backup"_i18n, "Match the other MMC"_i18n};
    auto popup = std::make_unique<ui::PopupList>("Calibrate"_i18n, std::move(items), [](auto op) {
        if (!op) {
            return;
        }
        if (*op == 0) {
            std::string why;
            if (!ResetGate(why)) {
                App::Push<ui::OptionBox>(why, "OK"_i18n);
                return;
            }
            App::Push<ui::OptionBox>(
                "Write the factory battery model into the fuel gauge? The chip's learned values are replaced; a backup is saved first. Afterwards the console needs two full cycles (empty, then full) to learn the battery."_i18n,
                "Back"_i18n, "Reset"_i18n, 0, [](auto o) {
                    if (o && *o == 1) {
                        RunReset();
                    }
                });
        } else if (*op == 1) {
            RunRestore();
        } else {
            RunMatch();
        }
    });
    popup->SetDescriptions({
        "Battery Desync Fix X: the factory model for this battery, then this MMC's copy. Needs the charger and 100%."_i18n,
        "Battery Desync Fix Y: the last backup of this MMC back into the chip."_i18n,
        "No chip write: this MMC's copy takes the chip's model and the larger cycle count of the two MMCs."_i18n,
    });
    App::Push(std::move(popup));
}

} // namespace sphaira::battery
