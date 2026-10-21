#pragma once

#include "fs.hpp"

#include <vector>
#include <switch.h>

namespace sphaira::ui::menu::homebrew {

struct DeletePlan {
    fs::FsPath folder{};      // the app's own /switch/<folder>, empty when it shares the folder with other apps
    s64 folder_bytes{};
    std::vector<u64> tiles{}; // HOME tiles (forwarders) made by Kefir Hub that start this NRO
};

// what goes with an NRO: its folder (only when nothing else lives there; never the running Hub's) and its HOME tiles
auto PlanDelete(const fs::FsPath& nro) -> DeletePlan;

// removes the tiles of the plan; the folder is the caller's to delete (it is the user's choice)
void DeleteTiles(const DeletePlan& plan);

} // namespace sphaira::ui::menu::homebrew
