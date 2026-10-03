#pragma once

#include "ui/menus/game_menu.hpp"
#include "ui/menus/game_list_info.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "dumper.hpp"
#include "title_info.hpp"
#include "title_nsp.hpp"
#include "ui/types.hpp"
#include "ui/menus/game/game_save_manager.hpp"

#include <array>
#include <string>
#include <vector>

namespace sphaira::ui::menu::game {

using title::NspEntry;

struct NspSource final : dump::BaseSource {
    NspSource(const std::vector<NspEntry>& entries);

    Result Read(const std::string& path, void* buf, s64 off, s64 size, u64* bytes_read) override;
    auto GetName(const std::string& path) const -> std::string;
    auto GetSize(const std::string& path) const -> s64;
    auto GetIcon(const std::string& path) const -> int override;

private:
    std::vector<NspEntry> m_entries{};
    bool m_is_file_based_emummc{};
};

Result Notify(Result rc, const std::string& error_message);
Result GetMetaEntries(const Entry& e, title::MetaEntries& out, u32 flags = title::ContentFlag_All);
bool LoadControlImage(Entry& e, title::ThreadResultData* result);
void LoadResultIntoEntry(Entry& e, title::ThreadResultData* result);
void LoadControlEntry(Entry& e, bool force_image_load = false);
Result BuildNspEntries(Entry& e, u32 flags, std::vector<NspEntry>& out);
void FreeEntry(NVGcontext* vg, Entry& e);
void LaunchEntry(const Entry& e);
Result CreateSave(u64 app_id, AccountUid uid);

struct GameComponentRow {
    NsApplicationContentMetaStatus status{};
    u64 size{};
    u32 content_count{};
    u32 rights_count{};
};

struct GameTicketRow {
    FsRightsId id{};
    u8 key_generation{};
    u8 meta_type{};
    u64 ticket_size{};
    bool personalized{};
};


auto ContentFlagFromMetaType(u8 meta_type) -> u32;
void ProbeModsFolder(Entry& entry);
Result LoadGameSummary(Entry& entry);

auto StorageName(u8 storage_id) -> std::string;
auto MovingToLabel(u8 target) -> std::string;
auto MovedToLabel(u8 target) -> std::string;
void DropBoostForMove();
auto BuildMoveSummary(const title::MovePlan& plan, NcmStorageId target) -> std::string;

auto CollectEntryBadgeLabels(const Entry& e, bool include_storage,
    std::array<const char*, kMaxGameBadges>& out) -> std::size_t;
auto GameBadgeColour(const char* label) -> NVGcolor;
void DrawGameCardOutline(NVGcontext* vg, const Vec4& v);
void DrawGameBadges(NVGcontext* vg, Theme*, const Vec4& image, const Entry& entry);
auto MeasureListBadges(NVGcontext* vg, const Entry& e) -> float;
void DrawListBadges(NVGcontext* vg, const Vec4& row, const Entry& e, bool has_size);

// the summary block above the tab bar is a second focus region: these cells are
// entered with Up from the first list row and opened with A (or a touch).
// everything else up there is read-only text.
enum HeaderItem : u8 {
    HeaderItem_Languages,
    HeaderItem_Mods,
    HeaderItem_Components,
    HeaderItem_Tickets,
    HeaderItem_Saves,
    HeaderItem_Count,
};

auto FormatPlaytime(u64 nanoseconds) -> std::string;
auto FormatLastPlayed(u64 posix_seconds) -> std::string;

// stat rows: left column label at x=265 (value at 372), right column at x=810
// (value at 917). the cells below wrap those two columns row by row.
inline constexpr float STAT_ROW_Y[]{178.f, 207.f, 236.f, 265.f};
inline constexpr Vec4 HEADER_CELL[HeaderItem_Count]{
    {256.f, STAT_ROW_Y[2] - 6.f, 530.f, 28.f}, // languages
    {256.f, STAT_ROW_Y[3] - 6.f, 530.f, 28.f}, // mods folder
    {801.f, STAT_ROW_Y[0] - 6.f, 440.f, 28.f}, // components
    {801.f, STAT_ROW_Y[1] - 6.f, 440.f, 28.f}, // tickets
    {801.f, STAT_ROW_Y[2] - 6.f, 440.f, 28.f}, // saves
};

// the tab bar, and the visible rows of the tab body below it. both are tappable,
// so their geometry is shared between Draw and Update.
inline constexpr float BAR_X = 40.f;
inline constexpr float BAR_W = 1200.f;
inline constexpr float TAB_TOP = 306.f;
inline constexpr float TAB_H = 46.f;
inline constexpr float TAB_GAP = 4.f;
inline constexpr float TAB_COUNT = 3.f;
inline constexpr float TAB_W = (BAR_W - TAB_GAP * (TAB_COUNT - 1.f)) / TAB_COUNT;
inline constexpr Vec4 TAB_BAR{BAR_X, TAB_TOP, BAR_W, TAB_H};
inline constexpr Vec4 LIST_AREA{BAR_X, TAB_TOP + TAB_H, BAR_W, 634.f - (TAB_TOP + TAB_H)};

// neighbour tables indexed by HeaderItem. movement stays inside its column;
// -1 means "leave the summary and hand focus back to the list".
inline constexpr s8 HEADER_UP[]   {  0,  0,  2,  2,  3 };
inline constexpr s8 HEADER_DOWN[] {  1, -1,  3,  4, -1 };
inline constexpr s8 HEADER_LEFT[] {  0,  1,  0,  1,  1 };
inline constexpr s8 HEADER_RIGHT[]{  2,  3,  2,  3,  4 };

Result DeleteApplicationKeepSave(u64 app_id);

// mods = everything in /atmosphere/contents/<tid> except cheats/, which the cheats menu owns.
auto ModsFolderSize(u64 app_id) -> s64;
Result DeleteGameMods(u64 app_id);

} // namespace sphaira::ui::menu::game