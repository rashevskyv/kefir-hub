#pragma once

#include "ui/menus/ownfoil.hpp"
#include "ui/menus/ownfoil_title.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include "app.hpp"
#include "image.hpp"
#include "swkbd.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "i18n.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace sphaira::ui::menu::ownfoil {

// bumped by every install from a shop page. read by the fetch worker too.
extern std::atomic<u32> g_installs;

// the list stops short of the menu body to leave room for the discover button,
// which is why it pages 4 rows rather than filling the area.
constexpr Vec4 LIST_POS{30.f, 87.f, 1190.f, 425.f};
constexpr Vec4 LIST_ITEM{75.f, 110.f, 1130.f, 90.f};
constexpr Vec2 LIST_PAD{0.f, 12.f};
constexpr Vec4 DISCOVER_BUTTON{75.f, 530.f, 1130.f, 60.f};

// the banner view has no equivalent in grid::Menu, so it lays itself out, in
// the shape themezer uses for its pack cards.
constexpr Vec4 BANNER_ITEM{75.f, 110.f, 350.f, 250.f};
constexpr Vec2 BANNER_PAD{10.f, 10.f};
constexpr float BANNER_W = 320.f;
constexpr float BANNER_H = 180.f;

// every page is a round trip and a screenful of artwork, so the choices stay
// modest at the low end.
constexpr s64 PAGE_SIZES[] = {12, 24, 48, 96};

// the title row draws the term back, so the cap keeps the header from scrolling.
constexpr s64 SEARCH_MAX = 64;

// what "Sort" offers. the shop's OrderField has more members, but VERSION is the
// app's own version (0 on every retail base game) and SIZE/DOWNLOAD_COUNT aren't
// app fields at all.
struct SortMode {
    const char* field; // the shop's own OrderField name.
    const char* name;  // shown in the sidebar, translated at use.
};

// a bullet keeps the title row's parts apart without reading as a range.
constexpr const char* SEPARATOR = "  \u2022  ";

constexpr SortMode SORT_MODES[] = {
    {"NAME", "Name"},
    {"ADDED_AT", "Date added"},
    {"RELEASE_DATE", "Release date"},
};

// the first three are the shop's catalog crossed with what this console holds,
// which is why they cost a scan of it; the last two only ask the shop.
struct CategoryMode {
    sphaira::ownfoil::api::Category category;
    const char* name; // shown in the panel, translated at use.
};

// the first is what every connect opens on: what the shop can add to this
// console is the reason to open it.
constexpr CategoryMode CATEGORIES[] = {
    {sphaira::ownfoil::api::Category::NewGames, "New games"},
    {sphaira::ownfoil::api::Category::Updates, "Updates"},
    {sphaira::ownfoil::api::Category::Dlc, "DLC"},
    {sphaira::ownfoil::api::Category::All, "All games"},
    {sphaira::ownfoil::api::Category::Search, "Search"},
};

// looked up once: the '-' action is re-asserted every frame the catalog is up.
inline auto GetViewActionHint() -> const std::string& {
    static const std::string hint = "View"_i18n;
    return hint;
}

// and Y, for the same reason.
inline auto GetCategoryActionHint() -> const std::string& {
    static const std::string hint = "Show"_i18n;
    return hint;
}

} // namespace sphaira::ui::menu::ownfoil
