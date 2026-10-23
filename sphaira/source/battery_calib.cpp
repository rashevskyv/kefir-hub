// Tools > Battery calibration (docs/dev/BATTERY-CALIBRATION.md, "Cutoff pipeline"): the pages of the guide
// (docs/site/en/battery-wizard) and the handling of whatever the marker on the SD says is going on. The marker
// (battery_calib_state.hpp) lives in /config/kefir/battery/calib.txt; the drain and the charge run on the drain page
// (battery_drain.cpp, StartCalibRun); the zero is the factory reset at the cut-off (battery_write.cpp, ResetAtCutoff).
#include "battery_page.hpp"
#include "battery_calib_state.hpp"
#include "battery_calib_log.hpp"
#include "battery_calib_ui.hpp"
#include "battery_internal.hpp"
#include "fuel_gauge.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::battery {
namespace {

using namespace calib;
namespace cu = calib_ui;
using ui::menu::tools::SystemToolsMenu;
using ui::menu::settings::SettingsItem;
using ui::menu::settings::SettingsItemKind;

constexpr const char* MARKER_PATH = "/config/kefir/battery/calib.txt";
constexpr const char* LOG_PATH = "/config/kefir/battery/calib-log.csv";

// the Hub looks at the marker once a run: a later look must not count the same cut-off twice
bool g_checked = false;

auto LoadMarker() -> std::optional<Marker> {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd{}.read_entire_file(MARKER_PATH, data)) || data.empty()) {
        return std::nullopt;
    }
    return Parse({reinterpret_cast<const char*>(data.data()), data.size()});
}

void SaveMarker(const Marker& m) {
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively(detail::BACKUP_DIR);
    const auto text = Format(m);
    sd.write_entire_file(MARKER_PATH, std::vector<u8>(text.begin(), text.end()));
    sd.Commit();
}

auto ReadBattery() -> Battery {
    Battery b;
    u32 pct{};
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) {
        b.percent = static_cast<int>(pct);
    }
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    b.charger_in = charger != PsmChargerType_Unconnected;
    // when this boot began: now minus the time the console has been up
    b.boot_time = static_cast<std::int64_t>(std::time(nullptr)) - static_cast<std::int64_t>(armTicksToNs(armGetSystemTick()) / 1'000'000'000ULL);
    return b;
}

auto Now() -> std::int64_t {
    return static_cast<std::int64_t>(std::time(nullptr));
}

auto ChargerIn() -> bool {
    return ReadBattery().charger_in;
}

auto StepLabel(int i) -> std::string {
    return "Step"_i18n + " " + std::to_string(i) + " / 5";
}

void Say(const std::string& text, std::function<void()> then = {}) {
    App::Push<ui::OptionBox>(text, "OK"_i18n, [then](auto) {
        if (then) {
            then();
        }
    });
}

// the brightness and auto-brightness the user has now, for the marker (what the Hub gives back later)
auto ScreenNow() -> std::pair<int, bool> {
    float brightness = 0.5f;
    bool auto_on = false;
    if (R_SUCCEEDED(lblInitialize())) {
        lblGetCurrentBrightnessSetting(&brightness);
        lblIsAutoBrightnessControlEnabled(&auto_on);
        lblExit();
    }
    return {static_cast<int>(brightness * 100.f + 0.5f), auto_on};
}

void ShowSteps();
void ShowStartPage();
void ShowZeroReady();
void ShowReport();

// ---- the runs -----------------------------------------------------------------------------------------------------------

void StartDrainRun(Marker& m) {
    const auto b = ReadBattery();
    StartPhase(m, b.percent, Now(), b.boot_time);
    SaveMarker(m);
    CalibRun run;
    run.tail = m.cutoffs > 0;
    run.step = static_cast<int>(m.step);
    StartCalibRun(std::move(run));
}

void OnChargeDone() {
    auto m = LoadMarker();
    if (!m) {
        return;
    }
    m->phase = Phase::Drain; // 100 % and the charger came out: the drain of this cycle starts at once
    StartDrainRun(*m);
}

