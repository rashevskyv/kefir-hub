#pragma once

#include "fs.hpp"
#include <functional>
#include <vector>

namespace sphaira::romfs_pack {

// <title_dir>/romfs exists and <title_dir>/romfs.bin does not.
auto CanPack(const fs::FsPath& title_dir) -> bool;

// Packs <dir>/romfs into <dir>/romfs.bin on the SD card for each title folder
// (atmosphere/contents/<id>). Asks first whether to delete the romfs folders after
// packing. `on_done` runs on the main thread when the job ends.
void PackTitles(std::vector<fs::FsPath> title_dirs, std::function<void()> on_done);

} // namespace sphaira::romfs_pack
