// Tools > Battery calibration (docs/dev/BATTERY-CALIBRATION.md, docs/dev/BATTERY-HANDOFF.md 1a): five steps.
// 1 the factory reset, at any charge level (battery_write.cpp, detail::RunReset: the init of 0.14.111-163; the init seeds the
//   counters from the voltage, so the old "charge to 100 % first" is not needed).
// 2 cycle 1: drain to the cut-off; at the next Hub start the last voltage of the drain log is written into the gauge as the
//   zero (battery_zero_write.cpp); then charge to 100 %.
// 3 cycle 2: drain to the cut-off, charge to 100 %.
// 4 report, 5 transfer to the other systems (battery_calib_report.cpp).
// A cycle counts by the drain log alone (battery_calib_log.hpp, CycleDoneByLog): the console cut off, then a charge line saw
// 100 %. The marker on the SD (battery_calib_state.hpp, calib.txt) keeps the step, the zero and what a drain needs to go on
// after a cut-off. The drain and the charge run on the drain page (battery_drain.cpp, StartCalibRun).
#include "battery_page.hpp"
#include "battery_calib_state.hpp"
#include "battery_calib_internal.hpp"
#include "battery_cutoff.hpp"
#include "battery_calib_log.hpp"
#include "battery_calib_ui.hpp"
#include "battery_internal.hpp"
#include "account/account_restore.hpp"
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

using namespace calib;
namespace cu = calib_ui;
using ui::menu::tools::SystemToolsMenu;
using ui::menu::settings::SettingsItem;
using ui::menu::settings::SettingsItemKind;

// shared with battery_calib_report.cpp (battery_calib_internal.hpp)
namespace calib_flow {

constexpr const char* MARKER_PATH = "/config/kefir/battery/calib.txt";
constexpr const char* LOG_PATH = "/config/kefir/battery/calib-log.csv";

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

auto Now() -> std::int64_t {
    return static_cast<std::int64_t>(std::time(nullptr));
}

auto StepLabel(int i) -> std::string {
    return "Step"_i18n + " " + std::to_string(i) + " / " + std::to_string(STEP_COUNT);
}

} // namespace calib_flow

using namespace calib_flow;

