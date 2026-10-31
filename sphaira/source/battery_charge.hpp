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

} // namespace sphaira::battery::charge