void StartChargeRun(Marker& m) {
    const auto b = ReadBattery();
    StartPhase(m, b.percent, Now(), b.boot_time);
    SaveMarker(m);
    CalibRun run;
    run.charge = true;
    run.step = static_cast<int>(m.step);
    run.on_charge_done = OnChargeDone;
    StartCalibRun(std::move(run));
}

// ---- the intro ----------------------------------------------------------------------------------------------------------

void ShowIntro2() {
    App::Push<cu::Page>("What happens to the console"_i18n, "Step by step: drain, charge, report. The cut-off is the real end."_i18n,
        std::vector<cu::Line>{
            cu::Head("Switching off and on (this is normal)"_i18n),
            cu::Para("When the battery is almost empty, the console switches itself off. You switch it on again, and it still works for a short time, because the voltage rises a little after the switch-off. Then it switches off again. This can repeat many times in a row. It is a needed part of the process: switch the console on each time and go through it to the end. It can take long."_i18n),
            cu::Head("When the console does not start any more"_i18n),
            cu::Para("Plug the charger in. The console may not start at once: the battery is empty, and it needs to lie on the charger for a while."_i18n),
            cu::Head("As soon as it has started"_i18n),
            cu::Para("Do not wait: open Kefir Hub at once. The Hub records the voltage at which the console switched off, sets the zero and goes on with the calibration."_i18n),
        },
        "Start calibration"_i18n, [](cu::Page& p) {
            if (!LoadMarker()) {
                const auto screen = ScreenNow();
                SaveMarker(Begin(screen.first, screen.second, Now()));
            }
            g_checked = true; // a fresh start: nothing to find yet
            p.SetPop();
            ShowSteps();
        });
}

void ShowIntro() {
    App::Push<cu::Page>("Battery calibration"_i18n, "Step by step: drain, charge, report. The cut-off is the real end."_i18n,
        std::vector<cu::Line>{
            cu::Head("Why this is needed"_i18n),
            cu::Para("With time the charge gauge goes wrong. The console can switch off at 35 %, or show 0 % for a whole hour. The wizard teaches the gauge again, so the percent is honest."_i18n),
            cu::Head("How it works"_i18n),
            cu::Para("We drain the battery to the very end: until the console switches itself off (about 3.2 V under load). That is the real zero. Then we charge and drain it fully, two times."_i18n),
            cu::Head("What you do"_i18n),
            cu::Para("Take the charger out and leave the console on a table: the Hub gives it a full load by itself. When the console switches off, switch it on again. When it stops starting at all, plug the charger in and, as soon as the console starts, open Kefir Hub at once."_i18n),
            cu::Head("How long it takes"_i18n),
            cu::Para("Several hours, with breaks for charging. Your games and saves are not touched. Before it writes to the gauge, the Hub makes a backup."_i18n),
        },
        "Go on"_i18n, [](cu::Page& p) {
            p.SetPop();
            ShowIntro2();
        });
}

// ---- the zero (step 2) ---------------------------------------------------------------------------------------------------

struct ZeroFacts {
    int n_mah{-1};          // the gauge's remaining charge now
    int x_mah{-1};          // its last logged value before the cut-off
    std::string volts{"?"}; // the last voltage in the log
};

auto ReadZeroFacts() -> ZeroFacts {
    ZeroFacts f;
    FuelGauge g;
    if (g.open) {
        if (const auto v = g.Read(MAX17050_RepCap)) {
            f.n_mah = static_cast<int>(*v) / 2; // 0.5 mAh a step (10 mOhm), as the drain page reads it
        }
    }
    std::vector<u8> log;
    if (R_SUCCEEDED(fs::FsNativeSd{}.read_entire_file(LOG_PATH, log)) && !log.empty()) {
        const std::string_view view{reinterpret_cast<const char*>(log.data()), log.size()};
        f.x_mah = LastDrainRemainingMah(view);
        if (const auto sample = LastSample(view)) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", sample->mv / 1000.0);
            f.volts = buf;
        }
    }
    return f;
}

