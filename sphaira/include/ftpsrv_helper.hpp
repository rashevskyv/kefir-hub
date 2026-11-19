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
std::vector<std::string> GetQueuedInstallFiles();
bool HasMoreQueuedFiles();
bool HasActiveOrQueuedFiles();

// a web transfer is running: an open FTP listener cuts the receive speed of every other socket 3-6x,
// so the FTP thread closes it once no FTP session is busy and opens it again when the count is 0.
// only changes a counter (nested pauses), never blocks.
void PauseForTransfer(bool pause);

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