namespace {

// the Hub looks at the marker once a run: a later look must not count the same cut-off twice
bool g_checked = false;

auto ReadBattery() -> Battery {
    Battery b;
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    b.charger_in = charger != PsmChargerType_Unconnected;
    // when this boot began: now minus the time the console has been up
    b.boot_time = Now() - static_cast<std::int64_t>(armTicksToNs(armGetSystemTick()) / 1'000'000'000ULL);
    return b;
}

auto ChargerIn() -> bool {
    return ReadBattery().charger_in;
}

auto ReadLog() -> std::string {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd{}.read_entire_file(LOG_PATH, data))) {
        return {};
    }
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

auto CycleName(int cycle) -> std::string {
    return cycle == 2 ? "Cycle 2"_i18n : "Cycle 1"_i18n;
}

// ---- the cycles: counted by the log --------------------------------------------------------------------------------------

// the cycle on screen is done (its drain cut off, then the charge saw 100 %): noted in the marker and in the old wizard
// state (the pages that read it). true = something was counted. `m` is updated.
auto CountCycle(Marker& m) -> bool {
    const int cycle = CycleOf(m.step);
    if (!cycle || m.full_at[cycle - 1]) {
        return false;
    }
    // cycle 1 begins with the reset; cycle 2 where cycle 1 ended: earlier log lines belong to an older run
    const auto st = detail::LoadState();
    const auto after = cycle == 1 ? static_cast<std::int64_t>(st.reset_at) : m.full_at[0];
    const auto done = CycleDoneByLog(ReadLog(), cycle, after);
    if (!done) {
        return false;
    }
    MarkCycleDone(m, done->full_time);
    auto ws = st;
    (cycle == 1 ? ws.full1_at : ws.full2_at) = static_cast<u64>(done->full_time);
    detail::SaveState(ws);
    account_restore::ClearReopenHubHint(); // the drain notice did its job
    log_write("[Calib] cycle %d is done by the log: the drain cut off at %d mV (t=%lld), then the charge saw 100 %% (t=%lld)\n",
              cycle, done->cutoff_mv, static_cast<long long>(done->cutoff_time), static_cast<long long>(done->full_time));
    return true;
}

// the marker with the step the state says; saved when something moved
auto LoadCurrent() -> std::optional<Marker> {
    auto m = LoadMarker();
    if (!m) {
        return m;
    }
    const bool counted = CountCycle(*m);
    if (Sync(*m, detail::LoadState().reset_at != 0) || counted) {
        SaveMarker(*m);
    }
    return m;
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

// ---- the runs -----------------------------------------------------------------------------------------------------------

void OnChargeDone();
void StartChargeRun(const Marker& m);
void WriteZeroThenCharge(Marker m);

void StartDrainRun(Marker& m) {
    StartDrain(m, Now(), ReadBattery().boot_time);
    SaveMarker(m);
    CalibRun run;
    run.tail = m.cutoffs > 0;
    run.step = CycleOf(m.step);
    run.on_charge_done = OnChargeDone;
    run.on_drained = []() { // the console was cut off and the charger is in: the drain is over, the charge follows
        auto cur = LoadCurrent();
        if (!cur || !IsCycle(cur->step)) {
            ShowSteps();
            return;
        }
        cur->drain_started = false;
        SaveMarker(*cur);
        if (cur->step == Step::Cycle1 && !cur->zero_at) {
            WriteZeroThenCharge(*cur);
        } else {
            StartChargeRun(*cur);
        }
    };
    run.on_stop = []() { // the user stopped it on purpose: the next start of the Hub must not take it up again
        if (auto cur = LoadMarker()) {
            cur->drain_started = false;
            SaveMarker(*cur);
        }
    };
    StartCalibRun(std::move(run));
}

// the charge saw 100 % and the charger came out: the cycle is counted, and the next step starts by itself
void OnChargeDone() {
    auto m = LoadCurrent();
    if (m && IsCycle(m->step)) {
        StartDrainRun(*m);
    } else {
        ShowSteps();
    }
}

void StartChargeRun(const Marker& m) {
    CalibRun run;
    run.charge = true;
    run.step = CycleOf(m.step);
    run.on_charge_done = OnChargeDone;
    StartCalibRun(std::move(run));
}

// ---- the zero (in cycle 1, after the cut-off) ----------------------------------------------------------------------------

// cycle 1's drain cut the console off: the last voltage of the log becomes the gauge's empty point, then the charge page
void WriteZeroThenCharge(Marker m) {
    const auto after = static_cast<std::int64_t>(detail::LoadState().reset_at);
    const auto cut = ZeroCutoff(ReadLog(), 1, after);
    if (!cut) {
        log_write("[Zero] the log has no cut-off of cycle 1's drain\n");
        App::Push<ui::OptionBox>("The console switched off, but the drain log has no last voltage. The zero cannot be set from it. Take the charger out and run the drain again."_i18n,
            "OK"_i18n, [](auto) { ShowSteps(); });
        return;
    }
    const int mv = cut->mv;
    auto failed = std::make_shared<std::string>();
    auto marker = std::make_shared<Marker>(std::move(m));
    App::Push<ui::ProgressBox>(0, "Setting the zero"_i18n, std::to_string(mv) + " mV", [failed, mv](auto) -> Result {
        return zero_write::WriteZero(mv, *failed);
    }, [failed, marker, mv](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<ui::OptionBox>("The zero was not written. A backup was saved first."_i18n + (failed->empty() ? std::string{} : "\n\n" + *failed),
                "OK"_i18n, [](auto) { ShowSteps(); });
            return;
        }
        marker->zero_mv = mv;
        marker->zero_at = Now();
        SaveMarker(*marker);
        char v[16];
        std::snprintf(v, sizeof(v), "%.2f", mv / 1000.0);
        App::Push<cu::Page>("Cycle 1: the zero"_i18n, StepLabel(2),
            std::vector<cu::Line>{
                cu::Ok("The console switched off at"_i18n + " " + v + " V. " + "This is the empty battery."_i18n),
                cu::Ok("Written into the gauge: this voltage is now 0 %."_i18n),
                cu::Item("Now plug the charger in and charge to 100 %. Cycle 1 is then done, and cycle 2 begins."_i18n),
            },
            "Go on"_i18n, [marker](cu::Page& p) {
                p.SetPop();
                StartChargeRun(*marker);
            }, cu::Page::Act{}, std::function<bool()>{}, 2);
    });
}

// ---- the intro ----------------------------------------------------------------------------------------------------------

void ShowIntro2() {
    App::Push<cu::Page>("What happens to the console"_i18n, "Step by step: the reset, two cycles, report."_i18n,
        std::vector<cu::Line>{
            cu::Head("Switching off and on (this is normal)"_i18n),
            cu::Para("When the battery is almost empty, the console switches itself off. You switch it on again, and it still works for a short time, because the voltage rises a little after the switch-off. Then it switches off again. This can repeat many times in a row. It is a needed part of the process: switch the console on each time and go through it to the end. It can take long."_i18n),
            cu::Head("When the console does not start any more"_i18n),
            cu::Para("Plug the charger in and charge it to 100 %. The console may not start at once: the battery is empty, and it needs to lie on the charger for a while."_i18n),
            cu::Head("After the charge"_i18n),
            cu::Para("Open Kefir Hub and Battery calibration. The Hub counts the cycle by itself and shows what comes next."_i18n),
        },
        "Start calibration"_i18n, [](cu::Page& p) {
            // a backup of the gauge before anything else: the way back if the calibration goes wrong
            auto name = std::make_shared<std::string>();
            App::Push<ui::ProgressBox>(0, "Back up the chip's values"_i18n, "Battery"_i18n, [name](auto) -> Result {
                return detail::Backup(detail::Read(), *name);
            }, [name, &p](Result rc) {
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Backup failed"_i18n);
                    return;
                }
                const auto screen = ScreenNow();
                SaveMarker(Begin(screen.first, screen.second, Now()));
                detail::SaveState({}); // a new calibration: what an earlier one counted does not count
                g_checked = true;      // a fresh start: nothing to find yet
                App::Notify("Saved: "_i18n + *name + ".cal");
                p.SetPop();
                ShowSteps();
            });
        });
}

