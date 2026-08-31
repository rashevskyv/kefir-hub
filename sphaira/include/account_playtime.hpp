#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::account_playtime {

// Raw PDM PlayEvent.dat entries for one Horizon user (account events for
// that UID plus applet/power events while they were logged in).
auto CollectUserPlayEvents(const AccountUid& uid, std::vector<PdmPlayEvent>& out) -> Result;
auto WritePackPlayEvents(const std::string& dir, const std::vector<PdmPlayEvent>& events) -> Result;
auto LoadPackPlayEvents(const std::string& dir, std::vector<PdmPlayEvent>& out) -> Result;
auto PackHasPlayEvents(const std::string& dir) -> bool;

// Rewrite account-event UIDs in the slice to the restored profile, then
// append into this console's 00F0 PlayEvent.dat. Other users' events stay.
// SD rollback of the dest file before any write.
auto AppendPlayEventsForUser(const AccountUid& new_uid, const std::vector<PdmPlayEvent>& events) -> Result;

} // namespace sphaira::account_playtime
