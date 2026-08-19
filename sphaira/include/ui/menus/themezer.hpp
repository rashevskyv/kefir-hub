#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/scrollable_text.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/list.hpp"
#include "option.hpp"
#include <span>

namespace sphaira::ui::menu::themezer {

enum class ImageDownloadState {
    None, // not started
    Progress, // Download started
    Done, // finished downloading
    Failed, // attempted to download but failed
};

struct LazyImage {
    ~LazyImage();
    int image{};
    int w{}, h{};
    bool tried_cache{};
    bool cached{};
    ImageDownloadState state{ImageDownloadState::None};
};

enum MenuState {
    MenuState_Normal,
    MenuState_Search,
    MenuState_Creator,
};

enum ListType {
    ListType_Pack, // list complete packs
    ListType_Target, // list types
};

enum class PageLoadState {
    None,
    Loading,
    Done,
    Error,
};

struct Creator {
    std::string id{};
    std::string display_name{};
};

struct Details {
    std::string name{};
    std::string description{};
};

struct Preview {
    std::string thumb{};
    std::string full{};
    LazyImage lazy_image{};
};

struct ThemeEntry {
    std::string id{};
    Creator creator{};
    Details details{};
    std::string target{};
    std::string download_url{};
    Preview preview{};
};

struct PackListEntry {
    std::string id{};
    Creator creator{};
    Details details{};
    Preview preview{};
    std::vector<ThemeEntry> themes{};
};

struct Pagination {
    u64 page{};
    u64 limit{};
    u64 page_count{};
    u64 item_count{};
};

struct PackList {
    std::vector<PackListEntry> packList{};
    Pagination pagination{};
};

struct Config {
    // these index into a string array
    u32 sort_index{};
    u32 order_index{};
    // search query, if empty, its not used
    std::string query{};
    std::string target{};
    std::vector<std::string> tags{};
    // defaults
    u32 page{1};
    u32 limit{18};

    void SetQuery(std::string new_query) {
        query = new_query;
    }

    void RemoveQuery() {
        query.clear();
    }
};

struct PageEntry {
    std::vector<PackListEntry> m_packList{};
    Pagination m_pagination{};
    PageLoadState m_ready{PageLoadState::None};
};

} // namespace sphaira::ui::menu::themezer

namespace sphaira::ui {
struct ProgressBox;
}

namespace sphaira::ui::menu::themezer {

auto InstallTheme(ProgressBox* pbox, const PackListEntry& entry) -> Result;
auto InstallThemePackage(ProgressBox* pbox, const std::string& name, const std::string& url) -> Result;
void PromptInstallTheme(const std::vector<std::string>& nxtheme_paths = {});
auto HasNro() -> bool;
auto GetNroPath() -> const char*;
auto PackListEntryToJson(const PackListEntry& entry) -> std::string;
auto JsonToPackListEntry(const std::string& json_str, PackListEntry& entry) -> bool;
auto GetFavoriteIds() -> std::vector<std::string>;
auto GetFavorites() -> std::vector<PackListEntry>;

struct Menu final : MenuBase {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Themezer"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index) {
        m_index = index;
        if (!m_index) {
            m_list->SetYoff(0);
        }
        UpdateFavoriteAction();
    }

    void InvalidateAllPages();
    void PackListDownload();
    void DisplayOptions();
    void DisplayScreenshots();

    void ToggleFavorite();
    void UpdateFavoriteAction();
    bool IsFavorite(const std::string& id) const;

private:
    static constexpr inline const char* INI_SECTION = "themezer";
    static constexpr inline u32 MAX_ON_PAGE = 16; // same as website

    std::vector<PageEntry> m_pages{};
    s64 m_page_index{};
    s64 m_page_index_max{1};
    u64 m_page_generation{};

    std::string m_search{};

    s64 m_index{}; // where i am in the array
    std::unique_ptr<List> m_list{};

    ScrollingText m_scroll_name{};
    ScrollingText m_scroll_author{};

    // options
    option::OptionLong m_sort{INI_SECTION, "sort", 0};
    option::OptionLong m_order{INI_SECTION, "order", 0};
    option::OptionLong m_target{INI_SECTION, "target", 0};
    option::OptionString m_tags{INI_SECTION, "tags", ""};

    bool m_checked_for_nro{};
    std::vector<std::string> m_favorite_ids{};
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace sphaira::ui::menu::themezer
