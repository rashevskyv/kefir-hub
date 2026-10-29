// Tools > Battery calibration (docs/dev/BATTERY-CALIBRATION.md): the pages of the guide (docs/site/en/battery-wizard) over the
// logic of 0.14.111-163 (battery_wizard.hpp, calibration.txt): the reset to the factory model at 100 %, then two cycles, each
// counted by HOS's cycle total (90 % drained, then 100 % on the charger). The marker on the SD (battery_calib_state.hpp,
// calib.txt) keeps the report and the transfer, and what a drain needs to go on after a cut-off. The drain and the charge run
// on the drain page (battery_drain.cpp, StartCalibRun); the reset is battery_write.cpp, detail::RunReset.
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
    return "Step"_i18n + " " + std::to_string(i) + " / 5";
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

auto PercentNow() -> std::uint32_t {
    u32 pct{};
    psmGetBatteryChargePercentage(&pct);
    return pct;
}

// ---- the wizard's state: the reset and the two cycles ---------------------------------------------------------------------

// a finished cycle (enough drained, now full) is counted; true = something was counted
auto AdvanceNow() -> bool {
    const auto total = detail::CycleTotalNow();
    if (!total) {
        return false;
    }
    auto st = detail::LoadState();
    const wizard::Live live{*total, ChargerIn(), PercentNow(), 0, false, static_cast<u64>(Now())};
    if (!wizard::Advance(st, live)) {
        return false;
    }
    detail::SaveState(st);
    account_restore::ClearReopenHubHint(); // the drain notice did its job
    log_write("[Calib] a cycle was counted (total %u)\n", *total);
    return true;
}

// x100 of the battery drained in this cycle so far (HOS's cycle total), -1 when it cannot be read
auto CycleGot(int cycle) -> int {
    const auto total = detail::CycleTotalNow();
    if (!total) {
        return -1;
    }
    const wizard::Live live{*total, false, 0, 0, false, static_cast<u64>(Now())};
    return static_cast<int>(wizard::Got(detail::LoadState(), live, cycle));
}

