#include "battery_drain_menu.hpp"
#include "battery_calib_internal.hpp"

namespace sphaira::battery {

std::atomic<bool> g_load{false};
std::atomic<bool> g_stop{false};
// ticks each load thread spent burning: busy ticks / elapsed ticks is the load the Hub puts on that core.
// (svcGetInfo IdleTickCount answers 0 here, which drew every core at 100 % even on the charger.)
std::atomic<u64> g_busy[LOAD_CORES]{};

namespace {

void Burn(void* arg) {
    const auto core = reinterpret_cast<uintptr_t>(arg);
    // pinned: the idle-tick call answers only for the core the caller runs on, and a sleeping thread
    // that wakes on another core made it fail and left the chart at 100 %
    const Result mask_rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, static_cast<s32>(core), 1u << core);
    log_write("[Battery] burn thread %zu started, core mask rc 0x%X\n", static_cast<size_t>(core), mask_rc);
    bool first_chunk = true;
    volatile double x = 1.0001;
    while (!g_stop) {
        if (!g_load) {
            svcSleepThread(50'000'000ULL);
            continue;
        }
        const u64 t0 = armGetSystemTick();
        for (int i = 0; i < 200000; i++) {
            x = std::sqrt(x * 1.0000001 + 0.5);
        }
        const u64 spent = armGetSystemTick() - t0;
        g_busy[core] += spent;
        if (first_chunk) {
            first_chunk = false;
            log_write("[Battery] burn thread %zu first chunk took %llu ticks\n", static_cast<size_t>(core), (unsigned long long)spent);
        }
    }
}

constexpr const char* BRIGHTNESS_FILE = "/config/kefir/battery/brightness.txt";

struct SavedBrightness {
    float brightness{};
    bool auto_on{};
};

auto ReadSavedBrightness() -> std::optional<SavedBrightness> {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd{}.read_entire_file(BRIGHTNESS_FILE, data)) || data.empty()) {
        return std::nullopt;
    }
    const std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    float b{};
    int a{};
    if (std::sscanf(text.c_str(), "brightness=%f\nauto=%d", &b, &a) != 2 || b < 0.f || b > 1.f) {
        return std::nullopt;
    }
    return SavedBrightness{b, a != 0};
}

void WriteSavedBrightness(float brightness, bool auto_on) {
    char buf[64];
    const auto n = std::snprintf(buf, sizeof(buf), "brightness=%.4f\nauto=%d\n", brightness, auto_on ? 1 : 0);
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively("/config/kefir/battery");
    sd.write_entire_file(BRIGHTNESS_FILE, std::vector<u8>(buf, buf + n));
}

void DeleteSavedBrightness() {
    fs::FsNativeSd{}.DeleteFile(BRIGHTNESS_FILE);
}
}  // namespace

