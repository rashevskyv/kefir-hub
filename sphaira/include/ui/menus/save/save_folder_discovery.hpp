#pragma once

#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
#include <string>
#include <string_view>

namespace sphaira::ui::menu::save {

auto ExtractTitleIdFromDir(std::string_view name) -> u64;
auto ExtractTitleNameFromDir(std::string_view name) -> std::string;

auto InspectBackupFolder(fs::Fs* fs, const fs::FsPath& folder_path, std::string_view folder_name, std::string_view game_dir_name, BackupArchiveInfo& out) -> bool;
auto InspectSaveFolderAdmission(const fs::FsPath& folder_path, ProgressBox* pbox = nullptr, bool allow_empty = false) -> SaveArchiveAdmissionResult;

} // namespace sphaira::ui::menu::save