auto Mah(int v) -> std::string {
    return v < 0 ? std::string{"—"} : std::to_string(v) + " mAh";
}

auto ZeroLines(const ZeroFacts& f, bool done, int brightness_before) -> std::vector<cu::Line> {
    std::vector<cu::Line> lines{
        cu::Item("The console switched off at the cut-off, and you charged it."_i18n),
        cu::Item("Now the Hub sets this point as zero."_i18n),
        cu::Row("Last voltage in the log before the cut-off"_i18n, f.volts + " V"),
        cu::Boxes({{"Gauge counter N"_i18n, Mah(f.n_mah)}, {"Last logged value X"_i18n, Mah(f.x_mah)},
                   {"Result N − X"_i18n, f.n_mah >= 0 && f.x_mah >= 0 ? Mah(std::max(0, f.n_mah - f.x_mah)) : Mah(-1)}}),
    };
    if (done) {
        lines.push_back(cu::Ok("Factory model written"_i18n));
        lines.push_back(cu::Ok("The backup was made."_i18n));
        if (brightness_before >= 0) {
            lines.push_back(cu::Ok("Auto-brightness is on again; brightness set back to what it was before the calibration"_i18n + " (" + std::to_string(brightness_before) + " %)."));
        }
    } else {
        lines.push_back(cu::Dim("A backup of the gauge is made first, then the factory model is written. The values above are kept for the report."_i18n));
    }
    return lines;
}

void ShowZeroDone(const ZeroFacts& f, int brightness_before) {
    App::Push<cu::Page>("Gauge zero"_i18n, StepLabel(2), ZeroLines(f, true, brightness_before), "Go on"_i18n, [](cu::Page& p) {
        p.SetPop();
        ShowSteps();
    });
}

void RunZero(ZeroFacts f, cu::Page& page) {
    auto failed = std::make_shared<std::string>();
    auto* ready = &page;
    App::Push<ui::ProgressBox>(0, "Setting the zero"_i18n, "Battery calibration"_i18n, [failed, f](auto pbox) -> Result {
        log_write("[Calib] zero: N %d mAh, X %d mAh, last logged voltage %s V\n", f.n_mah, f.x_mah, f.volts.c_str());
        return detail::ResetAtCutoff(pbox, *failed);
    }, [failed, f, ready](Result rc) {
        if (R_FAILED(rc)) {
            Say("The zero was not set. A backup was made first; open the Battery page to restore it."_i18n + (failed->empty() ? std::string{} : "\n\n" + *failed));
            return;
        }
        int brightness_before = -1;
        if (auto m = LoadMarker()) {
            brightness_before = m->brightness_before;
            m->last_logged_remaining_mah = f.x_mah;
            m->zero_n_mah = f.n_mah;
            ZeroDone(*m);
            SaveMarker(*m);
        }
        ready->SetPop();
        ShowZeroDone(f, brightness_before);
    });
}

void ShowZeroReady() {
    const auto f = ReadZeroFacts();
    App::Push<cu::Page>("Gauge zero"_i18n, StepLabel(2), ZeroLines(f, false, -1), "Set the zero"_i18n, [f](cu::Page& p) { RunZero(f, p); },
        [](cu::Page& p) { p.SetPop(); ShowSteps(); });
}

// ---- what the Hub finds when it starts ---------------------------------------------------------------------------------

