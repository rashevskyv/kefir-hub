#pragma once

// DOCS_DEMO builds only: queued button presses from sdmc:/config/kefir/demo/input.txt (see demo_cmd.hpp).

#include <switch.h>
#include <string>

namespace sphaira::demo {

// once per frame, after the pad was read: ORs the queued press into the pad state.
void PollInput(u64& kdown, u64& kheld, u64& kup);

// the string of a queued "text" line, once: the system keyboard returns it instead of opening.
bool TakeText(std::string& out);

} // namespace sphaira::demo
