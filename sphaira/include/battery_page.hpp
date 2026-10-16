#pragma once

namespace sphaira::battery {

// Tools → Battery: reads the fuel gauge chip and this MMC's saved copy in a ProgressBox,
// then shows the verdict and the values; X backs them up to /config/kefir/battery.
void Open();

// + on the Battery page: reset from factory data, restore from the last backup, match the other MMC.
void OpenCalibrate();

} // namespace sphaira::battery