// returns true when it took over the screen (a page or a dialog)
auto Resume(Marker m) -> bool {
    if (g_checked) {
        return false;
    }
    g_checked = true;
    const auto found = Check(m, ReadBattery());
    if (found != Found::Nothing) {
        SaveMarker(m);
    }
    switch (found) {
        case Found::CutoffSeen:
            App::Notify("The console switched off: the drain goes on slowly"_i18n);
            StartDrainRun(m);
            return true;
        case Found::HubRestarted:
            App::Notify("The Hub started again: the drain goes on"_i18n);
            StartDrainRun(m); // same boot, same mode: only the Hub was left or replaced
            return true;
        case Found::DeadWithCharger:
            ShowZeroReady();
            return true;
        case Found::ChargeDone:
            Say("The charge is done: 100 %. Now take the charger out: the drain starts by itself."_i18n, []() { ShowSteps(); });
            return true;
        case Found::ChargeInterrupted:
            Say("The charge was interrupted: the charger came out before 100 %. Start the charge again."_i18n, []() { ShowSteps(); });
            return true;
        case Found::AskDrainFinished:
            App::Push<ui::OptionBox>("Did the console switch off from the drain?"_i18n, "No"_i18n, "Yes"_i18n, 1, [m](auto op) mutable {
                if (op && *op == 1) {
                    DrainConfirmed(m);
                    SaveMarker(m);
                }
                ShowSteps();
            });
            return true;
        default:
            return false;
    }
}

// ---- the start pages of the steps (the guide's "start" screen) ------------------------------------------------------------

void ShowStartPage() {
    auto m = LoadMarker();
    if (!m) {
        return;
    }
    const bool cycle = m->step == Step::Cycle1 || m->step == Step::Cycle2;
    const int number = static_cast<int>(m->step);
    std::vector<cu::Line> lines;
    std::string label;
    std::string title;
    cu::Page::Act act;
    const auto back = [](cu::Page& p) { p.SetPop(); ShowSteps(); };

    if (m->step == Step::Drain) {
        title = "Drain to the dead battery"_i18n;
        lines.push_back(cu::Check("Charger is out"_i18n, "Take the charger out."_i18n, []() { return !ChargerIn(); }));
        lines.push_back(cu::Para("Now the Hub drains the battery to the very end, until the console switches itself off. To keep the drain steady, the Hub loads the CPU and the GPU fully and sets the highest brightness."_i18n));
        lines.push_back(cu::Para("After 3 minutes (or at once, on Minus), the screen saver starts: the screen goes black and the numbers slowly move across it. This stops the pixels from burning in. The right stick changes the brightness, the left stick moves the numbers."_i18n));
        lines.push_back(cu::Para("When the console switches off, that is normal. Just switch it on again. The Hub goes on with the drain by itself, now at minimum brightness and weak load, to squeeze out the last percent. Keep switching it on until it does not start at all."_i18n));
        lines.push_back(cu::Para("When it does not start any more: plug in the charger and let it lie on it for a while. It may not start at once. As soon as it starts, open Kefir Hub at once. Do not wait."_i18n));
        lines.push_back(cu::Para("The brightness is lowered only while the console drains. As soon as you plug in the charger, the Hub gives back your brightness and auto-brightness."_i18n));
        lines.push_back(cu::Warn("In this step keep the Hub open: it needs the log."_i18n));
        label = "Start the drain"_i18n;
        act = [](cu::Page& p) {
            if (ChargerIn()) {
                App::Notify("Take the charger out."_i18n);
                return;
            }
            auto cur = LoadMarker();
            if (!cur) {
                return;
            }
            p.SetPop();
            StartDrainRun(*cur);
        };
    } else if (cycle && m->phase == Phase::Charge) {
        title = m->step == Step::Cycle1 ? "Cycle 1"_i18n : "Cycle 2"_i18n;
        lines.push_back(cu::Check("Charger is plugged in."_i18n, "Plug the charger in."_i18n, []() { return ChargerIn(); }));
        lines.push_back(cu::Item("You can leave the console like this. You can also use it while it charges. When the charge reaches 100 %, come back to Kefir Hub and open Battery calibration."_i18n));
        lines.push_back(cu::Item("The screen stays on: so the console charges to the end. The screensaver saves the screen."_i18n));
        label = m->phase_started ? "Open the charge screen"_i18n : "Start charging"_i18n;
        act = [](cu::Page& p) {
            if (!ChargerIn()) {
                App::Notify("Plug the charger in."_i18n);
                return;
            }
            auto cur = LoadMarker();
            if (!cur) {
                return;
            }
            p.SetPop();
            StartChargeRun(*cur);
        };
    } else if (cycle) {
        title = m->step == Step::Cycle1 ? "Cycle 1"_i18n : "Cycle 2"_i18n;
        lines.push_back(cu::Warn("Full. Now the drain: take the charger out."_i18n));
        lines.push_back(cu::Check("Charger is out"_i18n, "Take the charger out."_i18n, []() { return !ChargerIn(); }));
        lines.push_back(cu::Para("You can also drain it yourself: play, or simply leave the console on, and you may close the Hub. When the console switches off, switch it on with the charger in and open Kefir Hub: it will ask whether it was the drain."_i18n));
        label = "Start the drain"_i18n;
        act = [](cu::Page& p) {
            if (ChargerIn()) {
                App::Notify("Take the charger out."_i18n);
                return;
            }
            auto cur = LoadMarker();
            if (!cur) {
                return;
            }
            p.SetPop();
            StartDrainRun(*cur);
        };
    } else {
        return;
    }
    App::Push<cu::Page>(title, StepLabel(number), std::move(lines), label, std::move(act), back);
}

