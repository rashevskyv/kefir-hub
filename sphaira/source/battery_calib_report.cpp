// Tools > Battery calibration: the report (step 4), the transfer to the other MMC (step 5) and the ledger that tells every
// system which calibration it holds (docs/dev/BATTERY-CALIBRATION.md, "Transfer to the other MMC"). The steps are battery_calib.cpp.
#include "battery_page.hpp"
#include "battery_calib_state.hpp"
#include "battery_calib_internal.hpp"
#include "battery_cutoff.hpp"
#include "battery_calib_log.hpp"
#include "battery_calib_ui.hpp"
#include "battery_internal.hpp"
#include "fuel_gauge.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::battery {

using namespace calib;
namespace cu = calib_ui;
using namespace calib_flow;

namespace {

constexpr const char* LOG_PATH = "/config/kefir/battery/calib-log.csv";
constexpr const char* LEDGER_PATH = "/config/kefir/battery/ledger.txt";

void Say(const std::string& text, std::function<void()> then = {}) {
    App::Push<ui::OptionBox>(text, "OK"_i18n, [then](auto) {
        if (then) {
            then();
        }
    });
}

// ---- the ledger: where a calibration was made and where it was written (several emuMMCs and sysMMC share the SD) ----------

auto LoadLedger() -> cutoff::Ledger {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd{}.read_entire_file(LEDGER_PATH, data)) || data.empty()) {
        return {};
    }
    return cutoff::Parse({reinterpret_cast<const char*>(data.data()), data.size()});
}

void SaveLedger(const cutoff::Ledger& l) {
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively(detail::BACKUP_DIR);
    const auto text = cutoff::Format(l);
    sd.write_entire_file(LEDGER_PATH, std::vector<u8>(text.begin(), text.end()));
    sd.Commit();
}

// the system the Hub runs in: sysMMC, a file-based emuMMC by its folder, a partition emuMMC by its start sector
auto CurrentSystemId() -> std::string {
    if (!App::IsEmummc()) {
        return cutoff::SystemId(false, false, 0, {});
    }
    auto nintendo = App::GetEmummcNintendoPath();
    if (!nintendo.empty()) {
        if (nintendo.ends_with("/Nintendo")) {
            nintendo.resize(nintendo.size() - 9);
        }
        while (!nintendo.empty() && nintendo.front() == '/') {
            nintendo.erase(nintendo.begin());
        }
        return cutoff::SystemId(true, false, 0, nintendo);
    }
    // a partition emuMMC: the sector is in /emuMMC/emummc.ini (hekate writes it)
    std::uint64_t sector = 0;
    std::vector<u8> ini;
    if (R_SUCCEEDED(fs::FsNativeSd{}.read_entire_file("/emuMMC/emummc.ini", ini))) {
        const std::string text(reinterpret_cast<const char*>(ini.data()), ini.size());
        if (const auto pos = text.find("sector="); pos != std::string::npos) {
            sector = std::strtoull(text.c_str() + pos + 7, nullptr, 0);
        }
    }
    return cutoff::SystemId(true, true, sector, {});
}

// the battery model of this console (the CAL0 version and vendor): another battery must not get this calibration
auto ModelOf(const detail::Snapshot& s) -> std::string {
    return std::to_string(s.cal0_version) + std::string(1, s.cal0_vendor ? s.cal0_vendor : '?');
}

} // namespace

