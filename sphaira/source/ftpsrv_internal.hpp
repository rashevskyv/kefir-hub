#pragma once

#include "ftpsrv_helper.hpp"
#include <ftpsrv_vfs.h>

#if ENABLE_NETWORK_INSTALL
namespace sphaira::ftpsrv {
extern FtpVfs g_vfs_install;
} // namespace sphaira::ftpsrv

extern "C" int ftp_root_write_router(const char* path);
#endif
