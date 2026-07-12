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

// Shared constants for Cheats
inline constexpr const char* CHEATSLIPS_API_URL = "https://www.cheatslips.com/api/v1/cheats";
inline constexpr const char* CHEATSLIPS_TOKEN_URL = "https://www.cheatslips.com/api/v1/token";
inline constexpr const char* VERSIONS_DB_URL = "https://raw.githubusercontent.com/HamletDuFromage/switch-cheats-db/master/versions";
inline constexpr const char* ATMOSPHERE_CONTENTS_PATH = "/atmosphere/contents";
inline constexpr const char* CHEATS_SUBDIR = "cheats";
inline constexpr const char* TOKEN_PATH = "/config/hats-tools/cheatslips_token.json";
inline constexpr const char* PAYLOAD_LAUNCH_CONFIG_PATH = "/config/hats-tools/payload-launch.ini";
inline constexpr const char* NX_DB_PATH = "/config/hats-tools/cheats-db";
inline constexpr const char* NX_DB_VERSIONS_FILE = "versions.json";
inline constexpr const char* NX_DB_GITHUB_BASE = "https://raw.githubusercontent.com/sthetix/nx-cheats-db/main";
inline constexpr const char* NX_DB_VERSIONS_URL = "https://raw.githubusercontent.com/sthetix/nx-cheats-db/main/versions.json";
inline constexpr const char* AIO_TOKEN_PATH = "/config/aio-switch-updater/token.json";
inline constexpr const char* CHEAT_METADATA_CACHE_PATH = "/config/hats-tools/cheat-metadata.json";
inline constexpr u32 CHEAT_METADATA_CACHE_VERSION = 1;
inline constexpr size_t ATMOSPHERE_MAX_CHEATS_PER_FILE = 128;
inline constexpr const char* KEFIR_CHEATS_URL = "https://github.com/HamletDuFromage/switch-cheats-db/releases/latest/download/contents.zip";
inline constexpr const char* KEFIR_CHEATS_GFX_URL = "https://github.com/HamletDuFromage/switch-cheats-db/releases/latest/download/contents_60fps-res-gfx.zip";
inline constexpr const char* KEFIR_CHEATS_CACHE_DIR = "/config/kefir-updater";
inline constexpr const char* KEFIR_CHEATS_ZIP = "/config/kefir-updater/cheats.zip";
inline constexpr const char* KEFIR_VERSIONS_DIRECTORY = "https://raw.githubusercontent.com/HamletDuFromage/switch-cheats-db/master/versions/";
inline constexpr const char* KEFIR_CHEATS_GBATEMP_DIRECTORY = "https://raw.githubusercontent.com/HamletDuFromage/switch-cheats-db/master/cheats_gbatemp/";
inline constexpr s32 ENTRY_CHUNK_COUNT = 1000;

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
