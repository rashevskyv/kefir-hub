#pragma once

#include <switch.h>
#include <optional>
#include <string>
#include <vector>
#include "ui/progress_box.hpp"
#include "utils/core.hpp"

namespace sphaira {

// npdm meta flags, bits 1-3 (ProcessAddressSpace). The 32-bit spaces start at
// 0x00200000 rather than 0x08000000, which lets homebrew (Wine-NX, Box64) map a
// fixed low image base; they cap total VA at 4 GiB.
enum class ForwarderAddressSpace : u8 {
    Bit32 = 0,        // AddressSpace32Bit
    Bit36 = 1,        // AddressSpace64BitOld, what the hbl npdm ships with.
    Bit32NoAlias = 2, // AddressSpace32BitNoReserved
    Bit39 = 3,        // AddressSpace64Bit, needed by homebrew that wants more va space.
};

// the kac ForceDebug bit. automatic follows the ams version, see patch_npdm().
enum class ForwarderSvcDebugMode : u8 {
    Automatic,
    Enabled,
    Disabled,
};

struct ForwarderOptions {
    bool profile_selection{};
    ForwarderAddressSpace address_space{ForwarderAddressSpace::Bit39};
    CpuCoreMode core_mode{CpuCoreMode::Three};
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

    // left unset, derived from SHA-256(nro_path + args).
    std::optional<u64> title_id{};
};

auto install_forwarder(OwoConfig& config, NcmStorageId storage_id) -> Result;
auto install_forwarder(ui::ProgressBox* pbox, OwoConfig& config, NcmStorageId storage_id) -> Result;

} // namespace sphaira
