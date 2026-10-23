// Tools > Battery calibration (docs/dev/BATTERY-CALIBRATION.md, "Cutoff pipeline"): the intro, the list of steps and the
// handling of whatever the marker on the SD says is going on. The marker (battery_calib_state.hpp) lives in
// /config/kefir/battery/calib.txt; the drain and the charge run on the drain page (battery_drain.cpp, StartCalibRun);
// the zero is the factory reset at the cut-off (battery_write.cpp, ResetAtCutoff).
#include "battery_page.hpp"
#include "battery_calib_state.hpp"
#include "battery_calib_log.hpp"
#include "battery_internal.hpp"
#include "fuel_gauge.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <switch.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::battery {
namespace {

using namespace calib;
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
    return b;
}

auto Now() -> std::int64_t {
    return static_cast<std::int64_t>(std::time(nullptr));
}

// a text page: headings and paragraphs, wrapped, for the intro
struct TextPage final : ui::menu::MenuBase {
    struct Block {
        std::string head;
        std::string text;
    };

    TextPage(const std::string& title, std::vector<Block> blocks, const std::string& a_label, std::function<void()> on_a)
        : MenuBase{title, ui::menu::MenuFlag_None}, m_blocks{std::move(blocks)} {
        SetAction(Button::B, Action{"Back"_i18n, [this]() { SetPop(); }});
        SetAction(Button::A, Action{a_label, std::move(on_a)});
    }

    auto GetShortTitle() const -> const char* override { return "Battery calibration"; }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        float y = 124.f;
        constexpr float X = 90.f, W = 1100.f;
        for (const auto& b : m_blocks) {
            ui::gfx::drawTextArgs(vg, X, y, 21.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", b.head.c_str());
            y += 30.f;
            ui::gfx::drawTextBox(vg, X, y, 21.f, W, text, b.text.c_str());
            float bounds[4]{};
            nvgFontSize(vg, 21.f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgTextBoxBounds(vg, X, y, W, b.text.c_str(), nullptr, bounds);
            y += (bounds[3] - bounds[1]) + 16.f;
        }
    }

    std::vector<Block> m_blocks;
};

void ShowSteps();
void ShowIntro2();

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

void StartDrainRun(Marker& m) {
    StartPhase(m, ReadBattery().percent, Now());
    SaveMarker(m);
    CalibRun run;
    run.tail = m.cutoffs > 0;
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
    StartPhase(m, ReadBattery().percent, Now());
    SaveMarker(m);
    CalibRun run;
    run.charge = true;
    run.on_charge_done = OnChargeDone;
    StartCalibRun(std::move(run));
}

// step 2: the factory model at the cut-off; N and X are logged for the report (the gauge may keep counting while off)
void RunZero() {
    auto failed = std::make_shared<std::string>();
    auto text = std::make_shared<std::string>();
    App::Push<ui::ProgressBox>(0, "Setting the zero"_i18n, "Battery calibration"_i18n, [failed, text](auto pbox) -> Result {
        FuelGauge g;
        R_UNLESS(g.open, Result_ArchiveRead);
        int n = -1;
        if (const auto v = g.Read(MAX17050_RepCap)) {
            n = static_cast<int>(*v) / 2; // 0.5 mAh a step (10 mOhm), as the drain page reads it
        }
        int x = -1;
        std::string last_v = "?";
        std::vector<u8> log;
        if (R_SUCCEEDED(fs::FsNativeSd{}.read_entire_file(LOG_PATH, log)) && !log.empty()) {
            const std::string_view view{reinterpret_cast<const char*>(log.data()), log.size()};
            x = LastDrainRemainingMah(view);
            if (const auto sample = LastSample(view)) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.2f", sample->mv / 1000.0);
                last_v = buf;
            }
        }
        log_write("[Calib] zero: N %d mAh, X %d mAh, last logged voltage %s V\n", n, x, last_v.c_str());
        R_TRY(detail::ResetAtCutoff(pbox, *failed));
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s %s V\n%s %d mAh, %s %d mAh", "Last voltage in the log:"_i18n.c_str(), last_v.c_str(),
            "The gauge's counter then:"_i18n.c_str(), x, "now:"_i18n.c_str(), n);
        *text = buf;
        R_SUCCEED();
    }, [failed, text](Result rc) {
        if (R_FAILED(rc)) {
            Say("The zero was not set. A backup was made first; open the Battery page to restore it."_i18n + (failed->empty() ? std::string{} : "\n\n" + *failed), []() { ShowSteps(); });
            return;
        }
        if (auto m = LoadMarker()) {
            ZeroDone(*m);
            SaveMarker(*m);
        }
        Say("The zero is set: the gauge now follows the factory model from this point."_i18n + "\n\n" + *text + "\n\n" +
            "Next: charge to 100 %, then drain again, twice."_i18n, []() { ShowSteps(); });
    });
}

