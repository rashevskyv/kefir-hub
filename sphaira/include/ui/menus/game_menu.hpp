#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "title_info.hpp"
#include "fs.hpp"
#include "option.hpp"
#include <memory>
#include <vector>
#include <span>

namespace sphaira::ui::menu::game {

struct Entry {
    u64 app_id{};
    u8 last_event{};
    u64 last_updated{};
    NacpLanguageEntry lang{};
    int image{};
    bool selected{};
    bool summary_attempted{};
    // /atmosphere/contents/<tid> exists. an empty one is not a mod, so it does
    // not set layeredfs - it only means there is nothing left to create.
    bool mods_folder{};
    // that folder holds something, i.e. LayeredFS actually loads files for
    // this title.
    bool layeredfs{};
    u32 content_flags{};
    u64 nand_size{};
    u64 sd_size{};
    // posix seconds, from pdm. 0 when never played (or pdm is unavailable).
    u64 last_played{};
    // nanoseconds, summed over every user profile. filled by LoadPlaytime().
    u64 playtime{};
    Result summary_result{};
    title::NacpLoadStatus status{title::NacpLoadStatus::None};

    auto GetName() const -> const char* {
        return lang.name;
    }

    auto GetAuthor() const -> const char* {
        return lang.author;
    }
};

// appended, never reordered: the index is what gets written to the ini.
enum SortType {
    SortType_Updated,
    SortType_Alphabetical,
    SortType_Publisher,
    SortType_Storage,
    SortType_LastPlayed,
    SortType_PlayTime,
};

enum OrderType {
    OrderType_Descending,
    OrderType_Ascending,
};

using LayoutType = grid::LayoutType;

struct Menu final : grid::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Games"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void ScanHomebrew();
    // fills last_played from pdm and playtime from the cache, on every scan.
    void LoadPlayStats();
    // queries pdm for total playtime per title and caches it in playlog.ini.
    void LoadPlaytime();
    void SetSearch();
    void Sort();
    void SortAndFindLastFile(bool scan);
    void FreeEntries();
    void OnLayoutChange();

    auto GetSelectedEntries() const {
        std::vector<Entry> out;
        for (auto& e : m_entries) {
            if (e.selected) {
                out.emplace_back(e);
            }
        }

        if (!m_entries.empty() && out.empty()) {
            out.emplace_back(m_entries[m_index]);
        }

        return out;
    }

    void ClearSelection() {
        for (auto& e : m_entries) {
            e.selected = false;
        }

        m_selected_count = 0;
        UpdateStorageHighlight();
    }

    void DeleteGames();
    void DumpGames(u32 flags);
    void DumpEntries(std::vector<Entry> targets, u32 flags, bool clear_selection);
    void CreateSaves(AccountUid uid);
    void ToggleCurrentSelection();
    void InvertSelection();
    void UpdateStorageHighlight();
    void CreateContentsFolders();

private:
    static constexpr inline const char* INI_SECTION = "games";
    static constexpr inline const char* INI_SECTION_DUMP = "dump";

    std::vector<Entry> m_entries{};
    s64 m_index{}; // where i am in the array
    s64 m_selected_count{};
    std::unique_ptr<List> m_list{};
    bool m_dirty{};
    bool m_pdm_initialized{};
    // applied while scanning, so there is no second copy of the list to keep
    // in sync with selection, sizes and deletes.
    std::string m_search_query{};

    option::OptionLong m_sort{INI_SECTION, "sort", SortType::SortType_Updated};
    option::OptionLong m_order{INI_SECTION, "order", OrderType::OrderType_Descending};
    option::OptionLong m_layout{INI_SECTION, "layout", LayoutType::LayoutType_Grid};
    option::OptionBool m_show_unavailable{INI_SECTION, "show_unavailable", true};
    option::OptionBool m_hide_forwarders{INI_SECTION, "hide_forwarders", false};
    option::OptionBool m_title_cache{INI_SECTION, "title_cache", true};
};

} // namespace sphaira::ui::menu::game