// ---- the report (step 5) -------------------------------------------------------------------------------------------------

void ShowReport() {
    struct Facts {
        int full_mah{-1};
        int design_mah{-1};
        std::vector<Cutoff> cuts;
        int zero_n{-1};
        int zero_x{-1};
        bool large{};
        bool low{};
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
        std::vector<u8> log;
        if (R_SUCCEEDED(fs::FsNativeSd{}.read_entire_file(LOG_PATH, log)) && !log.empty()) {
            facts->cuts = Cutoffs({reinterpret_cast<const char*>(log.data()), log.size()});
        }
        if (const auto m = LoadMarker()) {
            facts->zero_n = m->zero_n_mah;
            facts->zero_x = m->last_logged_remaining_mah;
        }
        R_SUCCEED();
    }, [facts](Result rc) {
        if (R_FAILED(rc)) {
            Say("The gauge could not be read."_i18n);
            return;
        }
        std::vector<cu::Line> lines;
        lines.push_back(cu::Row("Learned capacity"_i18n, std::to_string(facts->full_mah) + " / " + std::to_string(facts->design_mah) + " mAh"));
        lines.push_back(cu::Row("Verdict"_i18n, facts->large ? "Larger battery is built in."_i18n
            : facts->low ? "Sign of desync: learned capacity below 90 % of design."_i18n : "Normal: no sign of desync."_i18n));
        lines.push_back(cu::Head("Gauge before each cut-off"_i18n));
        // per drain: how many cut-offs it had, so "cut-off 2/2" reads right
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
            const auto where = c.step == 1 ? "Drain to zero"_i18n : "Cycle"_i18n + " " + (c.step == 3 ? "1" : "2");
            const auto sensor = c.gauge_pct >= 0 ? std::to_string(c.gauge_pct) + " %" : "not measured"_i18n;
            lines.push_back(cu::Item(where + " · " + "cut-off"_i18n + " " + std::to_string(index) + "/" + std::to_string(total) + ": " + v + " V · " +
                                     "sensor"_i18n + ": " + sensor));
        }
        if (facts->zero_n >= 0) {
            lines.push_back(cu::Item("Zero"_i18n + ": N " + Mah(facts->zero_n) + ", X " + Mah(facts->zero_x)));
        }
        lines.push_back(cu::Item("Written: calibration on the microSD, copy in this system."_i18n));
        App::Push<cu::Page>("Calibration report"_i18n, StepLabel(5), std::move(lines), "Go on"_i18n, [](cu::Page& p) {
            if (auto m = LoadMarker()) {
                ReportSeen(*m);
                TransferDone(*m);
                SaveMarker(*m);
            }
            p.SetPop();
            ShowSteps();
        });
    });
}

// ---- the list of steps ----------------------------------------------------------------------------------------------------

enum class Mark { Done, Active, Locked };

