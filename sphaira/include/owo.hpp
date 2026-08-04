#pragma once

#include <switch.h>
#include <string>
#include <vector>
#include "ui/progress_box.hpp"

namespace sphaira {

// npdm meta flags, bits 1-3 (ProcessAddressSpace).
enum class ForwarderAddressSpace : u8 {
    Bit36 = 1, // AddressSpace64BitOld, what the hbl npdm ships with.
    Bit39 = 3, // AddressSpace64Bit, needed by homebrew that wants more va space.
};

struct OwoConfig {
    std::string nro_path;
    std::string args{};
    std::string name{};
    std::string author{};
    NacpStruct nacp;
    std::vector<u8> icon;
    std::vector<u8> logo;
    std::vector<u8> gif;
    ForwarderAddressSpace address_space{ForwarderAddressSpace::Bit36};

    std::vector<u8> program_nca{};
};

auto install_forwarder(OwoConfig& config, NcmStorageId storage_id) -> Result;
auto install_forwarder(ui::ProgressBox* pbox, OwoConfig& config, NcmStorageId storage_id) -> Result;

} // namespace sphaira
