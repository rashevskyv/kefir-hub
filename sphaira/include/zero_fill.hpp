#pragma once

#include "ui/progress_box.hpp"
#include <switch.h>

namespace sphaira::zero_fill {

// Overwrites the free space of an ncm storage (SD card or NAND user) with zeros:
// one placeholder as large as the free space, written with zeros, then deleted.
Result FillFreeSpace(NcmStorageId storage_id, ui::ProgressBox* pbox);

} // namespace sphaira::zero_fill
