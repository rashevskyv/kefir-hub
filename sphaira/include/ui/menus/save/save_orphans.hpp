#pragma once

#include <switch.h>
#include <vector>

namespace sphaira::ui::menu::save {

// Save records of the User space whose file (/save/<id> on the user partition) is gone. The save
// index lives in SYSTEM, so formatting USER (a compressed emuMMC is made that way) keeps the records
// of games and saves whose data no longer exists.
auto FindRecordsWithoutData(const std::vector<FsSaveDataInfo>& saves) -> std::vector<FsSaveDataInfo>;

// fs refuses to delete such a record (2002-1002 TargetNotFound: it looks for the file first). Puts an
// empty file in its place and deletes again, so fs removes the file and the record; removes the empty
// file again when that fails too.
auto DeleteRecordWithoutData(u64 save_data_id) -> Result;

// Once per run, from the first save scan: when records without data exist, explains why and offers
// to remove them.
void OfferToRemoveRecordsWithoutData(const std::vector<FsSaveDataInfo>& saves);

} // namespace sphaira::ui::menu::save
