#pragma once

// Tools > Battery calibration: what the steps (battery_calib.cpp) and the report with the transfer (battery_calib_report.cpp) share.

#include "battery_calib_state.hpp"

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