void ShowIntro() {
    App::Push<cu::Page>("Battery calibration"_i18n, "Step by step: the reset, two cycles, report."_i18n,
        std::vector<cu::Line>{
            cu::Head("Why this is needed"_i18n),
            cu::Para("With time the charge gauge goes wrong. The console can switch off at 35 %, or show 0 % for a whole hour. The wizard teaches the gauge again, so the percent is honest."_i18n),
            cu::Head("How it works"_i18n),
            cu::Para("First the Hub backs the gauge up and writes the factory battery model into it, so what the gauge learned wrongly is dropped. Then two cycles: the battery is run down until the console switches itself off, and charged to 100 %. In the first cycle the voltage at the switch-off is written into the gauge as the zero: the real empty battery. On the cycles the gauge learns the real capacity."_i18n),
            cu::Head("What you do"_i18n),
            cu::Para("The reset needs no charging: it is done at once. Then take the charger out and leave the console on a table: the Hub gives it a full load by itself. When the console switches off, switch it on again and open Kefir Hub. When it stops starting at all, plug the charger in and charge to 100 %."_i18n),
            cu::Head("How long it takes"_i18n),
            cu::Para("Several hours, with breaks for charging. Your games and saves are not touched. Before it writes to the gauge, the Hub makes a backup."_i18n),
        },
        "Go on"_i18n, [](cu::Page& p) {
            p.SetPop();
            ShowIntro2();
        });
}

