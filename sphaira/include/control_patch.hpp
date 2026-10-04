#pragma once

#include "nacp_patch.hpp"
#include <switch.h>
#include <vector>

namespace sphaira::control_patch {

// Rewrites control.nacp inside a whole Control NCA held in memory: decrypt the RomFS section,
// patch, rehash the IVFC tree, update the fs header hash, encrypt again. The NCA header signature
// no longer matches afterwards, so the console needs sigpatches (Kefir ships sys-patch).
// `changed` is false when the NACP already had those values (the NCA is left untouched).
Result PatchNca(std::vector<u8>& nca, const nacp_patch::Patch& patch, bool& changed);

// current restrictions of an installed game, from its control data.
Result ReadState(u64 app_id, nacp_patch::State& out);

// patches every installed Control NCA of the game (base and update, SD and NAND) and re-registers
// it under the same content id.
Result PatchInstalled(u64 app_id, const nacp_patch::Patch& patch);

} // namespace sphaira::control_patch
