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
inline const char* Snapshot00F0FileName() { return "80000000000000F0"; }
inline const char* Snapshot00F0Path() { return "/config/kefir/restore_pending/80000000000000F0"; }
// Written by account_0010_dump.te after a successful raw copy.
inline const char* DumpedOkPath() { return "/config/kefir/restore_pending/dumped.ok"; }
// Written by account_0010_apply_link.te after baas/nas commit into live 0010.
inline const char* LinkAppliedOkPath() { return "/config/kefir/restore_pending/link_applied.ok"; }
// Written by nand_transfer_restore_auto.te after profiles & play hours commit.
inline const char* NandRestoredOkPath() { return "/config/kefir/restore_pending/nand_restored.ok"; }
// Unix SD path of the nand_transfer pack folder (no sd: prefix).
inline const char* NandPackPath() { return "/config/kefir/restore_pending/nand_pack.txt"; }
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
inline const char* NandRestoreTeName() { return "nand_transfer_restore_auto.te"; }
inline const char* NandDumpTeName() { return "nand_transfer_dump_auto.te"; }
inline const char* ReopenHubFlagPath() { return "/config/kefir/reopen_hub.flag"; }

struct Pending {
    bool present{};
    bool snapshot_ok{};
    bool rolled_back{};
    std::string phase; // wait_dump | ready | wait_link | wait_nand_restore | wait_nand_dump | applied
    std::vector<std::string> pack_dirs;
};

struct RawSnapshotReport {
    bool save_0010{};
    bool save_00F0{};
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
// Best-effort raw BIS copies of 0010 and 00F0 for Undo. Never fails the caller.
auto TrySnapshotRawSystemSaves(ui::ProgressBox* pbox, RawSnapshotReport& out) -> void;
auto InstallRestoreTeScripts() -> void;
auto LaunchTegraRomfs(const char* romfs_name) -> bool;
auto LaunchTegraDump() -> bool;

// One-shot Ultrahand [on-boot] toast: overlay cannot launch the HOME forwarder
// or NRO, so after CFW start the user is told to open Kefir Hub.
auto ArmReopenHubHint() -> void;
auto ClearReopenHubHint() -> void;

} // namespace sphaira::account_restore
