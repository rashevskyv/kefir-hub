#pragma once

// Tools > Battery calibration: what the steps (battery_calib.cpp) and the report with the transfer (battery_calib_report.cpp) share.

#include "battery_calib_state.hpp"

#include <switch.h>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace sphaira::battery::calib_flow {

auto LoadMarker() -> std::optional<calib::Marker>;
void SaveMarker(const calib::Marker& m);
auto Now() -> std::int64_t;
auto StepLabel(int i) -> std::string;
auto CycleName(int cycle) -> std::string; // "Calibration cycle 1/2"

void ShowSteps();
// The full battery of the cycle (battery_zero.hpp): the charger is in and it ended the charge. Cycle 1 writes the factory capacity;
// cycle 2 writes nothing and notes what the chip learned. Not tied to the Hub's pages: the Hub also calls it at its start.
// NotYet = not full / no cycle in the charge phase; Done = done (now or before); Waits = the charger ended the charge but the
// console shows less than 100 %: the gauge has not taken the battery for full, nothing is noted.
enum class Measure { NotYet, Done, Waits };
auto MeasureCapacityIfFull() -> Measure;
void StartOver();
void StartCheck();
void ShowReport();
void ShowTransfer();
// at the Hub's start in a system that does not hold the newest calibration yet: write it there
void ApplyLedger();

} // namespace sphaira::battery::calib_flow

// the zero (battery_zero_write.cpp): the cut-off voltage of the zero drain into the gauge
namespace sphaira::battery::zero_write {
// the cut-off voltage into V_empty; the counters stay as the chip has them
auto WriteZero(int mv, std::string& failed) -> Result;
} // namespace sphaira::battery::zero_write
