// Tools → Battery (F.18 step 1): read only. The fuel gauge chip against the copy HOS keeps for this
// MMC, a verdict, and a backup in Battery Desync Fix's batt.cal format plus a full register dump.
#include "battery_page.hpp"
#include "battery_gauge.hpp"
#include "fuel_gauge.hpp"
#include "system_info.hpp"
#include "ui/menus/system_info_menu.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <switch.h>
#include <array>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

namespace sphaira::battery {
namespace {

constexpr const char* BACKUP_DIR = "/config/kefir/battery";
// CAL0 (PRODINFO) offsets the Battery Desync Fix reads: BatteryLot and the raw BatteryVersion.
constexpr s64 CAL0_BATTERY_LOT = 0x2CE0;
constexpr s64 CAL0_BATTERY_VERSION = 0x4310;

struct Snapshot {
    bool gauge{};
    std::array<u16, 256> regs{};
    std::array<bool, 256> read{};
    bool hos_ok{};
    SetSysPtmFuelGaugeParameter hos{};
    int reliability{-1};
    int setcal_version{-1};
    int cal0_version{-1};
    std::string lot;
    bool emummc{};
};

auto Fmt(const char* fmt, auto... args) -> std::string {
    char buf[200];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    return buf;
}

auto Read() -> Snapshot {
    Snapshot s;
    s.emummc = App::IsEmummc();
    {
        FuelGauge gauge;
        s.gauge = gauge.open;
        for (int r = 0; s.gauge && r < 256; r++) {
            if (const auto v = gauge.Read(static_cast<u8>(r))) {
                s.regs[r] = *v;
                s.read[r] = true;
            }
        }
    }
    // the Hub keeps no set:sys session open; every module opens its own
    const bool setsys = R_SUCCEEDED(setsysInitialize());
    ON_SCOPE_EXIT({
        if (setsys) {
            setsysExit();
        }
    });
    if (const auto rc = setsysGetPtmFuelGaugeParameter(&s.hos); R_SUCCEEDED(rc)) {
        s.hos_ok = true;
    } else {
        log_write("[Battery] set:sys GetPtmFuelGaugeParameter: 0x%X\n", rc);
    }
    if (hosversionAtLeast(7, 0, 0)) {
        SetSysPtmCycleCountReliability rel{};
        if (R_SUCCEEDED(setsysGetPtmCycleCountReliability(&rel))) {
            s.reliability = rel;
        }
    }
    if (R_SUCCEEDED(setcalInitialize())) {
        u8 v{};
        if (hosversionAtLeast(6, 0, 0) && R_SUCCEEDED(setcalGetBatteryVersion(&v))) {
            s.setcal_version = v;
        }
        SetBatteryLot lot{};
        if (R_SUCCEEDED(setcalGetBatteryLot(&lot))) {
            s.lot.assign(lot.lot, strnlen(lot.lot, sizeof(lot.lot)));
        }
        setcalExit();
    }
    FsStorage cal0{};
    if (R_SUCCEEDED(fsOpenBisStorage(&cal0, FsBisPartitionId_CalibrationBinary))) {
        u8 v{};
        if (R_SUCCEEDED(fsStorageRead(&cal0, CAL0_BATTERY_VERSION, &v, 1))) {
            s.cal0_version = v;
        }
        fsStorageClose(&cal0);
    }
    return s;
}

auto ChipParams(const Snapshot& s) -> Params {
    Params p;
    for (size_t i = 0; i < PARAM_REGS.size(); i++) {
        p.v[i] = s.regs[PARAM_REGS[i]];
    }
    return p;
}

auto HosParams(const Snapshot& s) -> Params {
    const auto& h = s.hos;
    return Params{{h.rcomp0, h.tempc0, h.fullcap, h.fullcapnom, h.lavgempty, h.qresidual00, h.qresidual10, h.qresidual20, h.qresidual30}};
}

auto MmcName(const Snapshot& s) -> std::string {
    return s.emummc ? "emuMMC" : "sysMMC";
}

auto FindingRow(const Diagnosis& r) -> system_info::Row {
    switch (r.finding) {
        case Finding::NoSign: return {"Verdict"_i18n, "No sign of desync on this MMC"_i18n};
        case Finding::RestorePending: return {"Restore flag (MiscCFG bit 15)"_i18n, "Set: HOS rewrites the chip from this MMC at the next start"_i18n};
        case Finding::PowerOnReset: return {"Restart flag (Status POR)"_i18n, "Set: the chip restarted and was not set up since"_i18n};
        case Finding::CopyDiffers: return {"Chip vs this MMC's copy"_i18n, "Differ: "_i18n + r.detail};
        case Finding::CopyUnknown: return {"Chip vs this MMC's copy"_i18n, "Not compared: the copy could not be read"_i18n};
        case Finding::VersionMismatch: return {"Battery version"_i18n, "CAL0 and set:cal disagree"_i18n};
        case Finding::LiteNoCycles: return {"Cycle count"_i18n, "Not trusted: charge limit falls back to 4.192 V"_i18n};
        case Finding::BrokenModel: {
            const bool copy = r.detail.ends_with(" copy");
            const auto pct = r.detail.substr(0, r.detail.find(' '));
            return {"Desync: learned capacity"_i18n, pct + "% " + (copy ? "of design, the saved copy too"_i18n : "of design: the chip's model is broken"_i18n)};
        }
        case Finding::LowCapacity: return {"Learned capacity"_i18n, r.detail + "% " + "of design: worn or mislearned"_i18n};
    }
    return {};
}

auto Groups(const Snapshot& s) -> std::vector<system_info::Group> {
    std::vector<system_info::Group> out;
    if (!s.gauge) {
        out.push_back({"Verdict"_i18n, {{"Fuel gauge"_i18n, "Cannot open the chip over i2c"_i18n}}});
        return out;
    }
    const auto chip = ChipParams(s);
    const auto hos = HosParams(s);
    Input in{chip, hos, s.regs[MAX17050_MiscCFG], s.regs[MAX17050_Status], s.regs[MAX17050_DesignCap],
             s.regs[MAX17050_FullCap], s.cal0_version, s.setcal_version, s.reliability};
    in.hos_read = s.hos_ok;
    system_info::Group verdict{"Verdict"_i18n, {}};
    for (const auto& r : Diagnose(in)) {
        verdict.rows.push_back(FindingRow(r));
    }
    out.push_back(std::move(verdict));

    // the nine learned parameters side by side: a row that differs is the thing to look at
    system_info::Group params{"Model: chip / "_i18n + MmcName(s), {}};
    for (size_t i = 0; i < PARAM_REGS.size(); i++) {
        const bool cap = i == 2 || i == 3; // FullCap, FullCapNom: 0.5 mAh per lsb
        std::string v = s.hos_ok ? Fmt("%04X / %04X", chip.v[i], hos.v[i]) : Fmt("%04X / ----", chip.v[i]);
        if (cap) {
            v += s.hos_ok ? Fmt("  (%u / %u mAh)", chip.v[i] / 2, hos.v[i] / 2) : Fmt("  (%u mAh)", chip.v[i] / 2);
        }
        params.rows.push_back({std::string{PARAM_NAMES[i]} + (s.hos_ok && chip.v[i] != hos.v[i] ? "  ≠" : ""), v});
    }
    if (!s.hos_ok) {
        params.rows.push_back({"System copy"_i18n, "Cannot read set:sys"_i18n});
    }
    out.push_back(std::move(params));

    out.push_back({"Fuel gauge (chip)"_i18n, {
        {"Charge"_i18n, Fmt("%u%%", s.regs[MAX17050_RepSOC] >> 8)},
        {"Remaining capacity"_i18n, Fmt("%u mAh", s.regs[MAX17050_RepCap] / 2)},
        {"Full capacity now"_i18n, Fmt("%u mAh", s.regs[MAX17050_FullCap] / 2)},
        {"Design capacity"_i18n, Fmt("%u mAh", s.regs[MAX17050_DesignCap] / 2)},
        {"Cell voltage"_i18n, Fmt("%.0f mV", (s.regs[MAX17050_VCell] >> 3) * 0.625)},
        {"Open-circuit voltage"_i18n, Fmt("%.0f mV", (s.regs[MAX17050_VFOCV] >> 3) * 0.625)},
        {"Cycles register"_i18n, Fmt("%.2f", s.regs[MAX17050_Cycles] / 100.0)},
        {"MiscCFG / Status"_i18n, Fmt("%04X / %04X", s.regs[MAX17050_MiscCFG], s.regs[MAX17050_Status])},
    }});

    system_info::Group sys{"Saved by the system"_i18n + " (" + MmcName(s) + ")", {}};
    if (s.hos_ok) {
        sys.rows.push_back({"Charge cycles"_i18n, Fmt("%.2f", s.hos.cycles_actual / 100.0)});
    }
    if (s.reliability >= 0) {
        sys.rows.push_back({"Cycle count reliability"_i18n, s.reliability ? "On"_i18n : "Off"_i18n});
    }
    out.push_back(std::move(sys));

    system_info::Group bat{"Battery"_i18n, {}};
    if (s.setcal_version >= 0) {
        bat.rows.push_back({"Type"_i18n, s.setcal_version == 2 ? "LiHV (4.32 V)" : "LiPo (4.2 V)"});
    }
    bat.rows.push_back({"Version: CAL0 / set:cal"_i18n, Fmt("%d / %d", s.cal0_version, s.setcal_version)});
    if (!s.lot.empty()) {
        bat.rows.push_back({"Battery lot"_i18n, s.lot});
    }
    out.push_back(std::move(bat));
    return out;
}

auto Backup(const Snapshot& s, std::string& name) -> Result {
    R_UNLESS(s.gauge, Result_ArchiveRead);
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    fs.CreateDirectoryRecursively(BACKUP_DIR);

    char stamp[32];
    const auto now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    name = Fmt("%s/%s-%s", BACKUP_DIR, stamp, s.emummc ? "emummc" : "sysmmc");

    const auto cal = Pack(ChipParams(s), s.hos.cycles, s.hos.cycles_actual);
    const auto* p = reinterpret_cast<const u8*>(&cal);
    R_TRY(fs.write_entire_file(fs::FsPath{name + ".cal"}, std::vector<u8>(p, p + sizeof(cal))));

    std::string txt = "Kefir Hub " APP_VERSION " battery backup\n";
    txt += "MMC: " + MmcName(s) + "\n";
    txt += Fmt("battery version: CAL0 %d, set:cal %d; lot %s\n", s.cal0_version, s.setcal_version, s.lot.c_str());
    txt += Fmt("cycle count reliability: %d\n", s.reliability);
    const auto hos = HosParams(s);
    txt += s.hos_ok ? "set:sys PtmFuelGaugeParameter:" : "set:sys PtmFuelGaugeParameter: not read; the .cal has 0 cycles\nlast value seen:";
    for (const auto v : hos.v) {
        txt += Fmt(" %04X", v);
    }
    txt += Fmt(" pad %04X cycles %u\n\nMAX17050 registers (-- = not read):\n", s.hos.cycles, s.hos.cycles_actual);
    for (int r = 0; r < 256; r++) {
        txt += (r % 16 == 0 ? Fmt("%02X:", r) : std::string{}) + (s.read[r] ? Fmt(" %04X", s.regs[r]) : std::string{" ----"});
        if (r % 16 == 15) {
            txt += "\n";
        }
    }
    R_TRY(fs.write_entire_file(fs::FsPath{name + ".txt"}, std::vector<u8>(txt.begin(), txt.end())));
    log_write("[Battery] backup %s.cal / .txt\n", name.c_str());
    R_SUCCEED();
}

void BackUp() {
    auto name = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Back up"_i18n, "Battery"_i18n, [name](auto) -> Result {
        return Backup(Read(), *name);
    }, [name](Result rc) {
        if (R_SUCCEEDED(rc)) {
            App::Notify("Saved: "_i18n + *name + ".cal");
        } else {
            App::PushErrorBox(rc, "Backup failed"_i18n);
        }
    });
}

} // namespace

void Open() {
    auto groups = std::make_shared<std::vector<system_info::Group>>();
    App::Push<ui::ProgressBox>(0, "Reading"_i18n, "Battery"_i18n, [groups](auto) -> Result {
        *groups = Groups(Read());
        R_SUCCEED();
    }, [groups](Result rc) {
        if (R_SUCCEEDED(rc)) {
            App::Push<ui::menu::sysinfo::Menu>("Battery"_i18n, std::move(*groups), "Back up"_i18n, BackUp);
        }
    });
}

} // namespace sphaira::battery