// the marker with the step the wizard's state says; saved when the step moved
auto LoadCurrent() -> std::optional<Marker> {
    auto m = LoadMarker();
    if (!m) {
        return m;
    }
    AdvanceNow(); // a cycle that was filled while the Hub was closed
    const auto st = detail::LoadState();
    if (Sync(*m, st.reset_at != 0, st.full1_at != 0, st.full2_at != 0)) {
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

void OnChargeDone();

// ---- the runs -----------------------------------------------------------------------------------------------------------

void StartChargeRun(const Marker& m);

void StartDrainRun(Marker& m) {
    StartDrain(m, Now(), ReadBattery().boot_time);
    SaveMarker(m);
    CalibRun run;
    run.tail = m.cutoffs > 0;
    run.step = m.step == Step::Cycle1 ? 1 : 2;
    run.on_charge_done = OnChargeDone; // the cycle is counted and the charger is out: the next drain starts by itself
    run.on_drained = []() { // the console was cut off and the charger is in: the drain is over, the charge starts
        auto cur = LoadCurrent();
        if (cur && IsCycle(cur->step)) {
            cur->drain_started = false;
            SaveMarker(*cur);
            StartChargeRun(*cur);
        } else {
            ShowSteps();
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

// the charge is full and the charger came out: the cycle is counted, and the next one starts by itself
void OnChargeDone() {
    AdvanceNow();
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
    run.step = m.step == Step::Cycle1 ? 1 : 2;
    run.on_charge_done = OnChargeDone;
    StartCalibRun(std::move(run));
}

// ---- the intro ----------------------------------------------------------------------------------------------------------

void ShowIntro2() {
    App::Push<cu::Page>("What happens to the console"_i18n, "Step by step: reset, two cycles, report."_i18n,
        std::vector<cu::Line>{
            cu::Head("Switching off and on (this is normal)"_i18n),
            cu::Para("When the battery is almost empty, the console switches itself off. You switch it on again, and it still works for a short time, because the voltage rises a little after the switch-off. Then it switches off again. This can repeat many times in a row. It is a needed part of the process: switch the console on each time and go through it to the end. It can take long."_i18n),
            cu::Head("When the console does not start any more"_i18n),
            cu::Para("Plug the charger in and charge it to 100 %. The console may not start at once: the battery is empty, and it needs to lie on the charger for a while."_i18n),
            cu::Head("After the charge"_i18n),
            cu::Para("Open Kefir Hub and Battery calibration. The Hub counts the cycle by itself and shows what comes next."_i18n),
        },
        "Start calibration"_i18n, [](cu::Page& p) {
            const auto screen = ScreenNow();
            SaveMarker(Begin(screen.first, screen.second));
            detail::SaveState({}); // a new calibration: what an earlier one counted does not count
            g_checked = true;      // a fresh start: nothing to find yet
            p.SetPop();
            ShowSteps();
        });
}

void ShowIntro() {
    App::Push<cu::Page>("Battery calibration"_i18n, "Step by step: reset, two cycles, report."_i18n,
        std::vector<cu::Line>{
            cu::Head("Why this is needed"_i18n),
            cu::Para("With time the charge gauge goes wrong. The console can switch off at 35 %, or show 0 % for a whole hour. The wizard teaches the gauge again, so the percent is honest."_i18n),
            cu::Head("How it works"_i18n),
            cu::Para("First the Hub writes the factory battery model into the gauge, so what the gauge learned wrongly is dropped. Then the battery is run down and charged to 100 %, two times. On these cycles the gauge learns the real capacity."_i18n),
            cu::Head("What you do"_i18n),
            cu::Para("Charge the console to 100 % for the reset. Then take the charger out and leave the console on a table: the Hub gives it a full load by itself. When the console switches off, switch it on again. When it stops starting at all, plug the charger in."_i18n),
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
            cu::Item("Now two cycles: the battery is run down and charged to 100 %, twice. The gauge learns the real capacity on them."_i18n),
        },
        "Go on"_i18n, [](cu::Page& p) {
            p.SetPop();
            ShowCyclePage(); // straight to cycle 1
        });
}

// the charge page counts from the moment the charger is in; when the battery is full it asks once, then resets
void StartResetRun() {
    CalibRun run;
    run.charge = true;
    run.reset = true;
    run.step = 0;
    run.on_reset = []() {
        detail::RunReset([](bool ok) {
            if (ok) {
                ShowResetDone();
            } else {
                ShowSteps();
            }
        });
    };
    StartCalibRun(std::move(run));
}

// ---- steps 2 and 3: a cycle -----------------------------------------------------------------------------------------------

void ShowCyclePage() {
    const auto m = LoadCurrent();
    if (!m || !IsCycle(m->step)) {
        ShowSteps();
        return;
    }
    const int cycle = m->step == Step::Cycle1 ? 1 : 2;
    const int got = CycleGot(cycle);
    // a cut-off seen in this cycle means the drain is done (the cycle total may lag behind)
    const bool drained = got >= static_cast<int>(wizard::CYCLE_DONE) || m->cutoffs > 0;
    const auto back = [](cu::Page& p) { p.SetPop(); ShowSteps(); };
    std::vector<cu::Line> lines;
    std::string label;
    cu::Page::Act act;
    if (!drained) {
        lines.push_back(cu::Check("Charger is out"_i18n, "Take the charger out."_i18n, []() { return !ChargerIn(); }));
        lines.push_back(cu::Row("Discharged in this cycle"_i18n, (got < 0 ? std::string{"?"} : std::to_string(got)) + " % / " + std::to_string(wizard::CYCLE_DONE) + " %"));
        lines.push_back(cu::Para("Now the Hub drains the battery to the very end, until the console switches itself off. To keep the drain steady, the Hub loads the CPU and the GPU fully and sets the highest brightness."_i18n));
        lines.push_back(cu::Para("After 3 minutes (or at once, on Minus), the screen saver starts: the screen goes black and the numbers slowly move across it. This stops the pixels from burning in. The right stick changes the brightness, the left stick moves the numbers."_i18n));
        lines.push_back(cu::Para("When the console switches off, that is normal. Just switch it on again. The Hub goes on with the drain by itself, now at minimum brightness and weak load, to squeeze out the last percent. Keep switching it on until it does not start at all."_i18n));
        lines.push_back(cu::Para("When it does not start any more: plug the charger in and charge it to 100 %. Then open Kefir Hub: the cycle counts by itself."_i18n));
        lines.push_back(cu::Para("The brightness is lowered only while the console drains. As soon as you plug in the charger, the Hub gives back your brightness and auto-brightness."_i18n));
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
        lines.push_back(cu::Ok("The battery ran down enough. Now charge it to 100 %. The cycle then counts by itself."_i18n));
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
    App::Push<cu::Page>(cycle == 1 ? "Cycle 1"_i18n : "Cycle 2"_i18n, StepLabel(cycle + 1), std::move(lines), label, std::move(act), back,
        drained ? std::function<bool()>([]() { return !ChargerIn(); }) : std::function<bool()>([]() { return ChargerIn(); }));
}

// ---- what the Hub finds when it starts ---------------------------------------------------------------------------------

// returns true when it took over the screen (a page or a dialog)
auto Resume(Marker m) -> bool {
    if (g_checked) {
        return false;
    }
    g_checked = true;
    if (AdvanceNow()) {
        App::Notify("Cycle counted"_i18n);
    }
    const auto st = detail::LoadState();
    Sync(m, st.reset_at != 0, st.full1_at != 0, st.full2_at != 0);
    const auto found = Check(m, ReadBattery());
    SaveMarker(m);
    switch (found) {
        case Found::CutoffSeen:
            App::Notify("The console switched off: the drain goes on slowly"_i18n);
            StartDrainRun(m);
            return true;
        case Found::CutoffCharging:
            App::Notify("The console switched off: the drain is done. Now charge it to 100 %."_i18n);
            log_write("[Calib] cut-off found, the console charges; cycle %d got %d\n", m.step == Step::Cycle1 ? 1 : 2, CycleGot(m.step == Step::Cycle1 ? 1 : 2));
            ShowCyclePage();
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

// what a cycle row says: where the cycle is
auto CycleText(const Marker& m, int cycle) -> std::string {
    const int got = CycleGot(cycle);
    const bool drained = got >= static_cast<int>(wizard::CYCLE_DONE) || m.cutoffs > 0;
    const auto progress = got < 0 ? std::string{} : " (" + std::to_string(got) + " % / " + std::to_string(wizard::CYCLE_DONE) + " %)";
    if (drained) {
        return "next: charge to 100 %"_i18n;
    }
    return (m.drain_started ? "the drain is going on"_i18n : "next: drain to the cut-off"_i18n) + progress;
}

} // namespace

namespace calib_flow {

void ShowSteps() {
    const auto m = LoadCurrent();
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
    rows.push_back({"1. Reset the gauge"_i18n, m->step == Step::Reset ? "next: charge to 100 %, then reset"_i18n : std::string{}, at(Step::Reset), open(StartResetRun)});
    rows.push_back({"2. Cycle 1: drain, then charge"_i18n, m->step == Step::Cycle1 ? CycleText(*m, 1) : std::string{}, at(Step::Cycle1), open(ShowCyclePage)});
    rows.push_back({"3. Cycle 2: drain, then charge"_i18n, m->step == Step::Cycle2 ? CycleText(*m, 2) : std::string{}, at(Step::Cycle2), open(ShowCyclePage)});
    rows.push_back({"4. Report"_i18n, {}, static_cast<int>(m->step) >= static_cast<int>(Step::Transfer) ? Mark::Done : m->step == Step::Report ? Mark::Active : Mark::Locked,
        open(ShowReport)});
    rows.push_back({"5. Transfer to the other MMC"_i18n, {}, m->step == Step::Finished ? Mark::Done : m->step == Step::Transfer ? Mark::Active : Mark::Locked,
        open(ShowTransfer)});

    std::vector<SettingsItem> items;
    for (auto& row : rows) {
        const auto text = StateText(row.mark) + (row.detail.empty() ? std::string{} : ": " + row.detail);
        // an active row acts; the report and the transfer also open once they are done; finished steps do nothing
        const bool acts = row.mark == Mark::Active || (row.mark == Mark::Done && (row.label.starts_with("4.") || row.label.starts_with("5.")));
        SettingsItem item{row.label, text, {}, acts ? row.action : std::function<void()>{}, SettingsItemKind::Normal};
        item.disabled = row.mark == Mark::Locked; // dimmed, and the cursor steps over it
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
    const auto m = LoadMarker();
    if (m && m->step != Step::Finished) {
        Resume(*m);
    }
    ApplyLedger(); // in another system than the one that was calibrated: take the new calibration
}

} // namespace sphaira::battery
