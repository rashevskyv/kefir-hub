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
    List = 3,
    PackageStatus = 4,
    StorageInfo = 5,
};

struct CmdHeader {
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

struct PackageStatusHeader {
    u32 status; // 0 = Installed, 1 = User Skipped, 2 = Already Installed, 3 = Failed
    u32 result_code;
    u32 name_len;
    // followed by name
};

struct StorageInfoHeader {
    u64 nand_free;
    u64 nand_total;
    u64 sd_free;
    u64 sd_total;
};

static_assert(sizeof(CmdHeader) == 0x10, "CmdHeader must be 0x10!");
static_assert(sizeof(FileRangeHeader) == 16, "FileRangeHeader must be 16 bytes!");
static_assert(sizeof(PackageStatusHeader) == 12, "PackageStatusHeader must be 12 bytes!");
static_assert(sizeof(StorageInfoHeader) == 32, "StorageInfoHeader must be 32 bytes!");

} // namespace sphaira::usb::dbi
