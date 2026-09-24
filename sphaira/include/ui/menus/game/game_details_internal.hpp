#pragma once

#include "ui/menus/game/game_details.hpp"
#include "ui/menus/game/game_internal.hpp"
#include "ui/list.hpp"
#include "ui/popup_list.hpp"
#include "ui/scrolling_text.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::game {

struct DbiDetailsMenu final : MenuBase {
    enum class Tab : u8 { Content, Tickets, Saves };

    DbiDetailsMenu(std::vector<Entry>* entries, s64 index,
        std::function<void(Entry, u32)> dump_callback,
        std::function<void(Entry, u32)> repack_callback,
        std::function<void(s64)> selection_callback);

    auto GetShortTitle() const -> const char* override { return "Game Details"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    void ChangeGame(s64 delta);
    void ChangeTab(s64 delta);
    void SetTab(Tab tab);
    void SetHeaderFocus(bool focus);
    void FireA();
    void SyncActionHint();
    void ActivateHeaderItem();
    void OpenModsFolder();
    static void BrowseSdPath(const fs::FsPath& path);
    void LoadGame();
    void LoadSaves(u64 app_id);
    void OpenComponentInBrowser(const GameComponentRow& component);
    void ShowCurrentActions();
    void ShowGameActions();
    void ShowCreateRepackSidebar();
    auto CurrentCount() const -> size_t;
    auto TabHeaderItem() const -> u8;
    static auto HeaderItemHint(u8 item) -> std::string;

    auto CurrentEntry() -> Entry& { return (*m_entries)[m_game_index]; }
    auto CurrentEntry() const -> const Entry& { return (*m_entries)[m_game_index]; }

    std::vector<Entry>* m_entries{};
    s64 m_game_index{};
    s64 m_row_index{};
    Tab m_tab{};
    std::unique_ptr<List> m_list{};
    std::vector<GameComponentRow> m_components{};
    std::vector<GameTicketRow> m_tickets{};
    std::vector<GameSaveRow> m_saves{};
    std::function<void(Entry, u32)> m_dump_callback{};
    std::function<void(Entry, u32)> m_repack_callback{};
    std::function<void(s64)> m_selection_callback{};
    Result m_load_result{};
    char m_display_version[sizeof(NacpStruct::display_version) + 1]{};
    std::string m_languages{};
    PopupList::Items m_language_list{};
    ScrollingText m_language_scroll{};
    std::array<ScrollingText, 10> m_stat_label_scrolls{};
    bool m_header_focus{};
    bool m_hint_is_header{};
    u8 m_header_index{};
    u64 m_save_size{};
    u64 m_save_journal_size{};
    u64 m_save_allocated_size{};
};

} // namespace sphaira::ui::menu::game
