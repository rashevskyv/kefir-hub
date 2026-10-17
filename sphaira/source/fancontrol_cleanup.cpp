#include "fancontrol_cleanup.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <switch.h>

namespace sphaira {
namespace {

constexpr u64 FUNCONTROL_PROGRAM_ID = 0x00FF46554E43544CULL;
constexpr fs::FsPath FUNCONTROL_DIR{"/atmosphere/contents/00FF46554E43544C"};
constexpr fs::FsPath FUNCONTROL_OVERLAY{"/switch/.overlays/NX-FanControl.ovl"};

} // namespace

auto RemoveFunControl() -> bool {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) {
        return false;
    }
    const bool has_dir = sd.DirExists(FUNCONTROL_DIR);
    const bool has_overlay = sd.FileExists(FUNCONTROL_OVERLAY);
    if (!has_dir && !has_overlay) {
        return false;
    }

    // stop it first, so its files are not held open; "not running" is the usual answer
    if (has_dir && R_SUCCEEDED(pmshellInitialize())) {
        const auto rc = pmshellTerminateProgram(FUNCONTROL_PROGRAM_ID);
        pmshellExit();
        log_write("[FunControl] terminate rc=0x%X\n", rc);
    }

    if (has_dir) {
        const auto rc = sd.DeleteDirectoryRecursively(FUNCONTROL_DIR);
        log_write("[FunControl] removed %s rc=0x%X\n", FUNCONTROL_DIR.s, rc);
    }
    if (has_overlay) {
        const auto rc = sd.DeleteFile(FUNCONTROL_OVERLAY);
        log_write("[FunControl] removed %s rc=0x%X\n", FUNCONTROL_OVERLAY.s, rc);
    }
    sd.Commit();
    return true;
}

} // namespace sphaira
