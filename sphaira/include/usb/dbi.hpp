#pragma once

#include <switch.h>

namespace sphaira::usb::dbi {

enum Magic : u32 {
    Magic_Dbi0 = 0x30494244, // DBI0 (DBI USB Protocol 0)
};

enum class CmdType : u32 {
    Request = 0,
    Response = 1,
    Ack = 2
};

enum class CmdId : u32 {
    Exit = 0,
    FileRange = 2,
    List = 3
};

struct NX_PACKED CmdHeader {
    u32 magic; // DBI0
    CmdType type;
    CmdId id;
    u32 data_size;
};

struct NX_PACKED FileRangeHeader {
    u32 range_size;
    u64 range_offset;
    u32 name_len;
    // followed by name
};

static_assert(sizeof(CmdHeader) == 0x10, "CmdHeader must be 0x10!");
static_assert(sizeof(FileRangeHeader) == 16, "FileRangeHeader must be 16 bytes!");

} // namespace sphaira::usb::dbi