DrainMenu::DrainMenu(int cycle, std::optional<CalibRun> calib)
    : MenuBase{calib && calib->charge ? "Battery charge"_i18n : "Battery drain"_i18n, ui::menu::MenuFlag_None}, m_cycle{cycle}, m_calib{std::move(calib)} {
    SetAction(Button::B, Action{"Stop"_i18n, [this]() {
        // the full load starves the UI, so the question would appear late: pause the load while it is open
        m_stop_ask = true;
        g_load = false;
        App::Push<ui::OptionBox>(m_calib
            ? (m_calib->charge ? "Leave the charge page? The calibration remembers where it is."_i18n
                               : "Stop the drain? The calibration remembers where it is."_i18n)
            : "Stop draining? The cycle then needs to be finished another way."_i18n,
            "Back"_i18n, "Stop"_i18n, 0, [this](auto op) {
                m_stop_ask = false;
                if (op && *op == 1) {
                    account_restore::ClearReopenHubHint();
                    SetAutoDrainSuppressed(true); // the user stopped it: the wizard must not restart it now
                    if (m_calib && m_calib->on_stop) {
                        m_calib->on_stop();
                    }
                    SetPop();
                }
            });
    }});

    // Minus starts the screensaver here (the menus use it to jump to the Homebrew tab, which would close this page)
    SetAction(Button::SELECT, Action{"Screensaver"_i18n, [this]() {
        if (!m_saver.IsActive()) {
            m_saver.Start();
            m_idle.Reset(m_idle_clock.GetSecondsD());
        }
    }});

    App::SetAutoSleepDisabled(true);
    appletSetMediaPlaybackState(true); // keeps the screen from dimming
    if ((m_lbl = R_SUCCEEDED(lblInitialize()))) {
        lblGetCurrentBrightnessSetting(&m_saved_brightness);
        lblIsAutoBrightnessControlEnabled(&m_saved_auto);
        // a file from an earlier drain (the Hub was left, or died, before it could restore) holds the real values;
        // what the screen shows now is the drain's own full brightness
        if (const auto kept = ReadSavedBrightness()) {
            m_saved_brightness = kept->brightness;
            m_saved_auto = kept->auto_on;
        } else {
            WriteSavedBrightness(m_saved_brightness, m_saved_auto);
        }
        // automatic brightness off and the brightness at the maximum on every model: the drain goes faster.
        // The calibration's charge leaves the user's brightness alone (the screen only stays on); its drain after a
        // cut-off goes to the minimum (the tail).
        if (!(m_calib && m_calib->charge)) {
            if (m_saved_auto) {
                lblDisableAutoBrightnessControl();
            }
            lblSetCurrentBrightnessSetting(m_calib && m_calib->tail ? TAIL_BRIGHTNESS : 1.0f);
        }
        m_dimmed = m_calib && m_calib->tail;
        lblIsDimmingEnabled(&m_saved_dimming);
        lblDisableDimming();
    }
    appletGetIdleTimeDetectionExtension(&m_saved_idle);
    appletSetIdleTimeDetectionExtension(AppletIdleTimeDetectionExtension_ExtendedUnsafe);
    m_state = detail::LoadState();
    PsmChargerType charger_now{};
    psmGetChargerType(&charger_now);
    m_charger = charger_now != PsmChargerType_Unconnected;
    NoteState(true);
    if (m_calib) {
        OpenCalibLog();
        account_restore::ArmReopenHubHint("Battery calibration is not finished. Open Kefir Hub: it goes on by itself."_i18n, false);
    } else {
        account_restore::ArmReopenHubHint("Battery calibration: put the console on the charger until 100%, then open Kefir Hub → Tools → Battery → Calibrate."_i18n, false);
    }

    g_stop = false;
    g_load = false;
    for (int i = 0; i < LOAD_CORES; i++) {
        const Result create_rc = threadCreate(&m_threads[i], Burn, reinterpret_cast<void*>(static_cast<uintptr_t>(i)), nullptr, 0x4000, LOAD_PRIO, i);
        if (R_SUCCEEDED(create_rc)) {
            const Result start_rc = threadStart(&m_threads[i]);
            log_write("[Battery] burn thread %d created, start rc 0x%X\n", i, start_rc);
            m_started[i] = true;
        } else {
            log_write("[Battery] burn thread %d NOT created: 0x%X\n", i, create_rc);
        }
    }
    if (R_SUCCEEDED(nvInitialize())) {
        m_nv = true;
        m_gpu_fd_ok = R_SUCCEEDED(nvOpen(&m_gpu_fd, "/dev/nvhost-ctrl-gpu"));
    }
    log_write("[Battery] drain started (cycle %d, gpu load %s)\n", m_cycle, m_gpu_fd_ok ? "readable" : "not readable");
}

