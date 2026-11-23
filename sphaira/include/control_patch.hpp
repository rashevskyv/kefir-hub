#pragma once

#include "nacp_patch.hpp"
#include <switch.h>
#include <vector>

namespace sphaira::control_patch {

// Rewrites control.nacp inside a whole Control NCA held in memory: decrypt the RomFS section,
// patch, rehash the IVFC tree, update the fs header hash, encrypt again. The NCA header signature
// no longer matches afterwards, so the console needs sigpatches (Kefir ships sys-patch).
// `changed` is false when the NACP already had those values (the NCA is left untouched).
// `icon_out`, when given, gets the icon as it was before the patch.
Result PatchNca(std::vector<u8>& nca, const nacp_patch::Patch& patch, bool& changed, std::vector<u8>* icon_out = nullptr);

// current restrictions of an installed game, from its control data.
Result ReadState(u64 app_id, nacp_patch::State& out);

// patches every installed Control NCA of the game (base and update, SD and NAND) and re-registers
// it under the same content id. Before the first name, version or icon change the original NCA is
// kept in /config/kefir/control_original/<content id>.nca. An icon that cannot be made small
// enough for its slot gives Result_IconTooLarge.
Result PatchInstalled(u64 app_id, const nacp_patch::Patch& patch);

// a kept original exists for an installed control NCA of the game.
bool HasOriginal(u64 app_id);
// the icon the game came with, from a kept original.
Result ReadOriginalIcon(u64 app_id, std::vector<u8>& out);
// puts the kept originals back (name, publisher, version, icon) with today's restrictions, and
// deletes them. A game update brings a new control NCA, which has no original kept.
Result RestoreOriginal(u64 app_id);

// install-time switches (Settings → Install), kept in the Hub config.ini, not in App.
bool GetInstallNoLinkedAccount();
void SetInstallNoLinkedAccount(bool v);
bool GetInstallScreenshots();
void SetInstallScreenshots(bool v);
bool GetInstallVideo();
void SetInstallVideo(bool v);
// the patch the switches ask for after an install; nothing set = all off.
auto InstallPatch() -> nacp_patch::Patch;

} // namespace sphaira::control_patch
