#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "option.hpp"
#include "title_info.hpp"
#include <string>
#include <vector>
#include <memory>

namespace sphaira::ui {

struct ScrollableText;

} // namespace sphaira::ui

namespace sphaira::ui::menu::hats {

void RefreshCheatMetadataCache();

// Cheat source types
enum class CheatSource {
    Gbatemp,
    Cheatslips,
    NxDb,       // nx-cheats-db (default, local database)
    ManualFile, // local .txt cheat file import
};

// Structure to hold cheat entry data
struct CheatEntry {
    std::string name;
    std::string content;
    std::string build_id;  // Build ID this cheat belongs to
    CheatSource source;    // Source of this cheat (GitHub, CheatSlips, NxDb)
    bool selected;
};

// Structure to hold game information for cheat selection
struct GameCheatInfo {
    u64 title_id;
    std::string name;
    std::string build_id;  // Detected build ID from dmnt:cht
    u32 version;
    size_t cheat_count{};  // Number of cheat files installed
    NacpLanguageEntry lang{};
    int image{};
    title::NacpLoadStatus status{title::NacpLoadStatus::None};
};

// Structure for existing cheat files
struct ExistingCheat {
    std::string build_id;
    std::string filename;
    bool installed;
};

// Main cheats menu - main entry point with multiple options
struct CheatsMenu final : MenuBase {
    CheatsMenu();
    ~CheatsMenu();

    auto GetShortTitle() const -> const char* override { return "Cheats"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<std::pair<std::string, std::string>> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

// Menu to view installed cheats across all games
struct CheatViewMenu final : MenuBase {
    CheatViewMenu();
    ~CheatViewMenu();

    auto GetShortTitle() const -> const char* override { return "Installed Cheats"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnSelect();
    void OnDelete();
    void ScanGamesWithCheats();

private:
    std::vector<GameCheatInfo> m_games;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    bool m_scanning{false};
    bool m_loaded{false};
};

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
    void FetchCheatsFileAndExtractBuildIds();  // Version not in db, show not found
    void FetchNxDbCheatsFromGithub(const std::string& build_id);
    void FetchKefirCheatsFromGithub(const std::string& build_id);
    void CacheNxDbCheatFile(const std::string& content);
    void FetchCheatsFromApi(const std::string& build_id);
    void DownloadCheats();
    void DeleteCheat();
    void ShowExistingCheats();
    void PreviewCheat();  // View cheat content before downloading
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

// Menu to handle CheatSlips login
struct CheatslipsLoginMenu final : MenuBase {
    enum class LoginState {
        Email,
        Password
    };

    CheatslipsLoginMenu();
    ~CheatslipsLoginMenu();

    auto GetShortTitle() const -> const char* override { return "CheatSlips Login"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void ShowEmailKeyboard();
    void ShowPasswordKeyboard();
    void Authenticate();

private:
    LoginState m_state{LoginState::Email};
    std::string m_email;
    std::string m_password;
    bool m_keyboard_shown{false};
};

} // namespace sphaira::ui::menu::hats
