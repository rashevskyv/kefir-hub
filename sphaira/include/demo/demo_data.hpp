#pragma once

// DOCS_DEMO builds only: fictional content for docs screenshots.
// Data lives on the SD card at sdmc:/config/kefir/demo/ (fixtures: docs/site/fixtures/sdmc/).

#include <switch.h>
#include <span>

namespace sphaira::demo {

// application ids from titles.json, parsed once; empty if the file is missing.
std::span<const u64> TitleIds();

// NAND user partition space from titles.json "nand" (Eden has no BIS filesystem); false if absent.
bool NandSpace(s64* free, s64* total);

// header shows "EmuNAND" (titles.json "nand.emummc"); App::IsEmummc() itself is not changed.
bool EmuNand();

} // namespace sphaira::demo
