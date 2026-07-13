#pragma once

#include "ui/menus/save_menu.hpp"
#include "title_info.hpp"

namespace sphaira::ui::menu::save::detail {

auto GetSystemSaveName(u64 system_save_data_id) -> const char*;
void FakeNacpEntryForSystem(Entry& e);
bool LoadControlImage(Entry& e, title::ThreadResultData* result);
void LoadResultIntoEntry(Entry& e, title::ThreadResultData* result);
void LoadControlEntry(Entry& e, bool force_image_load = false);

} // namespace sphaira::ui::menu::save::detail
