#pragma once

#include "ui/menus/cheats_menu.hpp"
#include <string>

namespace sphaira::ui {
    struct ProgressBox;
}

namespace sphaira::ui::menu::hats {

void RefreshCheatMetadataCache();
auto DownloadAndExtractKefirCheats(ui::ProgressBox* pbox, const char* url) -> Result;
void PromptKefirCheatsDownload(const char* title, const char* url);
// The Build ID needs prod.keys and there are none: says so and offers a Lockpick_RCM key dump.
void ShowProdKeysMissingDialog();

namespace detail {

auto GetCheatslipsToken() -> std::string;
auto AuthenticateCheatslips(const std::string& email, const std::string& password) -> std::string;
auto SaveCheatslipsToken(const std::string& token) -> void;
auto DeleteAllCheatsForTitle(u64 title_id) -> bool;
auto ClearCheatsCache() -> Result;
auto DeleteAllCheats() -> Result;
auto DeleteOrphanedCheats() -> Result;

} // namespace detail

} // namespace sphaira::ui::menu::hats