namespace calib_flow {

// ---- the report (step 4) -------------------------------------------------------------------------------------------------

void ShowReport() {
    struct Facts {
        int full_mah{-1};
        int design_mah{-1};
        std::vector<Cutoff> cuts;
        bool large{};
        bool low{};
        std::string model;
    };
    auto facts = std::make_shared<Facts>();
    App::Push<ui::ProgressBox>(0, "Reading"_i18n, "Calibration report"_i18n, [facts](auto) -> Result {
        auto s = detail::Read();
        R_UNLESS(s.gauge, Result_ArchiveRead);
        const auto full = s.regs[MAX17050_FullCap];
        const auto design = s.regs[MAX17050_DesignCap];
        facts->full_mah = full / 2;
        facts->design_mah = design / 2;
        facts->large = CapacityHigh(full, design);
        facts->low = CapacityLow(full, design);
        facts->model = ModelOf(s);
        std::vector<u8> log;
        if (R_SUCCEEDED(fs::FsNativeSd{}.read_entire_file(LOG_PATH, log)) && !log.empty()) {
            facts->cuts = Cutoffs({reinterpret_cast<const char*>(log.data()), log.size()});
        }
        R_SUCCEED();
    }, [facts](Result rc) {
        if (R_FAILED(rc)) {
            Say("The gauge could not be read."_i18n);
            return;
        }
        std::vector<cu::Line> lines;
        if (const auto m = LoadMarker(); m && m->zero_at) {
            char v[16];
            std::snprintf(v, sizeof(v), "%.2f V", m->zero_mv / 1000.0);
            lines.push_back(cu::Row("The zero (empty battery)"_i18n, v));
        }
        lines.push_back(cu::Row("Learned capacity"_i18n, std::to_string(facts->full_mah) + " / " + std::to_string(facts->design_mah) + " mAh"));
        lines.push_back(cu::Row("Verdict"_i18n, facts->large ? "Larger battery is built in."_i18n
            : facts->low ? "Sign of desync: learned capacity below 90 % of design."_i18n : "Normal: no sign of desync."_i18n));
        lines.push_back(cu::Head("Gauge before each cut-off"_i18n));
        // per cycle: how many cut-offs it had, so "cut-off 2/2" reads right
        const auto& cuts = facts->cuts;
        const size_t first = cuts.size() > 6 ? cuts.size() - 6 : 0;
        for (size_t i = first; i < cuts.size(); i++) {
            const auto& c = cuts[i];
            int index = 0, total = 0;
            for (size_t k = 0; k < cuts.size(); k++) {
                if (cuts[k].step == c.step) {
                    total++;
                    if (k <= i) {
                        index++;
                    }
                }
            }
            char v[16];
            std::snprintf(v, sizeof(v), "%.2f", c.mv / 1000.0);
            const auto where = "Cycle"_i18n + " " + std::to_string(c.step);
            const auto sensor = c.gauge_pct >= 0 ? std::to_string(c.gauge_pct) + " %" : "not measured"_i18n;
            lines.push_back(cu::Item(where + " · " + "cut-off"_i18n + " " + std::to_string(index) + "/" + std::to_string(total) + ": " + v + " V · " +
                                     "sensor"_i18n + ": " + sensor));
        }
        lines.push_back(cu::Item("Written: calibration on the microSD, copy in this system."_i18n));
        App::Push<cu::Page>("Calibration report"_i18n, StepLabel(4), std::move(lines), "Go on"_i18n, [facts](cu::Page& p) {
            if (auto m = LoadMarker()) {
                if (m->step == Step::Report) {
                    // the calibration is made here: a new number in the ledger; the other systems take it at their next start
                    auto l = LoadLedger();
                    cutoff::RecordCalibration(l, CurrentSystemId(), facts->model, !facts->low, Now());
                    SaveLedger(l);
                }
                ReportSeen(*m);
                SaveMarker(*m);
            }
            p.SetPop();
            ShowTransfer();
        }, cu::Page::Act{}, std::function<bool()>{}, 4);
    });
}

// ---- the transfer to the other MMC (step 5) ---------------------------------------------------------------------------------

void ShowTransfer() {
    const auto l = LoadLedger();
    const auto id = CurrentSystemId();
    std::vector<cu::Line> lines;
    if (l.seq) {
        lines.push_back(cu::Item("Calibration saved on the microSD."_i18n + " " + "Marker"_i18n + ": " + "from"_i18n + " " + cutoff::SystemName(l.origin) + ", " +
                                 "number"_i18n + " " + std::to_string(l.seq) + "."));
        lines.push_back(cu::Item("Open the Hub in the other system. The calibration moves by itself."_i18n));
        // this system and every system the ledger has seen
        std::vector<std::string> systems{id};
        for (const auto& w : l.written) {
            if (std::find(systems.begin(), systems.end(), w.system) == systems.end()) {
                systems.push_back(w.system);
            }
        }
        for (const auto& sys : systems) {
            lines.push_back(cu::Row(cutoff::SystemName(sys), cutoff::WrittenSeq(l, sys) >= l.seq ? "Done"_i18n : "Waiting"_i18n));
        }
    } else {
        lines.push_back(cu::Ok("Calibration finished"_i18n));
    }
    lines.push_back(cu::Dim("The hekate hop is a manual action only."_i18n));
    const bool finish = [] { const auto m = LoadMarker(); return m && m->step == Step::Transfer; }();
    App::Push<cu::Page>("Transfer to the other systems"_i18n, StepLabel(5), std::move(lines), finish ? "Go on"_i18n : std::string{}, [](cu::Page& p) {
        if (auto m = LoadMarker()) {
            TransferDone(*m);
            SaveMarker(*m);
        }
        p.SetPop();
        ShowSteps();
    }, cu::Page::Act{}, std::function<bool()>{}, 5);
}

// at the Hub's start in a system that does not hold the newest calibration yet: write it there, say so, and note it down
void ApplyLedger() {
    auto l = LoadLedger();
    if (!l.seq) {
        return;
    }
    const auto id = CurrentSystemId();
    const auto snap = detail::Read();
    if (!snap.gauge || cutoff::Decide(l, id, ModelOf(snap)) != cutoff::Decision::Apply) {
        return;
    }
    std::string failed;
    if (R_FAILED(detail::MatchNow(failed))) {
        log_write("[Calib] the transfer to %s failed: %s\n", id.c_str(), failed.c_str());
        App::Notify("The battery calibration could not be written here:"_i18n + " " + failed);
        return;
    }
    cutoff::RecordWritten(l, id, Now());
    SaveLedger(l);
    log_write("[Calib] the calibration of %s (number %llu) written to %s\n", l.origin.c_str(), static_cast<unsigned long long>(l.seq), id.c_str());
    App::Notify("Battery calibration was transferred from"_i18n + " " + cutoff::SystemName(l.origin) + " " + "to"_i18n + " " + cutoff::SystemName(id));
}

} // namespace calib_flow

} // namespace sphaira::battery
