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
// "Match Build ID exactly" (on by default): when off, a game version that has no cheats in the
// database gets the cheats of the closest version, written under its own Build ID.
auto CheatsMatchBuildIdExactly() -> bool;
void SetCheatsMatchBuildIdExactly(bool value);
auto DeleteAllCheats() -> Result;
auto DeleteOrphanedCheats() -> Result;

} // namespace detail

} // namespace sphaira::ui::menu::hats
