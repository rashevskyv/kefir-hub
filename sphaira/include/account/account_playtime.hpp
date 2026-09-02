#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::account_playtime {

struct PlaySlice {
    AccountUid uid{};
    std::vector<PdmPlayEvent> events;
};

auto CollectUserPlayEvents(const AccountUid& uid, std::vector<PdmPlayEvent>& out) -> Result;
auto WritePackPlayEvents(const std::string& dir, const std::vector<PdmPlayEvent>& events) -> Result;
auto LoadPackPlayEvents(const std::string& dir, std::vector<PdmPlayEvent>& out) -> Result;
auto PackHasPlayEvents(const std::string& dir) -> bool;

// Remap slices to the new UIDs, read dest PlayEvent.dat read-only, append,
// write /config/kefir/playtime_pending/PlayEvent.dat and playtime_restore.te.
// Never opens 00F0 writable — that User-Breaks ns under Horizon.
auto PreparePlayHours(const std::vector<PlaySlice>& slices) -> Result;

} // namespace sphaira::account_playtime
