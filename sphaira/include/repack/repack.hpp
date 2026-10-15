#pragma once

#include "title_nsp.hpp"
#include <functional>
#include <string>

namespace sphaira::repack {

// Create repack: with this bit in the content flags, the title's LayeredFS mod goes into the NSP.
inline constexpr u32 FLAG_WITH_MOD = 1u << 31;

// atmosphere/contents/<app_id> holds a romfs folder or a romfs.bin.
auto HasRomfsMod(u64 app_id) -> bool;

// Builds the merged NSP of the `flags` components with the mod baked into the newest program NCA:
// the update's when the update is in the repack, else the base's. Runs under a ProgressBox (the
// new romfs is hashed there); on_ready gets the NSP on the main thread.
void PrepareModRepack(u64 app_id, std::string name, u32 flags, std::function<void(title::NspEntry&&)> on_ready);

} // namespace sphaira::repack
