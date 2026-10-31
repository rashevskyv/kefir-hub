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

void ShowSteps();
void ShowReport();
void ShowTransfer();
// at the Hub's start in a system that does not hold the newest calibration yet: write it there
void ApplyLedger();

} // namespace sphaira::battery::calib_flow

// the zero (battery_zero_write.cpp): the cut-off voltage of the zero drain into the gauge
namespace sphaira::battery::zero_write {
// once at the Hub's start: the coulomb counter then, for "what came in since the switch-on"
void ReadQhAtStart();
auto WriteZero(int mv, std::string& failed, int qh_last) -> Result;
// the battery is full by voltage and current but the chip shows far less: the counters to 100 % (V_empty stays)
auto WriteFull(std::string& failed) -> Result;
} // namespace sphaira::battery::zero_write