// ---- step 1: the reset ----------------------------------------------------------------------------------------------------

void ShowCyclePage();

void ShowResetDone() {
    App::Push<cu::Page>("Reset the gauge"_i18n, StepLabel(1),
        std::vector<cu::Line>{
            cu::Ok("Factory model written"_i18n),
            cu::Ok("The backup was made."_i18n),
            cu::Item("Now cycle 1: take the charger out, the Hub drains the battery until the console switches itself off. That voltage becomes the zero. Then charge to 100 %."_i18n),
        },
        "Go on"_i18n, [](cu::Page& p) {
            p.SetPop();
            ShowCyclePage(); // straight to cycle 1
        }, cu::Page::Act{}, std::function<bool()>{}, 1);
}

// at once, at any charge level: backup, then the factory model (the init seeds the counters from the voltage)
void StartResetRun() {
    detail::RunReset([](bool ok) {
        if (ok) {
            ShowResetDone();
        } else {
            ShowSteps();
        }
    });
}

// ---- steps 2 and 3: a cycle -----------------------------------------------------------------------------------------------

void ShowCyclePage() {
    const auto m = LoadCurrent();
    if (!m || !IsCycle(m->step)) {
        ShowSteps();
        return;
    }
    const int cycle = CycleOf(m->step);
    const auto phase = PhaseOf(*m);
    const auto back = [](cu::Page& p) { p.SetPop(); ShowSteps(); };
    std::vector<cu::Line> lines;
    std::string label;
    cu::Page::Act act;
    if (phase == Phase::Drain) {
        lines.push_back(cu::Check("Charger is out"_i18n, "Take the charger out."_i18n, []() { return !ChargerIn(); }));
        lines.push_back(cu::Para("Now the Hub drains the battery to the very end, until the console switches itself off. To keep the drain steady, the Hub loads the CPU and the GPU fully and sets the highest brightness."_i18n));
        if (cycle == 1) {
            lines.push_back(cu::Para("The voltage at the moment the console switches off is the real empty battery. When you switch the console on again and open Kefir Hub, the Hub writes it into the gauge as the zero, by itself."_i18n));
        }
        lines.push_back(cu::Para("After 3 minutes (or at once, on Minus), the screen saver starts: the screen goes black and the numbers slowly move across it. This stops the pixels from burning in. The right stick changes the brightness, the left stick moves the numbers."_i18n));
        lines.push_back(cu::Para("When the console switches off, that is normal. Just switch it on again. The Hub goes on with the drain by itself, now at minimum brightness and weak load, to squeeze out the last percent. Keep switching it on until it does not start at all."_i18n));
        lines.push_back(cu::Para("When it does not start any more: plug the charger in and switch it on. The Hub shows the charge, and counts the cycle by itself at 100 %."_i18n));
        label = m->drain_started ? "Open the drain screen"_i18n : "Start the drain"_i18n;
        act = [](cu::Page& p) {
            if (ChargerIn()) {
                App::Notify("Take the charger out."_i18n);
                return;
            }
            auto cur = LoadCurrent();
            if (!cur || !IsCycle(cur->step)) {
                return;
            }
            p.SetPop();
            StartDrainRun(*cur);
        };
    } else {
        lines.push_back(cu::Ok(CycleName(cycle) + ": " + "the drain is done."_i18n + " " + "Now plug the charger in and wait for 100 %. The Hub counts the cycle by itself, from the log."_i18n));
        if (cycle == 1 && m->zero_at) {
            char v[16];
            std::snprintf(v, sizeof(v), "%.2f V", m->zero_mv / 1000.0);
            lines.push_back(cu::Row("The zero (empty battery)"_i18n, v));
        }
        lines.push_back(cu::Check("Charger is plugged in."_i18n, "Plug the charger in."_i18n, []() { return ChargerIn(); }));
        lines.push_back(cu::Item("You can leave the console like this. You can also use it while it charges. When the charge reaches 100 %, come back to Kefir Hub and open Battery calibration."_i18n));
        lines.push_back(cu::Item("The screen stays on: so the console charges to the end. The screensaver saves the screen."_i18n));
        label = "Start charging"_i18n;
        act = [](cu::Page& p) {
            if (!ChargerIn()) {
                App::Notify("Plug the charger in."_i18n);
                return;
            }
            const auto cur = LoadCurrent();
            if (!cur || !IsCycle(cur->step)) {
                return;
            }
            p.SetPop();
            StartChargeRun(*cur);
        };
    }
    // the page waits for the charger: the drain starts when it comes out, the charge when it goes in
    App::Push<cu::Page>(CycleName(cycle), StepLabel(cycle + 1), std::move(lines), label, std::move(act), back,
        phase == Phase::Charge ? std::function<bool()>([]() { return !ChargerIn(); }) : std::function<bool()>([]() { return ChargerIn(); }), cycle + 1);
}