DrainMenu::~DrainMenu() {
    g_stop = true;
    for (int i = 0; i < LOAD_CORES; i++) {
        if (m_started[i]) {
            threadWaitForExit(&m_threads[i]);
            threadClose(&m_threads[i]);
        }
    }
    if (m_gpu_fd_ok) {
        nvClose(m_gpu_fd);
    }
    if (m_nv) {
        nvExit();
    }
    if (m_lbl) {
        lblSetCurrentBrightnessSetting(m_saved_brightness);
        if (m_saved_auto) {
            lblEnableAutoBrightnessControl();
        }
        if (m_saved_dimming) {
            lblEnableDimming();
        }
        lblExit();
        DeleteSavedBrightness();
    }
    appletSetIdleTimeDetectionExtension(m_saved_idle);
    appletSetMediaPlaybackState(false);
    App::SetAutoSleepDisabled(false);
    CloseCalibLog();
    log_write("[Battery] drain stopped\n");
}

void DrainMenu::Update(Controller* controller, TouchInfo* touch) {
    // The panel shows the same still charts for hours at full brightness: after the idle time (the one set for the install
    // screen, two minutes when that is off) the screensaver takes over with the numbers that matter. A press wakes it and
    // does nothing else; the measuring below goes on behind it.
    const double now_sec = m_idle_clock.GetSecondsD();
    if (m_saver.IsActive()) {
        m_saver.Update(controller, touch);
        if (m_saver.WantsWake(controller, touch)) {
            m_saver.Stop();
            App::SetAutoSleepDisabled(true); // the saver's Stop lets the console sleep again: a sleeping console stops the drain
            m_idle.Reset(now_sec);
            m_reapply = {};
            m_reapply_due = true; // the brightness the saver restored is put back to the drain's own
            return;
        }
    } else {
        MenuBase::Update(controller, touch);
        ui::TimeoutInput in{};
        if (controller) {
            in.kdown = controller->m_kdown;
            in.kheld = controller->m_kheld;
            in.stick_l_x = controller->m_stick_l.x;
            in.stick_l_y = controller->m_stick_l.y;
            in.stick_r_x = controller->m_stick_r.x;
            in.stick_r_y = controller->m_stick_r.y;
        }
        if (touch) {
            in.is_touching = touch->is_touching;
            in.is_clicked = touch->is_clicked;
        }
        const long timeout = App::GetBlankTimeout() > 0 ? App::GetBlankTimeout() : SAVER_DEFAULT_S;
        if (m_idle.Update(ui::BlankPhase_Installing, false, timeout, in, now_sec)) {
            m_saver.Start();
            m_idle.Reset(now_sec);
        }
    }
    if (m_live.GetMs() >= LIVE_MS) {
        m_live.Update();
        SampleLive();
    }
    if (m_poll.GetSeconds() < 1 && m_read) {
        return;
    }
    m_poll.Update();
    m_read = true;
    FuelGauge g;
    u32 shown{};
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&shown))) m_pct = static_cast<int>(shown); // what the console shows
    if (const auto v = g.Read(MAX17050_RepSOC)) m_gauge_pct = *v >> 8;
    // 0.5 mAh per step (10 mOhm sense resistor, as the Battery page reads it)
    if (const auto v = g.Read(MAX17050_RepCap)) m_cap_rem_mah = static_cast<int>(*v) / 2;
    if (const auto v = g.Read(MAX17050_FullCap)) m_cap_full_mah = static_cast<int>(*v) / 2;
    NoteState(false);
    // one line a minute: if the console cuts off early (HOS shuts down on voltage while the % is still high),
    // the log shows what the gauge and the voltage were doing
    if (m_trace.GetSeconds() >= 60) {
        m_trace.Update();
        log_write("[Battery] trace psm %d%% gauge %d%% %d mV %d mA %.1f C step %d\n", m_pct, m_gauge_pct, m_mv, m_ma, m_temp, static_cast<int>(m_step));
    }
    if (const auto v = g.Read(MAX17050_Temp)) m_temp = static_cast<s16>(*v) / 256.0;
    if (const auto v = g.Read(MAX17050_TTE)) m_tte_s = *v == 0xFFFF ? 0 : static_cast<u64>(*v * 5.625);
    if (m_lbl && !m_saver.IsActive() && (m_reapply.GetSeconds() >= 10 || m_reapply_due)) {
        m_reapply_due = false;
        // the system can switch dimming back on; the screen must stay at full brightness
        m_reapply.Update();
        lblDisableDimming();
        if (m_calib) {
            m_calib_mode = -1; // the calibration puts its own value back (ApplyCalibScreen)
        } else {
            lblSetCurrentBrightnessSetting(m_dimmed ? MIN_BRIGHTNESS : 1.0f);
        }
    }
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    // a charging current also means a charger (psm can lag behind a cable plugged in at 0 %)
    const auto avg = g.Current(MAX17050_AvgCurrent);
    m_charger = charger != PsmChargerType_Unconnected || (avg && *avg > CHARGING_MA);
    UpdateStep();
    m_docked = appletGetOperationMode() == AppletOperationMode_Console;
    if (m_temp >= HOT_C) {
        m_hot = true;
    } else if (m_temp <= COOL_C) {
        m_hot = false;
    }
    // the calibration: full load until the first cut-off (no 35 % gentle mode), none in a charge or in the tail after a cut-off
    const bool load = m_stop_ask ? false : m_calib ? (!m_charger && !m_docked && !m_hot && !m_calib->charge && !m_calib->tail)
                              : (!m_charger && !m_docked && !m_hot && !(m_pct >= 0 && m_pct <= GENTLE_PCT));
    if (load != g_load) {
        log_write("[Battery] drain load %s (%d%%, %.1f C, charger %d, docked %d)\n", load ? "on" : "off", m_pct, m_temp, m_charger, m_docked);
    }
    g_load = load;
    // below GENTLE_PCT the screen goes to the minimum too: the lowest current that still drains the battery, so
    // the console reaches a real empty at rest-like load instead of cutting off on a voltage collapse
    if (m_calib) {
        ApplyCalibScreen();
        CalibTick();
    } else if (m_lbl) {
        const bool gentle = m_pct >= 0 && m_pct <= GENTLE_PCT && !m_charger;
        if (gentle != m_dimmed) {
            m_dimmed = gentle;
            if (!m_saver.IsActive()) { // the saver owns the brightness while it is on; the reapply puts this back after it
                lblSetCurrentBrightnessSetting(gentle ? MIN_BRIGHTNESS : 1.0f);
            }
        }
    }
    using wizard::DrainStep;
    if (m_calib) {
        m_status = CalibStatus();
    } else if (m_step == DrainStep::CycleCounted) {
        m_status = "Both cycles are done. Press B and finish step 5 in the wizard: match in the other MMC."_i18n;
    } else if (m_step == DrainStep::Charging) {
        m_status = "The battery ran down: this cycle's discharge is done. Keep the charger in until 100%; the cycle then counts by itself."_i18n
            + " " + "Keep the screen on while it charges. After a red battery, leave it on the charger for about 3 minutes before you switch it on."_i18n;
    } else if (m_step == DrainStep::NotEmptyYet) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " %u%% / %u%%", m_cycle_got, wizard::CYCLE_DONE);
        m_status = "Charging, the drain is paused. Discharged in this cycle:"_i18n + buf + ". "
            + "Charge to 100% and unplug the charger: the drain goes on. Or unplug it now to drain further first."_i18n;
    } else {
        m_status = m_docked ? "Paused: take the console out of the dock"_i18n
            : m_hot ? "Paused: the battery is hot, waiting until it cools to 55 °C"_i18n
            : m_pct >= 0 && m_pct <= GENTLE_PCT ? "Below 35%: the load is off and the screen is dimmed, so the console runs down gently to a real empty. It switches itself off; then charge it to 100%."_i18n
        : "Kefir Hub loads the CPU and GPU to drain the battery. The console switches itself off; then charge it to 100%."_i18n;
    }
}

