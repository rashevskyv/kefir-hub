#pragma once

#include <string>
#include <switch.h>

namespace sphaira::ui { struct ProgressBox; }

namespace sphaira::nand_transfer {

struct Report {
    std::string dir;
    bool save_0010{};
    bool save_0011{};
    bool save_00F0{};
    bool save_0041{};
};

auto IsPack(const std::string& dir) -> bool;

// Decrypt system saves 0010/0011/00F0/0041 onto SD (source Horizon keys).
auto Export(ui::ProgressBox* pbox, Report& out) -> Result;

// Write that pack into this console's existing system saves. Horizon encrypts
// and signs with destination keys. Does not copy raw SYSTEM:/save blobs.
auto Import(ui::ProgressBox* pbox, const std::string& dir, Report& out) -> Result;

} // namespace sphaira::nand_transfer