// ---- what the Hub finds when it starts ---------------------------------------------------------------------------------

// returns true when it took over the screen (a page or a dialog)
auto Resume(Marker m) -> bool {
    if (g_checked) {
        return false;
    }
    g_checked = true;
    if (CountCycle(m)) {
        App::Notify("Cycle counted"_i18n);
    }
    Sync(m, detail::LoadState().reset_at != 0);
    auto b = ReadBattery();
    // the log's own word: a drain line this low and then nothing (or the charge) = the console switched off in this drain
    if (IsCycle(m.step) && m.drain_started) {
        for (const auto& c : Cutoffs(ReadLog())) {
            if (c.step == CycleOf(m.step) && c.time >= m.drain_time) {
                b.cut_in_log = true;
            }
        }
    }
    const auto found = Check(m, b);
    log_write("[Calib] resume: step %d, drain started %d (t=%lld, boot %lld), now boot %lld, charger %d, cut-off in log %d -> %s\n",
              static_cast<int>(m.step), m.drain_started ? 1 : 0, static_cast<long long>(m.drain_time), static_cast<long long>(m.boot_time),
              static_cast<long long>(b.boot_time), b.charger_in ? 1 : 0, b.cut_in_log ? 1 : 0,
              found == Found::CutoffCharging ? "cut off, charging" : found == Found::CutoffSeen ? "cut off, on battery"
              : found == Found::HubRestarted ? "the Hub started again, same boot" : "nothing to go on with");
    SaveMarker(m);
    const bool zero_due = m.step == Step::Cycle1 && !m.zero_at;
    switch (found) {
        case Found::CutoffSeen:
            if (zero_due) {
                // switched on without the charger: the zero is written now, the drain goes on in the tail mode
                App::Notify("The console switched off: setting the zero"_i18n);
                WriteZeroThenCharge(m);
                return true;
            }
            App::Notify("The console switched off: the drain goes on slowly"_i18n);
            StartDrainRun(m);
            return true;
        case Found::CutoffCharging:
            if (zero_due) {
                App::Notify("The console switched off: setting the zero"_i18n);
                WriteZeroThenCharge(m);
                return true;
            }
            App::Notify(CycleName(CycleOf(m.step)) + ": " + "the drain is done. Now charge to 100 %."_i18n);
            StartChargeRun(m); // the charge page, with the battery and the road map: not the list of steps
            return true;
        case Found::HubRestarted:
            App::Notify("The Hub started again: the drain goes on"_i18n);
            StartDrainRun(m); // same boot, same mode: only the Hub was left or replaced
            return true;
        default:
            return false;
    }
}

// ---- the list of steps ----------------------------------------------------------------------------------------------------

enum class Mark { Done, Active, Locked };