void DrainMenu::UpdateStep() {
    using wizard::DrainStep;
    if (m_step_poll.GetSeconds() < 5 && m_step_read) {
        return;
    }
    m_step_poll.Update();
    m_step_read = true;
    const auto total = detail::CycleTotalNow();
    if (!total) {
        return;
    }
    const wizard::Live live{*total, m_charger, static_cast<std::uint32_t>(std::max(m_pct, 0)), 0, false,
                            static_cast<u64>(std::time(nullptr))};
    if (wizard::Advance(m_state, live)) {
        detail::SaveState(m_state);
        account_restore::ClearReopenHubHint();
        log_write("[Battery] cycle %d counted on the drain page (total %u)\n", m_cycle, *total);
    }
    m_cycle_got = wizard::Got(m_state, live, m_cycle);
    m_step = wizard::DrainStepOf(m_state, live, m_cycle);
    if (m_step == DrainStep::CycleCounted && m_cycle == 1 && !m_calib) { // the calibration runs one cycle at a time
        m_cycle = 2;
        m_step = wizard::DrainStepOf(m_state, live, m_cycle);
    }
}

void DrainMenu::ApplyCalibScreen() {
    if (!m_lbl || !m_calib || m_calib->charge || m_saver.IsActive()) {
        return;
    }
    const int mode = m_charger ? 1 : m_calib->tail ? 2 : 3;
    if (mode == m_calib_mode) {
        return;
    }
    m_calib_mode = mode;
    if (mode == 1) {
        lblSetCurrentBrightnessSetting(m_saved_brightness);
        if (m_saved_auto) {
            lblEnableAutoBrightnessControl();
        }
    } else {
        if (m_saved_auto) {
            lblDisableAutoBrightnessControl();
        }
        lblSetCurrentBrightnessSetting(mode == 2 ? TAIL_BRIGHTNESS : 1.0f);
    }
}

