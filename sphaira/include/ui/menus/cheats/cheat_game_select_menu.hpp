#pragma once

#include "ui/menus/cheats_menu.hpp"
#include <unordered_set>

namespace sphaira::ui::menu::hats {

// Menu to select installed games for cheat downloading
struct CheatGameSelectMenu final : grid::Menu {
    CheatGameSelectMenu(CheatSource source);
    CheatGameSelectMenu(CheatSource source, const fs::FsPath& manual_cheat_path);
    ~CheatGameSelectMenu();

    auto GetShortTitle() const -> const char* override { return "Select Game"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnSelect();
    void ScanGames();
    void OnLayoutChange();
    void DisplayOptions();
    void FreeGames();

private:
    static constexpr inline const char* INI_SECTION = "cheats";
    CheatSource m_source;
    fs::FsPath m_manual_cheat_path{};
    std::vector<GameCheatInfo> m_games;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    bool m_scanning{false};
    bool m_loaded{false};
    option::OptionLong m_layout{INI_SECTION, "game_layout", grid::LayoutType_Grid};
};
namespace detail {
    auto GetTitleVersion(u64 title_id) -> u32;
    auto GetTitleName(u64 title_id) -> std::string;
    void AppendGameCardGames(std::vector<GameCheatInfo>& games, std::unordered_set<u64>& seen_title_ids);
    auto EnumerateInstalledGames() -> std::vector<GameCheatInfo>;
    auto ImportManualCheatFile(u64 title_id, const fs::FsPath& source_path, const std::string& target_build_id) -> Result;
}

} // namespace sphaira::ui::menu::hats
