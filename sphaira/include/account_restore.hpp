#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui { struct ProgressBox; }

namespace sphaira::account_restore {

inline const char* PendingDir() { return "/config/kefir/restore_pending"; }
// Raw BIS save blob (not an unpacked /su tree).
inline const char* SnapshotFileName() { return "8000000000000010"; }
inline const char* SnapshotPath() { return "/config/kefir/restore_pending/8000000000000010"; }
// Written by account_0010_dump.te after a successful raw copy.
inline const char* DumpedOkPath() { return "/config/kefir/restore_pending/dumped.ok"; }
// Written by account_0010_apply_link.te after baas/nas commit into live 0010.
inline const char* LinkAppliedOkPath() { return "/config/kefir/restore_pending/link_applied.ok"; }
// Hub stages remapped baas + nas here; TE writes them into 0010 (no Horizon OpenAccountSaveWritable).
inline const char* LinkStagingDir() { return "/config/kefir/restore_pending/link"; }
inline const char* LinkBaasDir() { return "/config/kefir/restore_pending/link/baas"; }
inline const char* LinkNasDir() { return "/config/kefir/restore_pending/link/nas"; }
// Legacy unpacked tree from older builds; deleted on new snapshots.
inline const char* LegacySnapshotDir() { return "/config/kefir/restore_pending/0010"; }
// "emu" or "sys" — written at dump time; Undo mounts the same NAND.
inline const char* NandFlagPath() { return "/config/kefir/restore_pending/nand"; }
inline const char* StatePath() { return "/config/kefir/restore_pending/state.json"; }
inline const char* RolledBackPath() { return "/config/kefir/restore_pending/rolled_back.ok"; }
inline const char* RollbackTeName() { return "Undo_restore_if_wont_boot.te"; }
inline const char* DumpTeName() { return "account_0010_dump.te"; }
inline const char* ApplyLinkTeName() { return "account_0010_apply_link.te"; }

struct Pending {
    bool present{};
    bool snapshot_ok{};
    bool rolled_back{};
    std::string phase; // wait_dump | ready | wait_link | applied
    std::vector<std::string> pack_dirs;
};

auto LoadPending() -> Pending;
auto HasUnfinishedRestore() -> bool;
auto SavePending(const std::vector<std::string>& pack_dirs, const std::string& phase, bool snapshot_ok) -> Result;
auto ClearPending() -> Result;
auto SnapshotOk() -> bool;

// Records whether the snapshot came from emuMMC or sysMMC (App::IsEmummc()).
auto WriteNandFlag() -> Result;

// Raw BIS copy of 8000000000000010. Does not terminate ACCOUNT / unpack /su.
auto Dump0010ReadOnly(ui::ProgressBox* pbox) -> Result;
auto InstallRestoreTeScripts() -> void;
auto LaunchTegraRomfs(const char* romfs_name) -> bool;
auto LaunchTegraDump() -> bool;

} // namespace sphaira::account_restore