void DrainMenu::CalibTick() {
    const auto now = static_cast<s64>(std::time(nullptr));
    if (m_calib_last && now > m_calib_last && now - m_calib_last <= 5) {
        m_run_s += static_cast<u64>(now - m_calib_last);
    }
    // a gap in the once-a-second tick after a low voltage on battery: the console slept (a low battery puts it to sleep) and
    // was woken, with or without the charger (the user plugs it in before he wakes it). The same gap and voltage that
    // Cutoffs() in the log calls a cut-off. The values before the gap decide: after it the charger may already be in.
    if (!m_calib->charge && !m_calib->tail && m_calib_last && now - m_calib_last > calib::CUTOFF_GAP_S
        && !m_tick_charger && m_tick_mv > 0 && m_tick_mv <= calib::CUTOFF_MAX_MV) {
        OnSleptAwake(now);
    }
    m_calib_last = now;
    m_tick_mv = m_mv;
    m_tick_charger = m_charger;
    // the QH counter on battery: the cut-off is the baseline for the charge that comes in after it, also when the console was off
    // meanwhile (the Hub restarted) and the charger filled the battery. Kept in the marker every 30 s, the sleep gap writes it too.
    if (!m_charger && !m_calib->charge && m_qh) {
        m_tick_qh = m_qh;
        if (m_qh_saved.GetSeconds() >= 30) {
            m_qh_saved.Update();
            if (auto m = calib_flow::LoadMarker()) {
                m->qh_last = m_tick_qh;
                calib_flow::SaveMarker(*m);
            }
        }
    }
    if (m_log) {
        calib::Sample sm;
        sm.time = now;
        sm.kind = m_calib->charge ? 'C' : 'D';
        sm.step = m_calib->step;
        sm.psm_pct = m_pct;
        sm.gauge_pct = m_gauge_pct;
        sm.rem_mah = m_cap_rem_mah;
        sm.full_mah = m_cap_full_mah;
        sm.mv = m_mv;
        sm.ma = m_ma;
        sm.temp_c = m_temp;
        sm.load = g_load;
        sm.brightness = m_calib->charge ? -1 : m_calib_mode == 1 ? static_cast<int>(m_saved_brightness * 100.f + 0.5f) : m_calib_mode == 2 ? 1 : 100;
        sm.charger = m_charger;
        const auto line = calib::FormatLine(sm);
        std::fputs(line.c_str(), m_log);
        std::fflush(m_log);
        if (m_log_commit.GetSeconds() >= 10) {
            m_log_commit.Update();
            fsdevCommitDevice("sdmc"); // the last seconds before a cut-off must reach the card
            if (!fs::FsNativeSd().FileExists(CALIB_LOG)) { // deleted under the page (a cleanup over FTP): the lines went nowhere
                log_write("[Calib] the drain log vanished: a new one is started\n");
                CloseCalibLog();
                OpenCalibLog();
            }
        }
    }
    HealFullCharge();
    if (m_calib->charge && m_charger && m_pct >= 100) {
        m_full_seen = true;
    }
    // enough is drained and the charger is in (the console was cut off and now charges): the drain is over, go to the charge
    if (!m_calib->charge && m_charger && (m_step == wizard::DrainStep::Charging || m_calib->tail) && m_calib->on_drained) {
        const auto go = std::move(m_calib->on_drained);
        m_calib->on_drained = nullptr;
        log_write("[Calib] drain over, %llu s on battery; the charge follows\n", static_cast<unsigned long long>(m_state.drain_s[m_cycle - 1]));
        // the callbacks read the drain log (the zero, the cycle count). A file open for writing cannot be opened again on
        // Horizon's FS: close it first, or they see no log at all ("the drain log has no last voltage")
        CloseCalibLog();
        SetPop();
        go();
        return;
    }
    // the cycle is counted (full after enough drained) and the charger came out: the Hub goes on to the next drain by itself
    const bool counted = m_calib->charge ? m_full_seen : m_step == wizard::DrainStep::CycleCounted;
    if (counted && !m_charger && m_calib->on_charge_done) {
        const auto done = std::move(m_calib->on_charge_done);
        m_calib->on_charge_done = nullptr;
        CloseCalibLog(); // same: the cycle count reads the log
        SetPop();
        done();
    }
}