void AskZero() {
    App::Push<ui::OptionBox>(
        "The console was drained to the cut-off. Set this point as the zero now? A backup of the gauge is made first, then the factory model is written."_i18n,
        "Later"_i18n, "Set the zero"_i18n, 1, [](auto op) {
            if (op && *op == 1) {
                RunZero();
            } else {
                ShowSteps();
            }
        });
}

// what the Hub finds when it starts, once; returns true when it took over the screen (a page or a dialog)
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
        case Found::DeadWithCharger:
            AskZero();
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

enum class Mark { Done, Active, Locked };

struct Row {
    std::string label;
    std::string detail;
    Mark mark;
    std::function<void()> action;
};

auto StateText(Mark m) -> std::string {
    return m == Mark::Done ? "Done"_i18n : m == Mark::Active ? "Active"_i18n : "Locked"_i18n;
}

auto PhaseText(const Marker& m) -> std::string {
    if (m.phase == Phase::Charge) {
        return m.phase_started ? "the charge is going on"_i18n : "next: charge to 100 %"_i18n;
    }
    return m.phase_started ? "the drain is going on"_i18n : "next: drain to the cut-off"_i18n;
}

void BeginDrainStep(Marker m) {
    if (ReadBattery().charger_in) {
        Say("Take the charger out to start the drain."_i18n);
        return;
    }
    App::Push<ui::OptionBox>(
        "Start the drain? The Hub loads the CPU and GPU at full brightness until the console switches itself off. Keep the Hub open; switch the console on again after each switch-off."_i18n,
        "Back"_i18n, "Start the drain"_i18n, 1, [m](auto op) mutable {
            if (op && *op == 1) {
                StartDrainRun(m);
            }
        });
}

void CycleAction(Marker m) {
    const auto b = ReadBattery();
    if (m.phase == Phase::Charge) {
        if (!b.charger_in) {
            Say("Plug the charger in to start the charge."_i18n);
            return;
        }
        StartChargeRun(m);
        return;
    }
    if (b.charger_in) {
        Say("Take the charger out to start the drain."_i18n);
        return;
    }
    StartDrainRun(m);
}

