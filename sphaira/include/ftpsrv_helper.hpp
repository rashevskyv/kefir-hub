#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sphaira::ftpsrv {

bool Init();
void Exit();

using OnInstallStart = std::function<bool(const char* path)>;
using OnInstallWrite = std::function<bool(const void* buf, size_t size)>;
using OnInstallClose = std::function<void()>;

void InitInstallMode(OnInstallStart on_start, OnInstallWrite on_write, OnInstallClose on_close);
void DisableInstallMode();

bool IsRunning();
unsigned GetPort();
bool IsAnon();
const char* GetUser();
const char* GetPass();

// the folders exposed as extra root devices next to "sdmc:" and "install:".
// replaces the whole set; the card root is ignored (it is already "sdmc:").
void SetFtpMountedFolders(const std::vector<std::string>& paths);
void ClearFtpMountedFolders();
std::vector<std::string> GetFtpMountedNames();

} // namespace sphaira::ftpsrv
