#pragma once

#include "ui/menus/cheats_menu.hpp"

namespace sphaira::ui::menu::hats {

// Menu to view cheat files for a specific game
struct CheatFilesMenu final : MenuBase {
    CheatFilesMenu(const GameCheatInfo& game);
    ~CheatFilesMenu();

    auto GetShortTitle() const -> const char* override { return "Cheat Files"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnView();
    void OnDelete();
    void OnFixBuildId();
    void LoadCheatFiles();

private:
    GameCheatInfo m_game;
    std::vector<ExistingCheat> m_cheats;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

// Menu to view cheat file content (shows cheat titles in a list)
struct CheatContentMenu final : MenuBase {
    CheatContentMenu(const GameCheatInfo& game, const std::string& build_id, const std::string& content);
    ~CheatContentMenu();

    auto GetShortTitle() const -> const char* override { return "Cheat Content"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void ParseCheatContent(const std::string& content);
    void OnViewCheat(); // View individual cheat code

private:
    GameCheatInfo m_game;
    std::string m_build_id;
    struct CheatTitle {
        std::string name;
        std::string content; // Full cheat code content
        CheatSource source; // Source of this cheat
        bool is_empty; // Whether the cheat has actual code content
    };
    std::vector<CheatTitle> m_cheats;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

// Menu to view individual cheat code content (scrollable)
struct CheatCodeViewerMenu final : MenuBase {
    CheatCodeViewerMenu(const std::string& title, const std::string& content, bool is_empty);
    ~CheatCodeViewerMenu();

    auto GetShortTitle() const -> const char* override { return "Cheat Code"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override {}

private:
    std::string m_title;
    std::string m_content;
    bool m_is_empty;
    float m_scroll_offset{};
    float m_content_height{};
};

namespace detail {
    auto GetCheatsDirPath(u64 title_id) -> std::string;
    auto GetFileStem(const std::string& path) -> std::string;
    auto GetExistingCheats(u64 title_id) -> std::vector<std::pair<std::string, std::string>>;
    auto DeleteCheatFile(u64 title_id, const std::string& build_id) -> bool;
    auto ResolveManualTargetBuildId(const GameCheatInfo& game, const fs::FsPath* source_path = nullptr) -> std::string;
    auto GetManualCheatImportPath(u64 title_id, const std::string& build_id) -> fs::FsPath;
    auto IsCheatHeaderLine(const std::string& line) -> bool;
    auto GetCheatHeaderName(const std::string& line) -> std::string;
}

} // namespace sphaira::ui::menu::hats
