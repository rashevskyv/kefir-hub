#pragma once

#include <switch.h>
#include <optional>
#include <string>
#include <vector>
#include "ui/progress_box.hpp"

namespace sphaira {

// npdm meta flags, bits 1-3 (ProcessAddressSpace).
enum class ForwarderAddressSpace : u8 {
    Bit36 = 1, // AddressSpace64BitOld, what the hbl npdm ships with.
    Bit39 = 3, // AddressSpace64Bit, needed by homebrew that wants more va space.
};

// the kac ForceDebug bit. automatic follows the ams version, see patch_npdm().
enum class ForwarderSvcDebugMode : u8 {
    Automatic,
    Enabled,
    Disabled,
};

struct ForwarderOptions {
    bool profile_selection{};
    ForwarderAddressSpace address_space{ForwarderAddressSpace::Bit36};
    bool screenshot{true};
    bool video_capture{true};
    ForwarderSvcDebugMode svc_debug_mode{ForwarderSvcDebugMode::Automatic};
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

    // left unset, the global defaults from settings are used.
    std::optional<ForwarderOptions> options{};

    std::vector<u8> program_nca{};
};

auto install_forwarder(OwoConfig& config, NcmStorageId storage_id) -> Result;
auto install_forwarder(ui::ProgressBox* pbox, OwoConfig& config, NcmStorageId storage_id) -> Result;

} // namespace sphaira
