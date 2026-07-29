#pragma once

#include <functional>
#include <memory>
#include <string>
#include "fs.hpp"

namespace sphaira::haze {

bool Init();
void Exit();
bool IsRunning();

// an arbitrary fs exposed as an extra, "pinned" MTP storage. fs_factory
// produces the fs on demand (so it can be recreated across MTP restarts);
// base_path roots the storage at a subfolder ("" = whole fs). Works for a plain
// folder (FsNativeSd + base_path) and for virtual mounts (FsNcm / FsZip),
// because MTP serves through the fs::Fs abstraction.
struct PinnedMount {
    std::function<std::unique_ptr<fs::Fs>()> fs_factory{};
    std::string display_name{};
    std::string base_path{};
    // unique devoptab id, assigned by MountFs.
    std::string internal{};
};

// replace the pinned storages and (re)start MTP so a PC re-enumerates.
bool MountFs(std::vector<PinnedMount> mounts);
// clear the pinned storages and restart MTP (or stop it if nothing else remains).
void UnmountPinned();
// true if any pinned storage is currently mounted.
bool HasPinned();
// display names of the pinned storages, comma separated. empty if none.
std::string GetPinnedName();

using OnInstallStart = std::function<bool(const char* path)>;
using OnInstallWrite = std::function<bool(const void* buf, size_t size)>;
using OnInstallClose = std::function<void()>;

void InitInstallMode(OnInstallStart on_start, OnInstallWrite on_write, OnInstallClose on_close);
void DisableInstallMode();

} // namespace sphaira::haze
