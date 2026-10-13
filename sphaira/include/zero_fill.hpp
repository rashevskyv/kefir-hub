#pragma once

#include "ui/progress_box.hpp"
#include <switch.h>

namespace sphaira::zero_fill {

// Overwrites the free space of the microSD card with zeros: one temporary file grows with zeros
// until the card is full, then it is deleted.
Result FillSdFreeSpace(ui::ProgressBox* pbox);

// Deletes the temporary file a crash or a power-off left during a fill. Called at start.
void RemoveLeftover();

} // namespace sphaira::zero_fill