void DrainMenu::OnSleptAwake(s64 now) {
    const auto slept = now - m_calib_last;
    m_calib->tail = true;  // the next poll: load off (g_load), the tail brightness (ApplyCalibScreen), the tail title
    m_calib_mode = -1;
    if (auto m = calib_flow::LoadMarker()) {
        m->cutoffs++;          // a Hub started again after this goes on in the tail too
        m->cut_mv = m_tick_mv; // the zero's fallback when the drain log is lost
        if (m_tick_qh) m->qh_last = m_tick_qh;
        calib_flow::SaveMarker(*m);
    }
    if (m_log) {
        std::fprintf(m_log, "%lld,S,%d,%d,%d,-1,-1,%d,0,0.0,0,0,%d\n", static_cast<long long>(now), m_calib->step, m_pct, m_gauge_pct, m_tick_mv, m_charger ? 1 : 0);
        std::fflush(m_log);
        fsdevCommitDevice("sdmc");
    }
    log_write("[Calib] the console slept %lld s after %d mV on battery (now %d mV, charger %d): counted as a cut-off, the tail mode from here\n",
              static_cast<long long>(slept), m_tick_mv, m_mv, m_charger ? 1 : 0);
    // with the charger in the next tick ends the drain (on_drained): the zero in cycle 1, then the charge page
    App::Notify(m_charger ? "The console slept: the drain is over"_i18n : "The console slept: the drain goes on slowly"_i18n);
}

auto DrainMenu::CalibTitle() const -> std::string {
    const auto which = m_calib->step == 2 ? "Cycle 2"_i18n : "Cycle 1"_i18n;
    return which + ": " + (m_calib->charge ? "charge to 100 %"_i18n : m_calib->tail ? "drain after the cut-off"_i18n : "drain to the cut-off"_i18n);
}

