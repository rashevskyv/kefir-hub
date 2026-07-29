#pragma once

#include "fs.hpp"
#include "yati/source/base.hpp"
#include "location.hpp"

#include <switch.h>
#include <memory>

namespace sphaira::devoptab {

Result MountSaveSystem(u64 id, fs::FsPath& out_path);
Result MountZip(fs::Fs* fs, const fs::FsPath& path, fs::FsPath& out_path);
Result MountNsp(fs::Fs* fs, const fs::FsPath& path, fs::FsPath& out_path);
Result MountXci(fs::Fs* fs, const fs::FsPath& path, fs::FsPath& out_path);
Result MountXciSource(const std::shared_ptr<sphaira::yati::source::Base>& source, s64 size, const fs::FsPath& path, fs::FsPath& out_path);
Result MountNca(fs::Fs* fs, const fs::FsPath& path, fs::FsPath& out_path);
Result MountNcaNcm(NcmContentStorage* cs, const NcmContentId* id, fs::FsPath& out_path);
Result MountBfsar(fs::Fs* fs, const fs::FsPath& path, fs::FsPath& out_path);
Result MountNro(fs::Fs* fs, const fs::FsPath& path, fs::FsPath& out_path);

Result MountVfsAll();
Result MountWebdavAll();
Result MountHttpAll();
Result MountFtpAll();
Result MountSftpAll();
Result MountNfsAll();
Result MountSmb2All();
Result MountFatfsAll();
Result MountInternalMounts();

Result GetNetworkDevices(location::StdioEntries& out);
void UmountAllNeworkDevices();
void UmountNeworkDevice(const fs::FsPath& mount);

void DisplayDevoptabSideBar();

} // namespace sphaira::devoptab
