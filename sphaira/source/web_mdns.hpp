#pragma once

#include <switch.h>

namespace sphaira {

auto StartMdnsResponder(u32 ip) -> bool;
void StopMdnsResponder();
auto IsMdnsActive() -> bool;

} // namespace sphaira
