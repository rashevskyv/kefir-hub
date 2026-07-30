#pragma once

#include "fs.hpp"

#include <switch.h>

namespace sphaira::devoptab {

// Mounts the romfs of an installed title's nca so cheat lookup can read it.
Result MountNcaNcm(NcmContentStorage* cs, const NcmContentId* id, fs::FsPath& out_path);
void UmountNeworkDevice(const fs::FsPath& mount);

} // namespace sphaira::devoptab
