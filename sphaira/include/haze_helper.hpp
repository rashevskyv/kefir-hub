#pragma once

#include <functional>
#include <memory>
#include <string>
#include "fs.hpp"

namespace sphaira::haze {

bool Init();
void Exit();
bool IsRunning();

// mount an arbitrary fs as an extra, "pinned" MTP storage and (re)start MTP so
// a PC can see it. fs_factory produces the fs on demand (so it can be recreated
// across MTP restarts); base_path roots the storage at a subfolder ("" = whole
// fs). Works for a plain folder (FsNativeSd + base_path) and for virtual mounts
// (FsNcm / FsZip), because MTP serves through the fs::Fs abstraction.
bool MountFs(std::function<std::unique_ptr<fs::Fs>()> fs_factory, const std::string& display_name, const std::string& base_path = "");
// clear the pinned storage and restart MTP (or stop it if nothing else remains).
void UnmountPinned();
// true if a pinned storage is currently mounted.
bool HasPinned();
// display name of the pinned storage, empty if there is none.
std::string GetPinnedName();

using OnInstallStart = std::function<bool(const char* path)>;
using OnInstallWrite = std::function<bool(const void* buf, size_t size)>;
using OnInstallClose = std::function<void()>;

void InitInstallMode(OnInstallStart on_start, OnInstallWrite on_write, OnInstallClose on_close);
void DisableInstallMode();

} // namespace sphaira::haze
