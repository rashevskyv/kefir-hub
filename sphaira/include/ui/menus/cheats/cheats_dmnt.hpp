#pragma once

#include <switch.h>
#include <string>

namespace sphaira::ui::menu::hats {

struct DmntMemoryRegionExtents {
    u64 base;
    u64 size;
};

struct DmntCheatProcessMetadata {
    u64 process_id;                              // offset 0x00
    u64 title_id;                                // offset 0x08
    DmntMemoryRegionExtents main_nso_extents;    // offset 0x10
    DmntMemoryRegionExtents heap_extents;        // offset 0x20
    DmntMemoryRegionExtents alias_extents;       // offset 0x30
    DmntMemoryRegionExtents address_space_extents; // offset 0x40
    u8 main_nso_build_id[0x20];                 // offset 0x50
};

auto GetBuildIdFromDmnt(u64 title_id) -> std::string;

} // namespace sphaira::ui::menu::hats
