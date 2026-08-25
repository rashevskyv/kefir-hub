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

// Decrypt system saves 0010/0011/00F0/0041 onto SD. Restore is TegraExplorer
// restore.te (destination keys via Save.commit), not a raw NAND blob copy.
auto Export(ui::ProgressBox* pbox, Report& out) -> Result;

} // namespace sphaira::nand_transfer
