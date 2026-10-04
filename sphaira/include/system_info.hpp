#pragma once

#include <string>

namespace sphaira::system_info {

// Tools → System information: a plain text report (firmware, Atmosphère, battery, hardware),
// saved to REPORT_PATH and shown in the text viewer, so it can also be copied off the console.
inline constexpr const char* REPORT_PATH = "/config/kefir/system-info.txt";

auto BuildReport() -> std::string;

} // namespace sphaira::system_info
