#pragma once

#include <functional>

namespace sphaira::battery {

// Tools → Battery: reads the fuel gauge chip and this MMC's saved copy in a ProgressBox,
// then shows the verdict and the values; X backs them up to /config/kefir/battery.
void Open();

// + on the Battery page: the calibration (the same as Tools > Battery calibration).
void OpenCalibrate();
// the actions of the calibration: back up, align the charge to the voltage, restore, match the other MMC, battery type
void OpenActions();

// steps 3 and 4: drain under load until the console switches off (battery_drain.cpp).
void OpenDrain(int cycle);
// the same without the question, for the wizard's automatic start when the charger comes out
void StartDrain(int cycle);
// at the Hub's start: restores the brightness a drain that did not end properly left changed
void RestoreDrainBrightness();
// true when this start found the leftovers of a drain that did not end properly (the console switched itself off)
auto DrainWasInterrupted() -> bool;
// from the app loop: after such a drain, once the UI is up, checks the chip against its voltage estimate and offers to align it
void PollInterruptedDrain();
// true after the user stopped a drain; cleared when the charger goes in (so Back from the drain page does not
// start it again at once)
auto AutoDrainSuppressed() -> bool;
// step 5, A in the MMC where the cycles were done: restart into the other MMC, match there, offer to come back
void OfferHop();
// from the app loop: a hop waits for this MMC, so the match runs by itself and the Hub offers to go back
void PollHop();
// Actions → Battery type: the battery version byte of PRODINFO (LiHV 4.32 V for a larger battery), backed up and verified
void OpenBatteryType();
// from the app loop: a PRODINFO change waits for the restart that opens Atmosphère's write lock, so it is done now
void PollCal0Pending();
void SetAutoDrainSuppressed(bool v);

// ---- Tools > Battery calibration (battery_calib.cpp, docs/dev/BATTERY-CALIBRATION.md "Cutoff pipeline") ----
// One run of the drain page for the calibration: a drain (max load and brightness; `tail` after a cut-off: minimal load and
// brightness, auto-brightness off) or a charge (no load, the screen kept on, the screensaver). Both log a line a second.
struct CalibRun {
    bool charge{};
    bool tail{};
    int step{1};   // the cycle (1 or 2) this run belongs to: it goes into every log line
    // charge only: the charger came out at 100 % (the cycle's charge phase is done)
    std::function<void()> on_charge_done;
    // charge only, the reset (step 1): at 100 % on the charger the page asks once, and on yes it closes and calls on_reset
    bool reset{};
    std::function<void()> on_reset;
    // drain only: the user stopped it on purpose (B, Stop)
    std::function<void()> on_stop;
};
void StartCalibRun(CalibRun run);
// the entry of Tools: the intro, the steps, and everything the marker on the SD says is going on
void OpenCalibration();
// from the app loop, once the UI is up: a drain that was cut off goes on by itself, and a calibration made in another system is taken over
void PollCalibration();

} // namespace sphaira::battery