auto StateText(Mark m) -> std::string {
    return m == Mark::Done ? "Done"_i18n : m == Mark::Active ? "Active"_i18n : "Locked"_i18n;
}

// what the active cycle row says: where the cycle is and what the user does now
auto CycleText(const Marker& m) -> std::string {
    if (PhaseOf(m) == Phase::Charge) {
        return "the drain is done. Plug the charger in and wait for 100 %: the cycle then counts by itself"_i18n;
    }
    return m.drain_started ? "the drain is going on"_i18n : "next: take the charger out and start the drain"_i18n;
}

} // namespace

namespace calib_flow {

void ShowSteps() {
    const auto m = LoadCurrent();
    if (!m) {
        return;
    }
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
    const auto at = [&m](Step s) { return StepIndex(m->step) > StepIndex(s) ? Mark::Done : m->step == s ? Mark::Active : Mark::Locked; };
    char zero_v[16]{};
    if (m->zero_mv) {
        std::snprintf(zero_v, sizeof(zero_v), "%.2f V", m->zero_mv / 1000.0);
    }
    std::vector<Row> rows;
    rows.push_back({"1. Reset the gauge"_i18n, m->step == Step::Reset ? "next: backup, then the factory model; no charging needed"_i18n : std::string{}, at(Step::Reset), open(StartResetRun)});
    rows.push_back({"2. Cycle 1: drain, the zero, charge"_i18n, m->step == Step::Cycle1 ? CycleText(*m) : m->zero_at ? "the zero"_i18n + ": " + zero_v : std::string{},
        at(Step::Cycle1), open(ShowCyclePage)});
    rows.push_back({"3. Cycle 2: drain, charge"_i18n, m->step == Step::Cycle2 ? CycleText(*m) : std::string{}, at(Step::Cycle2), open(ShowCyclePage)});
    rows.push_back({"4. Report"_i18n, {}, static_cast<int>(m->step) >= static_cast<int>(Step::Transfer) ? Mark::Done : m->step == Step::Report ? Mark::Active : Mark::Locked,
        open(ShowReport)});
    rows.push_back({"5. Transfer to the other systems"_i18n, {}, m->step == Step::Finished ? Mark::Done : m->step == Step::Transfer ? Mark::Active : Mark::Locked,
        open(ShowTransfer)});

    std::vector<SettingsItem> items;
    for (auto& row : rows) {
        const auto text = StateText(row.mark) + (row.detail.empty() ? std::string{} : ": " + row.detail);
        // an active row acts; the report and the transfer also open once they are done; finished steps do nothing
        const bool acts = row.mark == Mark::Active || (row.mark == Mark::Done && (row.label.starts_with("4.") || row.label.starts_with("5.")));
        auto label = row.label;
        if (row.mark == Mark::Done) { // a green tick takes the place of the number
            if (const auto p = label.find(". "); p != std::string::npos && p <= 2) {
                label = label.substr(p + 2);
            }
        }
        SettingsItem item{label, text, {}, acts ? row.action : std::function<void()>{}, SettingsItemKind::Normal};
        item.done = row.mark == Mark::Done;
        item.disabled = row.mark == Mark::Locked || (row.mark == Mark::Done && !acts); // the cursor steps over it
        items.push_back(std::move(item));
    }
    items.push_back(SettingsItem{"Actions"_i18n,
        "Back up the chip, align the charge, restore, match the other MMC, battery type."_i18n, {}, OpenActions, SettingsItemKind::Normal});
    auto menu = std::make_unique<SystemToolsMenu>("Battery calibration"_i18n, std::move(items));
    *self = menu.get();
    App::Push(std::move(menu));
}

} // namespace calib_flow

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
    zero_write::ReadQhAtStart(); // before anything charges or drains for long: "what came in since the switch-on"
    const auto m = LoadMarker();
    if (m && m->step != Step::Finished) {
        Resume(*m);
    }
    ApplyLedger(); // in another system than the one that was calibrated: take the new calibration
}

} // namespace sphaira::battery
