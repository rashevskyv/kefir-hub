#pragma once

#include "haze/haze_internal.hpp"
#include <map>
#include <string>
#include <switch.h>

namespace sphaira::haze {

using SaveTypeMap = std::map<std::string, FsSaveDataInfo, CaseInsensitiveLess>;
using SaveTreeMap = std::map<std::string, SaveTypeMap, CaseInsensitiveLess>;

void ScanMtpSaves(SaveTreeMap& out_tree);

} // namespace sphaira::haze
