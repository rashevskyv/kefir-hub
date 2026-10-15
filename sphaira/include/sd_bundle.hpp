#pragma once

#include "ui/install_progress.hpp"
#include "yati/source/base.hpp"

namespace sphaira::sd_bundle {

// Reads a microSD bundle (sd_bundle_rules.hpp) of `size` bytes from `source` as one forward
// stream and writes its files to the microSD. Archives in it are extracted with their rules.
Result Receive(ui::InstallProgress* progress, yati::source::Base* source, s64 size);

} // namespace sphaira::sd_bundle