auto DrainMenu::CalibStatus() const -> std::string {
    if (m_calib->charge) {
        const auto which = m_calib->step == 2 ? "Cycle 2"_i18n : "Cycle 1"_i18n;
        if (!m_charger) {
            return which + ": " + "the charger is out: the charge is paused. Plug it in to go on."_i18n;
        }
        if (m_pct >= 100) {
            return which + ": " + (m_calib->step == 2 ? "charged to 100 %. This was the last cycle: take the charger out, the report follows."_i18n
                                                      : "charged to 100 %. Now cycle 2: take the charger out, the drain starts by itself."_i18n);
        }
        return which + ": " + "charging to 100 %. The screen stays on, so the console surely charges to the end. You can leave the console as it is or use it; when it reaches 100 %, come back to Kefir Hub and open Battery calibration."_i18n;
    }
    if (m_charger) {
        if (m_step == wizard::DrainStep::CycleCounted) {
            return "This cycle is counted. Take the charger out: the next step starts by itself."_i18n;
        }
        if (m_step == wizard::DrainStep::Charging) {
            return "The battery ran down enough. Keep the charger in until 100 %: the cycle then counts by itself."_i18n;
        }
        return "The charger is in. The drain is paused. Take the charger out to go on."_i18n;
    }
    if (m_docked) {
        return "Paused: take the console out of the dock"_i18n;
    }
    if (m_hot) {
        return "Paused: the battery is hot, waiting until it cools to 55 °C"_i18n;
    }
    return (m_calib->tail
        ? "The console has switched off already: now it drains slowly to squeeze out the last charge. Auto-brightness is off and the brightness is at the minimum."_i18n
        : "Full load: the console drains as fast as it can. When it switches off, plug the charger in. It may not start for about 20 minutes: this is normal. Then switch it on and open Kefir Hub."_i18n)
        + " " + "Everything is written to a log."_i18n;
}

auto DrainMenu::DrainSeconds() const -> u64 {
    return m_calib ? m_run_s : m_state.drain_s[m_cycle - 1];
}



namespace {
std::atomic<bool> g_auto_suppressed{false};
} // namespace

auto AutoDrainSuppressed() -> bool { return g_auto_suppressed; }
void SetAutoDrainSuppressed(bool v) { g_auto_suppressed = v; }

// at the Hub's start: a drain that ended without restoring the screen (crash, switch-off, the Hub killed) leaves its
// file behind; put the brightness and the automatic brightness back as they were
namespace {
std::atomic<bool> g_drain_interrupted{false};
} // namespace

auto DrainWasInterrupted() -> bool { return g_drain_interrupted; }

void RestoreDrainBrightness() {
    const auto kept = ReadSavedBrightness();
    if (!kept) {
        return;
    }
    g_drain_interrupted = true; // the drain did not end by the user: the console switched off or the Hub died
    if (R_SUCCEEDED(lblInitialize())) {
        lblSetCurrentBrightnessSetting(kept->brightness);
        if (kept->auto_on) {
            lblEnableAutoBrightnessControl();
        }
        lblExit();
        log_write("[Battery] screen restored after an unfinished drain (brightness %.2f, auto %d)\n", kept->brightness, kept->auto_on ? 1 : 0);
    }
    DeleteSavedBrightness();
}

void StartDrain(int cycle) {
    App::Push<DrainMenu>(cycle);
}

void StartCalibRun(CalibRun run) {
    const int cycle = run.step == 2 ? 2 : 1;
    App::Push<DrainMenu>(cycle, std::move(run));
}

void OpenDrain(int cycle) {
    App::Push<ui::OptionBox>(
        "Drain the battery now? Unplug the charger and keep the console in your hands. Kefir Hub sets the screen to full brightness and loads the CPU and GPU until the console switches itself off; the load pauses above 60 °C. Afterwards charge it to 100% and open Kefir Hub."_i18n,
        "Back"_i18n, "Start"_i18n, 0, [cycle](auto op) {
            if (op && *op == 1) {
                App::Push<DrainMenu>(cycle);
            }
        });
}

} // namespace sphaira::battery
