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
    // console -> PC: the queue as the console planned it (selection, target,
    // where each Auto package really goes, install size). DBI Backend Qt only.
    QueuePlan = 6,
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

struct QueuePlanHeader {
    u32 count;
    u32 revision; // the PC list revision this plan was built on
};

struct NX_PACKED QueuePlanRecord {
    u8 selected;
    u8 target;   // 0 auto, 1 sd, 2 nand (as the user set it)
    u8 planned;  // 1 sd, 2 nand (where it will go)
    u8 flags;    // bit0 analysis ok, bit1 title already installed, bit2 takes no space (will be skipped)
    u64 install_size;
    u32 name_len;
    // followed by name
};

constexpr u8 QueuePlanFlag_AnalysisOk = 1 << 0;
constexpr u8 QueuePlanFlag_AlreadyInstalled = 1 << 1;
constexpr u8 QueuePlanFlag_NoSpace = 1 << 2;

static_assert(sizeof(CmdHeader) == 0x10, "CmdHeader must be 0x10!");
static_assert(sizeof(QueuePlanHeader) == 8, "QueuePlanHeader must be 8 bytes!");
static_assert(sizeof(QueuePlanRecord) == 16, "QueuePlanRecord must be 16 bytes!");
static_assert(sizeof(FileRangeHeader) == 16, "FileRangeHeader must be 16 bytes!");
static_assert(sizeof(PackageStatusHeader) == 12, "PackageStatusHeader must be 12 bytes!");
static_assert(sizeof(StorageInfoHeader) == 32, "StorageInfoHeader must be 32 bytes!");

} // namespace sphaira::usb::dbi
