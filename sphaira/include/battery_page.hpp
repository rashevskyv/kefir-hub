#pragma once

namespace sphaira::battery {

// Tools → Battery: reads the fuel gauge chip and this MMC's saved copy in a ProgressBox,
// then shows the verdict and the values; X backs them up to /config/kefir/battery.
void Open();

// + on the Battery page: reset from factory data, restore from the last backup, match the other MMC.
void OpenCalibrate();

// steps 3 and 4: drain under load until the console switches off (battery_drain.cpp).
void OpenDrain(int cycle);
// the same without the question, for the wizard's automatic start when the charger comes out
void StartDrain(int cycle);
// at the Hub's start: restores the brightness a drain that did not end properly left changed
void RestoreDrainBrightness();
// true after the user stopped a drain; cleared when the charger goes in (so Back from the drain page does not
// start it again at once)
auto AutoDrainSuppressed() -> bool;
void SetAutoDrainSuppressed(bool v);

} // namespace sphaira::battery
