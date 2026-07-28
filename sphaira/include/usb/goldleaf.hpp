#pragma once

#include <switch.h>

namespace sphaira::usb::goldleaf {

enum Magic : u32 {
    Magic_GoldleafList0 = 0x31304C47,    // GL01 ('G', 'L', '0', '1')
    Magic_GoldleafCommand0 = 0x31434C47, // GLC1 ('G', 'L', 'C', '1')
};

enum USBCmdType : u8 {
    REQUEST = 0,
    RESPONSE = 1
};

enum USBCmdId : u32 {
    EXIT = 0,
    FILE_RANGE = 1
};

enum USBFlag : u8 {
    USBFlag_NONE = 0,
    USBFlag_STREAM = 1 << 0,
};

struct GLHeader {
    u32 magic; // GL01
    u32 nspListSize;
    u8 flags;
    u8 padding[0x7];
};

struct NX_PACKED USBCmdHeader {
    u32 magic; // GLC1
    USBCmdType type;
    u8 padding[0x3];
    u32 cmdId;
    u64 dataSize;
    u8 reserved[0xC];
};

struct FileRangeCmdHeader {
    u64 size;
    u64 offset;
    u64 nspNameLen;
    u64 padding;
};

static_assert(sizeof(GLHeader) == 0x10, "GLHeader must be 0x10!");
static_assert(sizeof(USBCmdHeader) == 0x20, "USBCmdHeader must be 0x20!");

} // namespace sphaira::usb::goldleaf
