#pragma once

#include "fs.hpp"

namespace sphaira::account_restore {

auto RemoveLegacyUnpackedSnapshot(fs::FsNativeSd& sd) -> void;

} // namespace sphaira::account_restore
