#pragma once

#include "ui/menus/cheats_menu.hpp"

namespace sphaira::ui::menu::hats {

// Menu to select and download specific cheats
struct CheatDownloadMenu final : MenuBase {
    CheatDownloadMenu(CheatSource source, const GameCheatInfo& game);
    ~CheatDownloadMenu();

    auto GetShortTitle() const -> const char* override { return "Select Cheats"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnSelect();
    void FetchCheats();
    void FetchCheatsFromNxDb();
    void FetchKefirBuildIdFromVersionMap();
    void FetchCheatsFileAndExtractBuildIds();
    void FetchNxDbCheatsFromGithub(const std::string& build_id);
    void FetchKefirCheatsFromGithub(const std::string& build_id);
    void CacheNxDbCheatFile(const std::string& content);
    void FetchCheatsFromApi(const std::string& build_id);
    void DownloadCheats();
    void DeleteCheat();
    void ShowExistingCheats();
    void PreviewCheat();
    void RunBuildIdDiagnostics(bool fetch_on_success);

private:
    CheatSource m_source;
    GameCheatInfo m_game;
    std::vector<CheatEntry> m_cheats;
    std::vector<ExistingCheat> m_existing_cheats;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    bool m_loading{false};
    bool m_loaded{false};
    std::string m_error_message;
    bool m_showing_existing{false};
    bool m_should_close{false}; // Flag to close menu in Update instead of callback
};

namespace detail {
    auto WriteCheatFile(u64 title_id, const std::string& build_id, const std::vector<CheatEntry>& cheats) -> Result;
    auto ParseNxDbCheats(const std::string& json_str, const std::string& target_build_id) -> std::vector<CheatEntry>;
    auto ParseCheatslipsCheats(const std::string& json_str, const std::string& target_build_id) -> std::vector<CheatEntry>;
    auto ExtractNxDbBuildIds(const std::string& json_str) -> std::vector<std::string>;
}

} // namespace sphaira::ui::menu::hats
