#pragma once

#include "ui/menus/save_menu.hpp"
#include "title_info.hpp"

namespace sphaira::ui::menu::save::detail {

auto GetSystemSaveName(u64 system_save_data_id) -> const char*;
void FakeNacpEntryForSystem(Entry& e);
bool LoadControlImage(Entry& e, title::ThreadResultData* result);
void LoadResultIntoEntry(Entry& e, title::ThreadResultData* result);
void LoadControlEntry(Entry& e, bool force_image_load = false);

auto IsValidGameTitleId(u64 id) -> bool;
auto FormatTitleIdHex(u64 id) -> std::string;
auto IsValidBoundedJpeg(std::span<const u8> data) -> bool;
auto BuildRemoteIconUrl(u64 app_id) -> std::string;
auto GetTitleIconCachePath(u64 app_id) -> fs::FsPath;

} // namespace sphaira::ui::menu::save::detail
