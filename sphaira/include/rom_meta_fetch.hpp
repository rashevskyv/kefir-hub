#pragma once

// Facts about a ROM, the libnx part: the merged file of a system on the card and its download. The pure part is rom_meta.hpp.

#include "rom_meta.hpp"
#include <memory>
#include <stop_token>
#include <string>

namespace sphaira::rom {

// the facts of a system from the card (/config/kefir/cache/meta/<console>.txt), or null when they are not there yet.
// The last system stays in memory, so this is cheap on every cursor move.
auto LoadMeta(const std::string& console) -> std::shared_ptr<const MetaTable>;

// Downloads the five dats of a system, merges them and keeps the result on the card. Blocking: call it from a worker
// thread. Nothing is kept when a dat is missing or the token stops it. After a failure it does not try again in this run.
// Returns true when the card holds the facts afterwards.
bool FetchMeta(const std::string& console, std::stop_token token);

} // namespace sphaira::rom
