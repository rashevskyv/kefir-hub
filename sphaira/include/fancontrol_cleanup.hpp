#pragma once

namespace sphaira {

// Kefir no longer ships FunControl. At every start: stop the FunControl sysmodule if it runs and delete
// /atmosphere/contents/00FF46554E43544C and /switch/.overlays/NX-FanControl.ovl. Returns true when something
// was found (and removed, or tried to be). A no-op, quiet pass on a console that has neither.
auto RemoveFunControl() -> bool;

} // namespace sphaira
