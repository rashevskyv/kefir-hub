#pragma once

namespace sphaira::forwarder_auto {

void StartCheck();
void StopCheck();
// Shows a notice that was held back while the first-start page was open (in the chosen language).
void ShowDeferredNotice();

// Before the owner deletes the second copy of Kefir Hub: the HOME icon of Kefir Hub (if there
// is one) is installed again with /hbmenu.nro as its target, so it cannot point at the copy.
// True when there is no such icon or it was reinstalled; false when it could not be.
auto PointHubForwarderAtHbmenu() -> bool;
auto IsHubForwarderInstalled() -> bool;

} // namespace sphaira::forwarder_auto
