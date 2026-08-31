#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui { struct ProgressBox; }

namespace sphaira::account_restore {

inline const char* PendingDir() { return "/config/kefir/restore_pending"; }
inline const char* SnapshotDir() { return "/config/kefir/restore_pending/0010"; }
inline const char* StatePath() { return "/config/kefir/restore_pending/state.json"; }
inline const char* FilesPath() { return "/config/kefir/restore_pending/files.txt"; }
inline const char* RolledBackPath() { return "/config/kefir/restore_pending/rolled_back.ok"; }
inline const char* RollbackTeName() { return "Undo_restore_if_wont_boot.te"; }
inline const char* DumpTeName() { return "account_0010_dump.te"; }

struct Pending {
    bool present{};
    bool snapshot_ok{};
    bool rolled_back{};
    std::string phase; // wait_dump | ready | applied
    std::vector<std::string> pack_dirs;
};

auto LoadPending() -> Pending;
auto HasUnfinishedRestore() -> bool;
auto SavePending(const std::vector<std::string>& pack_dirs, const std::string& phase, bool snapshot_ok) -> Result;
auto ClearPending() -> Result;
auto SnapshotOk() -> bool;

// Read-only 0010 dump. Does not terminate ACCOUNT.
auto Dump0010ReadOnly(ui::ProgressBox* pbox) -> Result;
auto WriteExpectedFileList() -> Result;
auto InstallRestoreTeScripts() -> void;
auto LaunchTegraDump() -> bool;

} // namespace sphaira::account_restore
