#pragma once

// Battery charging on and off through the psm service (libnx psmEnableBatteryCharging / psmDisableBatteryCharging).
// With charging off the charger can stay in the console while it drains: this is what the calibration cycles want.
// Charging must never stay off by accident: a flag file remembers it, the Hub switches charging on again at its start and at its end.

#include <switch.h>
#include <optional>

namespace sphaira::battery::charge {

// nullopt = the service did not answer
auto IsOn() -> std::optional<bool>;
// on = true: charging as usual. A refusal of the service is returned as it is.
auto Set(bool on) -> Result;
// at the Hub's start and end: if charging was left off, switch it on
void RestoreIfLeftOff();
// the diagnostic: /config/kefir/battery/charge_test.run (a file the Hub finds at its start) runs it once and writes
// /config/kefir/battery/charge_test.txt: the current of the battery with the charger in, with charging off and with a load
void RunTestIfAsked();
// the same for the charger chip: /config/kefir/battery/hiz_test.run -> hiz_test.txt: with its input cut (bq24193 EN_HIZ) does the
// console live on the battery while the charger stays in? Reads the chip's registers; writes REG00 for 30 s and puts it back.
void RunHizTestIfAsked();

// The cut-off test (owner, 2026-10-31): the drain page with the charger's input cut, until the console switches itself off.
// /config/kefir/battery/hiz_cutoff.run at the Hub's start begins it (true: the caller opens the drain page). Every second the drain
// page calls HizTick: it keeps the input cut and writes /config/kefir/battery/hiz_cutoff.txt (flushed to the card every 5 s). At the
// next Hub start, HizStartCheck logs what REG00 and REG08 hold (did the cut survive the switch-off?) and puts the input back.
auto HizActive() -> bool;
auto StartCutoffTestIfAsked() -> bool;
void HizTick(int mv, int avg_ma, int pct);
void HizStartCheck();
void HizRelease(); // the input back, the flag removed, the log closed

} // namespace sphaira::battery::charge