auto StateText(Mark m) -> std::string {
    return m == Mark::Done ? "Done"_i18n : m == Mark::Active ? "Active"_i18n : "Locked"_i18n;
}

auto PhaseText(const Marker& m) -> std::string {
    if (m.phase == Phase::Charge) {
        return m.phase_started ? "the charge is going on"_i18n : "next: charge to 100 %"_i18n;
    }
    return m.phase_started ? "the drain is going on"_i18n : "next: drain to the cut-off"_i18n;
}

void ShowSteps() {
    auto m = LoadMarker();
    if (!m) {
        return;
    }
    const auto at = [&m](Step s) { return static_cast<int>(m->step) > static_cast<int>(s) ? Mark::Done : m->step == s ? Mark::Active : Mark::Locked; };
    auto self = std::make_shared<SystemToolsMenu*>(nullptr);
    const auto open = [self](std::function<void()> then) {
        return std::function<void()>([self, then]() {
            if (*self) {
                (*self)->SetPop();
            }
            then();
        });
    };
    struct Row {
        std::string label, detail;
        Mark mark;
        std::function<void()> action;
    };
    std::vector<Row> rows;
    rows.push_back({"1. Drain to the dead battery"_i18n, m->step == Step::Drain ? (m->phase_started ? "the drain is going on"_i18n : "next: drain to the cut-off"_i18n) : std::string{},
        at(Step::Drain), open(ShowStartPage)});
    rows.push_back({"2. Charge and set the zero"_i18n, m->step == Step::Zero ? "next: set the zero"_i18n : std::string{}, at(Step::Zero), open(ShowZeroReady)});
    rows.push_back({"3. Cycle 1: charge, then drain"_i18n, m->step == Step::Cycle1 ? PhaseText(*m) : std::string{}, at(Step::Cycle1), open(ShowStartPage)});
    rows.push_back({"4. Cycle 2: charge, then drain"_i18n, m->step == Step::Cycle2 ? PhaseText(*m) : std::string{}, at(Step::Cycle2), open(ShowStartPage)});
    rows.push_back({"5. Report"_i18n, {}, static_cast<int>(m->step) >= static_cast<int>(Step::Transfer) ? Mark::Done : m->step == Step::Report ? Mark::Active : Mark::Locked,
        open(ShowReport)});

    std::vector<SettingsItem> items;
    for (auto& row : rows) {
        const auto text = StateText(row.mark) + (row.detail.empty() ? std::string{} : ": " + row.detail);
        // an active row acts; the report also opens once it is done; locked rows and finished steps do nothing
        const bool acts = row.mark == Mark::Active || (row.mark == Mark::Done && row.label.starts_with("5."));
        items.push_back(SettingsItem{row.label, text, {}, acts ? row.action : std::function<void()>{}, SettingsItemKind::Normal});
    }
    items.push_back(SettingsItem{"Remembered"_i18n,
        "The calibration is remembered on the SD card: you can close the Hub and use the console between the steps."_i18n, {}, {}, SettingsItemKind::Normal});
    auto menu = std::make_unique<SystemToolsMenu>("Battery calibration"_i18n, std::move(items));
    *self = menu.get();
    App::Push(std::move(menu));
}

} // namespace

auto CalibrationActive() -> bool {
    const auto m = LoadMarker();
    return m && m->step != Step::Finished;
}

void OpenCalibration() {
    const auto m = LoadMarker();
    if (!m || m->step == Step::Finished) {
        ShowIntro();
        return;
    }
    if (!Resume(*m)) {
        ShowSteps();
    }
}

void PollCalibration() {
    static bool done = false;
    static TimeStamp since_start{};
    if (done) {
        return;
    }
    // wait until the UI has settled and nothing else runs
    if (since_start.GetSeconds() < 10 || App::GetProgressActive()) {
        return;
    }
    done = true;
    const auto m = LoadMarker();
    if (m && m->step != Step::Finished && m->phase_started) {
        Resume(*m);
    }
}

} // namespace sphaira::battery
