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
#include "battery_charge.hpp"
#include "battery_zero.hpp"
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

void OnChargeDone(int was);
void StartChargeRun(const Marker& m);
void WriteZeroThenCharge(Marker m);

void StartDrainRun(Marker& m) {
    StartDrain(m, Now(), ReadBattery().boot_time);
    SaveMarker(m);
    CalibRun run;
    run.tail = m.cutoffs > 0;
    run.step = CycleOf(m.step);
    run.on_charge_done = [was = CycleOf(m.step)]() { OnChargeDone(was); };
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
void OnChargeDone(int was) {
    auto m = LoadCurrent();
    if (m && IsCycle(m->step) && CycleOf(m->step) == was) {
        // the log did not count this cycle (its cut-off is missing): the same drain again would loop, the list says where it is
        log_write("[Calib] the charge ended but cycle %d was not counted: the steps, not a new drain\n", was);
        App::Notify("This cycle was not counted: its switch-off is not in the log."_i18n);
        ShowSteps();
    } else if (m && IsCycle(m->step)) {
        StartDrainRun(*m);
    } else if (m && m->step == Step::Report) {
        ShowReport(); // both cycles are done: straight to the result
    } else {
        ShowSteps();
    }
}

void StartChargeRun(const Marker& m) {
    CalibRun run;
    run.charge = true;
    run.step = CycleOf(m.step);
    run.on_charge_done = [was = CycleOf(m.step)]() { OnChargeDone(was); };
    StartCalibRun(std::move(run));
}

// ---- the zero (in cycle 1, after the cut-off) ----------------------------------------------------------------------------

// cycle 1's drain cut the console off: the last voltage of the log becomes the gauge's empty point, then the charge page
void WriteZeroThenCharge(Marker m) {
    const auto after = static_cast<std::int64_t>(detail::LoadState().reset_at);
    const auto cut = ZeroCutoff(ReadLog(), 1, after);
    int mv = cut ? cut->mv : 0;
    if (!cut && m.cut_mv) {
        // the log is gone (deleted under the page), but the page noted the voltage before the sleep itself
        log_write("[Zero] the log has no cut-off of cycle 1's drain; the marker's own note says %d mV\n", m.cut_mv);
        mv = m.cut_mv;
    }
    if (!mv) {
        log_write("[Zero] the log has no cut-off of cycle 1's drain\n");
        App::Push<ui::OptionBox>("The console switched off, but the drain log has no last voltage. The zero cannot be set from it. Take the charger out and run the drain again."_i18n,
            "OK"_i18n, [](auto) { ShowSteps(); });
        return;
    }
    auto failed = std::make_shared<std::string>();
    auto marker = std::make_shared<Marker>(std::move(m));
    App::Push<ui::ProgressBox>(0, "Setting the zero"_i18n, std::to_string(mv) + " mV", [failed, mv, qh = marker->qh_last](auto) -> Result {
        return zero_write::WriteZero(mv, *failed, qh);
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

// a backup of the gauge first (the way back if it goes wrong), then a fresh marker at step 1; `then` runs when it is done
void BeginNew(std::function<void()> then) {
    auto name = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Back up the chip's values"_i18n, "Battery"_i18n, [name](auto) -> Result {
        return detail::Backup(detail::Read(), *name);
    }, [name, then = std::move(then)](Result rc) {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Backup failed"_i18n);
            return;
        }
        const auto screen = ScreenNow();
        SaveMarker(Begin(screen.first, screen.second, Now()));
        detail::SaveState({}); // a new calibration: what an earlier one counted does not count
        g_checked = true;      // a fresh start: nothing to find yet
        App::Notify("Saved: "_i18n + *name + ".cal");
        then();
    });
}

void ShowIntro2() {
    App::Push<cu::Page>("What happens to the console"_i18n, "Step by step: the reset, two cycles, report."_i18n,
        std::vector<cu::Line>{
            cu::Head("When the console switches off (this is normal)"_i18n),
            cu::Para("When the battery is empty, the console switches itself off. This is what the Hub waits for. Plug the charger in and switch the console on: the Hub opens the charge page by itself. If the charger is not plugged in, the page says so."_i18n),
            cu::Head("When the console does not start any more"_i18n),
            cu::Para("Plug the charger in at once and charge it to 100 %. The console may not start for about 20 minutes: the battery is empty, and it needs to lie on the charger for a while. This is normal. Do not leave the empty console without the charger for long. If the console stays off, the Hub still counts the charge that came in meanwhile."_i18n),
            cu::Head("After the charge"_i18n),
            cu::Para("Open Kefir Hub and Battery calibration. The Hub counts the cycle by itself and shows what comes next."_i18n),
        },
        "Start calibration"_i18n, [](cu::Page& p) {
            BeginNew([&p]() {
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
            cu::Para("First the Hub backs the gauge up and writes the factory battery model into it, so what the gauge learned wrongly is dropped. Then two cycles: the battery is run down until the console switches itself off, and charged to 100 %. In the first cycle the voltage at the switch-off is written into the gauge as the zero: the real empty battery. On the cycles the Hub measures the real capacity: the charge that comes in from the switch-off to the full battery is written into the gauge as its capacity."_i18n),
            cu::Head("What you do"_i18n),
            cu::Para("The reset needs no charging: it is done at once. Then leave the console on a table, the charger can stay in: the Hub gives it a full load by itself. When the console switches off, switch it on again (with the charger in) and open Kefir Hub yourself: it writes the empty point into the gauge and shows the charge. This step is yours: without it nothing is written."_i18n),
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
            cu::Item("Now cycle 1: the Hub drains the battery until the console switches itself off. The charger can stay in. After the switch-off, switch the console on (it may need 10 to 15 minutes to start) and open Kefir Hub yourself: it writes that voltage into the gauge as the zero. Then charge to 100 %."_i18n),
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
        lines.push_back(cu::Para("The charger can stay in: the Hub cuts its input, so the console drains on the battery. You can also take the charger out."_i18n));
        lines.push_back(cu::Para("Now the Hub drains the battery to the very end, until the console switches itself off. To keep the drain steady, the Hub loads the CPU and the GPU fully and sets the highest brightness."_i18n));
        if (cycle == 1) {
            lines.push_back(cu::Para("The voltage at the moment the console switches off is the real empty battery. When you switch the console on again and open Kefir Hub, the Hub writes it into the gauge as the zero, by itself."_i18n));
        }
        {
            // the time is the one set for the screensaver in the settings
            const long s = detail::SaverSeconds();
            char t[32];
            if (s < 60) {
                std::snprintf(t, sizeof(t), "%ld %s", s, "sec"_i18n.c_str());
            } else if (s % 60 == 0) {
                std::snprintf(t, sizeof(t), "%ld %s", s / 60, "min"_i18n.c_str());
            } else {
                std::snprintf(t, sizeof(t), "%ld %s %ld %s", s / 60, "min"_i18n.c_str(), s % 60, "sec"_i18n.c_str());
            }
            auto text = "After %s without a button press (or at once, on Minus), the screen saver starts: the screen goes black and the numbers slowly move across it. This stops the pixels from burning in. The right stick changes the brightness, the left stick moves the numbers."_i18n;
            if (const auto p = text.find("%s"); p != std::string::npos) {
                text.replace(p, 2, t);
            }
            lines.push_back(cu::Para(std::move(text)));
        }
        lines.push_back(cu::Para("When the console switches off, that is normal. Switch it on again (with the charger in) and open Kefir Hub yourself. Only then the Hub writes the empty point into the gauge and goes on with the charge: it does not start by itself. If the charger was out, plug it in first. The console may not start at once: with the charger in it can take 10 to 15 minutes. Wait, do not give up."_i18n));
        lines.push_back(cu::Para("When it does not start any more: plug the charger in and switch it on. The Hub shows the charge, and counts the cycle by itself at 100 %."_i18n));
        label = m->drain_started ? "Open the drain screen"_i18n : "Start the drain"_i18n;
        act = [](cu::Page& p) {
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
        lines.push_back(cu::Item("You can leave the console like this. You can also use it while it charges. When the charge reaches 100 %, come back to Kefir Hub and open Battery calibration. Keep the charger in until then: the Hub measures the battery capacity while it is full."_i18n));
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
            m.drain_started = false; // the drain is over: a Hub restart in this boot must not start it again in the tail mode
            SaveMarker(m);
            if (zero_due) {
                // switched on without the charger: the zero is written now, the drain goes on in the tail mode
                App::Notify("The console switched off: setting the zero"_i18n);
                WriteZeroThenCharge(m);
                return true;
            }
            // the console switched off and was switched on without the charger: the charge page, which asks for it
            App::Notify(CycleName(CycleOf(m.step)) + ": " + "the drain is done. Plug the charger in."_i18n);
            StartChargeRun(m);
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
    return m.drain_started ? "the drain is going on"_i18n : "next: start the drain (the charger can stay in)"_i18n;
}

} // namespace

namespace calib_flow {

auto MeasureCapacityIfFull() -> Measure {
    auto m = LoadMarker();
    if (!m || !IsCycle(m->step) || PhaseOf(*m) != Phase::Charge) {
        return Measure::NotYet;
    }
    const int cycle = CycleOf(m->step);
    if (m->cap_raw[cycle - 1]) {
        return Measure::Done;
    }
    FuelGauge g;
    if (!g.open) {
        return Measure::NotYet;
    }
    const auto vcell = g.Read(MAX17050_VCell);
    const auto avg = g.Current(MAX17050_AvgCurrent);
    if (!vcell || !avg) {
        return Measure::NotYet;
    }
    const int mv = static_cast<int>((*vcell >> 3) * 0.625);
    const int ma = static_cast<int>(*avg);
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    const bool in = charger != PsmChargerType_Unconnected;
    // on the charger only the constant-voltage tail counts; off it a cell still at the top is full (nothing left it since)
    if (!(in ? zero::FullForHeal(mv, ma) : zero::FullNow(mv, ma))) {
        return Measure::NotYet;
    }
    if (m->qh_last <= 0) {
        log_write("[Cap] cycle %d: full (%d mV, %d mA) but there is no QH note of the cut-off: nothing measured\n", cycle, mv, ma);
        return Measure::Refused;
    }
    std::string failed;
    std::uint16_t cap{};
    const auto rc = zero_write::WriteCapacity(m->qh_last, failed, cap);
    log_write("[Cap] cycle %d: full by voltage (%d mV, %d mA, charger %s), capacity %d mAh, rc 0x%X\n", cycle, mv, ma, in ? "in" : "out", cap / 2, rc);
    if (R_FAILED(rc)) {
        App::Notify(failed.empty() ? std::string{"The gauge did not take the measured capacity."_i18n} : failed);
        return Measure::Refused;
    }
    m->cap_raw[cycle - 1] = cap;
    SaveMarker(*m);
    App::Notify("Battery capacity measured and written:"_i18n + " " + std::to_string(cap / 2) + " mAh");
    return Measure::Done;
}

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

// Actions > Start the calibration over: the steps and the cycles counted so far are dropped, step 1 is the reset again
void StartOver() {
    App::Push<ui::OptionBox>("Start the calibration over? The steps and the cycles counted so far are dropped, and the first step is the reset of the gauge. A backup of the gauge is made first."_i18n,
        "Cancel"_i18n, "Start over"_i18n, 0, [](auto op) {
            if (op && *op == 1) {
                BeginNew([]() { ShowSteps(); });
            }
        });
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
    charge::HizStartCheck(); // the first start after a cut-off test: what the chip held, then the input back
    charge::RestoreIfLeftOff();
    if (charge::StartCutoffTestIfAsked()) {
        StartDrain(1); // the drain page with the charger input cut
        return;
    }
    if (fs::FsNativeSd().FileExists("/config/kefir/battery/hiz_test.run")) {
        charge::RunHizTestIfAsked(); // a diagnostic the owner asked for, like the one below
        return;
    }
    if (fs::FsNativeSd().FileExists("/config/kefir/battery/charge_test.run")) {
        charge::RunTestIfAsked(); // a diagnostic the owner asked for: it takes the screen, the calibration goes on at the next start
        return;
    }
    zero_write::ReadQhAtStart(); // before anything charges or drains for long: "what came in since the switch-on"
    const auto m = LoadMarker();
    if (m && m->step != Step::Finished) {
        calib_flow::MeasureCapacityIfFull(); // the charge may have ended while the Hub was closed
        if (!Resume(*m)) {
            const auto now = LoadMarker();
            if (now && now->step == Step::Report) {
                ShowReport(); // the calibration is made: the user sees the result without looking for it
            }
        }
    }
    ApplyLedger(); // in another system than the one that was calibrated: take the new calibration
}

} // namespace sphaira::battery