auto Rows(const Marker& m) -> std::vector<Row> {
    const auto at = [&m](Step s) { return static_cast<int>(m.step) > static_cast<int>(s) ? Mark::Done : m.step == s ? Mark::Active : Mark::Locked; };
    std::vector<Row> rows;
    rows.push_back({"1. Drain to the dead battery"_i18n,
        m.step == Step::Drain ? (m.phase_started ? "the drain is going on"_i18n : "next: drain to the cut-off"_i18n) : std::string{},
        at(Step::Drain), [m]() { BeginDrainStep(m); }});
    rows.push_back({"2. Charge and set the zero"_i18n,
        m.step == Step::Zero ? "next: set the zero"_i18n : std::string{}, at(Step::Zero), []() { AskZero(); }});
    rows.push_back({"3. Cycle 1: charge, then drain"_i18n, m.step == Step::Cycle1 ? PhaseText(m) : std::string{}, at(Step::Cycle1), [m]() { CycleAction(m); }});
    rows.push_back({"4. Cycle 2: charge, then drain"_i18n, m.step == Step::Cycle2 ? PhaseText(m) : std::string{}, at(Step::Cycle2), [m]() { CycleAction(m); }});
    rows.push_back({"5. Report"_i18n, {}, static_cast<int>(m.step) >= static_cast<int>(Step::Transfer) ? Mark::Done : m.step == Step::Report ? Mark::Active : Mark::Locked, []() {
        if (auto cur = LoadMarker()) {
            ReportSeen(*cur);
            TransferDone(*cur);
            SaveMarker(*cur);
        }
        Open(); // the Battery page: the learned capacity and the verdict
    }});
    return rows;
}

void ShowSteps() {
    auto m = LoadMarker();
    if (!m) {
        return;
    }
    std::vector<SettingsItem> items;
    for (auto& row : Rows(*m)) {
        const auto text = StateText(row.mark) + (row.detail.empty() ? std::string{} : ": " + row.detail);
        // an active row acts; the report row also opens once it is done; locked rows and finished steps do nothing
        const bool acts = row.mark == Mark::Active || (row.mark == Mark::Done && row.label.starts_with("5."));
        items.push_back(SettingsItem{row.label, text, {}, acts ? row.action : std::function<void()>{}, SettingsItemKind::Normal});
    }
    items.push_back(SettingsItem{"The calibration is remembered on the SD card: you can close the Hub and use the console between the steps."_i18n,
        {}, {}, {}, SettingsItemKind::Normal});
    App::Push<SystemToolsMenu>("Battery calibration"_i18n, std::move(items));
}

void ShowIntro2() {
    std::vector<TextPage::Block> blocks{
        {"Switching off and on (this is normal)"_i18n,
         "When the battery is almost empty, the console switches itself off. You switch it on again, and it still works for a short time, because the voltage rises a little after the switch-off. Then it switches off again. This can repeat many times in a row. It is a needed part of the process: switch the console on each time and go through it to the end. It can take long."_i18n},
        {"When the console does not start any more"_i18n,
         "Plug the charger in. The console may not start at once: the battery is empty, and it needs to lie on the charger for a while."_i18n},
        {"As soon as it has started"_i18n,
         "Do not wait: open Kefir Hub at once. The Hub records the voltage at which the console switched off, sets the zero and goes on with the calibration."_i18n},
    };
    App::Push<TextPage>("What happens to the console"_i18n, std::move(blocks), "Start calibration"_i18n, []() {
        if (!LoadMarker()) {
            const auto screen = ScreenNow();
            SaveMarker(Begin(screen.first, screen.second, Now()));
        }
        g_checked = true; // a fresh start: nothing to find yet
        ShowSteps();
    });
}

void ShowIntro() {
    std::vector<TextPage::Block> blocks{
        {"Why this is needed"_i18n,
         "With time the charge gauge goes wrong. The console can switch off at 35 %, or show 0 % for a whole hour. The wizard teaches the gauge again, so the percent is honest."_i18n},
        {"How it works"_i18n,
         "We drain the battery to the very end: until the console switches itself off (about 3.2 V under load). That is the real zero. Then we charge and drain it fully, two times."_i18n},
        {"What you do"_i18n,
         "Take the charger out and leave the console on a table: the Hub gives it a full load by itself. When the console switches off, switch it on again. When it stops starting at all, plug the charger in and, as soon as the console starts, open Kefir Hub at once."_i18n},
        {"How long it takes"_i18n,
         "Several hours, with breaks for charging. Your games and saves are not touched. Before it writes to the gauge, the Hub makes a backup."_i18n},
    };
    App::Push<TextPage>("Battery calibration"_i18n, std::move(blocks), "Go on"_i18n, []() { ShowIntro2(); });
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
