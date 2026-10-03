#pragma once

// DOCS_DEMO builds only: queued button presses from sdmc:/config/kefir/demo/input.txt (see demo_cmd.hpp).

#include <switch.h>

namespace sphaira::demo {

// once per frame, after the pad was read: ORs the queued press into the pad state.
void PollInput(u64& kdown, u64& kheld, u64& kup);

} // namespace sphaira::demo
